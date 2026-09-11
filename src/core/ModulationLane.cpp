#include "core/ModulationLane.h"

#include <algorithm>
#include <cmath>

namespace lps
{

NormalizedValue NormalizedValue::fromFloat(float value) noexcept
{
    const auto clamped = std::clamp(value, 0.0f, 1.0f);
    return { static_cast<std::uint16_t>(std::lround(
        clamped * static_cast<float>(maximum))) };
}

float ValueMapping::map(NormalizedValue value) const noexcept
{
    return minimum + (maximum - minimum) * value.toFloat();
}

ModulationLaneDefinition makeIntensityLaneDefinition(LaneId id) noexcept
{
    ModulationLaneDefinition definition;
    definition.id = id;
    definition.target = ModulationTarget::intensity;
    definition.advanceOn = ModulationAdvancePoint::candidateTrigger;
    definition.resetOn = ModulationResetReason::sourceSelection
        | ModulationResetReason::transportDiscontinuity
        | resetMask(ModulationResetReason::explicitRestart);
    return definition;
}

ModulationLaneRuntime::ModulationLaneRuntime(
    ModulationLaneDefinition definition) noexcept
    : definition_(definition)
{
}

void ModulationLaneRuntime::configure(
    ModulationLaneDefinition definition) noexcept
{
    definition_ = definition;
    nextStep_ = 0;
    currentStep_ = -1;
}

void ModulationLaneRuntime::publishState(
    const ModulationLaneState& state) noexcept
{
    state_ = state;
    state_.length = static_cast<std::uint8_t>(std::min<std::size_t>(
        state.length, state.values.size()));
    if (state_.length == 0)
    {
        nextStep_ = 0;
        currentStep_ = -1;
    }
    else
    {
        nextStep_ %= state_.length;
    }
}

void ModulationLaneRuntime::reset(ModulationResetReason reason) noexcept
{
    if ((definition_.resetOn & resetMask(reason)) == 0)
        return;
    nextStep_ = 0;
    currentStep_ = -1;
}

std::optional<ModulationSample> ModulationLaneRuntime::advance(
    ModulationAdvancePoint point) noexcept
{
    if (point != definition_.advanceOn || state_.length == 0)
        return std::nullopt;

    const auto step = nextStep_;
    const auto normalized = state_.values[step];
    currentStep_ = static_cast<int>(step);
    nextStep_ = static_cast<std::uint8_t>((step + 1u) % state_.length);
    return ModulationSample {
        definition_.id,
        definition_.target,
        definition_.logicalControl,
        normalized,
        definition_.mapping.map(normalized),
        step
    };
}

const ModulationLaneDefinition& ModulationLaneRuntime::definition() const noexcept
{
    return definition_;
}

int ModulationLaneRuntime::currentStep() const noexcept
{
    return currentStep_;
}

ModulationLaneRuntime* ModulationBank::addLane(
    ModulationLaneDefinition definition) noexcept
{
    if (definition.id.value == 0 || size_ == lanes_.size() || lane(definition.id) != nullptr)
        return nullptr;
    lanes_[size_].configure(definition);
    return &lanes_[size_++];
}

ModulationLaneRuntime* ModulationBank::lane(LaneId id) noexcept
{
    for (std::size_t index = 0; index < size_; ++index)
        if (lanes_[index].definition().id == id)
            return &lanes_[index];
    return nullptr;
}

const ModulationLaneRuntime* ModulationBank::lane(LaneId id) const noexcept
{
    for (std::size_t index = 0; index < size_; ++index)
        if (lanes_[index].definition().id == id)
            return &lanes_[index];
    return nullptr;
}

void ModulationBank::reset(ModulationResetReason reason) noexcept
{
    for (std::size_t index = 0; index < size_; ++index)
        lanes_[index].reset(reason);
}

} // namespace lps
