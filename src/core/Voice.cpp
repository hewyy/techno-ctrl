#include "core/Voice.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lps
{

bool Voice::addParameter(VoiceParameterDescriptor descriptor) noexcept
{
    if (!descriptor.id.isValid()
        || parameterCount_ == parameters_.size()
        || parameter(descriptor.id) != nullptr
        || !std::isfinite(descriptor.minimum)
        || !std::isfinite(descriptor.maximum)
        || descriptor.minimum > descriptor.maximum)
    {
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
    SequencerEventBuffer& output) noexcept
{
    if (value.type != PlayerSignalType::modulationValue
        || !std::isfinite(value.ppqPosition))
        return false;

    auto* state = findParameter(destination);
    if (state == nullptr)
        return false;

    state->normalized = value.normalizedValue;
    state->mapped = state->descriptor.map(value.normalizedValue);
    state->valid = true;
    if (state->descriptor.behavior != VoiceParameterBehavior::continuous)
        return true;

    if (!output.push(SequencerEvent::voiceControlPoint(
            value.ppqPosition,
            destination,
            state->mapped,
            state->descriptor.interpolation)))
    {
        incrementBounded(eventOverflowCount_);
        return false;
    }
    return true;
}

bool Voice::requiredParametersAreValid() const noexcept
{
    for (std::size_t index = 0; index < parameterCount_; ++index)
        if (parameters_[index].descriptor.requiredForTrigger
            && !parameters_[index].valid)
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
    double ppqPosition, SequencerEventBuffer& output) noexcept
{
    if (!active_)
        return;
    if (!output.push(SequencerEvent::triggerEnd(ppqPosition, activeTriggerId_)))
        incrementBounded(eventOverflowCount_);
    active_ = false;
    activeTriggerId_ = {};
}

void Voice::emitEndsBefore(
    double ppqPosition,
    bool inclusive,
    SequencerEventBuffer& output) noexcept
{
    if (active_
        && (activeEndPpq_ < ppqPosition
            || (inclusive && activeEndPpq_ <= ppqPosition)))
    {
        endActive(activeEndPpq_, output);
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
        return false;
    }

    emitEndsBefore(hit.ppqPosition, true, output);
    if (active_)
        endActive(hit.ppqPosition, output);

    const auto* pitch = findRole(VoiceParameterRole::pitch);
    const auto* intensity = findRole(VoiceParameterRole::intensity);
    const auto* gate = findRole(VoiceParameterRole::gate);
    if (!requiredParametersAreValid()
        || pitch == nullptr || !pitch->valid
        || intensity == nullptr || !intensity->valid
        || gate == nullptr || !gate->valid)
    {
        incrementBounded(droppedMissingRequired_);
        return false;
    }

    const auto gateRatio = std::clamp(gate->mapped, 0.0f, 1.0f);
    if (gateRatio <= 0.0f)
        return true;

    const auto voicePart = static_cast<std::uint64_t>(id_.value) << 32u;
    activeTriggerId_ = TriggerId {voicePart | nextTriggerSequence_++};
    if (!output.push(SequencerEvent::triggerStart(
            hit.ppqPosition,
            activeTriggerId_,
            std::clamp(intensity->mapped, 0.0f, 1.0f),
            pitch->mapped)))
    {
        incrementBounded(eventOverflowCount_);
        activeTriggerId_ = {};
        return false;
    }

    const auto minimumGate = std::isfinite(minimumPositiveGatePpq)
        ? std::max(minimumPositiveGatePpq, 0.0)
        : 0.0;
    activeEndPpq_ = hit.ppqPosition + std::max(
        static_cast<double>(gateRatio) * hit.nominalStepLengthPpq,
        minimumGate);
    active_ = true;
    return true;
}

void Voice::reset() noexcept
{
    active_ = false;
    activeTriggerId_ = {};
    activeEndPpq_ = 0.0;
    nextTriggerSequence_ = 1;
    for (std::size_t index = 0; index < parameterCount_; ++index)
        parameters_[index].valid = false;
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
