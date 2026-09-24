#include "plugin/MidiBufferRenderer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lps
{

MidiBufferRenderer::MidiBufferRenderer(int midiChannel) noexcept
    : midiChannel_(std::clamp(midiChannel, 1, 16))
{
    invalidateControlCache();
}

bool MidiBufferRenderer::configureMidiChannel(
    RouteId route,
    int midiChannel) noexcept
{
    if (!route.isValid() || midiChannel < 1 || midiChannel > 16)
        return false;

    for (auto& configured : channelRoutes_)
    {
        if (configured.configured && configured.route == route)
        {
            configured.midiChannel = static_cast<std::uint8_t>(midiChannel);
            invalidateControlCache();
            return true;
        }
    }
    for (auto& configured : channelRoutes_)
    {
        if (!configured.configured)
        {
            configured = {
                route,
                static_cast<std::uint8_t>(midiChannel),
                true
            };
            invalidateControlCache();
            return true;
        }
    }
    return false;
}

bool MidiBufferRenderer::configureControlRoute(
    RouteId route,
    int controllerNumber) noexcept
{
    if (!route.isValid() || controllerNumber < 0 || controllerNumber > 127)
        return false;

    for (auto& configured : controlRoutes_)
    {
        if (configured.configured && configured.route == route)
        {
            configured.controllerNumber = static_cast<std::uint8_t>(
                controllerNumber);
            configured.secondaryControllerNumber = 0;
            configured.kind = ControlRoute::Kind::standard;
            invalidateControlCache();
            return true;
        }
    }
    for (auto& configured : controlRoutes_)
    {
        if (!configured.configured)
        {
            configured = {
                route,
                static_cast<std::uint8_t>(controllerNumber),
                0,
                ControlRoute::Kind::standard,
                true
            };
            invalidateControlCache();
            return true;
        }
    }
    return false;
}

bool MidiBufferRenderer::configureVolcaSampleSelectRoute(
    RouteId route,
    int bankControllerNumber,
    int sampleControllerNumber) noexcept
{
    if (!route.isValid()
        || bankControllerNumber < 0 || bankControllerNumber > 127
        || sampleControllerNumber < 0 || sampleControllerNumber > 127)
    {
        return false;
    }

    for (auto& configured : controlRoutes_)
    {
        if (configured.configured && configured.route == route)
        {
            configured.controllerNumber = static_cast<std::uint8_t>(
                bankControllerNumber);
            configured.secondaryControllerNumber = static_cast<std::uint8_t>(
                sampleControllerNumber);
            configured.kind = ControlRoute::Kind::volcaSampleSelect;
            invalidateControlCache();
            return true;
        }
    }
    for (auto& configured : controlRoutes_)
    {
        if (!configured.configured)
        {
            configured = {
                route,
                static_cast<std::uint8_t>(bankControllerNumber),
                static_cast<std::uint8_t>(sampleControllerNumber),
                ControlRoute::Kind::volcaSampleSelect,
                true
            };
            invalidateControlCache();
            return true;
        }
    }
    return false;
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
    invalidateControlCache();
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
        {
            const auto* route = controlRoute(routed.routeId);
            if (route == nullptr)
                return false;
            const auto midiChannel = midiChannelForRoute(routed.routeId);
            if (route->kind == ControlRoute::Kind::volcaSampleSelect)
            {
                const auto sample = std::clamp(
                    static_cast<int>(std::lround(routed.event.normalizedValue)),
                    0,
                    199);
                const std::array<std::pair<std::uint8_t, std::uint8_t>, 2>
                    controls {{
                        {route->controllerNumber,
                            static_cast<std::uint8_t>(sample / 100)},
                        {route->secondaryControllerNumber,
                            static_cast<std::uint8_t>(sample % 100)}
                    }};
                for (const auto [controller, value] : controls)
                {
                    if (controlValueWasSent(midiChannel, controller, value))
                        continue;
                    if (!midiBuffer_->addEvent(
                            juce::MidiMessage::controllerEvent(
                                midiChannel, controller, value),
                            static_cast<int>(routed.frameOffset)))
                    {
                        return false;
                    }
                    rememberControlValue(midiChannel, controller, value);
                }
                continue;
            }
            const auto value = std::clamp(
                static_cast<int>(std::lround(routed.event.normalizedValue)),
                0,
                127);
            if (controlValueWasSent(
                    midiChannel,
                    route->controllerNumber,
                    static_cast<std::uint8_t>(value)))
            {
                continue;
            }
            if (!midiBuffer_->addEvent(
                    juce::MidiMessage::controllerEvent(
                        midiChannel,
                        route->controllerNumber,
                        value),
                    static_cast<int>(routed.frameOffset)))
            {
                return false;
            }
            rememberControlValue(
                midiChannel,
                route->controllerNumber,
                static_cast<std::uint8_t>(value));
            continue;
        }

        std::optional<std::uint8_t> note;
        if (routed.event.type == SemanticEventType::triggerStart)
        {
            if (!routed.event.hasMusicalPitch)
                return false;
            note = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(std::lround(
                    routed.event.musicalPitchSemitones)),
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
            ? juce::MidiMessage::noteOn(
                midiChannelForRoute(routed.routeId), *note, velocity)
            : juce::MidiMessage::noteOff(
                midiChannelForRoute(routed.routeId), *note);
        if (!midiBuffer_->addEvent(message, static_cast<int>(routed.frameOffset)))
            return false;
    }

    return true;
}

void MidiBufferRenderer::resetOutputs() noexcept
{
    for (auto& trigger : activeTriggers_)
        trigger = {};
    invalidateControlCache();
    if (midiBuffer_ != nullptr)
    {
        std::array<bool, 17> cleared {};
        cleared[static_cast<std::size_t>(midiChannel_)] = true;
        (void) midiBuffer_->addEvent(
            juce::MidiMessage::allNotesOff(midiChannel_), 0);
        for (const auto& configured : channelRoutes_)
        {
            if (!configured.configured || cleared[configured.midiChannel])
                continue;
            cleared[configured.midiChannel] = true;
            (void) midiBuffer_->addEvent(
                juce::MidiMessage::allNotesOff(configured.midiChannel), 0);
        }
    }
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

const MidiBufferRenderer::ControlRoute* MidiBufferRenderer::controlRoute(
    RouteId route) const noexcept
{
    for (const auto& configured : controlRoutes_)
        if (configured.configured && configured.route == route)
            return &configured;
    return nullptr;
}

int MidiBufferRenderer::midiChannelForRoute(RouteId route) const noexcept
{
    for (const auto& configured : channelRoutes_)
        if (configured.configured && configured.route == route)
            return configured.midiChannel;
    return midiChannel_;
}

void MidiBufferRenderer::invalidateControlCache() noexcept
{
    for (auto& channel : lastSentControlValues_)
        channel.fill(-1);
}

bool MidiBufferRenderer::controlValueWasSent(
    int midiChannel,
    int controllerNumber,
    std::uint8_t value) const noexcept
{
    return lastSentControlValues_[static_cast<std::size_t>(midiChannel - 1)]
        [static_cast<std::size_t>(controllerNumber)] == value;
}

void MidiBufferRenderer::rememberControlValue(
    int midiChannel,
    int controllerNumber,
    std::uint8_t value) noexcept
{
    lastSentControlValues_[static_cast<std::size_t>(midiChannel - 1)]
        [static_cast<std::size_t>(controllerNumber)] = value;
}

} // namespace lps
