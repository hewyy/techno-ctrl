#pragma once

#include "core/IPlayer.h"
#include "core/ITransport.h"

#include <vector>
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>

namespace lps
{

class SequencerEngine
{
public:
    static constexpr std::size_t noMasterPlayerIndex =
        std::numeric_limits<std::size_t>::max();

    void add_stuff(IPlayer& newPlayer, ITransport& newTransport);
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void run(const ClockBlock& block) noexcept;

    [[nodiscard]] std::size_t playerCount() const noexcept;
    [[nodiscard]] bool setMasterPlayer(std::size_t playerIndex) noexcept;
    [[nodiscard]] std::optional<std::size_t> masterPlayerIndex() const noexcept;
    [[nodiscard]] bool requestResetToMaster(std::size_t playerIndex) noexcept;
    [[nodiscard]] bool resetToMasterPending(std::size_t playerIndex) const noexcept;
    [[nodiscard]] PatternView pattern(std::size_t playerIndex = 0) const noexcept;
    [[nodiscard]] PlaybackSnapshot snapshot(std::size_t playerIndex = 0) const noexcept;
    void setPlayerMuted(std::size_t playerIndex, bool muted) noexcept;
    [[nodiscard]] bool playerMuted(std::size_t playerIndex) const noexcept;
    void setSuppression(
        std::size_t suppressorIndex,
        std::size_t suppressedIndex,
        bool enabled) noexcept;
    [[nodiscard]] bool suppression(
        std::size_t suppressorIndex,
        std::size_t suppressedIndex) const noexcept;

private:
    struct Route
    {
        IPlayer* player;
        ITransport* transport;
    };

    std::vector<Route> routes_;
    std::vector<IPlayer*> players;
    std::vector<SequencerEventBuffer> playerEvents_;
    std::vector<std::unique_ptr<std::atomic_bool>> playerMutes_;
    std::vector<std::vector<std::unique_ptr<std::atomic_bool>>> suppressionMatrix_;
    std::vector<std::unique_ptr<std::atomic_bool>> sequenceResetRequests_;
    // Pre-sized alongside players so run() can take a stable block-level
    // request snapshot without allocating on the audio thread.
    std::vector<std::uint8_t> sequenceResetRequestSnapshot_;
    std::size_t masterPlayerIndex_ = noMasterPlayerIndex;

    [[nodiscard]] bool isSuppressed(
        std::size_t playerIndex,
        const SequencerEvent& event) const noexcept;
};

} // namespace lps
