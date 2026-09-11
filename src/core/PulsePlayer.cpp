#include "core/PulsePlayer.h"

#include <algorithm>
#include <cmath>

namespace lps
{

PulsePlayer::PulsePlayer(double periodPpq, double gateRatio) noexcept
    : periodPpq_(std::isfinite(periodPpq) && periodPpq > 0.0
          ? periodPpq : 0.5),
      gateRatio_(std::clamp(gateRatio, 0.0, 1.0))
{
    (void) modulationBank_.addLane(makeIntensityLaneDefinition());
}

void PulsePlayer::publishModulationState(
    const ModulationLaneState& state) noexcept
{
    (void) publishModulationState({ 1 }, state);
}

bool PulsePlayer::addModulationLane(
    ModulationLaneDefinition definition,
    const ModulationLaneState& state) noexcept
{
    auto* lane = modulationBank_.addLane(definition);
    if (lane == nullptr)
        return false;
    lane->publishState(state);
    return true;
}

bool PulsePlayer::publishModulationState(
    LaneId laneId,
    const ModulationLaneState& state) noexcept
{
    auto* lane = modulationBank_.lane(laneId);
    if (lane == nullptr)
        return false;
    lane->publishState(state);
    return true;
}

void PulsePlayer::setBasePitch(std::optional<float> semitones) noexcept
{
    basePitchSemitones_ = semitones;
}

void PulsePlayer::prepare(const PrepareSpec& /*spec*/) noexcept
{
    reset();
}

void PulsePlayer::reset() noexcept
{
    originPpq_ = 0.0;
    pendingEndPpq_ = std::numeric_limits<double>::infinity();
    lastPulse_ = std::numeric_limits<std::int64_t>::min();
    activeTriggerId_ = {};
    triggerActive_ = false;
    modulationBank_.reset(ModulationResetReason::transportDiscontinuity);
    modulationSnapshot_ = {};
}

PlayerProcessResult PulsePlayer::process(
    const TimelineBlock& block,
    const PlayerDirectives& /*directives*/,
    SequencerEventBuffer& output) noexcept
{
    PlayerProcessResult result;
    const auto emitEnd = [&](double ppq)
    {
        (void) output.push(SequencerEvent::triggerEnd(ppq, activeTriggerId_));
        triggerActive_ = false;
        pendingEndPpq_ = std::numeric_limits<double>::infinity();
    };

    if (block.transportDiscontinuity)
    {
        if (triggerActive_)
            emitEnd(block.ppqStart);
        originPpq_ = block.ppqStart;
        lastPulse_ = std::numeric_limits<std::int64_t>::min();
        modulationBank_.reset(ModulationResetReason::transportDiscontinuity);
        modulationSnapshot_ = {};
    }

    if (!block.playing || block.ppqEnd <= block.ppqStart)
    {
        if (triggerActive_)
            emitEnd(block.ppqStart);
        result.eventOverflow = output.overflowed();
        return result;
    }

    constexpr double tolerance = 1.0e-9;
    auto pulse = static_cast<std::int64_t>(std::ceil(
        (block.ppqStart - originPpq_) / periodPpq_ - tolerance));
    while (originPpq_ + static_cast<double>(pulse) * periodPpq_ < block.ppqEnd)
    {
        const auto pulsePpq = originPpq_ + static_cast<double>(pulse) * periodPpq_;
        if (triggerActive_ && pendingEndPpq_ <= pulsePpq)
            emitEnd(pendingEndPpq_);

        if (!result.firstCycleBoundaryPpq.has_value())
            result.firstCycleBoundaryPpq = std::max(pulsePpq, block.ppqStart);

        if (pulse > lastPulse_)
        {
            if (triggerActive_)
                emitEnd(pulsePpq);

            float intensity = 1.0f;
            float probability = 1.0f;
            auto gateRatio = static_cast<float>(gateRatio_);
            auto pitch = basePitchSemitones_;
            std::array<ModulationSample, ModulationBank::maximumLaneCount> samples;
            const auto applySamples = [&](ModulationAdvancePoint point)
            {
                const auto sampleCount = modulationBank_.advance(point, samples);
                for (std::size_t index = 0; index < sampleCount; ++index)
                {
                    const auto& sample = samples[index];
                    const auto combine = sample.combine;
                    const auto combineValue = [combine](float current, float value)
                    {
                        if (combine == ModulationCombineMode::add)
                            return current + value;
                        if (combine == ModulationCombineMode::multiply)
                            return current * value;
                        return value;
                    };

                    switch (sample.target)
                    {
                        case ModulationTarget::intensity:
                            intensity = combineValue(intensity, sample.mappedValue);
                            break;
                        case ModulationTarget::pitch:
                            pitch = combineValue(pitch.value_or(0.0f), sample.mappedValue);
                            break;
                        case ModulationTarget::gateLength:
                            gateRatio = combineValue(gateRatio, sample.mappedValue);
                            break;
                        case ModulationTarget::probability:
                            probability = combineValue(probability, sample.mappedValue);
                            break;
                        case ModulationTarget::control:
                            (void) output.push(SequencerEvent::controlPoint(
                                pulsePpq,
                                sample.logicalControl,
                                NormalizedValue::fromFloat(
                                    sample.mappedValue).toFloat()));
                            break;
                    }
                }
            };

            applySamples(ModulationAdvancePoint::sourceStep);
            applySamples(ModulationAdvancePoint::time);
            applySamples(ModulationAdvancePoint::candidateTrigger);
            const auto randomUnit = static_cast<float>(
                (static_cast<std::uint64_t>(pulse) * 1'103'515'245u + 12'345u)
                    & 0xffffu) / 65'535.0f;
            if (std::clamp(probability, 0.0f, 1.0f) < randomUnit)
            {
                lastPulse_ = pulse;
                ++pulse;
                continue;
            }

            applySamples(ModulationAdvancePoint::emittedTrigger);
            activeTriggerId_ = { nextTriggerId_++ };
            (void) output.push(SequencerEvent::triggerStart(
                pulsePpq,
                activeTriggerId_,
                std::clamp(intensity, 0.0f, 1.0f),
                pitch));
            triggerActive_ = true;
            lastPulse_ = pulse;
            if (const auto* intensityLane = modulationBank_.lane({ 1 }))
                modulationSnapshot_.currentStep = intensityLane->currentStep();
            pendingEndPpq_ = pulsePpq + periodPpq_
                * std::clamp(static_cast<double>(gateRatio), 0.0, 1.0);
            if (pendingEndPpq_ < block.ppqEnd)
                emitEnd(pendingEndPpq_);
        }
        ++pulse;
    }

    if (triggerActive_ && pendingEndPpq_ < block.ppqEnd)
        emitEnd(pendingEndPpq_);

    result.active = true;
    result.eventOverflow = output.overflowed();
    return result;
}

PlayerSyncCapabilities PulsePlayer::syncCapabilities() const noexcept
{
    return { true, false, false };
}

ModulationPlaybackSnapshot PulsePlayer::modulationPlaybackSnapshot() const noexcept
{
    return modulationSnapshot_;
}

} // namespace lps
