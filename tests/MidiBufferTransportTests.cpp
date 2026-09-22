#include "plugin/MidiBufferRenderer.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

[[nodiscard]] lps::RoutedEvent routedTrigger(
    lps::SemanticEventType type,
    std::uint64_t trigger,
    float note,
    float intensity,
    std::uint32_t frameOffset)
{
    lps::RoutedEvent routed;
    routed.event = type == lps::SemanticEventType::triggerStart
        ? lps::SequencerEvent::triggerStart(0.0, { trigger }, intensity)
        : lps::SequencerEvent::triggerEnd(0.0, { trigger });
    routed.frameOffset = frameOffset;
    routed.event.hasMusicalPitch = true;
    routed.event.musicalPitchSemitones = note;
    return routed;
}

void testDistinctDrumNotesUseOneTransportChannel()
{
    constexpr int drumChannel = 1;
    lps::MidiBufferRenderer transport { drumChannel };
    transport.prepare({ 48'000.0, 512 });
    juce::MidiBuffer midi;
    transport.setMidiBuffer(midi);

    const std::array events {
        routedTrigger(lps::SemanticEventType::triggerStart, 1, 36.0f,
            100.0f / 127.0f, 0),
        routedTrigger(lps::SemanticEventType::triggerStart, 2, 39.0f,
            100.0f / 127.0f, 120),
        routedTrigger(lps::SemanticEventType::triggerEnd, 1, 36.0f, 0.0f, 240),
        routedTrigger(lps::SemanticEventType::triggerEnd, 2, 39.0f, 0.0f, 360)
    };
    CHECK(transport.renderBlock({}, { events.data(), events.size() }));

    std::vector<juce::MidiMessage> messages;
    std::vector<int> samplePositions;
    for (const juce::MidiMessageMetadata metadata : midi)
    {
        messages.push_back(metadata.getMessage());
        samplePositions.push_back(metadata.samplePosition);
    }

    CHECK(messages.size() == 4);
    CHECK(messages[0].isNoteOn());
    CHECK(messages[0].getChannel() == drumChannel);
    CHECK(messages[0].getNoteNumber() == 36);
    CHECK(messages[0].getVelocity() == 100);
    CHECK(messages[1].isNoteOn());
    CHECK(messages[1].getChannel() == drumChannel);
    CHECK(messages[1].getNoteNumber() == 39);
    CHECK(messages[1].getVelocity() == 100);
    CHECK(messages[2].isNoteOff());
    CHECK(messages[2].getChannel() == drumChannel);
    CHECK(messages[2].getNoteNumber() == 36);
    CHECK(messages[3].isNoteOff());
    CHECK(messages[3].getChannel() == drumChannel);
    CHECK(messages[3].getNoteNumber() == 39);
    CHECK(samplePositions == std::vector<int>({ 0, 120, 240, 360 }));
}

void testEightBitVelocityLevelsMapAcrossTheMidiRange()
{
    lps::MidiBufferRenderer transport;
    juce::MidiBuffer midi;
    transport.setMidiBuffer(midi);

    constexpr std::array<std::uint8_t, 4> levels {255, 100, 225, 150};
    std::array<lps::RoutedEvent, levels.size()> events;
    for (std::size_t index = 0; index < levels.size(); ++index)
    {
        const auto level = levels[index];
        events[index] = routedTrigger(
            lps::SemanticEventType::triggerStart,
            static_cast<std::uint64_t>(level),
            36.0f,
            static_cast<float>(level) / 255.0f,
            0);
    }
    CHECK(transport.renderBlock({}, { events.data(), events.size() }));

    constexpr std::array<int, 4> expectedMidiVelocities {127, 50, 112, 75};
    std::size_t index = 0;
    for (const juce::MidiMessageMetadata metadata : midi)
    {
        CHECK(index < expectedMidiVelocities.size());
        CHECK(metadata.getMessage().getVelocity()
              == expectedMidiVelocities[index]);
        ++index;
    }
    CHECK(index == expectedMidiVelocities.size());
}

void testResetOutputsSendsMidiPanicAtBlockStart()
{
    constexpr int midiChannel = 7;
    lps::MidiBufferRenderer transport { midiChannel };
    juce::MidiBuffer midi;
    transport.setMidiBuffer(midi);

    lps::TimelineBlock block;
    block.tempoBpm = 120.0;
    block.sampleRate = 48'000.0;
    block.sampleCount = 512;
    const auto start = routedTrigger(
        lps::SemanticEventType::triggerStart, 1, 60.0f, 1.0f, 0);
    CHECK(transport.renderBlock({}, { &start, 1 }));

    transport.resetOutputs();

    CHECK(midi.getNumEvents() == 2);
    auto event = midi.begin();
    CHECK((*event).getMessage().isNoteOn());
    ++event;
    CHECK((*event).samplePosition == 0);
    CHECK((*event).getMessage().isAllNotesOff());
    CHECK((*event).getMessage().getChannel() == midiChannel);
}

void testTriggerEndUsesThePitchRememberedForItsStart()
{
    lps::MidiBufferRenderer renderer;
    juce::MidiBuffer midi;
    renderer.setMidiBuffer(midi);

    auto start = routedTrigger(
        lps::SemanticEventType::triggerStart, 42, 73.0f, 1.0f, 5);
    auto end = routedTrigger(
        lps::SemanticEventType::triggerEnd, 42, 0.0f, 0.0f, 9);
    start.sourceVoiceId = { 3 };
    start.routeId = { 7 };
    end.sourceVoiceId = start.sourceVoiceId;
    end.routeId = start.routeId;
    const std::array events { start, end };

    CHECK(renderer.renderBlock({}, { events.data(), events.size() }));
    CHECK(midi.getNumEvents() == 2);
    auto event = midi.begin();
    CHECK((*event).getMessage().isNoteOn());
    CHECK((*event).getMessage().getNoteNumber() == 73);
    ++event;
    CHECK((*event).getMessage().isNoteOff());
    CHECK((*event).getMessage().getNoteNumber() == 73);
}

void testRenderReportsMissingDestination()
{
    lps::MidiBufferRenderer transport;
    CHECK(!transport.renderBlock({}, {}));
}

void testContinuousControlRoutesRenderMidiCc()
{
    lps::MidiBufferRenderer renderer {13};
    CHECK(renderer.configureControlRoute({42}, 44));
    juce::MidiBuffer midi;
    renderer.setMidiBuffer(midi);

    lps::RoutedEvent routed;
    routed.routeId = {42};
    routed.frameOffset = 17;
    routed.event = lps::SequencerEvent::voiceControlPoint(
        0.0, lps::VoiceParameterId {3}, 91.2f);
    CHECK(renderer.renderBlock({}, {&routed, 1}));
    CHECK(midi.getNumEvents() == 1);
    const auto metadata = *midi.begin();
    CHECK(metadata.samplePosition == 17);
    CHECK(metadata.getMessage().isController());
    CHECK(metadata.getMessage().getChannel() == 13);
    CHECK(metadata.getMessage().getControllerNumber() == 44);
    CHECK(metadata.getMessage().getControllerValue() == 91);
}
}

int main()
{
    testDistinctDrumNotesUseOneTransportChannel();
    testEightBitVelocityLevelsMapAcrossTheMidiRange();
    testResetOutputsSendsMidiPanicAtBlockStart();
    testTriggerEndUsesThePitchRememberedForItsStart();
    testRenderReportsMissingDestination();
    testContinuousControlRoutesRenderMidiCc();
    std::cout << "All MIDI buffer transport tests passed.\n";
    return 0;
}
