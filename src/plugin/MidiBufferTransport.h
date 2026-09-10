#pragma once

#include "core/ITransport.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace lps
{

class MidiBufferTransport final : public ITransport
{
public:
    explicit MidiBufferTransport(int midiChannel = 1) noexcept;

    void setMidiBuffer(juce::MidiBuffer& midiBuffer) noexcept;
    void clearMidiBuffer() noexcept;

    void send(
        const SequencerEvent& event,
        const ClockBlock& block) noexcept override;

private:
    juce::MidiBuffer* midiBuffer_ = nullptr;
    int midiChannel_;
};

} // namespace lps
