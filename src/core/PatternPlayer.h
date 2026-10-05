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
    Pattern pattern;
    std::int32_t patternOffsetTicks = 0;
    PatternTick playbackStartTick = 0;
    PatternTick playbackEndTick = 0;
    std::uint8_t playbackSpeed = 1;
};

class PatternPlayer final : public IPlayer, public IPatternEditorModel
{
public:
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
    [[nodiscard]] bool adoptSavedDraft(PatternId patternId) noexcept;
    [[nodiscard]] bool canSelectSavedPattern(PatternId patternId) const noexcept;
    [[nodiscard]] PatternId selectedPatternId() const noexcept;
    [[nodiscard]] PatternId activePatternId() const noexcept;

    void offsetPatternLeft(PatternTick amount = Pattern::legacyStepTicks) noexcept;
    void offsetPatternRight(PatternTick amount = Pattern::legacyStepTicks) noexcept;
    [[nodiscard]] int patternOffset() const noexcept;
    [[nodiscard]] std::int32_t patternOffsetTicks() const noexcept;
    void setPatternOffsetTicks(std::int32_t offset) noexcept;

    void setPlaybackSpeed(std::size_t speedIndex) noexcept;
    [[nodiscard]] std::size_t playbackSpeed() const noexcept;
    [[nodiscard]] static double playbackSpeedMultiplier(
        std::size_t speedIndex) noexcept;

    // The playback window is half-open: [startTick, endTick).
    void setPlaybackWindow(std::size_t startTick, std::size_t endTick) noexcept;
    [[nodiscard]] std::size_t requestedPlaybackStart() const noexcept;
    [[nodiscard]] std::size_t requestedPlaybackEnd() const noexcept;

    [[nodiscard]] bool addHit(
        PatternTick startTick,
        PatternTick durationTicks = Pattern::legacyStepTicks) noexcept;
    [[nodiscard]] bool removeHit(PatternTick startTick) noexcept;
    [[nodiscard]] bool moveHit(
        PatternTick oldStartTick, PatternTick newStartTick) noexcept;
    [[nodiscard]] bool resizeHit(
        PatternTick startTick, PatternTick durationTicks) noexcept;
    [[nodiscard]] bool setCycleLength(PatternTick cycleLengthTicks) noexcept;
    // Compatibility helper for existing callers during the UI transition.
    void toggleStep(std::size_t visibleStep) noexcept;

    [[nodiscard]] Pattern patternForSave() const noexcept;
    [[nodiscard]] bool hasUnsavedPatternChanges() const noexcept;
    [[nodiscard]] bool installNewDraft(Pattern pattern) noexcept;

    [[nodiscard]] PatternPlayerPersistentState capturePersistentState() const noexcept;
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
    [[nodiscard]] bool activateSelectedPatternAtBoundary(
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept;
    [[nodiscard]] PlayerSyncCapabilities syncCapabilities() const noexcept override;

    [[nodiscard]] PatternView patternView() const noexcept override;
    [[nodiscard]] PatternPlaybackSnapshot patternPlaybackSnapshot() const noexcept override;

private:
    struct DraftSnapshot
    {
        PatternId activePatternId;
        Pattern pattern;
        std::int32_t patternOffsetTicks = 0;
        PatternTick playbackStartTick = 0;
        PatternTick playbackEndTick = 0;
    };

    const PatternLibrary& patternLibrary_;
    PatternPlayerId id_;
    AdvanceSource advanceSource_ {ClockAdvance {0.25}};
    PlayMode playMode_ = PlayMode::continuous;
    PatternTransitionPolicy transitionPolicy_;

    static constexpr std::uint64_t resetOffsetOnActivationFlag =
        std::uint64_t {1} << 63u;
    static constexpr std::uint64_t requestedPatternIdMask =
        ~resetOffsetOnActivationFlag;
    static_assert(PatternLibrary::maxEntryCount < resetOffsetOnActivationFlag);
    std::atomic<std::uint64_t> requestedPatternSelection_ {0};

    // Every draft field is atomic. The revision makes a multi-field snapshot
    // coherent. Audio performs one bounded attempt and otherwise keeps its
    // previous coherent snapshot; UI/control readers may retry.
    std::atomic<std::uint64_t> draftRevision_ {0};
    std::atomic<std::uint64_t> activePatternId_ {0};
    std::atomic<PatternTick> draftCycleLengthTicks_ {0};
    std::atomic<std::uint16_t> draftHitCount_ {0};
    std::array<std::atomic<std::uint64_t>, Pattern::maximumHitCount> draftHits_ {};
    std::atomic<std::int32_t> patternOffsetTicks_ {0};
    std::atomic<PatternTick> requestedPlaybackStartTick_ {0};
    std::atomic<PatternTick> requestedPlaybackEndTick_ {0};
    std::atomic<std::size_t> playbackSpeed_ {1};

    DraftSnapshot audioDraft_;
    PatternTick activePlaybackStartTick_ = 0;
    PatternTick activePlaybackEndTick_ = 0;
    PatternPlaybackSnapshot patternPlaybackSnapshot_;

    double playbackOriginPpq_ = 0.0;
    std::int64_t externalAdvanceCount_ = 0;
    std::uint64_t nextTriggerId_ = 1;
    bool commandPlaying_ = true;
    bool completed_ = false;
    bool processingExternalAdvance_ = false;
    double lastResetAndPlayPpq_ = -std::numeric_limits<double>::infinity();

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

    [[nodiscard]] static std::uint64_t packHit(PatternHit hit) noexcept;
    [[nodiscard]] static PatternHit unpackHit(std::uint64_t packed) noexcept;
    [[nodiscard]] std::uint64_t requestedPatternSelection() const noexcept;
    [[nodiscard]] static PatternId patternIdFromSelection(
        std::uint64_t selection) noexcept;
    [[nodiscard]] static bool selectionResetsOffset(
        std::uint64_t selection) noexcept;
    void consumeSaveSelectionRequest(std::uint64_t selection) noexcept;
    [[nodiscard]] bool activatePattern(PatternId patternId, bool resetOffset) noexcept;
    void storeDraft(const DraftSnapshot& draft) noexcept;
    [[nodiscard]] bool tryDraftSnapshot(DraftSnapshot& result) const noexcept;
    [[nodiscard]] DraftSnapshot draftSnapshot() const noexcept;
    void adoptDraftForAudio() noexcept;
    [[nodiscard]] bool replaceDraftPattern(Pattern pattern) noexcept;
    [[nodiscard]] PatternTick rotatedTick(
        PatternTick sourceTick, const DraftSnapshot& draft) const noexcept;
    [[nodiscard]] double tickDurationPpq(PatternTick ticks) const noexcept;
    void processClockRange(
        double rangeStart,
        double rangeEnd,
        PlayerSignalBuffer& output,
        PlayerProcessResult& result) noexcept;
};

} // namespace lps
