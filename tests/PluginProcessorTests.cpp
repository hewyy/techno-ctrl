#include "plugin/PluginProcessor.h"

#include <algorithm>
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

void testPitchEditingUsesADraftAndPreservesTheLibraryRecord()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-pitch-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");

    {
        LivePatternSequencerProcessor processor(catalog);
        processor.prepareToPlay(48'000.0, 512);
        const auto playerIndex = processor.synthTwoPlayerIndexForUi(0);
        const auto selected = processor.selectedModulationForPlayer(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        const auto savedBefore = processor.modulationAtForUi(selected);

        // Direct pitch placement is canonical in chromatic mode.
        processor.setPlayerModulationValue(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            0,
            131);
        auto draft = processor.modulationForUi(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(draft.values[0]
            == lps::NormalizedValue::fromUnipolar8(130));

        processor.setPitchRootForPlayer(playerIndex, 11);
        processor.setPitchScaleForPlayer(
            playerIndex, lps::ScaleId::phrygianDominant);
        processor.setPitchEditModeForPlayer(
            playerIndex, lps::PitchEditMode::scaleAware);

        // Merely changing the editing context does not touch the draft.
        draft = processor.modulationForUi(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(draft.values[0]
            == lps::NormalizedValue::fromUnipolar8(130));

        CHECK(processor.adjustPlayerPitchToScale(playerIndex));
        draft = processor.modulationForUi(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(draft.values[0]
            == lps::NormalizedValue::fromUnipolar8(128));
        CHECK(lps::modulationsEqual(
            processor.modulationAtForUi(selected), savedBefore));

        // Direct edits from either UI page are quantized by the processor,
        // so every entry path observes the selected per-player scale.
        processor.setPlayerModulationValue(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            0,
            130);
        draft = processor.modulationForUi(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(draft.values[0]
            == lps::NormalizedValue::fromUnipolar8(128));

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        processor.setPitchEditModeForPlayer(
            playerIndex, lps::PitchEditMode::chromatic);
        processor.setPitchRootForPlayer(playerIndex, 0);
        processor.setPitchScaleForPlayer(playerIndex, lps::ScaleId::major);
        processor.setStateInformation(
            state.getData(), static_cast<int>(state.getSize()));
        const auto restoredContext = processor.pitchEditContextForPlayer(
            playerIndex);
        CHECK(restoredContext.mode == lps::PitchEditMode::scaleAware);
        CHECK(restoredContext.rootPitchClass == 11);
        CHECK(restoredContext.scaleId == lps::ScaleId::phrygianDominant);

        // An off-scale edit moves strictly to the next scale note.
        processor.setPitchEditModeForPlayer(
            playerIndex, lps::PitchEditMode::chromatic);
        processor.setPlayerModulationValue(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            0,
            122);
        processor.setPitchEditModeForPlayer(
            playerIndex, lps::PitchEditMode::scaleAware);
        processor.editPlayerPitch(playerIndex, 0, 1);
        draft = processor.modulationForUi(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(draft.values[0]
            == lps::NormalizedValue::fromUnipolar8(126));

        // Appending in a scale context affects only the newly placed note.
        processor.setPitchEditModeForPlayer(
            playerIndex, lps::PitchEditMode::chromatic);
        processor.setPlayerModulationValue(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            0,
            122);
        processor.setPitchEditModeForPlayer(
            playerIndex, lps::PitchEditMode::scaleAware);
        processor.setPlayerModulationLength(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            2);
        draft = processor.modulationForUi(
            playerIndex,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(draft.values[0]
            == lps::NormalizedValue::fromUnipolar8(122));
        CHECK(draft.values[1]
            == lps::NormalizedValue::fromUnipolar8(120));
    }

    CHECK(directory.deleteRecursively());
}

void testVolcaSamplePartsUseMidiChannelsOneThroughTen()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-sample-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");

    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.voiceCountForUi() == 22);
        CHECK(processor.playerCountForUi() == 24);
        CHECK(processor.sampleLaneCountForUi()
            == LivePatternSequencerProcessor::samplePartCount * 13);

        const auto sampleSelectLane =
            LivePatternSequencerProcessor::samplePartCount;
        const auto sampleSelectInfo = processor.sampleLaneInfoForUi(
            sampleSelectLane);
        CHECK(sampleSelectInfo.name == "Current sample");
        CHECK(sampleSelectInfo.midiCc == 3);
        CHECK(sampleSelectInfo.secondaryMidiCc == 35);
        CHECK(sampleSelectInfo.maximumValue == 199);

        for (std::size_t slot = 0;
             slot < LivePatternSequencerProcessor::samplePartCount;
             ++slot)
        {
            const auto playerIndex = processor.samplePlayerIndexForUi(slot);
            CHECK(playerIndex < processor.playerCountForUi());
            CHECK(processor.playerNameForUi(playerIndex)
                == "Sample " + juce::String(static_cast<int>(slot + 1)));
            const auto pattern = processor.patternForUi(playerIndex);
            for (std::size_t step = 0; step < pattern.stepCount; ++step)
                if (!pattern.isHit(static_cast<std::uint16_t>(step)))
                    processor.togglePlayerStep(playerIndex, step);
            processor.setPlayerPlaybackWindow(
                playerIndex, 0, pattern.stepCount - 1);
        }

        processor.setRateAndBufferSizeDetails(48'000.0, 6'000);
        processor.prepareToPlay(48'000.0, 6'000);
        processor.setSampleLaneValue(sampleSelectLane, 0, 192);
        processor.setInternalTransportPlayingForUi(true);
        juce::AudioBuffer<float> audio(
            processor.getTotalNumOutputChannels(), 6'000);
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);

        std::array<int, 10> noteOns {};
        std::array<int, 10> controllers {};
        int partOneSampleBank = -1;
        int partOneSampleRemainder = -1;
        for (const auto metadata : midi)
        {
            const auto& message = metadata.getMessage();
            const auto channel = message.getChannel();
            if (channel < 1 || channel > 10)
                continue;
            if (message.isNoteOn())
                ++noteOns[static_cast<std::size_t>(channel - 1)];
            if (message.isController())
            {
                const auto cc = message.getControllerNumber();
                if (cc == 3 || cc == 35 || cc == 7 || cc == 10
                    || (cc >= 40 && cc <= 48))
                    ++controllers[static_cast<std::size_t>(channel - 1)];
                if (channel == 1 && cc == 3)
                    partOneSampleBank = message.getControllerValue();
                if (channel == 1 && cc == 35)
                    partOneSampleRemainder = message.getControllerValue();
            }
        }
        for (int block = 1; block < 16
             && std::any_of(noteOns.begin(), noteOns.end(),
                 [](int count) { return count == 0; });
             ++block)
        {
            midi.clear();
            processor.processBlock(audio, midi);
            for (const auto metadata : midi)
            {
                const auto& message = metadata.getMessage();
                const auto channel = message.getChannel();
                if (message.isNoteOn() && channel >= 1 && channel <= 10)
                    ++noteOns[static_cast<std::size_t>(channel - 1)];
            }
        }
        for (std::size_t slot = 0; slot < noteOns.size(); ++slot)
        {
            CHECK(noteOns[slot] >= 1);
            CHECK(controllers[slot] == 13);
        }
        CHECK(partOneSampleBank == 1);
        CHECK(partOneSampleRemainder == 50);

        // Every Sample parameter lane is constant by default. The following
        // hit must still produce its note, without resending the thirteen CCs
        // that established the same channel/controller values on the first
        // hit.
        noteOns.fill(0);
        controllers.fill(0);
        for (int block = 0; block < 16
             && std::any_of(noteOns.begin(), noteOns.end(),
                 [](int count) { return count == 0; });
             ++block)
        {
            midi.clear();
            processor.processBlock(audio, midi);
            for (const auto metadata : midi)
            {
                const auto& message = metadata.getMessage();
                const auto channel = message.getChannel();
                if (channel < 1 || channel > 10)
                    continue;
                if (message.isNoteOn())
                    ++noteOns[static_cast<std::size_t>(channel - 1)];
                if (message.isController())
                {
                    const auto cc = message.getControllerNumber();
                    if (cc == 3 || cc == 35 || cc == 7 || cc == 10
                        || (cc >= 40 && cc <= 48))
                        ++controllers[static_cast<std::size_t>(channel - 1)];
                }
            }
        }
        for (std::size_t slot = 0; slot < noteOns.size(); ++slot)
        {
            CHECK(noteOns[slot] >= 1);
            CHECK(controllers[slot] == 0);
        }

        // Player 24 exercises scheduled-state encoding above the old 4-bit
        // player-index ceiling.
        const auto lastPlayer = processor.samplePlayerIndexForUi(9);
        CHECK(processor.schedulePlayerMute(lastPlayer, true, 1));
        CHECK(processor.playerMuteScheduledAtBarOffsetForUi(
            lastPlayer, true, 1));

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        const auto parsed = juce::JSON::parse(juce::String::fromUTF8(
            static_cast<const char*>(state.getData()),
            static_cast<int>(state.getSize())));
        CHECK(parsed.getDynamicObject() != nullptr);
        CHECK(static_cast<int>(
            parsed.getDynamicObject()->getProperty("schemaVersion")) == 3);
        const auto* lanes = parsed.getDynamicObject()
            ->getProperty("sampleParameterLanes").getArray();
        CHECK(lanes != nullptr);
        CHECK(lanes->size() == 120);
    }

    CHECK(directory.deleteRecursively());
}
} // namespace

int main()
{
    testSynthTwoPatternPlayersProduceIndependentNotes();
    testPitchEditingUsesADraftAndPreservesTheLibraryRecord();
    testVolcaSamplePartsUseMidiChannelsOneThroughTen();
    std::cout << "PluginProcessor tests passed\n";
    return EXIT_SUCCESS;
}
