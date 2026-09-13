#include "plugin/CvBufferRenderer.h"

#include <algorithm>

namespace lps
{

bool CvBufferRenderer::configureRoute(
    RouteId routeId,
    CvRouteConfig config) noexcept
{
    if (routeId.value == RouteId::invalidValue
        || routeCount_ == routes_.size()
        || route(routeId) != nullptr)
    {
        return false;
    }

    const std::array channels {
        config.gateChannel, config.pitchChannel, config.controlChannel
    };
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        if (channels[index] < 0)
            continue;
        for (std::size_t other = index + 1; other < channels.size(); ++other)
            if (channels[index] == channels[other])
                return false;
        if (channelConfiguredElsewhere(channels[index]))
            return false;
    }

    routes_[routeCount_++] = {
        routeId, config, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, true
    };
    return true;
}

void CvBufferRenderer::setAudioBuffer(juce::AudioBuffer<float>& buffer) noexcept
{
    buffer_ = &buffer;
}

void CvBufferRenderer::clearAudioBuffer() noexcept
{
    buffer_ = nullptr;
}

void CvBufferRenderer::prepare(const RenderSpec& /*spec*/) noexcept
{
    for (auto& routeState : routes_)
    {
        routeState.gate = 0.0f;
        routeState.pitch = 0.0f;
        routeState.control = 0.0f;
        routeState.controlAtFrame = 0.0f;
        routeState.controlFrame = 0;
        routeState.activeTriggerCount = 0;
    }
    for (auto& trigger : activeTriggers_)
        trigger = {};
}

bool CvBufferRenderer::renderBlock(
    const TimelineBlock& /*block*/,
    RoutedEventView events) noexcept
{
    if (buffer_ == nullptr)
        return false;
    if (buffer_->getNumSamples() == 0)
        return events.empty();

    for (std::size_t index = 0; index < routeCount_; ++index)
    {
        const auto& state = routes_[index];
        if (!validChannel(state.config.gateChannel)
            || !validChannel(state.config.pitchChannel)
            || !validChannel(state.config.controlChannel))
        {
            return false;
        }
        fillFrom(state.config.gateChannel, 0, state.gate);
        fillFrom(state.config.pitchChannel, 0, state.pitch);
        fillFrom(state.config.controlChannel, 0, state.control);
        routes_[index].controlAtFrame = state.control;
        routes_[index].controlFrame = 0;
    }

    for (const auto& event : events)
    {
        auto* state = route(event.routeId);
        if (state == nullptr)
            continue;
        const auto frame = std::min<int>(
            static_cast<int>(event.frameOffset),
            std::max(0, buffer_->getNumSamples() - 1));

        if (event.event.type == SemanticEventType::triggerStart)
        {
            if (!rememberTrigger(event))
                return false;
            ++state->activeTriggerCount;
            state->gate = 1.0f;
            fillFrom(state->config.gateChannel, frame, state->gate);
            if (event.event.hasMusicalPitch)
            {
                const auto pitchSemitones = event.event.musicalPitchSemitones;
                state->pitch = state->config.voltsAtReferencePitch
                    + (pitchSemitones
                        - state->config.referencePitchSemitones)
                        / 12.0f * state->config.voltsPerOctave;
                fillFrom(state->config.pitchChannel, frame, state->pitch);
            }
        }
        else if (event.event.type == SemanticEventType::triggerEnd)
        {
            if (releaseTrigger(event) && state->activeTriggerCount != 0)
                --state->activeTriggerCount;
            if (state->activeTriggerCount == 0)
            {
                state->gate = 0.0f;
                fillFrom(state->config.gateChannel, frame, state->gate);
            }
        }
        else
        {
            const auto nextControl = std::clamp(
                event.event.normalizedValue, 0.0f, 1.0f);
            if (event.event.interpolation == InterpolationPolicy::linear)
            {
                fillRamp(
                    state->config.controlChannel,
                    state->controlFrame,
                    frame,
                    state->controlAtFrame,
                    nextControl);
            }
            state->control = nextControl;
            fillFrom(state->config.controlChannel, frame, state->control);
            state->controlAtFrame = state->control;
            state->controlFrame = frame;
        }
    }

    return true;
}

void CvBufferRenderer::resetOutputs() noexcept
{
    for (std::size_t index = 0; index < routeCount_; ++index)
    {
        auto& state = routes_[index];
        state.gate = 0.0f;
        state.pitch = 0.0f;
        state.control = 0.0f;
        state.controlAtFrame = 0.0f;
        state.controlFrame = 0;
        state.activeTriggerCount = 0;
        fillFrom(state.config.gateChannel, 0, 0.0f);
        fillFrom(state.config.pitchChannel, 0, 0.0f);
        fillFrom(state.config.controlChannel, 0, 0.0f);
    }
    for (auto& trigger : activeTriggers_)
        trigger = {};
}

void CvBufferRenderer::fillRamp(
    int channel,
    int startFrame,
    int endFrame,
    float startValue,
    float endValue) noexcept
{
    if (channel < 0 || buffer_ == nullptr || channel >= buffer_->getNumChannels())
        return;
    const auto start = std::clamp(startFrame, 0, buffer_->getNumSamples());
    const auto end = std::clamp(endFrame, start, buffer_->getNumSamples() - 1);
    const auto length = end - start;
    auto* samples = buffer_->getWritePointer(channel);
    if (length == 0)
    {
        samples[start] = endValue;
        return;
    }
    for (int frame = start; frame <= end; ++frame)
    {
        const auto amount = static_cast<float>(frame - start)
            / static_cast<float>(length);
        samples[frame] = startValue + (endValue - startValue) * amount;
    }
}

CvBufferRenderer::RouteState* CvBufferRenderer::route(RouteId routeId) noexcept
{
    for (std::size_t index = 0; index < routeCount_; ++index)
        if (routes_[index].routeId == routeId)
            return &routes_[index];
    return nullptr;
}

bool CvBufferRenderer::channelConfiguredElsewhere(int channel) const noexcept
{
    for (std::size_t index = 0; index < routeCount_; ++index)
    {
        const auto& config = routes_[index].config;
        if (config.gateChannel == channel
            || config.pitchChannel == channel
            || config.controlChannel == channel)
        {
            return true;
        }
    }
    return false;
}

bool CvBufferRenderer::validChannel(int channel) const noexcept
{
    return channel < 0
        || (buffer_ != nullptr && channel < buffer_->getNumChannels());
}

void CvBufferRenderer::fillFrom(int channel, int frame, float value) noexcept
{
    if (channel < 0 || buffer_ == nullptr || channel >= buffer_->getNumChannels())
        return;
    const auto start = std::clamp(frame, 0, buffer_->getNumSamples());
    const auto sampleCount = buffer_->getNumSamples() - start;
    if (sampleCount > 0)
        juce::FloatVectorOperations::fill(
            buffer_->getWritePointer(channel, start), value, sampleCount);
}

bool CvBufferRenderer::rememberTrigger(const RoutedEvent& event) noexcept
{
    for (auto& trigger : activeTriggers_)
    {
        if (!trigger.active)
        {
            trigger = {
                event.sourceVoiceId,
                event.routeId,
                event.event.triggerId,
                true
            };
            return true;
        }
    }
    return false;
}

bool CvBufferRenderer::releaseTrigger(const RoutedEvent& event) noexcept
{
    for (auto& trigger : activeTriggers_)
    {
        if (trigger.active
            && trigger.voiceId == event.sourceVoiceId
            && trigger.routeId == event.routeId
            && trigger.triggerId == event.event.triggerId)
        {
            trigger.active = false;
            return true;
        }
    }
    return false;
}

} // namespace lps
