#pragma once

#include "core/IOutputRenderer.h"

#include <array>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>

namespace lps
{

class MidiBufferRenderer final : public IOutputRenderer
{
public:
    explicit MidiBufferRenderer(int midiChannel = 1) noexcept;

    [[nodiscard]] bool configureMidiChannel(
        RouteId route, int midiChannel) noexcept;
    [[nodiscard]] bool configureControlRoute(
        RouteId route, int controllerNumber) noexcept;
    [[nodiscard]] bool configureVolcaSampleSelectRoute(
        RouteId route,
        int bankControllerNumber = 3,
        int sampleControllerNumber = 35) noexcept;
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
        enum class Kind : std::uint8_t
        {
            standard,
            volcaSampleSelect
        };

        RouteId route;
        std::uint8_t controllerNumber = 0;
        std::uint8_t secondaryControllerNumber = 0;
        Kind kind = Kind::standard;
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

    struct ChannelRoute
    {
        RouteId route;
        std::uint8_t midiChannel = 1;
        bool configured = false;
    };

    [[nodiscard]] std::optional<std::uint8_t> noteForEnd(
        const RoutedEvent& event) noexcept;
    [[nodiscard]] bool rememberStart(
        const RoutedEvent& event,
        std::uint8_t note) noexcept;
    [[nodiscard]] const ControlRoute* controlRoute(
        RouteId route) const noexcept;
    [[nodiscard]] int midiChannelForRoute(RouteId route) const noexcept;
    void invalidateControlCache() noexcept;
    [[nodiscard]] bool controlValueWasSent(
        int midiChannel,
        int controllerNumber,
        std::uint8_t value) const noexcept;
    void rememberControlValue(
        int midiChannel,
        int controllerNumber,
        std::uint8_t value) noexcept;

    juce::MidiBuffer* midiBuffer_ = nullptr;
    int midiChannel_;
    std::array<ActiveTrigger, SequencerEventBuffer::capacity> activeTriggers_ {};
    std::array<ChannelRoute, 256> channelRoutes_ {};
    std::array<ControlRoute, 256> controlRoutes_ {};
    std::array<std::array<std::int16_t, 128>, 16> lastSentControlValues_ {};
};

} // namespace lps
