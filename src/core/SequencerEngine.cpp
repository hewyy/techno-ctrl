#include "core/SequencerEngine.h"

#include <algorithm>
#include <cmath>

namespace lps
{

void SequencerEngine::run(const ClockBlock& block) noexcept
{
    // Take a stable request snapshot before processing any player. A request
    // arriving during this callback is therefore left for a later block rather
    // than being applied nondeterministically to an earlier PPQ in this block.
    for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex)
    {
        sequenceResetRequestSnapshot_[playerIndex] =
            sequenceResetRequests_[playerIndex]->load(std::memory_order_acquire)
                ? std::uint8_t { 1 }
                : std::uint8_t { 0 };
    }

    ClockBlock playerBlock = block;
    playerBlock.sequenceReset = false;
    playerBlock.sequenceResetPpq = 0.0;
    playerBlock.patternChangesFollowMaster = false;
    playerBlock.masterCycleBoundary = false;
    playerBlock.masterCycleBoundaryPpq = 0.0;

    CycleBoundarySnapshot masterBoundary;
    const bool hasMaster = masterPlayerIndex_ < players.size();
    if (hasMaster)
    {
        players[masterPlayerIndex_]->process(
            playerBlock, playerEvents_[masterPlayerIndex_]);
        masterBoundary = players[masterPlayerIndex_]->cycleBoundarySnapshot();

        // Only accept a boundary belonging to this half-open block. This also
        // protects followers from a stale or malformed custom-player report.
        masterBoundary.valid = masterBoundary.valid
            && std::isfinite(masterBoundary.ppqPosition)
            && masterBoundary.ppqPosition >= block.ppqStart
            && masterBoundary.ppqPosition < block.ppqEnd;
    }

    // Phase 1: collect every follower's output after the master has exposed
    // its first loop boundary for this block.
    for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex)
    {
        if (hasMaster && playerIndex == masterPlayerIndex_)
            continue;

        playerBlock.sequenceReset = false;
        playerBlock.sequenceResetPpq = 0.0;
        playerBlock.patternChangesFollowMaster = hasMaster;
        playerBlock.masterCycleBoundary = hasMaster && masterBoundary.valid;
        playerBlock.masterCycleBoundaryPpq = masterBoundary.ppqPosition;
        if (hasMaster
            && masterBoundary.valid
            && sequenceResetRequestSnapshot_[playerIndex] != 0
            && sequenceResetRequests_[playerIndex]->exchange(
                false, std::memory_order_acq_rel))
        {
            playerBlock.sequenceReset = true;
            playerBlock.sequenceResetPpq = masterBoundary.ppqPosition;
        }

        players[playerIndex]->process(playerBlock, playerEvents_[playerIndex]);
    }

    // Phase 2: route events after applying relationships between players.
    for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex)
    {
        const bool muted = playerMuted(playerIndex);
        for (const auto& route : routes_)
            if (route.player == players[playerIndex])
                for (const auto& event : playerEvents_[playerIndex])
                    if ((event.type != SequencerEventType::triggerOn || !muted)
                        && !isSuppressed(playerIndex, event))
                    {
                        route.transport->send(event, block);
                    }
    }
}

bool SequencerEngine::isSuppressed(
    std::size_t playerIndex,
    const SequencerEvent& event) const noexcept
{
    if (event.type != SequencerEventType::triggerOn)
        return false;

    constexpr double simultaneousTolerancePpq = 1.0e-9;
    for (std::size_t suppressorIndex = 0; suppressorIndex < players.size(); ++suppressorIndex)
    {
        if (!suppression(suppressorIndex, playerIndex))
            continue;

        for (const auto& suppressorEvent : playerEvents_[suppressorIndex])
            if (suppressorEvent.type == SequencerEventType::triggerOn
                && std::abs(suppressorEvent.ppqPosition - event.ppqPosition)
                    <= simultaneousTolerancePpq)
                return true;
    }

    return false;
}

void SequencerEngine::add_stuff(
    IPlayer& newPlayer,
    ITransport& newTransport)
{
    if (std::find(players.begin(), players.end(), &newPlayer) == players.end())
    {
        players.push_back(&newPlayer);
        playerEvents_.emplace_back();
        playerMutes_.push_back(std::make_unique<std::atomic_bool>(false));
        sequenceResetRequests_.push_back(
            std::make_unique<std::atomic_bool>(false));
        sequenceResetRequestSnapshot_.push_back(0);

        for (auto& row : suppressionMatrix_)
            row.push_back(std::make_unique<std::atomic_bool>(false));

        std::vector<std::unique_ptr<std::atomic_bool>> newRow;
        newRow.reserve(players.size());
        for (std::size_t column = 0; column < players.size(); ++column)
            newRow.push_back(std::make_unique<std::atomic_bool>(false));
        suppressionMatrix_.push_back(std::move(newRow));
    }

    routes_.push_back({ &newPlayer, &newTransport });
}

void SequencerEngine::setPlayerMuted(
    std::size_t playerIndex,
    bool muted) noexcept
{
    if (playerIndex >= playerMutes_.size())
        return;

    playerMutes_[playerIndex]->store(muted, std::memory_order_relaxed);
}

bool SequencerEngine::playerMuted(std::size_t playerIndex) const noexcept
{
    return playerIndex < playerMutes_.size()
        && playerMutes_[playerIndex]->load(std::memory_order_relaxed);
}

bool SequencerEngine::setMasterPlayer(std::size_t playerIndex) noexcept
{
    if (playerIndex >= players.size())
        return false;

    masterPlayerIndex_ = playerIndex;
    sequenceResetRequests_[playerIndex]->store(false, std::memory_order_release);
    sequenceResetRequestSnapshot_[playerIndex] = 0;
    return true;
}

std::optional<std::size_t> SequencerEngine::masterPlayerIndex() const noexcept
{
    if (masterPlayerIndex_ >= players.size())
        return std::nullopt;

    return masterPlayerIndex_;
}

bool SequencerEngine::requestResetToMaster(std::size_t playerIndex) noexcept
{
    if (masterPlayerIndex_ >= players.size()
        || playerIndex >= players.size()
        || playerIndex == masterPlayerIndex_)
    {
        return false;
    }

    sequenceResetRequests_[playerIndex]->store(true, std::memory_order_release);
    return true;
}

bool SequencerEngine::resetToMasterPending(std::size_t playerIndex) const noexcept
{
    if (masterPlayerIndex_ >= players.size()
        || playerIndex >= players.size()
        || playerIndex == masterPlayerIndex_)
    {
        return false;
    }

    return sequenceResetRequests_[playerIndex]->load(std::memory_order_acquire);
}

void SequencerEngine::setSuppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex,
    bool enabled) noexcept
{
    if (suppressorIndex >= players.size() || suppressedIndex >= players.size()
        || suppressorIndex == suppressedIndex)
        return;

    suppressionMatrix_[suppressorIndex][suppressedIndex]->store(
        enabled, std::memory_order_relaxed);
}

bool SequencerEngine::suppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex) const noexcept
{
    if (suppressorIndex >= players.size() || suppressedIndex >= players.size()
        || suppressorIndex == suppressedIndex)
        return false;

    return suppressionMatrix_[suppressorIndex][suppressedIndex]->load(
        std::memory_order_relaxed);
}

void SequencerEngine::prepare(double sampleRate) noexcept
{
    for (auto* player : players)
        player->prepare(sampleRate);
}

void SequencerEngine::reset() noexcept
{
    for (std::size_t playerIndex = 0; playerIndex < sequenceResetRequests_.size();
         ++playerIndex)
    {
        sequenceResetRequests_[playerIndex]->store(false, std::memory_order_release);
        sequenceResetRequestSnapshot_[playerIndex] = 0;
    }

    for (auto* player : players)
        player->reset();
}

std::size_t SequencerEngine::playerCount() const noexcept
{
    return players.size();
}

PatternView SequencerEngine::pattern(std::size_t playerIndex) const noexcept
{
    return playerIndex < players.size()
        ? players[playerIndex]->get_pattern_view()
        : PatternView {};
}

PlaybackSnapshot SequencerEngine::snapshot(std::size_t playerIndex) const noexcept
{
    return playerIndex < players.size()
        ? players[playerIndex]->snapshot()
        : PlaybackSnapshot {};
}

} // namespace lps
