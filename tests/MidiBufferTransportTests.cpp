#include "plugin/MidiBufferTransport.h"

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

void testDistinctDrumNotesUseOneTransportChannel()
{
    constexpr int drumChannel = 1;
    lps::MidiBufferTransport transport { drumChannel };
    juce::MidiBuffer midi;
    transport.setMidiBuffer(midi);

    lps::ClockBlock block;
    block.ppqStart = 0.0;
    block.tempoBpm = 120.0;
    block.sampleRate = 48'000.0;
    block.sampleCount = 512;

    transport.send(
        { 0.000, 0, 36.0f, 100.0f / 127.0f, lps::SequencerEventType::triggerOn },
        block);
    transport.send(
        { 0.005, 0, 39.0f, 100.0f / 127.0f, lps::SequencerEventType::triggerOn },
        block);
    transport.send(
        { 0.010, 0, 36.0f, 0.0f, lps::SequencerEventType::triggerOff },
        block);
    transport.send(
        { 0.015, 0, 39.0f, 0.0f, lps::SequencerEventType::triggerOff },
        block);

    std::vector<juce::MidiMessage> messages;
    for (const juce::MidiMessageMetadata metadata : midi)
        messages.push_back(metadata.getMessage());

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
}

void testEightBitVelocityLevelsMapAcrossTheMidiRange()
{
    lps::MidiBufferTransport transport;
    juce::MidiBuffer midi;
    transport.setMidiBuffer(midi);

    lps::ClockBlock block;
    block.tempoBpm = 120.0;
    block.sampleRate = 48'000.0;
    block.sampleCount = 512;

    constexpr std::array<std::uint8_t, 4> levels {255, 100, 225, 150};
    for (const auto level : levels)
    {
        transport.send(
            { 0.0,
              0,
              36.0f,
              static_cast<float>(level) / 255.0f,
              lps::SequencerEventType::triggerOn },
            block);
    }

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
}

int main()
{
    testDistinctDrumNotesUseOneTransportChannel();
    testEightBitVelocityLevelsMapAcrossTheMidiRange();
    std::cout << "All MIDI buffer transport tests passed.\n";
    return 0;
}
