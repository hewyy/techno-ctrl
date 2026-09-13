#include "plugin/MidiBufferRenderer.h"

#include <algorithm>
#include <cmath>

namespace lps
{

MidiBufferRenderer::MidiBufferRenderer(int midiChannel) noexcept
    : midiChannel_(std::clamp(midiChannel, 1, 16))
{
}

void MidiBufferRenderer::setMidiBuffer(juce::MidiBuffer& midiBuffer) noexcept
{
    midiBuffer_ = &midiBuffer;
}

void MidiBufferRenderer::clearMidiBuffer() noexcept
{
    midiBuffer_ = nullptr;
}

void MidiBufferRenderer::prepare(const RenderSpec& /*spec*/) noexcept
{
    for (auto& trigger : activeTriggers_)
        trigger = {};
}

bool MidiBufferRenderer::renderBlock(
    const TimelineBlock& /*block*/,
    RoutedEventView events) noexcept
{
    if (midiBuffer_ == nullptr)
        return false;

    for (const auto& routed : events)
    {
        if (routed.event.type == SemanticEventType::controlPoint)
            continue;

        std::optional<std::uint8_t> note;
        if (routed.event.type == SemanticEventType::triggerStart)
        {
            if (!routed.event.hasMusicalPitch && !routed.hasMappedPitch)
                return false;
            note = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(
                    routed.event.hasMusicalPitch
                        ? routed.event.musicalPitchSemitones
                        : routed.mappedPitchSemitones)),
                0,
                127));
            if (!rememberStart(routed, *note))
                return false;
        }
        else
        {
            note = noteForEnd(routed);
            if (!note.has_value())
                continue;
        }

        const auto velocity = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(
                routed.event.normalizedValue * 127.0f)), 1, 127));
        const auto message = routed.event.type == SemanticEventType::triggerStart
            ? juce::MidiMessage::noteOn(midiChannel_, *note, velocity)
            : juce::MidiMessage::noteOff(midiChannel_, *note);
        if (!midiBuffer_->addEvent(message, static_cast<int>(routed.frameOffset)))
            return false;
    }

    return true;
}

void MidiBufferRenderer::resetOutputs() noexcept
{
    for (auto& trigger : activeTriggers_)
        trigger = {};
    if (midiBuffer_ != nullptr)
        (void) midiBuffer_->addEvent(
            juce::MidiMessage::allNotesOff(midiChannel_), 0);
}

std::optional<std::uint8_t> MidiBufferRenderer::noteForEnd(
    const RoutedEvent& event) noexcept
{
    for (auto& active : activeTriggers_)
    {
        if (active.active
            && active.voiceId == event.sourceVoiceId
            && active.routeId == event.routeId
            && active.triggerId == event.event.triggerId)
        {
            active.active = false;
            return active.note;
        }
    }

    return std::nullopt;
}

bool MidiBufferRenderer::rememberStart(
    const RoutedEvent& event,
    std::uint8_t note) noexcept
{
    for (auto& active : activeTriggers_)
    {
        if (!active.active)
        {
            active = {
                event.sourceVoiceId,
                event.routeId,
                event.event.triggerId,
                note,
                true
            };
            return true;
        }
    }
    return false;
}

} // namespace lps
