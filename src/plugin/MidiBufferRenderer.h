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

    [[nodiscard]] bool configureControlRoute(
        RouteId route, int controllerNumber) noexcept;
    void setMidiBuffer(juce::MidiBuffer& midiBuffer) noexcept;
    void clearMidiBuffer() noexcept;

    void prepare(const RenderSpec& spec) noexcept override;
    [[nodiscard]] bool renderBlock(
        const TimelineBlock& block,
        RoutedEventView events) noexcept override;
    void resetOutputs() noexcept override;

private:
    struct ControlRoute
    {
        RouteId route;
        std::uint8_t controllerNumber = 0;
        bool configured = false;
    };

    struct ActiveTrigger
    {
        VoiceId voiceId;
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
    [[nodiscard]] const ControlRoute* controlRoute(
        RouteId route) const noexcept;

    juce::MidiBuffer* midiBuffer_ = nullptr;
    int midiChannel_;
    std::array<ActiveTrigger, SequencerEventBuffer::capacity> activeTriggers_ {};
    std::array<ControlRoute, 64> controlRoutes_ {};
};

} // namespace lps
