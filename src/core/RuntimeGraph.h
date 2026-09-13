#pragma once

#include "core/ModulationPlayer.h"
#include "core/PatternPlayer.h"
#include "core/Voice.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

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
};

struct CommandBinding
{
    CommandBindingId id;
    ControlSource source;
    PlayerRef destination;
    PlayerCommand command = PlayerCommand::play;
};

struct RuntimeGraphConfig
{
    static constexpr std::size_t maximumTriggerBindings = 32;
    static constexpr std::size_t maximumParameterBindings = 64;
    static constexpr std::size_t maximumCommandBindings = 64;

    std::array<TriggerBinding, maximumTriggerBindings> triggerBindings {};
    std::array<ParameterBinding, maximumParameterBindings> parameterBindings {};
    std::array<CommandBinding, maximumCommandBindings> commandBindings {};
    std::size_t triggerBindingCount = 0;
    std::size_t parameterBindingCount = 0;
    std::size_t commandBindingCount = 0;

    [[nodiscard]] bool add(TriggerBinding binding) noexcept;
    [[nodiscard]] bool add(ParameterBinding binding) noexcept;
    [[nodiscard]] bool add(CommandBinding binding) noexcept;
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
    multipleTriggerSources,
    multipleParameterSources
};

struct ResolvedVoiceEvent
{
    SequencerEvent event;
    VoiceId sourceVoiceId;
    PatternPlayerId triggerSource;
    std::uint64_t stableOrder = 0;
    bool triggerOutputEligible = true;
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
    static constexpr std::size_t maximumWorkSignals = 1024;

    [[nodiscard]] bool registerPatternPlayer(PatternPlayer& player) noexcept;
    [[nodiscard]] bool registerModulationPlayer(ModulationPlayer& player) noexcept;
    [[nodiscard]] bool registerVoice(Voice& voice) noexcept;

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
    [[nodiscard]] bool armCycleCommand(std::size_t slot) noexcept;
    [[nodiscard]] bool cycleCommandPending(std::size_t slot) const noexcept;
    void prepare(const PrepareSpec& spec) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool process(
        const TimelineBlock& block,
        ResolvedVoiceEventBuffer& output) noexcept;

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

    struct ArmedCycleCommand
    {
        PatternPlayerId source;
        PlayerRef destination;
        PlayerCommand command = PlayerCommand::resetAndPlay;
        bool configured = false;
    };

    [[nodiscard]] PatternPlayer* find(PatternPlayerId id) const noexcept;
    [[nodiscard]] ModulationPlayer* find(ModulationPlayerId id) const noexcept;
    [[nodiscard]] Voice* find(VoiceId id) const noexcept;
    [[nodiscard]] IPlayer* find(PlayerRef ref) const noexcept;
    [[nodiscard]] bool appendSignals(const PlayerSignalBuffer& signals) noexcept;
    void appendVoiceEvents(
        Voice& voice,
        PatternPlayerId triggerSource,
        const SequencerEventBuffer& events,
        ResolvedVoiceEventBuffer& output) noexcept;
    void resolveTimestamp(double ppq, ResolvedVoiceEventBuffer& output) noexcept;
    [[nodiscard]] bool hasControlCycle(
        const RuntimeGraphConfig& candidate) const noexcept;
    [[nodiscard]] const RuntimeGraphConfig& activeConfig() const noexcept;
    void adoptPublishedConfig() noexcept;

    std::array<PatternPlayer*, maximumPatternPlayers> patternPlayers_ {};
    std::array<ModulationPlayer*, maximumModulationPlayers> modulationPlayers_ {};
    std::array<Voice*, maximumVoices> voices_ {};
    std::size_t patternPlayerCount_ = 0;
    std::size_t modulationPlayerCount_ = 0;
    std::size_t voiceCount_ = 0;
    static constexpr std::size_t snapshotCount = 3;
    std::array<RuntimeGraphConfig, snapshotCount> configSnapshots_ {};
    std::array<std::uint64_t, snapshotCount> snapshotGenerations_ {};
    std::atomic<std::uint8_t> publishedSnapshot_ {0};
    std::atomic<std::uint8_t> acknowledgedSnapshot_ {0};
    std::atomic<std::uint64_t> publishedGeneration_ {0};
    std::atomic<std::uint64_t> activeGeneration_ {0};
    std::uint64_t nextPublicationGeneration_ = 0;
    std::uint8_t audioSnapshot_ = 0;
    std::array<ArmedCycleCommand, maximumPatternPlayers> armedCycleCommands_ {};
    std::array<std::atomic_bool, maximumPatternPlayers> armedCyclePending_ {};
    PrepareSpec prepareSpec_;
    std::array<WorkSignal, maximumWorkSignals> work_ {};
    std::size_t workSize_ = 0;
    std::uint64_t nextWorkOrder_ = 0;
    std::uint64_t nextResolvedOrder_ = 0;
    double currentPpqPerSample_ = 0.0;
    GraphValidationError lastValidationError_ = GraphValidationError::none;
    bool prepared_ = false;
    bool workOverflowed_ = false;
};

} // namespace lps
