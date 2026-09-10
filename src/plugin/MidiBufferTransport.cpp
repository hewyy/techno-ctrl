#include "plugin/MidiBufferTransport.h"

#include <algorithm>
#include <cmath>

namespace lps
{

MidiBufferTransport::MidiBufferTransport(int midiChannel) noexcept
    : midiChannel_(std::clamp(midiChannel, 1, 16))
{
}

void MidiBufferTransport::setMidiBuffer(juce::MidiBuffer& midiBuffer) noexcept
{
    midiBuffer_ = &midiBuffer;
}

void MidiBufferTransport::clearMidiBuffer() noexcept
{
    midiBuffer_ = nullptr;
}

void MidiBufferTransport::prepare(const PrepareSpec& /*spec*/) noexcept
{
}

bool MidiBufferTransport::send(
    const SequencerEvent& event,
    const TimelineBlock& block) noexcept
{
    if (midiBuffer_ == nullptr)
        return false;

    const auto note = std::clamp(
        static_cast<int>(std::lround(event.pitchSemitones)), 0, 127);
    const auto message = event.type == SequencerEventType::triggerOn
        ? juce::MidiMessage::noteOn(
            midiChannel_,
            note,
            std::clamp(event.value, 0.0f, 1.0f))
        : juce::MidiMessage::noteOff(
            midiChannel_,
            note);

    const double ppqPerSample = block.tempoBpm > 0.0 && block.sampleRate > 0.0
        ? block.tempoBpm / (60.0 * block.sampleRate)
        : 0.0;
    const auto maximumOffset = block.sampleCount == 0
        ? 0
        : static_cast<int>(block.sampleCount - 1);
    const auto sampleOffset = ppqPerSample > 0.0
        ? std::clamp(
            static_cast<int>(std::llround(
                (event.ppqPosition - block.ppqStart) / ppqPerSample)),
            0,
            maximumOffset)
        : 0;

    return midiBuffer_->addEvent(message, sampleOffset);
}

void MidiBufferTransport::resetOutputs(const TimelineBlock& /*block*/) noexcept
{
    if (midiBuffer_ != nullptr)
        (void) midiBuffer_->addEvent(
            juce::MidiMessage::allNotesOff(midiChannel_), 0);
}

} // namespace lps
