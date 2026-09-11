#pragma once

#include "core/IPlayer.h"
#include "core/IOutputRenderer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace lps
{

class SequencerEngine
{
public:
    // Registered players and connected renderers must outlive the engine.
    // Registration and connection are message-thread operations only.
    [[nodiscard]] std::optional<PlayerId> registerPlayer(IPlayer& player);
    [[nodiscard]] std::optional<RouteId> connect(
        PlayerId playerId,
        IOutputRenderer& renderer,
        RouteMapping mapping = {});

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
        IOutputRenderer* renderer = nullptr;
        RouteMapping mapping;
    };

    struct RendererSlot
    {
        IOutputRenderer* renderer = nullptr;
        std::vector<RoutedEvent> events;
        bool resetThisBlock = false;
    };

    std::vector<PlayerSlot> playerSlots_;
    std::vector<Route> routes_;
    std::vector<RendererSlot> rendererSlots_;
    std::vector<RoutedEvent> routedEvents_;
    std::optional<PlayerId> masterPlayerId_;
    bool topologyFrozen_ = false;

    [[nodiscard]] PlayerSlot* findSlot(PlayerId playerId) noexcept;
    [[nodiscard]] const PlayerSlot* findSlot(PlayerId playerId) const noexcept;
    [[nodiscard]] bool isSuppressed(
        PlayerId playerId,
        const SequencerEvent& event) const noexcept;
    [[nodiscard]] bool playerBlockFailed(PlayerId playerId) const noexcept;
    [[nodiscard]] RendererSlot* findRenderer(IOutputRenderer* renderer) noexcept;
    void validatePlayerEvents(
        PlayerSlot& slot,
        const TimelineBlock& block) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> frameOffsetFor(
        const SequencerEvent& event,
        const TimelineBlock& block) noexcept;
};

} // namespace lps
