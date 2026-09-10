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
    const RoutedEvent& routed) noexcept
{
    if (midiBuffer_ == nullptr)
        return false;
    if (routed.event.type == SemanticEventType::controlPoint)
        return true;
    if (!routed.hasMappedPitch)
        return false;

    const auto note = std::clamp(
        static_cast<int>(std::lround(routed.mappedPitchSemitones)), 0, 127);
    const auto message = routed.event.type == SemanticEventType::triggerStart
        ? juce::MidiMessage::noteOn(
            midiChannel_,
            note,
            std::clamp(routed.event.normalizedValue, 0.0f, 1.0f))
        : juce::MidiMessage::noteOff(
            midiChannel_,
            note);

    return midiBuffer_->addEvent(message, static_cast<int>(routed.frameOffset));
}

void MidiBufferTransport::resetOutputs(const TimelineBlock& /*block*/) noexcept
{
    if (midiBuffer_ != nullptr)
        (void) midiBuffer_->addEvent(
            juce::MidiMessage::allNotesOff(midiChannel_), 0);
}

} // namespace lps
