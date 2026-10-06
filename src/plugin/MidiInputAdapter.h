#pragma once

#include "core/MidiControlMapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace lps
{

class MidiInputAdapter
{
public:
    [[nodiscard]] static MidiControlEvent adapt(
        const juce::MidiMessage& message,
        int samplePosition,
        std::uint32_t eventOrder) noexcept;
};

} // namespace lps
