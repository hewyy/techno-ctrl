#include "core/SequencerEngine.h"

#include <algorithm>
#include <cmath>

namespace lps
{

void SequencerEngine::run(const TimelineBlock& block) noexcept
{
    routedEvents_.clear();
    for (auto& slot : playerSlots_)
    {
        slot.events.clear();
        slot.sequenceResetRequestSnapshot =
            slot.sequenceResetRequested->load(std::memory_order_acquire)
                ? std::uint8_t { 1 }
                : std::uint8_t { 0 };
    }

    PlayerDirectives directives;
    std::optional<double> masterBoundaryPpq;
    auto* masterSlot = masterPlayerId_.has_value()
        ? findSlot(*masterPlayerId_)
        : nullptr;
    if (masterSlot != nullptr)
    {
        masterSlot->processResult = masterSlot->player->process(
            block, directives, masterSlot->events);
        validatePlayerEvents(*masterSlot, block);
        masterBoundaryPpq = masterSlot->processResult.firstCycleBoundaryPpq;

        if (playerBlockFailed(masterSlot->id)
            || !masterBoundaryPpq.has_value()
            || !std::isfinite(*masterBoundaryPpq)
            || *masterBoundaryPpq < block.ppqStart
            || *masterBoundaryPpq >= block.ppqEnd)
        {
            masterBoundaryPpq.reset();
        }
    }

    for (auto& slot : playerSlots_)
    {
        if (masterSlot == &slot)
            continue;

        directives = {};
        const auto capabilities = slot.player->syncCapabilities();
        directives.quantizePendingTransitionsExternally = masterSlot != nullptr
            && capabilities.acceptsExternalCycleBoundaries;
        if (directives.quantizePendingTransitionsExternally)
            directives.externalCycleBoundaryPpq = masterBoundaryPpq;

        if (masterSlot != nullptr
            && masterBoundaryPpq.has_value()
            && slot.sequenceResetRequestSnapshot != 0
            && slot.sequenceResetRequested->exchange(
                false, std::memory_order_acq_rel))
        {
            directives.restartAtPpq = *masterBoundaryPpq;
        }

        slot.processResult = slot.player->process(
            block, directives, slot.events);
        validatePlayerEvents(slot, block);
    }

    for (auto& route : routes_)
        route.resetThisBlock = false;

    if (block.transportDiscontinuity)
        for (const auto& route : routes_)
            routes_[firstRouteFor(route.transport)].resetThisBlock = true;

    for (auto& slot : playerSlots_)
    {
        if (!playerBlockFailed(slot.id))
            continue;

        slot.player->reset();
        for (const auto& route : routes_)
            if (route.playerId == slot.id)
                routes_[firstRouteFor(route.transport)].resetThisBlock = true;
    }

    for (auto& route : routes_)
        if (route.resetThisBlock)
            route.transport->resetOutputs(block);

    std::uint64_t stableOrder = 0;
    for (const auto& route : routes_)
    {
        const auto* slot = findSlot(route.playerId);
        if (slot == nullptr || playerBlockFailed(route.playerId))
            continue;

        const bool muted = slot->muted->load(std::memory_order_relaxed);
        for (std::size_t eventIndex = 0;
             eventIndex < slot->events.size();
             ++eventIndex)
        {
            if (slot->validation.valid[eventIndex] == 0)
                continue;

            const auto& event = slot->events[eventIndex];
            if ((event.type == SemanticEventType::triggerStart && muted)
                || isSuppressed(slot->id, event))
            {
                continue;
            }

            const auto frameOffset = frameOffsetFor(event, block);
            if (!frameOffset.has_value())
            {
                ++stableOrder;
                continue;
            }

            RoutedEvent routed;
            routed.event = event;
            routed.sourcePlayerId = slot->id;
            routed.routeId = route.id;
            routed.frameOffset = *frameOffset;
            routed.stableOrder = stableOrder++;
            if (route.mapping.usesFixedPitch)
            {
                routed.mappedPitchSemitones = route.mapping.fixedPitchSemitones;
                routed.hasMappedPitch = true;
            }
            else if (event.hasMusicalPitch)
            {
                routed.mappedPitchSemitones = event.musicalPitchSemitones;
                routed.hasMappedPitch = true;
            }
            routedEvents_.push_back(routed);
        }
    }

    std::sort(
        routedEvents_.begin(),
        routedEvents_.end(),
        [](const RoutedEvent& left, const RoutedEvent& right)
        {
            if (left.frameOffset != right.frameOffset)
                return left.frameOffset < right.frameOffset;
            if (left.event.type != right.event.type)
                return left.event.type < right.event.type;
            return left.stableOrder < right.stableOrder;
        });

    for (const auto& routed : routedEvents_)
    {
        auto* slot = findSlot(routed.sourcePlayerId);
        if (slot == nullptr || slot->validation.failed)
            continue;

        const auto routeIndex = routed.routeId.value;
        if (routeIndex >= routes_.size()
            || !routes_[routeIndex].transport->send(routed))
        {
            resetPlayerOutputs(routed.sourcePlayerId, block);
            slot->player->reset();
            slot->validation.failed = true;
        }
    }
}

std::optional<PlayerId> SequencerEngine::registerPlayer(IPlayer& player)
{
    if (topologyFrozen_
        || playerSlots_.size() >= PlayerId::invalidValue
        || std::any_of(
            playerSlots_.begin(),
            playerSlots_.end(),
            [&player](const PlayerSlot& slot) { return slot.player == &player; }))
    {
        return std::nullopt;
    }

    PlayerSlot newSlot;
    newSlot.id = PlayerId { static_cast<std::uint32_t>(playerSlots_.size()) };
    newSlot.player = &player;
    newSlot.diagnostics = std::make_unique<EventDiagnostics>();
    newSlot.muted = std::make_unique<std::atomic_bool>(false);
    newSlot.sequenceResetRequested = std::make_unique<std::atomic_bool>(false);
    newSlot.suppresses.reserve(playerSlots_.size() + 1);
    for (std::size_t index = 0; index <= playerSlots_.size(); ++index)
        newSlot.suppresses.push_back(std::make_unique<std::atomic_bool>(false));

    const auto playerId = newSlot.id;
    playerSlots_.push_back(std::move(newSlot));
    for (std::size_t index = 0; index + 1 < playerSlots_.size(); ++index)
        playerSlots_[index].suppresses.push_back(
            std::make_unique<std::atomic_bool>(false));
    return playerId;
}

std::optional<RouteId> SequencerEngine::connect(
    PlayerId playerId,
    ITransport& transport,
    RouteMapping mapping)
{
    if (topologyFrozen_
        || findSlot(playerId) == nullptr
        || routes_.size() >= RouteId::invalidValue
        || std::any_of(
            routes_.begin(),
            routes_.end(),
            [playerId, &transport](const Route& route)
            {
                return route.playerId == playerId
                    && route.transport == &transport;
            }))
    {
        return std::nullopt;
    }

    const RouteId routeId { static_cast<std::uint32_t>(routes_.size()) };
    routes_.push_back({ routeId, playerId, &transport, mapping, false });
    return routeId;
}

void SequencerEngine::validatePlayerEvents(
    PlayerSlot& slot,
    const TimelineBlock& block) noexcept
{
    slot.validation.valid.fill(0);
    slot.validation.failed = false;

    double previousPpq = 0.0;
    bool hasPrevious = false;
    std::size_t invalidCount = 0;
    for (std::size_t eventIndex = 0; eventIndex < slot.events.size(); ++eventIndex)
    {
        const auto ppq = slot.events[eventIndex].ppqPosition;
        const bool valid = std::isfinite(ppq)
            && (!hasPrevious || ppq >= previousPpq)
            && frameOffsetFor(slot.events[eventIndex], block).has_value();
        if (!valid)
        {
            ++invalidCount;
            continue;
        }

        slot.validation.valid[eventIndex] = 1;
        previousPpq = ppq;
        hasPrevious = true;
    }

    slot.diagnostics->droppedCount.store(
        slot.events.droppedCount(), std::memory_order_release);
    slot.diagnostics->invalidCount.store(invalidCount, std::memory_order_release);
    slot.diagnostics->overflowed.store(
        slot.events.overflowed(), std::memory_order_release);
    slot.validation.failed = slot.events.overflowed()
        || slot.processResult.eventOverflow
        || invalidCount != 0;
}

SequencerEngine::PlayerSlot* SequencerEngine::findSlot(PlayerId playerId) noexcept
{
    if (playerId.value >= playerSlots_.size())
        return nullptr;

    auto& slot = playerSlots_[playerId.value];
    return slot.id == playerId ? &slot : nullptr;
}

const SequencerEngine::PlayerSlot* SequencerEngine::findSlot(
    PlayerId playerId) const noexcept
{
    if (playerId.value >= playerSlots_.size())
        return nullptr;

    const auto& slot = playerSlots_[playerId.value];
    return slot.id == playerId ? &slot : nullptr;
}

bool SequencerEngine::playerBlockFailed(PlayerId playerId) const noexcept
{
    const auto* slot = findSlot(playerId);
    return slot != nullptr && slot->validation.failed;
}

std::size_t SequencerEngine::firstRouteFor(ITransport* transport) const noexcept
{
    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
        if (routes_[routeIndex].transport == transport)
            return routeIndex;

    return routes_.size();
}

void SequencerEngine::resetPlayerOutputs(
    PlayerId playerId,
    const TimelineBlock& block) noexcept
{
    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
    {
        const auto& route = routes_[routeIndex];
        if (route.playerId != playerId)
            continue;

        bool alreadyReset = false;
        for (std::size_t earlier = 0; earlier < routeIndex; ++earlier)
        {
            if (routes_[earlier].playerId == playerId
                && routes_[earlier].transport == route.transport)
            {
                alreadyReset = true;
                break;
            }
        }

        if (!alreadyReset)
            route.transport->resetOutputs(block);
    }
}

bool SequencerEngine::isSuppressed(
    PlayerId playerId,
    const SequencerEvent& event) const noexcept
{
    if (event.type != SemanticEventType::triggerStart)
        return false;

    constexpr double simultaneousTolerancePpq = 1.0e-9;
    for (const auto& suppressor : playerSlots_)
    {
        if (playerBlockFailed(suppressor.id)
            || !suppression(suppressor.id, playerId))
        {
            continue;
        }

        for (std::size_t eventIndex = 0;
             eventIndex < suppressor.events.size();
             ++eventIndex)
        {
            if (suppressor.validation.valid[eventIndex] == 0)
                continue;

            const auto& suppressorEvent = suppressor.events[eventIndex];
            if (suppressorEvent.type == SemanticEventType::triggerStart
                && std::abs(suppressorEvent.ppqPosition - event.ppqPosition)
                    <= simultaneousTolerancePpq)
            {
                return true;
            }
        }
    }

    return false;
}

void SequencerEngine::setPlayerMuted(PlayerId playerId, bool muted) noexcept
{
    if (auto* slot = findSlot(playerId))
        slot->muted->store(muted, std::memory_order_relaxed);
}

bool SequencerEngine::playerMuted(PlayerId playerId) const noexcept
{
    const auto* slot = findSlot(playerId);
    return slot != nullptr
        && slot->muted->load(std::memory_order_relaxed);
}

bool SequencerEngine::setMasterPlayer(PlayerId playerId) noexcept
{
    auto* slot = findSlot(playerId);
    if (slot == nullptr
        || !slot->player->syncCapabilities().providesCycleBoundaries)
    {
        return false;
    }

    masterPlayerId_ = playerId;
    slot->sequenceResetRequested->store(false, std::memory_order_release);
    slot->sequenceResetRequestSnapshot = 0;
    return true;
}

std::optional<PlayerId> SequencerEngine::masterPlayerId() const noexcept
{
    return masterPlayerId_;
}

bool SequencerEngine::requestResetToMaster(PlayerId playerId) noexcept
{
    auto* slot = findSlot(playerId);
    if (!masterPlayerId_.has_value()
        || slot == nullptr
        || playerId == *masterPlayerId_
        || !slot->player->syncCapabilities().acceptsExternalRestart)
    {
        return false;
    }

    slot->sequenceResetRequested->store(true, std::memory_order_release);
    return true;
}

bool SequencerEngine::resetToMasterPending(PlayerId playerId) const noexcept
{
    const auto* slot = findSlot(playerId);
    return masterPlayerId_.has_value()
        && slot != nullptr
        && playerId != *masterPlayerId_
        && slot->sequenceResetRequested->load(std::memory_order_acquire);
}

void SequencerEngine::setSuppression(
    PlayerId suppressorId,
    PlayerId suppressedId,
    bool enabled) noexcept
{
    auto* suppressor = findSlot(suppressorId);
    if (suppressor == nullptr
        || findSlot(suppressedId) == nullptr
        || suppressorId == suppressedId)
    {
        return;
    }

    suppressor->suppresses[suppressedId.value]->store(
        enabled, std::memory_order_relaxed);
}

bool SequencerEngine::suppression(
    PlayerId suppressorId,
    PlayerId suppressedId) const noexcept
{
    const auto* suppressor = findSlot(suppressorId);
    if (suppressor == nullptr
        || findSlot(suppressedId) == nullptr
        || suppressorId == suppressedId)
    {
        return false;
    }

    return suppressor->suppresses[suppressedId.value]->load(
        std::memory_order_relaxed);
}

void SequencerEngine::prepare(const PrepareSpec& spec) noexcept
{
    topologyFrozen_ = true;
    routedEvents_.reserve(routes_.size() * SequencerEventBuffer::capacity);
    for (auto& slot : playerSlots_)
        slot.player->prepare(spec);

    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
        if (firstRouteFor(routes_[routeIndex].transport) == routeIndex)
            routes_[routeIndex].transport->prepare(spec);
}

std::optional<std::uint32_t> SequencerEngine::frameOffsetFor(
    const SequencerEvent& event,
    const TimelineBlock& block) noexcept
{
    if (!std::isfinite(event.ppqPosition))
        return std::nullopt;
    if (block.sampleCount == 0)
        return std::uint32_t { 0 };
    if (!std::isfinite(block.ppqStart)
        || !std::isfinite(block.tempoBpm)
        || !std::isfinite(block.sampleRate)
        || block.tempoBpm <= 0.0
        || block.sampleRate <= 0.0)
    {
        return std::nullopt;
    }

    const auto ppqPerSample = block.tempoBpm / (60.0 * block.sampleRate);
    const auto rawOffset = (event.ppqPosition - block.ppqStart) / ppqPerSample;
    if (!std::isfinite(rawOffset)
        || rawOffset < -0.5
        || rawOffset > static_cast<double>(block.sampleCount) - 0.5)
    {
        return std::nullopt;
    }

    const auto rounded = std::llround(rawOffset);
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        rounded,
        0,
        static_cast<std::int64_t>(block.sampleCount - 1)));
}

void SequencerEngine::reset() noexcept
{
    for (auto& slot : playerSlots_)
    {
        slot.sequenceResetRequested->store(false, std::memory_order_release);
        slot.sequenceResetRequestSnapshot = 0;
        slot.player->reset();
    }

    const TimelineBlock emptyBlock;
    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
        if (firstRouteFor(routes_[routeIndex].transport) == routeIndex)
            routes_[routeIndex].transport->resetOutputs(emptyBlock);
}

bool SequencerEngine::topologyFrozen() const noexcept
{
    return topologyFrozen_;
}

std::size_t SequencerEngine::playerCount() const noexcept
{
    return playerSlots_.size();
}

std::optional<PlayerId> SequencerEngine::playerIdAt(std::size_t index) const noexcept
{
    return index < playerSlots_.size()
        ? std::optional<PlayerId> { playerSlots_[index].id }
        : std::nullopt;
}

bool SequencerEngine::playerEventOverflowed(PlayerId playerId) const noexcept
{
    const auto* slot = findSlot(playerId);
    return slot != nullptr
        && slot->diagnostics->overflowed.load(std::memory_order_acquire);
}

std::size_t SequencerEngine::playerDroppedEventCount(PlayerId playerId) const noexcept
{
    const auto* slot = findSlot(playerId);
    return slot != nullptr
        ? slot->diagnostics->droppedCount.load(std::memory_order_acquire)
        : 0;
}

std::size_t SequencerEngine::playerInvalidEventCount(PlayerId playerId) const noexcept
{
    const auto* slot = findSlot(playerId);
    return slot != nullptr
        ? slot->diagnostics->invalidCount.load(std::memory_order_acquire)
        : 0;
}

} // namespace lps
