#include "plugin/PluginProcessor.h"

#include <cstdlib>
#include <iostream>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: "
              << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

void testSynthTwoPatternPlayersProduceIndependentNotes()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-processor-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");

    {
        LivePatternSequencerProcessor processor(catalog);
        processor.setRateAndBufferSizeDetails(48'000.0, 6'000);
        processor.prepareToPlay(48'000.0, 6'000);

        for (std::size_t slot = 0;
             slot < LivePatternSequencerProcessor::synthTwoPatternCount;
             ++slot)
        {
            const auto playerIndex = processor.synthTwoPlayerIndexForUi(slot);
            const auto pattern = processor.patternForUi(playerIndex);
            if (!pattern.isHit(0))
                processor.togglePlayerStep(playerIndex, 0);
            CHECK(!processor.playerMutedForUi(playerIndex));
        }

        processor.setInternalTransportPlayingForUi(true);
        juce::AudioBuffer<float> audio(
            processor.getTotalNumOutputChannels(), 6'000);
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);

        int synthNoteOns = 0;
        for (const auto metadata : midi)
        {
            if (metadata.getMessage().isNoteOn()
                && metadata.getMessage().getChannel() == 13)
            {
                ++synthNoteOns;
            }
        }
        if (synthNoteOns != static_cast<int>(
                LivePatternSequencerProcessor::synthTwoPatternCount))
        {
            std::cerr << "Expected three Synth 2 note-ons, received "
                      << synthNoteOns << " from " << midi.getNumEvents()
                      << " total MIDI events\n";
            for (const auto metadata : midi)
                std::cerr << metadata.getMessage().getDescription() << '\n';
        }
        CHECK(synthNoteOns == static_cast<int>(
            LivePatternSequencerProcessor::synthTwoPatternCount));

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        auto parsed = juce::JSON::parse(juce::String::fromUTF8(
            static_cast<const char*>(state.getData()),
            static_cast<int>(state.getSize())));
        auto* root = parsed.getDynamicObject();
        CHECK(root != nullptr);
        auto* players = root->getProperty("players").getArray();
        CHECK(players != nullptr);
        for (std::size_t slot = 0;
             slot < LivePatternSequencerProcessor::synthTwoPatternCount;
             ++slot)
        {
            const auto playerIndex = processor.synthTwoPlayerIndexForUi(slot);
            auto* player = players->getReference(
                static_cast<int>(playerIndex)).getDynamicObject();
            CHECK(player != nullptr);
            player->setProperty(
                "muted",
                slot == LivePatternSequencerProcessor::synthTwoPatternCount - 1);
        }
        const auto modifiedJson = juce::JSON::toString(parsed, true);
        processor.setStateInformation(
            modifiedJson.toRawUTF8(),
            static_cast<int>(modifiedJson.getNumBytesAsUTF8()));

        CHECK(!processor.playerMutedForUi(
            processor.synthTwoPlayerIndexForUi(0)));
        CHECK(!processor.playerMutedForUi(
            processor.synthTwoPlayerIndexForUi(1)));
        CHECK(processor.playerMutedForUi(
            processor.synthTwoPlayerIndexForUi(2)));

        processor.releaseResources();
        processor.setRateAndBufferSizeDetails(48'000.0, 6'000);
        processor.prepareToPlay(48'000.0, 6'000);
        processor.setInternalTransportPlayingForUi(true);
        midi.clear();
        processor.processBlock(audio, midi);
        synthNoteOns = 0;
        for (const auto metadata : midi)
        {
            if (metadata.getMessage().isNoteOn()
                && metadata.getMessage().getChannel() == 13)
            {
                ++synthNoteOns;
            }
        }
        CHECK(synthNoteOns == 2);
    }

    CHECK(directory.deleteRecursively());
}
} // namespace

int main()
{
    testSynthTwoPatternPlayersProduceIndependentNotes();
    std::cout << "PluginProcessor tests passed\n";
    return EXIT_SUCCESS;
}
