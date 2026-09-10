#include "core/SequencerEngine.h"

#include <algorithm>
#include <cmath>

namespace lps
{

void SequencerEngine::run(const TimelineBlock& block) noexcept
{
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
        validatePlayerEvents(*masterSlot);
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
        validatePlayerEvents(slot);
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

    for (auto& slot : playerSlots_)
    {
        if (playerBlockFailed(slot.id))
            continue;

        const bool muted = slot.muted->load(std::memory_order_relaxed);
        bool deliveryFailed = false;
        for (const auto& route : routes_)
        {
            if (route.playerId == slot.id)
            {
                for (std::size_t eventIndex = 0;
                     eventIndex < slot.events.size();
                     ++eventIndex)
                {
                    if (slot.validation.valid[eventIndex] == 0)
                        continue;

                    const auto& event = slot.events[eventIndex];
                    if ((event.type != SequencerEventType::triggerOn || !muted)
                        && !isSuppressed(slot.id, event)
                        && !route.transport->send(event, block))
                    {
                        resetPlayerOutputs(slot.id, block);
                        slot.player->reset();
                        slot.validation.failed = true;
                        deliveryFailed = true;
                        break;
                    }
                }
            }

            if (deliveryFailed)
                break;
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
    ITransport& transport)
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
    routes_.push_back({ routeId, playerId, &transport, false });
    return routeId;
}

void SequencerEngine::validatePlayerEvents(PlayerSlot& slot) noexcept
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
            && (!hasPrevious || ppq >= previousPpq);
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
    if (event.type != SequencerEventType::triggerOn)
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
            if (suppressorEvent.type == SequencerEventType::triggerOn
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
    for (auto& slot : playerSlots_)
        slot.player->prepare(spec);

    for (std::size_t routeIndex = 0; routeIndex < routes_.size(); ++routeIndex)
        if (firstRouteFor(routes_[routeIndex].transport) == routeIndex)
            routes_[routeIndex].transport->prepare(spec);
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
