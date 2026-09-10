#include "core/DemoPlayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lps
{

void DemoPlayer::prepare(double sampleRate) noexcept
{
    configuredSampleRate_ = sampleRate > 0.0 ? sampleRate : 44'100.0;
    reset();
}

void DemoPlayer::reset() noexcept
{
    pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
    lastTriggeredAbsoluteStep_ = std::numeric_limits<std::int64_t>::min();
    noteIsOn_ = false;
    snapshot_ = {};
}

PatternView DemoPlayer::get_pattern_view() const noexcept
{
    return PatternView {
        patternStepCount,
        patternHits.data(),
        patternHits.size()
    };
}

PlaybackSnapshot DemoPlayer::snapshot() const noexcept
{
    return snapshot_;
}

int DemoPlayer::wrapStep(std::int64_t step) noexcept
{
    const auto length = static_cast<std::int64_t>(patternStepCount);
    auto wrapped = step % length;

    if (wrapped < 0)
        wrapped += length;

    return static_cast<int>(wrapped);
}

std::uint32_t DemoPlayer::sampleOffsetFor(
    double eventPpq,
    const ClockBlock& block,
    double ppqPerSample) noexcept
{
    if (block.sampleCount == 0 || ppqPerSample <= 0.0)
        return 0;

    const auto rawOffset = std::llround((eventPpq - block.ppqStart) / ppqPerSample);
    const auto maximum = static_cast<std::int64_t>(block.sampleCount - 1);
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(rawOffset, 0, maximum));
}

void DemoPlayer::addNoteOn(
    NoteEventBuffer& output,
    std::uint32_t sampleOffset) noexcept
{
    output.push(NoteEvent {
        sampleOffset,
        midiChannel,
        midiNote,
        midiVelocity,
        true
    });
}

void DemoPlayer::addNoteOff(
    NoteEventBuffer& output,
    std::uint32_t sampleOffset) noexcept
{
    output.push(NoteEvent {
        sampleOffset,
        midiChannel,
        midiNote,
        0,
        false
    });
}

void DemoPlayer::process(
    const ClockBlock& incomingBlock) noexcept
{
    output.clear();

    ClockBlock block = incomingBlock;
    block.sampleRate = block.sampleRate > 0.0 ? block.sampleRate : configuredSampleRate_;

    if (block.transportDiscontinuity)
    {
        if (noteIsOn_)
            addNoteOff(output, 0);

        pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
        lastTriggeredAbsoluteStep_ = std::numeric_limits<std::int64_t>::min();
        noteIsOn_ = false;
    }

    if (!block.playing || block.tempoBpm <= 0.0 || block.sampleRate <= 0.0)
    {
        if (noteIsOn_)
            addNoteOff(output, 0);

        pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
        noteIsOn_ = false;
        snapshot_ = { -1, false };
        return;
    }

    const double ppqPerSample = block.tempoBpm / (60.0 * block.sampleRate);
    const double blockEndPpq = block.ppqStart
        + static_cast<double>(block.sampleCount) * ppqPerSample;
    const double tolerance = ppqPerSample * 0.25;

    const auto currentAbsoluteStep = static_cast<std::int64_t>(
        std::floor((block.ppqStart + tolerance) / stepLengthPpq));

    snapshot_ = { wrapStep(currentAbsoluteStep), true };

    if (noteIsOn_ && pendingNoteOffPpq_ < blockEndPpq - tolerance)
    {
        addNoteOff(output, sampleOffsetFor(pendingNoteOffPpq_, block, ppqPerSample));
        noteIsOn_ = false;
        pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
    }
    else if (noteIsOn_ && pendingNoteOffPpq_ <= block.ppqStart + tolerance)
    {
        addNoteOff(output, 0);
        noteIsOn_ = false;
        pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
    }

    auto absoluteStep = static_cast<std::int64_t>(
        std::ceil((block.ppqStart - tolerance) / stepLengthPpq));

    while (true)
    {
        const double boundaryPpq = static_cast<double>(absoluteStep) * stepLengthPpq;

        if (boundaryPpq >= blockEndPpq - tolerance)
            break;

        if (absoluteStep > lastTriggeredAbsoluteStep_
            && pattern().isHit(static_cast<std::uint16_t>(wrapStep(absoluteStep))))
        {
            const auto noteOnOffset = sampleOffsetFor(boundaryPpq, block, ppqPerSample);

            if (noteIsOn_)
                addNoteOff(output, noteOnOffset);

            addNoteOn(output, noteOnOffset);
            noteIsOn_ = true;
            lastTriggeredAbsoluteStep_ = absoluteStep;

            const double noteOffPpq = boundaryPpq + stepLengthPpq * gateRatio;

            if (noteOffPpq < blockEndPpq - tolerance)
            {
                addNoteOff(output, sampleOffsetFor(noteOffPpq, block, ppqPerSample));
                noteIsOn_ = false;
                pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
            }
            else
            {
                pendingNoteOffPpq_ = noteOffPpq;
            }
        }

        ++absoluteStep;
    }
}

} // namespace lps

