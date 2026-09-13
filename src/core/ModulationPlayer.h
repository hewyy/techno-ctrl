#pragma once

#include "core/IPlayer.h"
#include "core/IPlayerEditorModels.h"
#include "core/ModulationLibrary.h"
#include "core/SequencerTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace lps
{

struct ModulationPlayerStatus
{
    ModulationId selectedModulationId;
    ModulationId activeModulationId;
    int currentStep = -1;
    bool playing = false;
};

// Runtime cursor for a reusable, meaning-free modulation sequence. Parameter
// mapping belongs to Voice; this player only emits exact normalized values.
class ModulationPlayer final : public IPlayer, public IModulationEditorModel
{
public:
    explicit ModulationPlayer(
        const ModulationLibrary& library,
        ModulationPlayerId id = {}) noexcept;

    void setId(ModulationPlayerId id) noexcept { id_ = id; }
    [[nodiscard]] ModulationPlayerId id() const noexcept { return id_; }
    [[nodiscard]] PlayerRef playerRef() const noexcept override
    {
        return PlayerRef::modulation(id_);
    }
    void setRuntimeId(std::uint32_t id) noexcept override
    {
        id_ = ModulationPlayerId {id};
    }

    void setAdvanceSource(AdvanceSource source) noexcept;
    [[nodiscard]] const AdvanceSource& advanceSource() const noexcept;
    void setPlayMode(PlayMode mode) noexcept { playMode_ = mode; }
    [[nodiscard]] PlayMode playMode() const noexcept { return playMode_; }

    void selectModulation(ModulationId id) noexcept;
    [[nodiscard]] ModulationId selectedModulationId() const noexcept;
    [[nodiscard]] ModulationId activeModulationId() const noexcept;
    void setValue(std::size_t step, NormalizedValue value) noexcept;
    void setUnipolar8Value(std::size_t step, std::uint8_t value) noexcept;
    void setLength(std::size_t length) noexcept;
    [[nodiscard]] Modulation modulationForUi() const noexcept;
    [[nodiscard]] Modulation modulationForSave() const noexcept;
    [[nodiscard]] bool hasUnsavedChanges() const noexcept;

    void prepare(const PrepareSpec&) noexcept override;
    void reset() noexcept override;
    void command(
        PlayerCommand command,
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept override;
    [[nodiscard]] PlayerProcessResult process(
        const TimelineBlock& block,
        PlayerSignalBuffer& output) noexcept override;
    void processClock(
        const TimelineBlock& block,
        PlayerSignalBuffer& output) noexcept;
    void advanceFromPatternHit(
        PatternPlayerId source,
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept;
    void advanceFromPatternHit(
        const PlayerSignal& hit,
        PlayerSignalBuffer& output) noexcept override;
    [[nodiscard]] PlayerSyncCapabilities syncCapabilities() const noexcept override
    {
        return {};
    }

    [[nodiscard]] ModulationPlayerStatus status() const noexcept;
    [[nodiscard]] ModulationPlaybackSnapshot modulationPlaybackSnapshot()
        const noexcept override;

private:
    class DraftWriteGuard
    {
    public:
        explicit DraftWriteGuard(ModulationPlayer& owner) noexcept;
        ~DraftWriteGuard();

        DraftWriteGuard(const DraftWriteGuard&) = delete;
        DraftWriteGuard& operator=(const DraftWriteGuard&) = delete;

    private:
        ModulationPlayer& owner_;
        std::uint64_t previousRevision_ = 0;
    };

    [[nodiscard]] bool activateRequestedSelection() noexcept;
    [[nodiscard]] bool readDraft(Modulation& result) const noexcept;
    [[nodiscard]] bool evaluateStep(
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept;
    void resetPosition() noexcept;
    void scheduleNextClock(double commandPpq) noexcept;

    const ModulationLibrary& library_;
    ModulationPlayerId id_;
    AdvanceSource advanceSource_ { ClockAdvance {} };
    PlayMode playMode_ = PlayMode::continuous;

    std::atomic<std::uint64_t> requestedModulationId_ {0};
    std::atomic<std::uint64_t> activeModulationId_ {0};
    std::atomic<std::uint64_t> draftRevision_ {0};
    std::array<std::atomic<std::uint16_t>, Modulation::maxLength> values_ {};
    std::atomic<std::uint8_t> length_ {0};
    std::atomic<int> currentStep_ {-1};
    std::atomic_bool playing_ {false};

    std::uint8_t nextStep_ = 0;
    bool completed_ = false;
    double nextClockPpq_ = std::numeric_limits<double>::infinity();
    double lastResetAndPlayPpq_ = -std::numeric_limits<double>::infinity();
};

} // namespace lps
