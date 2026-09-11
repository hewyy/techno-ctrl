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
}

void PulsePlayer::publishModulationState(
    const ModulationLaneState& state) noexcept
{
    intensityLane_.publishState(state);
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
    intensityLane_.reset(ModulationResetReason::transportDiscontinuity);
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
        intensityLane_.reset(ModulationResetReason::transportDiscontinuity);
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
            const auto modulation = intensityLane_.advance(
                ModulationAdvancePoint::candidateTrigger);
            activeTriggerId_ = { nextTriggerId_++ };
            (void) output.push(SequencerEvent::triggerStart(
                pulsePpq,
                activeTriggerId_,
                modulation.has_value() ? modulation->mappedValue : 1.0f));
            triggerActive_ = true;
            lastPulse_ = pulse;
            modulationSnapshot_.currentStep = intensityLane_.currentStep();
            pendingEndPpq_ = pulsePpq + periodPpq_ * gateRatio_;
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
