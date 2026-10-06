#include "plugin/MidiInputAdapter.h"

#include <algorithm>

namespace lps
{

MidiControlEvent MidiInputAdapter::adapt(
    const juce::MidiMessage& message,
    int samplePosition,
    std::uint32_t eventOrder) noexcept
{
    MidiControlEvent event;
    event.samplePosition = samplePosition;
    event.eventOrder = eventOrder;
    event.channel = static_cast<std::uint8_t>(std::clamp(
        message.getChannel(), 0, 16));

    if (message.isController())
    {
        event.type = MidiControlMessageType::controlChange;
        event.control = static_cast<std::uint8_t>(
            message.getControllerNumber());
        event.rawValue = static_cast<std::uint8_t>(
            message.getControllerValue());
    }
    else if (message.isNoteOn())
    {
        event.type = MidiControlMessageType::noteOn;
        event.control = static_cast<std::uint8_t>(message.getNoteNumber());
        event.rawValue = static_cast<std::uint8_t>(
            message.getVelocity() * 127.0f + 0.5f);
    }
    else if (message.isNoteOff())
    {
        event.type = MidiControlMessageType::noteOff;
        event.control = static_cast<std::uint8_t>(message.getNoteNumber());
        event.rawValue = static_cast<std::uint8_t>(
            message.getVelocity() * 127.0f + 0.5f);
    }
    return event;
}

} // namespace lps
