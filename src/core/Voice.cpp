#include "core/Voice.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lps
{
namespace
{
bool shouldReportCount(std::uint32_t count) noexcept
{
    return count != 0 && (count & (count - 1u)) == 0;
}
}

bool Voice::addParameter(VoiceParameterDescriptor descriptor) noexcept
{
    if (!descriptor.id.isValid()
        || parameterCount_ == parameters_.size()
        || parameter(descriptor.id) != nullptr
        || !std::isfinite(descriptor.minimum)
        || !std::isfinite(descriptor.maximum)
        || descriptor.minimum > descriptor.maximum)
    {
        Logger::logf(LogLevel::warning, "voice", "parameter_rejected",
            "voice_id=%u parameter_id=%u count=%zu",
            id_.value, static_cast<unsigned>(descriptor.id.value), parameterCount_);
        return false;
    }

    for (std::size_t index = 0; index < parameterCount_; ++index)
    {
        if (parameters_[index].descriptor.role == descriptor.role
            && descriptor.role != VoiceParameterRole::continuousControl)
        {
            return false;
        }
    }

    parameters_[parameterCount_++].descriptor = descriptor;
    return true;
}

const VoiceParameterDescriptor* Voice::parameter(
    VoiceParameterId id) const noexcept
{
    for (std::size_t index = 0; index < parameterCount_; ++index)
        if (parameters_[index].descriptor.id == id)
            return &parameters_[index].descriptor;
    return nullptr;
}

Voice::ParameterState* Voice::findParameter(VoiceParameterId id) noexcept
{
    for (std::size_t index = 0; index < parameterCount_; ++index)
        if (parameters_[index].descriptor.id == id)
            return &parameters_[index];
    return nullptr;
}

const Voice::ParameterState* Voice::findRole(
    VoiceParameterRole role) const noexcept
{
    for (std::size_t index = 0; index < parameterCount_; ++index)
        if (parameters_[index].descriptor.role == role)
            return &parameters_[index];
    return nullptr;
}

bool Voice::applyParameterValue(
    VoiceParameterId destination,
    const PlayerSignal& value,
    SequencerEventBuffer& output,
    PatternPlayerId triggerScope) noexcept
{
    if (value.type != PlayerSignalType::modulationValue
        || !std::isfinite(value.ppqPosition))
        return false;

    auto* state = findParameter(destination);
    if (state == nullptr
        || (state->descriptor.behavior == VoiceParameterBehavior::continuous
            && triggerScope.isValid()))
        return false;

    auto* destinationValue = valueFor(*state, triggerScope);
    if (destinationValue == nullptr)
        return false;
    destinationValue->normalized = value.normalizedValue;
    destinationValue->mapped = state->descriptor.map(value.normalizedValue);
    destinationValue->valid = true;
    if (state->descriptor.behavior != VoiceParameterBehavior::continuous)
        return true;

    if (!output.push(SequencerEvent::voiceControlPoint(
            value.ppqPosition,
            destination,
            destinationValue->mapped,
            state->descriptor.interpolation)))
    {
        incrementBounded(eventOverflowCount_);
        const auto count = eventOverflowCount_.load(std::memory_order_relaxed);
        if (shouldReportCount(count))
            Logger::logf(LogLevel::error, "voice", "event_buffer_overflow",
                "voice_id=%u count=%u semantic_event=control_point", id_.value, count);
        return false;
    }
    return true;
}

Voice::ParameterValue* Voice::valueFor(
    ParameterState& state,
    PatternPlayerId triggerSource) noexcept
{
    if (!triggerSource.isValid())
        return &state.voiceWide;

    for (auto& value : state.scoped)
        if (value.triggerSource == triggerSource)
            return &value;
    for (auto& value : state.scoped)
    {
        if (!value.triggerSource.isValid())
        {
            value.triggerSource = triggerSource;
            return &value;
        }
    }
    return nullptr;
}

const Voice::ParameterValue* Voice::valueFor(
    const ParameterState& state,
    PatternPlayerId triggerSource) noexcept
{
    if (triggerSource.isValid())
    {
        for (const auto& value : state.scoped)
            if (value.triggerSource == triggerSource && value.valid)
                return &value;
    }
    return state.voiceWide.valid ? &state.voiceWide : nullptr;
}

bool Voice::requiredParametersAreValid(
    PatternPlayerId triggerSource) const noexcept
{
    for (std::size_t index = 0; index < parameterCount_; ++index)
        if (parameters_[index].descriptor.requiredForTrigger
            && valueFor(parameters_[index], triggerSource) == nullptr)
            return false;
    return true;
}

void Voice::incrementBounded(std::atomic<std::uint32_t>& value) noexcept
{
    auto current = value.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint32_t>::max()
        && !value.compare_exchange_weak(
            current, current + 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed))
    {
    }
}

void Voice::endActive(
    ActiveTrigger& trigger,
    double ppqPosition,
    SequencerEventBuffer& output) noexcept
{
    if (!trigger.active)
        return;
    if (!output.push(SequencerEvent::triggerEnd(ppqPosition, trigger.id)))
    {
        incrementBounded(eventOverflowCount_);
        const auto count = eventOverflowCount_.load(std::memory_order_relaxed);
        if (shouldReportCount(count))
            Logger::logf(LogLevel::error, "voice", "event_buffer_overflow",
                "voice_id=%u count=%u semantic_event=trigger_end", id_.value, count);
    }
    trigger = {};
}

void Voice::emitEndsBefore(
    double ppqPosition,
    bool inclusive,
    SequencerEventBuffer& output) noexcept
{
    for (auto& trigger : activeTriggers_)
    {
        if (trigger.active
            && (trigger.endPpq < ppqPosition
                || (inclusive && trigger.endPpq <= ppqPosition)))
        {
            endActive(trigger, trigger.endPpq, output);
        }
    }
}

bool Voice::trigger(
    const PlayerSignal& hit,
    double minimumPositiveGatePpq,
    SequencerEventBuffer& output) noexcept
{
    if (hit.type != PlayerSignalType::patternHit
        || !std::isfinite(hit.ppqPosition)
        || !std::isfinite(hit.nominalStepLengthPpq)
        || hit.nominalStepLengthPpq <= 0.0)
    {
        incrementBounded(droppedInvalidTrigger_);
        const auto count = droppedInvalidTrigger_.load(std::memory_order_relaxed);
        if (shouldReportCount(count))
            Logger::logf(LogLevel::warning, "voice", "trigger_dropped",
                "voice_id=%u reason=invalid_trigger count=%u", id_.value, count);
        return false;
    }

    emitEndsBefore(hit.ppqPosition, true, output);
    ActiveTrigger* activeTrigger = nullptr;
    for (auto& trigger : activeTriggers_)
    {
        if (trigger.active && trigger.source == hit.patternPlayerId)
        {
            endActive(trigger, hit.ppqPosition, output);
            activeTrigger = &trigger;
            break;
        }
    }
    if (activeTrigger == nullptr)
    {
        for (auto& trigger : activeTriggers_)
        {
            if (!trigger.active)
            {
                activeTrigger = &trigger;
                break;
            }
        }
    }

    const auto* pitch = findRole(VoiceParameterRole::pitch);
    const auto* intensity = findRole(VoiceParameterRole::intensity);
    const auto* gate = findRole(VoiceParameterRole::gate);
    const auto* pitchValue = pitch != nullptr
        ? valueFor(*pitch, hit.patternPlayerId) : nullptr;
    const auto* intensityValue = intensity != nullptr
        ? valueFor(*intensity, hit.patternPlayerId) : nullptr;
    const auto* gateValue = gate != nullptr
        ? valueFor(*gate, hit.patternPlayerId) : nullptr;
    if (!requiredParametersAreValid(hit.patternPlayerId)
        || pitchValue == nullptr || intensityValue == nullptr
        || gateValue == nullptr)
    {
        incrementBounded(droppedMissingRequired_);
        const auto count = droppedMissingRequired_.load(std::memory_order_relaxed);
        if (shouldReportCount(count))
            Logger::logf(LogLevel::warning, "voice", "trigger_dropped",
                "voice_id=%u reason=missing_required_parameter count=%u",
                id_.value, count);
        return false;
    }

    const auto gateRatio = std::clamp(gateValue->mapped, 0.0f, 1.0f);
    if (gateRatio <= 0.0f)
        return true;

    if (activeTrigger == nullptr)
    {
        incrementBounded(droppedInvalidTrigger_);
        return false;
    }

    const auto voicePart = static_cast<std::uint64_t>(id_.value) << 32u;
    const auto triggerId = TriggerId {voicePart | nextTriggerSequence_++};
    if (!output.push(SequencerEvent::triggerStart(
            hit.ppqPosition,
            triggerId,
            std::clamp(intensityValue->mapped, 0.0f, 1.0f),
            pitchValue->mapped)))
    {
        incrementBounded(eventOverflowCount_);
        const auto count = eventOverflowCount_.load(std::memory_order_relaxed);
        if (shouldReportCount(count))
            Logger::logf(LogLevel::error, "voice", "event_buffer_overflow",
                "voice_id=%u count=%u semantic_event=trigger_start", id_.value, count);
        return false;
    }

    const auto minimumGate = std::isfinite(minimumPositiveGatePpq)
        ? std::max(minimumPositiveGatePpq, 0.0)
        : 0.0;
    *activeTrigger = {
        hit.patternPlayerId,
        triggerId,
        hit.ppqPosition + std::max(
            static_cast<double>(gateRatio) * hit.nominalStepLengthPpq,
            minimumGate),
        true
    };
    return true;
}

void Voice::reset() noexcept
{
    for (auto& trigger : activeTriggers_)
        trigger = {};
    nextTriggerSequence_ = 1;
    for (std::size_t index = 0; index < parameterCount_; ++index)
    {
        parameters_[index].voiceWide.valid = false;
        for (auto& value : parameters_[index].scoped)
            value = {};
    }
}

bool Voice::active() const noexcept
{
    return std::any_of(
        activeTriggers_.begin(),
        activeTriggers_.end(),
        [](const auto& trigger) { return trigger.active; });
}

VoiceDiagnostics Voice::diagnostics() const noexcept
{
    return {
        droppedMissingRequired_.load(std::memory_order_acquire),
        droppedInvalidTrigger_.load(std::memory_order_acquire),
        eventOverflowCount_.load(std::memory_order_acquire)
    };
}

} // namespace lps
