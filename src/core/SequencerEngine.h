#pragma once

#include "core/IPlayer.h"
#include "core/ITransport.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace lps
{

struct PlayerId
{
    static constexpr std::uint32_t invalidValue =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t value = invalidValue;

    friend constexpr bool operator==(PlayerId left, PlayerId right) noexcept
    {
        return left.value == right.value;
    }

    friend constexpr bool operator!=(PlayerId left, PlayerId right) noexcept
    {
        return !(left == right);
    }
};

struct RouteId
{
    static constexpr std::uint32_t invalidValue =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t value = invalidValue;

    friend constexpr bool operator==(RouteId left, RouteId right) noexcept
    {
        return left.value == right.value;
    }
};

class SequencerEngine
{
public:
    // Registered players and connected transports must outlive the engine.
    // Registration and connection are message-thread operations only.
    [[nodiscard]] std::optional<PlayerId> registerPlayer(IPlayer& player);
    [[nodiscard]] std::optional<RouteId> connect(
        PlayerId playerId,
        ITransport& transport);

    void prepare(const PrepareSpec& spec) noexcept;
    void reset() noexcept;
    void run(const TimelineBlock& block) noexcept;

    [[nodiscard]] bool topologyFrozen() const noexcept;
    [[nodiscard]] std::size_t playerCount() const noexcept;
    [[nodiscard]] std::optional<PlayerId> playerIdAt(std::size_t index) const noexcept;
    [[nodiscard]] bool setMasterPlayer(PlayerId playerId) noexcept;
    [[nodiscard]] std::optional<PlayerId> masterPlayerId() const noexcept;
    [[nodiscard]] bool requestResetToMaster(PlayerId playerId) noexcept;
    [[nodiscard]] bool resetToMasterPending(PlayerId playerId) const noexcept;
    [[nodiscard]] bool playerEventOverflowed(PlayerId playerId) const noexcept;
    [[nodiscard]] std::size_t playerDroppedEventCount(PlayerId playerId) const noexcept;
    [[nodiscard]] std::size_t playerInvalidEventCount(PlayerId playerId) const noexcept;
    void setPlayerMuted(PlayerId playerId, bool muted) noexcept;
    [[nodiscard]] bool playerMuted(PlayerId playerId) const noexcept;
    void setSuppression(
        PlayerId suppressorId,
        PlayerId suppressedId,
        bool enabled) noexcept;
    [[nodiscard]] bool suppression(
        PlayerId suppressorId,
        PlayerId suppressedId) const noexcept;

private:
    struct EventValidation
    {
        std::array<std::uint8_t, SequencerEventBuffer::capacity> valid {};
        bool failed = false;
    };

    struct EventDiagnostics
    {
        std::atomic_bool overflowed { false };
        std::atomic<std::size_t> droppedCount { 0 };
        std::atomic<std::size_t> invalidCount { 0 };
    };

    struct PlayerSlot
    {
        PlayerId id;
        IPlayer* player = nullptr;
        SequencerEventBuffer events;
        PlayerProcessResult processResult;
        EventValidation validation;
        std::unique_ptr<EventDiagnostics> diagnostics;
        std::unique_ptr<std::atomic_bool> muted;
        std::unique_ptr<std::atomic_bool> sequenceResetRequested;
        std::uint8_t sequenceResetRequestSnapshot = 0;
        std::vector<std::unique_ptr<std::atomic_bool>> suppresses;
    };

    struct Route
    {
        RouteId id;
        PlayerId playerId;
        ITransport* transport = nullptr;
        bool resetThisBlock = false;
    };

    std::vector<PlayerSlot> playerSlots_;
    std::vector<Route> routes_;
    std::optional<PlayerId> masterPlayerId_;
    bool topologyFrozen_ = false;

    [[nodiscard]] PlayerSlot* findSlot(PlayerId playerId) noexcept;
    [[nodiscard]] const PlayerSlot* findSlot(PlayerId playerId) const noexcept;
    [[nodiscard]] bool isSuppressed(
        PlayerId playerId,
        const SequencerEvent& event) const noexcept;
    [[nodiscard]] bool playerBlockFailed(PlayerId playerId) const noexcept;
    [[nodiscard]] std::size_t firstRouteFor(ITransport* transport) const noexcept;
    void resetPlayerOutputs(
        PlayerId playerId,
        const TimelineBlock& block) noexcept;
    void validatePlayerEvents(PlayerSlot& slot) noexcept;
};

} // namespace lps
