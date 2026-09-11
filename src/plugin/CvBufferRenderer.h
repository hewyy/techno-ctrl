#pragma once

#include "core/IOutputRenderer.h"

#include <array>
#include <cstddef>
#include <juce_audio_basics/juce_audio_basics.h>

namespace lps
{

struct CvRouteConfig
{
    int gateChannel = -1;
    int pitchChannel = -1;
    int controlChannel = -1;
    float referencePitchSemitones = 60.0f;
    float voltsAtReferencePitch = 0.0f;
    float voltsPerOctave = 1.0f;
};

class CvBufferRenderer final : public IOutputRenderer
{
public:
    static constexpr std::size_t maximumRouteCount = 64;

    [[nodiscard]] bool configureRoute(
        RouteId routeId,
        CvRouteConfig config) noexcept;
    void setAudioBuffer(juce::AudioBuffer<float>& buffer) noexcept;
    void clearAudioBuffer() noexcept;

    void prepare(const RenderSpec& spec) noexcept override;
    [[nodiscard]] bool renderBlock(
        const TimelineBlock& block,
        RoutedEventView events) noexcept override;
    void resetOutputs() noexcept override;

private:
    struct RouteState
    {
        RouteId routeId;
        CvRouteConfig config;
        float gate = 0.0f;
        float pitch = 0.0f;
        float control = 0.0f;
        float controlAtFrame = 0.0f;
        int controlFrame = 0;
        std::uint16_t activeTriggerCount = 0;
        bool configured = false;
    };

    struct ActiveTrigger
    {
        PlayerId playerId;
        RouteId routeId;
        TriggerId triggerId;
        bool active = false;
    };

    [[nodiscard]] RouteState* route(RouteId routeId) noexcept;
    [[nodiscard]] bool channelConfiguredElsewhere(int channel) const noexcept;
    [[nodiscard]] bool validChannel(int channel) const noexcept;
    void fillFrom(int channel, int frame, float value) noexcept;
    void fillRamp(
        int channel,
        int startFrame,
        int endFrame,
        float startValue,
        float endValue) noexcept;
    [[nodiscard]] bool rememberTrigger(const RoutedEvent& event) noexcept;
    [[nodiscard]] bool releaseTrigger(const RoutedEvent& event) noexcept;

    juce::AudioBuffer<float>* buffer_ = nullptr;
    std::array<RouteState, maximumRouteCount> routes_ {};
    std::size_t routeCount_ = 0;
    std::array<ActiveTrigger, SequencerEventBuffer::capacity> activeTriggers_ {};
};

} // namespace lps
