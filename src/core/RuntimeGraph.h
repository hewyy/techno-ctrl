#pragma once

#include "core/ModulationPlayer.h"
#include "core/IOutputRenderer.h"
#include "core/PatternPlayer.h"
#include "core/Voice.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace lps
{

enum class ControlSourcePort : std::uint8_t
{
    hit,
    cycleBoundary
};

struct ControlSource
{
    PatternPlayerId player;
    ControlSourcePort port = ControlSourcePort::hit;
};

struct TriggerBinding
{
    TriggerBindingId id;
    PatternPlayerId source;
    VoiceId destination;
};

struct ParameterBinding
{
    ParameterBindingId id;
    ModulationPlayerId source;
    VoiceId voice;
    VoiceParameterId parameter;
    // Sampled parameters may be scoped to the PatternPlayer whose trigger
    // should consume this value. An invalid ID keeps the historical
    // Voice-wide behavior and is required for continuous parameters.
    PatternPlayerId triggerScope;
};

struct CommandBinding
{
    CommandBindingId id;
    ControlSource source;
    PlayerRef destination;
    PlayerCommand command = PlayerCommand::play;
};

enum class OutputSignalType : std::uint8_t
{
    triggers,
    continuousParameter
};

struct OutputBinding
{
    OutputBindingId id;
    VoiceId source;
    OutputEndpointId endpoint;
    RouteId route;
    VoiceParameterId parameter;
    OutputSignalType signal = OutputSignalType::triggers;
};

struct PatternPlayerRuntimeConfig
{
    PatternPlayerId player;
    AdvanceSource advanceSource {ClockAdvance {0.25}};
    PlayMode playMode = PlayMode::continuous;
    PatternTransitionPolicy transitionPolicy;
};

struct ModulationPlayerRuntimeConfig
{
    ModulationPlayerId player;
    AdvanceSource advanceSource {ClockAdvance {0.25}};
    PlayMode playMode = PlayMode::continuous;
    bool quantizeSelectionToPatternCycle = false;
};

struct RuntimeGraphConfig
{
    static constexpr std::size_t maximumTriggerBindings = 32;
    static constexpr std::size_t maximumParameterBindings = 64;
    static constexpr std::size_t maximumCommandBindings = 64;
    static constexpr std::size_t maximumOutputBindings = 64;
    static constexpr std::size_t maximumPatternPlayerConfigs = 16;
    static constexpr std::size_t maximumModulationPlayerConfigs = 64;

    std::array<TriggerBinding, maximumTriggerBindings> triggerBindings {};
    std::array<ParameterBinding, maximumParameterBindings> parameterBindings {};
    std::array<CommandBinding, maximumCommandBindings> commandBindings {};
    std::array<OutputBinding, maximumOutputBindings> outputBindings {};
    std::array<PatternPlayerRuntimeConfig, maximumPatternPlayerConfigs>
        patternPlayerConfigs {};
    std::array<ModulationPlayerRuntimeConfig, maximumModulationPlayerConfigs>
        modulationPlayerConfigs {};
    std::size_t triggerBindingCount = 0;
    std::size_t parameterBindingCount = 0;
    std::size_t commandBindingCount = 0;
    std::size_t outputBindingCount = 0;
    std::size_t patternPlayerConfigCount = 0;
    std::size_t modulationPlayerConfigCount = 0;

    [[nodiscard]] bool add(TriggerBinding binding) noexcept;
    [[nodiscard]] bool add(ParameterBinding binding) noexcept;
    [[nodiscard]] bool add(CommandBinding binding) noexcept;
    [[nodiscard]] bool add(OutputBinding binding) noexcept;
    [[nodiscard]] bool addPlayerConfig(
        PatternPlayerRuntimeConfig config) noexcept;
    [[nodiscard]] bool addPlayerConfig(
        ModulationPlayerRuntimeConfig config) noexcept;
};

enum class GraphValidationError : std::uint8_t
{
    none,
    capacityExceeded,
    missingNode,
    duplicateBindingId,
    duplicateBinding,
    selfEdge,
    controlCycle,
    multipleParameterSources,
    unsupportedOutputSignal
};

struct ResolvedVoiceEvent
{
    SequencerEvent event;
    VoiceId sourceVoiceId;
    PatternPlayerId triggerSource;
    std::uint64_t stableOrder = 0;
    // Captured while resolving the event so a mute change at a cycle
    // boundary remains sample-accurate even when one audio block straddles it.
    bool eligible = true;
};

class ResolvedVoiceEventBuffer
{
public:
    static constexpr std::size_t capacity = 512;
    void clear() noexcept { size_ = 0; overflowed_ = false; }
    [[nodiscard]] bool push(ResolvedVoiceEvent event) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] const ResolvedVoiceEvent& operator[](
        std::size_t index) const noexcept { return events_[index]; }
    [[nodiscard]] ResolvedVoiceEvent* begin() noexcept { return events_.data(); }
    [[nodiscard]] ResolvedVoiceEvent* end() noexcept { return events_.data() + size_; }
    [[nodiscard]] const ResolvedVoiceEvent* begin() const noexcept { return events_.data(); }
    [[nodiscard]] const ResolvedVoiceEvent* end() const noexcept { return events_.data() + size_; }

private:
    std::array<ResolvedVoiceEvent, capacity> events_ {};
    std::size_t size_ = 0;
    bool overflowed_ = false;
};

class RuntimeGraph
{
public:
    static constexpr std::size_t maximumPatternPlayers = 16;
    static constexpr std::size_t maximumModulationPlayers = 64;
    static constexpr std::size_t maximumVoices = 16;
    static constexpr std::size_t maximumArmedCommandGroups = 128;
    static constexpr std::size_t maximumArmedCommands = 192;
    static constexpr std::size_t maximumScheduledBars = 8;
    static constexpr std::size_t maximumScheduledChanges = 128;
    static constexpr std::size_t maximumWorkSignals = 1024;
    static constexpr std::size_t maximumOutputEndpoints = 8;
    static constexpr std::size_t maximumRoutedEventsPerEndpoint = 1024;
    static_assert(maximumPatternPlayers <= 16 && maximumVoices <= 16);
    static_assert(PatternLibrary::maxEntryCount <= 0x1ffu);

    [[nodiscard]] bool registerPatternPlayer(PatternPlayer& player) noexcept;
    [[nodiscard]] bool registerModulationPlayer(ModulationPlayer& player) noexcept;
    [[nodiscard]] bool registerVoice(Voice& voice) noexcept;
    [[nodiscard]] bool registerOutputEndpoint(
        OutputEndpointId id,
        IOutputRenderer& renderer) noexcept;

    [[nodiscard]] GraphValidationError validate(
        const RuntimeGraphConfig& candidate) const noexcept;
    [[nodiscard]] bool activate(const RuntimeGraphConfig& candidate) noexcept;
    [[nodiscard]] bool publish(const RuntimeGraphConfig& candidate) noexcept;
    [[nodiscard]] std::uint64_t publishedGeneration() const noexcept
    {
        return publishedGeneration_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t activeGeneration() const noexcept
    {
        return activeGeneration_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool configureArmedCycleCommand(
        std::size_t slot,
        PatternPlayerId source,
        PlayerRef destination,
        PlayerCommand command) noexcept;
    [[nodiscard]] bool configureArmedCommand(
        std::size_t slot,
        ControlSource source,
        PlayerRef destination,
        PlayerCommand command) noexcept;
    [[nodiscard]] bool armCommand(std::size_t slot) noexcept;
    [[nodiscard]] bool commandPending(std::size_t slot) const noexcept;
    // Compatibility names for the original cycle-only reset API.
    [[nodiscard]] bool armCycleCommand(std::size_t slot) noexcept;
    [[nodiscard]] bool cycleCommandPending(std::size_t slot) const noexcept;
    [[nodiscard]] bool configureBarClock(PatternPlayerId source) noexcept;
    [[nodiscard]] bool scheduleVoiceMute(
        VoiceId voice,
        bool muted,
        std::size_t barsFromNow = 1) noexcept;
    [[nodiscard]] bool schedulePatternPlayerMute(
        PatternPlayerId player,
        bool muted,
        std::size_t barsFromNow = 1) noexcept;
    [[nodiscard]] bool schedulePatternSelection(
        PatternPlayerId player,
        PatternId pattern,
        std::size_t barsFromNow = 1) noexcept;
    [[nodiscard]] std::optional<bool> scheduledVoiceMute(
        VoiceId voice) const noexcept;
    [[nodiscard]] std::optional<bool> scheduledPatternPlayerMute(
        PatternPlayerId player) const noexcept;
    [[nodiscard]] std::optional<PatternId> scheduledPatternSelection(
        PatternPlayerId player) const noexcept;
    [[nodiscard]] std::size_t scheduledVoiceMuteBarsRemaining(
        VoiceId voice) const noexcept;
    [[nodiscard]] std::size_t scheduledPatternPlayerMuteBarsRemaining(
        PatternPlayerId player) const noexcept;
    [[nodiscard]] bool voiceMuteScheduledAtBarOffset(
        VoiceId voice,
        bool muted,
        std::size_t barsFromNow) const noexcept;
    [[nodiscard]] bool patternPlayerMuteScheduledAtBarOffset(
        PatternPlayerId player,
        bool muted,
        std::size_t barsFromNow) const noexcept;
    [[nodiscard]] std::size_t scheduledPatternBarsRemaining(
        PatternPlayerId player) const noexcept;
    [[nodiscard]] std::optional<PatternId>
        patternSelectionScheduledAtBarOffset(
            PatternPlayerId player,
            std::size_t barsFromNow) const noexcept;
    [[nodiscard]] std::size_t scheduledChangeCountAtBarOffset(
        std::size_t barsFromNow) const noexcept;
    [[nodiscard]] std::size_t currentBar() const noexcept
    {
        return currentBar_.load(std::memory_order_acquire);
    }
    void prepare(const PrepareSpec& spec) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(
        const TimelineBlock& block,
        ResolvedVoiceEventBuffer& output) noexcept;
    [[nodiscard]] bool render(
        const TimelineBlock& block,
        const ResolvedVoiceEventBuffer& events) noexcept;
    void resetOutputs() noexcept;
    void setVoiceMuted(VoiceId voice, bool muted) noexcept;
    [[nodiscard]] bool voiceMuted(VoiceId voice) const noexcept;
    void setPatternPlayerMuted(
        PatternPlayerId player, bool muted) noexcept;
    [[nodiscard]] bool patternPlayerMuted(
        PatternPlayerId player) const noexcept;
    void setSuppression(
        PatternPlayerId suppressor,
        PatternPlayerId suppressed,
        bool enabled) noexcept;
    [[nodiscard]] bool suppression(
        PatternPlayerId suppressor,
        PatternPlayerId suppressed) const noexcept;

    [[nodiscard]] GraphValidationError lastValidationError() const noexcept
    {
        return lastValidationError_;
    }
    [[nodiscard]] bool workOverflowed() const noexcept
    {
        return workOverflowed_;
    }
    [[nodiscard]] bool patternHitOccurred(
        PatternPlayerId source,
        double ppqPosition) const noexcept;

private:
    struct WorkSignal
    {
        PlayerSignal signal;
        std::uint64_t order = 0;
        bool propagated = false;
        bool resolved = false;
    };

    struct ArmedCommand
    {
        std::size_t group = 0;
        ControlSource source;
        PlayerRef destination;
        PlayerCommand command = PlayerCommand::resetAndPlay;
        bool configured = false;
    };

    struct OutputEndpoint
    {
        OutputEndpointId id;
        IOutputRenderer* renderer = nullptr;
        std::array<RoutedEvent, maximumRoutedEventsPerEndpoint> events {};
        std::size_t eventCount = 0;
    };

    struct AudibleTriggerState
    {
        TriggerId id;
        bool eligible = false;
        bool active = false;
    };

    enum class ScheduledChangeType : std::uint8_t
    {
        voiceMute,
        patternSelection,
        patternPlayerMute
    };

    struct ScheduledChangeView
    {
        ScheduledChangeType type = ScheduledChangeType::voiceMute;
        std::size_t targetIndex = 0;
        std::uint16_t value = 0;
        std::size_t barsRemaining = 0;
        std::uint32_t sequence = 0;
    };

    [[nodiscard]] PatternPlayer* find(PatternPlayerId id) const noexcept;
    [[nodiscard]] ModulationPlayer* find(ModulationPlayerId id) const noexcept;
    [[nodiscard]] Voice* find(VoiceId id) const noexcept;
    [[nodiscard]] OutputEndpoint* find(OutputEndpointId id) noexcept;
    [[nodiscard]] const OutputEndpoint* find(OutputEndpointId id) const noexcept;
    [[nodiscard]] IPlayer* find(PlayerRef ref) const noexcept;
    [[nodiscard]] bool appendSignals(const PlayerSignalBuffer& signals) noexcept;
    void invalidatePlayerSignalsFrom(
        PlayerRef player,
        double ppqPosition,
        std::size_t limit) noexcept;
    void appendPlayerRemainder(IPlayer& player, double ppqPosition) noexcept;
    void applyArmedCommands(const PlayerSignal& source) noexcept;
    void appendVoiceEvents(
        Voice& voice,
        PatternPlayerId triggerSource,
        const SequencerEventBuffer& events,
        ResolvedVoiceEventBuffer& output) noexcept;
    [[nodiscard]] bool triggerEligible(
        std::size_t sourceVoiceIndex,
        PatternPlayerId triggerSource,
        const SequencerEvent& event) noexcept;
    [[nodiscard]] bool scheduleChange(
        ScheduledChangeType type,
        std::size_t targetIndex,
        std::uint16_t value,
        std::size_t barsFromNow) noexcept;
    [[nodiscard]] static std::uint64_t encodeScheduledChange(
        const ScheduledChangeView& change) noexcept;
    [[nodiscard]] static ScheduledChangeView decodeScheduledChange(
        std::uint64_t encoded) noexcept;
    [[nodiscard]] static bool scheduledChangeActive(
        std::uint64_t encoded) noexcept;
    [[nodiscard]] std::optional<ScheduledChangeView> nextScheduledChange(
        ScheduledChangeType type,
        std::size_t targetIndex) const noexcept;
    void applyScheduledBarBoundary(
        const PlayerSignal& boundary) noexcept;
    void resolveTimestamp(double ppq, ResolvedVoiceEventBuffer& output) noexcept;
    [[nodiscard]] bool hasControlCycle(
        const RuntimeGraphConfig& candidate) const noexcept;
    [[nodiscard]] const RuntimeGraphConfig& activeConfig() const noexcept;
    void adoptPublishedConfig() noexcept;
    void applyActivePlayerConfigs() noexcept;
    [[nodiscard]] std::optional<std::uint32_t> frameOffsetFor(
        const SequencerEvent& event,
        const TimelineBlock& block) const noexcept;
    [[nodiscard]] std::size_t patternPlayerIndex(
        PatternPlayerId id) const noexcept;
    [[nodiscard]] std::size_t voiceIndex(VoiceId id) const noexcept;

    std::array<PatternPlayer*, maximumPatternPlayers> patternPlayers_ {};
    std::array<ModulationPlayer*, maximumModulationPlayers> modulationPlayers_ {};
    std::array<Voice*, maximumVoices> voices_ {};
    std::array<OutputEndpoint, maximumOutputEndpoints> outputEndpoints_ {};
    std::size_t patternPlayerCount_ = 0;
    std::size_t modulationPlayerCount_ = 0;
    std::size_t voiceCount_ = 0;
    std::size_t outputEndpointCount_ = 0;
    static constexpr std::size_t snapshotCount = 3;
    std::array<RuntimeGraphConfig, snapshotCount> configSnapshots_ {};
    std::array<std::uint64_t, snapshotCount> snapshotGenerations_ {};
    std::atomic<std::uint8_t> publishedSnapshot_ {0};
    std::atomic<std::uint8_t> acknowledgedSnapshot_ {0};
    std::atomic<std::uint64_t> publishedGeneration_ {0};
    std::atomic<std::uint64_t> activeGeneration_ {0};
    std::uint64_t nextPublicationGeneration_ = 0;
    std::uint8_t audioSnapshot_ = 0;
    std::array<ArmedCommand, maximumArmedCommands> armedCommands_ {};
    std::size_t armedCommandCount_ = 0;
    std::array<std::atomic_bool, maximumArmedCommandGroups>
        armedCommandPending_ {};
    std::array<std::atomic_bool, maximumVoices> voiceMuted_ {};
    std::array<std::atomic_bool, maximumPatternPlayers>
        patternPlayerMuted_ {};
    std::array<std::atomic<std::uint64_t>, maximumScheduledChanges>
        scheduledChanges_ {};
    std::atomic<std::uint32_t> nextScheduledSequence_ {1};
    PatternPlayerId barClockSource_;
    std::atomic<std::uint8_t> currentBar_ {0};
    double lastBarBoundaryPpq_ = -std::numeric_limits<double>::infinity();
    std::array<std::array<std::atomic_bool, maximumPatternPlayers>,
        maximumPatternPlayers> suppression_ {};
    std::array<std::array<AudibleTriggerState,
        Voice::maximumActiveTriggerCount>, maximumVoices> audibleTriggers_ {};
    PrepareSpec prepareSpec_;
    TimelineBlock currentBlock_;
    std::array<WorkSignal, maximumWorkSignals> work_ {};
    std::size_t workSize_ = 0;
    std::uint64_t nextWorkOrder_ = 0;
    std::uint64_t nextResolvedOrder_ = 0;
    double currentPpqPerSample_ = 0.0;
    GraphValidationError lastValidationError_ = GraphValidationError::none;
    bool prepared_ = false;
    bool workOverflowed_ = false;
    bool transportStateKnown_ = false;
    bool lastTransportPlaying_ = false;
    bool processIssueActive_ = false;
    bool unpreparedProcessReported_ = false;
    std::uint64_t statusSampleCount_ = 0;
    std::uint64_t statusBlockCount_ = 0;
};

} // namespace lps
