#pragma once

#include "core/IPlayer.h"
#include "core/IPlayerEditorModels.h"
#include "core/PatternLibrary.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

namespace lps
{

struct PatternPlayerPersistentState
{
    PatternId patternId;
    std::uint32_t hitMask = 0;
    int patternOffset = 0;
    std::uint16_t playbackStart = 0;
    std::uint16_t playbackEnd = 0;
    std::uint8_t playbackSpeed = 1;
};

class PatternPlayer final
    : public IPlayer,
      public IPatternEditorModel
{
public:
    static constexpr std::size_t longestPatternLength = Pattern::maxLength;
    static constexpr std::size_t playbackSpeedCount = 3;

    explicit PatternPlayer(const PatternLibrary& patternLibrary) noexcept;

    [[nodiscard]] PlayerRef playerRef() const noexcept override
    {
        return PlayerRef::pattern(id_);
    }
    void setRuntimeId(std::uint32_t id) noexcept override
    {
        id_ = PatternPlayerId {id};
    }
    void setAdvanceSource(AdvanceSource source) noexcept;
    [[nodiscard]] const AdvanceSource& advanceSource() const noexcept;
    void setPlayMode(PlayMode mode) noexcept { playMode_ = mode; }
    [[nodiscard]] PlayMode playMode() const noexcept { return playMode_; }
    void setTransitionPolicy(PatternTransitionPolicy policy) noexcept;
    [[nodiscard]] PatternTransitionPolicy transitionPolicy() const noexcept
    {
        return transitionPolicy_;
    }

    void selectPattern(PatternId patternId) noexcept;
    void selectSavedPattern(PatternId patternId) noexcept;
    [[nodiscard]] bool canSelectSavedPattern(PatternId patternId) const noexcept;
    [[nodiscard]] PatternId selectedPatternId() const noexcept;
    [[nodiscard]] PatternId activePatternId() const noexcept;
    void offsetPatternLeft() noexcept;
    void offsetPatternRight() noexcept;
    [[nodiscard]] int patternOffset() const noexcept;
    void setPlaybackSpeed(std::size_t speedIndex) noexcept;
    [[nodiscard]] std::size_t playbackSpeed() const noexcept;
    [[nodiscard]] static double playbackSpeedMultiplier(std::size_t speedIndex) noexcept;
    void setPlaybackWindow(std::size_t startStep, std::size_t endStep) noexcept;
    [[nodiscard]] std::size_t requestedPlaybackStart() const noexcept;
    [[nodiscard]] std::size_t requestedPlaybackEnd() const noexcept;
    void toggleStep(std::size_t visibleStep) noexcept;
    [[nodiscard]] Pattern patternForSave() const noexcept;
    [[nodiscard]] bool hasUnsavedPatternChanges() const noexcept;

    [[nodiscard]] PatternPlayerPersistentState capturePersistentState()
        const noexcept;
    [[nodiscard]] bool restorePersistentState(
        const PatternPlayerPersistentState& state) noexcept;

    void prepare(const PrepareSpec& spec) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] PlayerProcessResult process(
        const TimelineBlock& block,
        PlayerSignalBuffer& output) noexcept override;
    void command(
        PlayerCommand command,
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept override;
    void advanceFromPatternHit(
        const PlayerSignal& hit,
        PlayerSignalBuffer& output) noexcept override;
    [[nodiscard]] bool observeCycleBoundary(
        const PlayerSignal& boundary,
        PlayerSignalBuffer& output) noexcept;
    // Applies the currently requested saved-pattern selection at an explicit
    // musical boundary. Runtime schedulers use this to quantize a selection
    // to a shared master cycle instead of this player's local cycle.
    [[nodiscard]] bool activateSelectedPatternAtBoundary(
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept;
    [[nodiscard]] PlayerSyncCapabilities syncCapabilities() const noexcept override;

    [[nodiscard]] PatternView patternView() const noexcept override;
    [[nodiscard]] PatternPlaybackSnapshot patternPlaybackSnapshot()
        const noexcept override;
private:
    const PatternLibrary& patternLibrary_;
    PatternPlayerId id_;
    AdvanceSource advanceSource_ { ClockAdvance {0.25} };
    PlayMode playMode_ = PlayMode::continuous;
    PatternTransitionPolicy transitionPolicy_;
    // PatternLibrary IDs are bounded by its fixed capacity. The otherwise
    // unused high bit makes the selection and its activation behavior one
    // atomic request, so the audio thread cannot observe a torn pair.
    static constexpr std::uint64_t resetOffsetOnActivationFlag =
        std::uint64_t { 1 } << 63u;
    static constexpr std::uint64_t requestedPatternIdMask =
        ~resetOffsetOnActivationFlag;
    static_assert(PatternLibrary::maxEntryCount < resetOffsetOnActivationFlag);

    std::atomic<std::uint64_t> requestedPatternSelection_ { 0 };
    // Pattern activation and UI edits change several independently atomic
    // values as one logical state transition. Writers claim an odd revision;
    // readers retry until they observe the same even revision on both sides.
    std::atomic<std::uint64_t> activationRevision_ { 0 };
    std::atomic<std::uint64_t> activePatternId_ { 0 };
    std::atomic<std::uint32_t> editableHitMask_ { 0 };
    std::atomic<int> patternOffset_ { 0 };
    std::atomic<std::size_t> playbackSpeed_ { 1 };
    std::atomic<std::uint32_t> requestedPlaybackWindow_ { 0x001f0000 };
    std::atomic<std::uint32_t> activePlaybackWindow_ { 0x001f0000 };
    PatternPlaybackSnapshot patternPlaybackSnapshot_;

    static constexpr double baseStepLengthPpq = 0.25;
    double playbackOriginPpq_ = 0.0;
    std::int64_t lastTriggeredPlaybackStep_ = std::numeric_limits<std::int64_t>::min();
    std::int64_t playbackWindowOriginStep_ = 0;
    std::uint64_t nextTriggerId_ = 1;
    bool commandPlaying_ = true;
    bool completed_ = false;
    bool processingExternalAdvance_ = false;
    std::uint64_t externalAdvanceCount_ = 0;
    double lastResetAndPlayPpq_ = -std::numeric_limits<double>::infinity();

    [[nodiscard]] static std::uint32_t packPlaybackWindow(
        std::size_t startStep, std::size_t endStep) noexcept;
    static void unpackPlaybackWindow(
        std::uint32_t packed, std::size_t& startStep, std::size_t& endStep) noexcept;
    [[nodiscard]] static std::size_t patternLength(const Pattern& pattern) noexcept;
    [[nodiscard]] static std::uint32_t patternHitMask(const Pattern& pattern) noexcept;
    [[nodiscard]] std::uint64_t requestedPatternSelection() const noexcept;
    [[nodiscard]] static PatternId patternIdFromSelection(
        std::uint64_t selection) noexcept;
    [[nodiscard]] static bool selectionResetsOffset(std::uint64_t selection) noexcept;
    void consumeSaveSelectionRequest(std::uint64_t selection) noexcept;
    [[nodiscard]] bool activatePattern(PatternId patternId, bool resetOffset) noexcept;

    class DraftWriteGuard
    {
    public:
        DraftWriteGuard(PatternPlayer& owner, bool waitForAccess) noexcept;
        ~DraftWriteGuard();

        DraftWriteGuard(const DraftWriteGuard&) = delete;
        DraftWriteGuard& operator=(const DraftWriteGuard&) = delete;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return owner_ != nullptr;
        }

    private:
        PatternPlayer* owner_ = nullptr;
        std::uint64_t previousRevision_ = 0;
    };

    struct DraftSnapshot
    {
        PatternId activePatternId;
        std::uint32_t hitMask = 0;
        int patternOffset = 0;
        std::size_t playbackStart = 0;
        std::size_t playbackEnd = 0;
    };

    [[nodiscard]] DraftSnapshot draftSnapshot() const noexcept;
    [[nodiscard]] static Pattern makePatternForSave(
        const DraftSnapshot& draft) noexcept;
};

} // namespace lps
