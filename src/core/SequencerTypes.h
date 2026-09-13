#pragma once

#include "core/NormalizedValue.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace lps
{

template <typename Tag, typename Storage = std::uint32_t>
struct StableId
{
    static constexpr Storage invalidValue =
        std::numeric_limits<Storage>::max();

    Storage value = invalidValue;

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return value != invalidValue;
    }

    friend constexpr bool operator==(StableId left, StableId right) noexcept
    {
        return left.value == right.value;
    }

    friend constexpr bool operator!=(StableId left, StableId right) noexcept
    {
        return !(left == right);
    }
};

struct PatternPlayerIdTag;
struct ModulationPlayerIdTag;
struct VoiceIdTag;
struct VoiceParameterIdTag;
struct TriggerBindingIdTag;
struct ParameterBindingIdTag;
struct CommandBindingIdTag;
struct OutputBindingIdTag;

using PatternPlayerId = StableId<PatternPlayerIdTag>;
using ModulationPlayerId = StableId<ModulationPlayerIdTag>;
using VoiceId = StableId<VoiceIdTag>;
using VoiceParameterId = StableId<VoiceParameterIdTag, std::uint16_t>;
using TriggerBindingId = StableId<TriggerBindingIdTag>;
using ParameterBindingId = StableId<ParameterBindingIdTag>;
using CommandBindingId = StableId<CommandBindingIdTag>;
using OutputBindingId = StableId<OutputBindingIdTag>;

enum class PlayerRefType : std::uint8_t
{
    pattern,
    modulation
};

struct PlayerRef
{
    std::uint32_t value = PatternPlayerId::invalidValue;
    PlayerRefType type = PlayerRefType::pattern;

    [[nodiscard]] static constexpr PlayerRef pattern(
        PatternPlayerId id) noexcept
    {
        return { id.value, PlayerRefType::pattern };
    }

    [[nodiscard]] static constexpr PlayerRef modulation(
        ModulationPlayerId id) noexcept
    {
        return { id.value, PlayerRefType::modulation };
    }
};

enum class PlayMode : std::uint8_t
{
    continuous,
    oneShot
};

enum class PlayerCommand : std::uint8_t
{
    play,
    stop,
    reset,
    resetAndPlay
};

struct ClockAdvance
{
    double stepLengthPpq = 0.25;
};

struct PatternHitAdvance
{
    PatternPlayerId source;
};

using AdvanceSource = std::variant<ClockAdvance, PatternHitAdvance>;

struct PrepareSpec
{
    double sampleRate = 44'100.0;
    std::uint32_t maximumBlockSize = 0;
};

struct TimelineBlock
{
    double ppqStart = 0.0;
    double ppqEnd = 0.0;
    double tempoBpm = 120.0;
    double sampleRate = 44'100.0;
    std::uint32_t sampleCount = 0;
    bool playing = false;
    bool transportDiscontinuity = false;
};

struct PlayerDirectives
{
    // Unlike a transport discontinuity, this restarts only player phase and
    // does not imply that the host timeline moved.
    std::optional<double> restartAtPpq;
    // Followers may use an external cycle as the quantization grid for their
    // own pending transitions.
    std::optional<double> externalCycleBoundaryPpq;
    bool quantizePendingTransitionsExternally = false;
};

struct PlayerProcessResult
{
    bool active = false;
    // First cycle start in the half-open TimelineBlock interval
    // [ppqStart, ppqEnd), whether or not it emitted a trigger.
    std::optional<double> firstCycleBoundaryPpq;
    bool eventOverflow = false;
};

struct PlayerSyncCapabilities
{
    bool providesCycleBoundaries = false;
    bool acceptsExternalCycleBoundaries = false;
    bool acceptsExternalRestart = false;
};

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

struct TriggerId
{
    std::uint64_t value = 0;

    friend constexpr bool operator==(TriggerId left, TriggerId right) noexcept
    {
        return left.value == right.value;
    }

    friend constexpr bool operator!=(TriggerId left, TriggerId right) noexcept
    {
        return !(left == right);
    }
};

enum class PlayerSignalType : std::uint8_t
{
    patternCycleBoundary,
    modulationValue,
    patternHit
};

struct PlayerSignal
{
    double ppqPosition = 0.0;
    double nominalStepLengthPpq = 0.0;
    TriggerId triggerId;
    PatternPlayerId patternPlayerId;
    ModulationPlayerId modulationPlayerId;
    NormalizedValue normalizedValue;
    std::uint8_t sourceStep = 0;
    PlayerSignalType type = PlayerSignalType::patternCycleBoundary;

    [[nodiscard]] static constexpr PlayerSignal patternHit(
        double ppq,
        PatternPlayerId source,
        TriggerId id,
        double nominalStepLength) noexcept
    {
        PlayerSignal signal;
        signal.ppqPosition = ppq;
        signal.nominalStepLengthPpq = nominalStepLength;
        signal.triggerId = id;
        signal.patternPlayerId = source;
        signal.type = PlayerSignalType::patternHit;
        return signal;
    }

    [[nodiscard]] static constexpr PlayerSignal patternCycleBoundary(
        double ppq,
        PatternPlayerId source) noexcept
    {
        PlayerSignal signal;
        signal.ppqPosition = ppq;
        signal.patternPlayerId = source;
        signal.type = PlayerSignalType::patternCycleBoundary;
        return signal;
    }

    [[nodiscard]] static constexpr PlayerSignal modulationValue(
        double ppq,
        ModulationPlayerId source,
        NormalizedValue value,
        std::uint8_t step) noexcept
    {
        PlayerSignal signal;
        signal.ppqPosition = ppq;
        signal.modulationPlayerId = source;
        signal.normalizedValue = value;
        signal.sourceStep = step;
        signal.type = PlayerSignalType::modulationValue;
        return signal;
    }
};

static_assert(std::is_trivially_copyable_v<PlayerSignal>);

class PlayerSignalBuffer
{
public:
    static constexpr std::size_t capacity = 128;

    void clear() noexcept
    {
        size_ = 0;
        overflowed_ = false;
        droppedCount_ = 0;
    }

    [[nodiscard]] bool push(PlayerSignal signal) noexcept
    {
        if (size_ == capacity)
        {
            overflowed_ = true;
            ++droppedCount_;
            return false;
        }
        signals_[size_++] = signal;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] std::size_t droppedCount() const noexcept { return droppedCount_; }
    [[nodiscard]] const PlayerSignal& operator[](
        std::size_t index) const noexcept
    {
        return signals_[index];
    }
    [[nodiscard]] const PlayerSignal* begin() const noexcept
    {
        return signals_.data();
    }
    [[nodiscard]] const PlayerSignal* end() const noexcept
    {
        return signals_.data() + size_;
    }

private:
    std::array<PlayerSignal, capacity> signals_ {};
    std::size_t size_ = 0;
    bool overflowed_ = false;
    std::size_t droppedCount_ = 0;
};

enum class SemanticEventType : std::uint8_t
{
    controlPoint,
    triggerEnd,
    triggerStart
};

enum class InterpolationPolicy : std::uint8_t
{
    step,
    linear
};

struct SequencerEvent
{
    double ppqPosition = 0.0;
    TriggerId triggerId;
    float musicalPitchSemitones = 0.0f;
    float normalizedValue = 0.0f;
    std::uint16_t logicalControl = 0;
    VoiceParameterId voiceParameterId;
    SemanticEventType type = SemanticEventType::triggerEnd;
    InterpolationPolicy interpolation = InterpolationPolicy::step;
    bool hasMusicalPitch = false;

    [[nodiscard]] static SequencerEvent triggerStart(
        double ppq,
        TriggerId id,
        float intensity,
        std::optional<float> pitch = std::nullopt) noexcept
    {
        SequencerEvent event;
        event.ppqPosition = ppq;
        event.triggerId = id;
        event.normalizedValue = intensity;
        event.type = SemanticEventType::triggerStart;
        event.hasMusicalPitch = pitch.has_value();
        event.musicalPitchSemitones = pitch.value_or(0.0f);
        return event;
    }

    [[nodiscard]] static SequencerEvent triggerEnd(
        double ppq,
        TriggerId id) noexcept
    {
        SequencerEvent event;
        event.ppqPosition = ppq;
        event.triggerId = id;
        event.type = SemanticEventType::triggerEnd;
        return event;
    }

    [[nodiscard]] static SequencerEvent controlPoint(
        double ppq,
        std::uint16_t control,
        float value,
        InterpolationPolicy interpolationPolicy =
            InterpolationPolicy::step) noexcept
    {
        SequencerEvent event;
        event.ppqPosition = ppq;
        event.logicalControl = control;
        event.normalizedValue = value;
        event.type = SemanticEventType::controlPoint;
        event.interpolation = interpolationPolicy;
        return event;
    }

    [[nodiscard]] static SequencerEvent voiceControlPoint(
        double ppq,
        VoiceParameterId parameter,
        float value,
        InterpolationPolicy interpolationPolicy =
            InterpolationPolicy::step) noexcept
    {
        SequencerEvent event;
        event.ppqPosition = ppq;
        event.voiceParameterId = parameter;
        event.normalizedValue = value;
        event.type = SemanticEventType::controlPoint;
        event.interpolation = interpolationPolicy;
        return event;
    }
};

struct RouteMapping
{
    bool usesFixedPitch = false;
    float fixedPitchSemitones = 60.0f;

    [[nodiscard]] static constexpr RouteMapping fixedPitch(
        float semitones) noexcept
    {
        return { true, semitones };
    }
};

struct RoutedEvent
{
    SequencerEvent event;
    VoiceId sourceVoiceId;
    RouteId routeId;
    std::uint32_t frameOffset = 0;
    std::uint64_t stableOrder = 0;
    float mappedPitchSemitones = 0.0f;
    bool hasMappedPitch = false;
};

static_assert(std::is_trivially_copyable_v<SequencerEvent>);
static_assert(std::is_trivially_copyable_v<RoutedEvent>);

class SequencerEventBuffer
{
public:
    static constexpr std::size_t capacity = 128;

    void clear() noexcept
    {
        size_ = 0;
        overflowed_ = false;
        droppedCount_ = 0;
    }

    [[nodiscard]] bool push(SequencerEvent event) noexcept
    {
        if (size_ == capacity)
        {
            overflowed_ = true;
            ++droppedCount_;
            return false;
        }

        events_[size_++] = event;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] std::size_t droppedCount() const noexcept { return droppedCount_; }
    [[nodiscard]] const SequencerEvent& operator[](std::size_t index) const noexcept
    {
        return events_[index];
    }

    [[nodiscard]] const SequencerEvent* begin() const noexcept { return events_.data(); }
    [[nodiscard]] const SequencerEvent* end() const noexcept { return events_.data() + size_; }

private:
    std::array<SequencerEvent, capacity> events_ {};
    std::size_t size_ = 0;
    bool overflowed_ = false;
    std::size_t droppedCount_ = 0;
};

struct Pattern
{
    static constexpr std::size_t maxLength = 32;
    std::array<bool, maxLength> hits {};
    std::size_t length = 0;
};

struct PitchList
{
    std::array<std::uint8_t, 4> values {60, 60, 60, 60};
};

struct PatternView
{
    // The per-player draft capacity. A library Pattern::length instead sets
    // the default playback end when that pattern is loaded.
    std::uint16_t stepCount = 0;
    std::uint32_t hitMask = 0;
    int stepOffset = 0;
    std::uint16_t playbackStart = 0;
    std::uint16_t playbackEnd = 0;

    [[nodiscard]] bool isHit(std::uint16_t step) const noexcept
    {
        if (stepCount == 0 || step >= stepCount)
            return false;

        const auto length = static_cast<int>(stepCount);
        auto sourceStep = (static_cast<int>(step) - stepOffset) % length;
        if (sourceStep < 0)
            sourceStep += length;
        if (sourceStep >= 32)
            return false;
        return (hitMask & (std::uint32_t { 1 } << sourceStep)) != 0;
    }

    [[nodiscard]] bool isInsidePlaybackWindow(std::uint16_t step) const noexcept
    {
        return step < stepCount && step >= playbackStart && step <= playbackEnd;
    }
};

struct PatternPlaybackSnapshot
{
    int currentStep = -1;
    bool playing = false;
};

struct ModulationPlaybackSnapshot
{
    // Modulation can advance independently of pattern steps.
    int currentStep = -1;
};

} // namespace lps
