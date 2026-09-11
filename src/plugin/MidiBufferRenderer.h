#pragma once

#include "core/IOutputRenderer.h"

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>

namespace lps
{

class MidiBufferRenderer final : public IOutputRenderer
{
public:
    explicit MidiBufferRenderer(int midiChannel = 1) noexcept;

    void setMidiBuffer(juce::MidiBuffer& midiBuffer) noexcept;
    void clearMidiBuffer() noexcept;

    void prepare(const RenderSpec& spec) noexcept override;
    [[nodiscard]] bool renderBlock(
        const TimelineBlock& block,
        RoutedEventView events) noexcept override;
    void resetOutputs() noexcept override;

private:
    struct ActiveTrigger
    {
        PlayerId playerId;
        RouteId routeId;
        TriggerId triggerId;
        std::uint8_t note = 0;
        bool active = false;
    };

    [[nodiscard]] std::optional<std::uint8_t> noteForEnd(
        const RoutedEvent& event) noexcept;
    [[nodiscard]] bool rememberStart(
        const RoutedEvent& event,
        std::uint8_t note) noexcept;

    juce::MidiBuffer* midiBuffer_ = nullptr;
    int midiChannel_;
    std::array<ActiveTrigger, SequencerEventBuffer::capacity> activeTriggers_ {};
};

} // namespace lps
