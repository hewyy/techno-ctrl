#pragma once

#include "core/SequencerTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lps
{

enum class VoiceParameterRole : std::uint8_t
{
    pitch,
    intensity,
    gate,
    continuousControl
};

enum class VoiceParameterBehavior : std::uint8_t
{
    sampledOnTrigger,
    continuous
};

struct VoiceParameterDescriptor
{
    VoiceParameterId id;
    VoiceParameterRole role = VoiceParameterRole::continuousControl;
    VoiceParameterBehavior behavior = VoiceParameterBehavior::continuous;
    float minimum = 0.0f;
    float maximum = 1.0f;
    InterpolationPolicy interpolation = InterpolationPolicy::step;
    bool requiredForTrigger = false;

    [[nodiscard]] float map(NormalizedValue value) const noexcept
    {
        return minimum + (maximum - minimum) * value.toFloat();
    }

    [[nodiscard]] static constexpr VoiceParameterDescriptor pitch(
        VoiceParameterId id) noexcept
    {
        return {id, VoiceParameterRole::pitch,
            VoiceParameterBehavior::sampledOnTrigger,
            0.0f, 127.0f, InterpolationPolicy::step, true};
    }

    [[nodiscard]] static constexpr VoiceParameterDescriptor intensity(
        VoiceParameterId id) noexcept
    {
        return {id, VoiceParameterRole::intensity,
            VoiceParameterBehavior::sampledOnTrigger,
            0.0f, 1.0f, InterpolationPolicy::step, true};
    }

    [[nodiscard]] static constexpr VoiceParameterDescriptor gate(
        VoiceParameterId id) noexcept
    {
        return {id, VoiceParameterRole::gate,
            VoiceParameterBehavior::sampledOnTrigger,
            0.0f, 1.0f, InterpolationPolicy::step, true};
    }
};

struct VoiceDiagnostics
{
    std::uint32_t droppedMissingRequired = 0;
    std::uint32_t droppedInvalidTrigger = 0;
    std::uint32_t eventOverflowCount = 0;
};

class Voice
{
public:
    static constexpr std::size_t maximumParameterCount = 32;
    static constexpr std::size_t maximumScopedSourceCount = 16;
    static constexpr std::size_t maximumActiveTriggerCount = 16;

    explicit Voice(VoiceId id = {}) noexcept : id_(id) {}

    void setId(VoiceId id) noexcept { id_ = id; }
    [[nodiscard]] VoiceId id() const noexcept { return id_; }

    [[nodiscard]] bool addParameter(
        VoiceParameterDescriptor descriptor) noexcept;
    [[nodiscard]] const VoiceParameterDescriptor* parameter(
        VoiceParameterId id) const noexcept;
    [[nodiscard]] std::size_t parameterCount() const noexcept
    {
        return parameterCount_;
    }

    [[nodiscard]] bool applyParameterValue(
        VoiceParameterId destination,
        const PlayerSignal& value,
        SequencerEventBuffer& output,
        PatternPlayerId triggerScope = {}) noexcept;
    [[nodiscard]] bool trigger(
        const PlayerSignal& hit,
        double minimumPositiveGatePpq,
        SequencerEventBuffer& output) noexcept;
    void emitEndsBefore(
        double ppqPosition,
        bool inclusive,
        SequencerEventBuffer& output) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] VoiceDiagnostics diagnostics() const noexcept;

private:
    struct ParameterValue
    {
        PatternPlayerId triggerSource;
        NormalizedValue normalized;
        float mapped = 0.0f;
        bool valid = false;
    };

    struct ParameterState
    {
        VoiceParameterDescriptor descriptor;
        ParameterValue voiceWide;
        std::array<ParameterValue, maximumScopedSourceCount> scoped {};
    };

    struct ActiveTrigger
    {
        PatternPlayerId source;
        TriggerId id;
        double endPpq = 0.0;
        bool active = false;
    };

    [[nodiscard]] ParameterState* findParameter(VoiceParameterId id) noexcept;
    [[nodiscard]] const ParameterState* findRole(
        VoiceParameterRole role) const noexcept;
    [[nodiscard]] static ParameterValue* valueFor(
        ParameterState& state, PatternPlayerId triggerSource) noexcept;
    [[nodiscard]] static const ParameterValue* valueFor(
        const ParameterState& state, PatternPlayerId triggerSource) noexcept;
    [[nodiscard]] bool requiredParametersAreValid(
        PatternPlayerId triggerSource) const noexcept;
    void endActive(
        ActiveTrigger& trigger,
        double ppqPosition,
        SequencerEventBuffer& output) noexcept;
    static void incrementBounded(std::atomic<std::uint32_t>& value) noexcept;

    VoiceId id_;
    std::array<ParameterState, maximumParameterCount> parameters_ {};
    std::size_t parameterCount_ = 0;
    std::uint32_t nextTriggerSequence_ = 1;
    std::array<ActiveTrigger, maximumActiveTriggerCount> activeTriggers_ {};
    std::atomic<std::uint32_t> droppedMissingRequired_ {0};
    std::atomic<std::uint32_t> droppedInvalidTrigger_ {0};
    std::atomic<std::uint32_t> eventOverflowCount_ {0};
};

} // namespace lps
