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
            const bool startsAtZero = pattern.pattern.hitCount != 0
                && pattern.pattern.hits[0].startTick == 0;
            if (!startsAtZero)
                CHECK(processor.addPlayerHit(playerIndex, 0, 240));
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
        if (synthNoteOns != 2)
            std::cerr << "Restored Synth 2 note-ons: " << synthNoteOns << '\n';
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
            for (lps::PatternTick tick = 0;
                 tick < pattern.pattern.cycleLengthTicks;
                 tick += lps::Pattern::legacyStepTicks)
            {
                const bool exists = std::any_of(
                    pattern.pattern.hits.begin(),
                    pattern.pattern.hits.begin() + pattern.pattern.hitCount,
                    [tick](const auto& hit) { return hit.startTick == tick; });
                if (!exists)
                    CHECK(processor.addPlayerHit(
                        playerIndex, tick, lps::Pattern::legacyStepTicks));
            }
            processor.setPlayerPlaybackWindow(
                playerIndex, 0, pattern.pattern.cycleLengthTicks);
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
            parsed.getDynamicObject()->getProperty("schemaVersion")) == 4);
        const auto* lanes = parsed.getDynamicObject()
            ->getProperty("sampleParameterLanes").getArray();
        CHECK(lanes != nullptr);
        CHECK(lanes->size() == 120);
    }

    CHECK(directory.deleteRecursively());
}

void testLegacyProcessorPatternStateMigratesToTicks()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-state-migration-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        auto parsed = juce::JSON::parse(juce::String::fromUTF8(
            static_cast<const char*>(state.getData()),
            static_cast<int>(state.getSize())));
        auto* root = parsed.getDynamicObject();
        CHECK(root != nullptr);
        root->setProperty("schemaVersion", 3);
        auto* players = root->getProperty("players").getArray();
        CHECK(players != nullptr);
        for (auto& serialized : *players)
        {
            auto* player = serialized.getDynamicObject();
            CHECK(player != nullptr);
            player->removeProperty("cycleLengthTicks");
            player->removeProperty("patternHits");
            player->removeProperty("offsetTicks");
            player->removeProperty("playbackStartTick");
            player->removeProperty("playbackEndTick");
            player->setProperty("hitMask", 5);
            player->setProperty("offset", 1);
            player->setProperty("playbackStart", 0);
            player->setProperty("playbackEnd", 3);
        }
        const auto legacy = juce::JSON::toString(parsed, true);
        processor.setStateInformation(
            legacy.toRawUTF8(), static_cast<int>(legacy.getNumBytesAsUTF8()));

        const auto migrated = processor.patternForUi(0);
        CHECK(migrated.pattern.hitCount == 2);
        CHECK(migrated.pattern.hits[0].startTick == 0);
        CHECK(migrated.pattern.hits[1].startTick == 480);
        CHECK(migrated.pattern.hits[0].durationTicks == 240);
        CHECK(migrated.offsetTicks == 240);
        CHECK(migrated.playbackStartTick == 0);
        CHECK(migrated.playbackEndTick == 960);
    }
    CHECK(directory.deleteRecursively());
}

void testLegacyPatternCatalogIsLeftUntouchedAndRejected()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-old-catalog-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    const juce::String legacy =
        "{\"format\":\"live-pattern-sequencer-pattern-library\","
        "\"schemaVersion\":1,\"patterns\":[{\"id\":1,\"steps\":\"x---\"}]}";
    CHECK(catalog.replaceWithText(legacy));
    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.patternCountForUi() == lps::PatternLibrary::builtInCount);
        CHECK(processor.patternCatalogErrorForUi().isNotEmpty());
        CHECK(catalog.loadFileAsString() == legacy);
    }
    CHECK(directory.deleteRecursively());
}

void testNamedLegacyDefaultsMigrateToNoneAndKeepSavedPatterns()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-default-migration-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    const juce::String oldCatalog = R"json({
        "format": "live-pattern-sequencer-pattern-library",
        "schemaVersion": 3,
        "patterns": [
            {"id": 1, "name": "Basic Kick", "cycleLengthTicks": 960,
             "hits": [{"startTick": 0, "durationTicks": 240}]},
            {"id": 11, "name": "Saved", "originVoiceName": "Snare",
             "cycleLengthTicks": 960,
             "hits": [{"startTick": 480, "durationTicks": 240}]}
        ]
    })json";
    CHECK(catalog.replaceWithText(oldCatalog));

    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.patternCatalogErrorForUi().isEmpty());
        CHECK(processor.patternCountForUi() == 2);
        CHECK(processor.patternNameForUi(0) == "None");
        CHECK(processor.patternAtForUi(0).hitCount == 0);
        CHECK(processor.patternAtForUi(0).cycleLengthTicks
            == 8 * lps::Pattern::ticksPerQuarterNote);
        CHECK(processor.patternNameForUi(1) == "Saved");
        CHECK(processor.patternOriginForUi(1) == "Snare");
    }

    const auto migrated = juce::JSON::parse(catalog.loadFileAsString());
    const auto* root = migrated.getDynamicObject();
    CHECK(root != nullptr);
    const auto* patterns = root->getProperty("patterns").getArray();
    CHECK(patterns != nullptr);
    CHECK(patterns->size() == 2);
    CHECK(patterns->getReference(0).getDynamicObject()->getProperty("name")
        == "None");
    CHECK(directory.deleteRecursively());
}

void testEventPatternCatalogRoundTripsExactTicks()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-event-catalog-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    std::size_t savedIndex = 0;
    lps::Pattern expected;
    expected.cycleLengthTicks = 1'337;
    expected.hitCount = 3;
    expected.hits[0] = {1, 17};
    expected.hits[1] = {31, 2'880};
    expected.hits[2] = {1'336, 1};
    {
        LivePatternSequencerProcessor processor(catalog);
        const auto saved = processor.savePlayerPattern(0, expected);
        CHECK(saved.status
            == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
        savedIndex = saved.patternIndex;
        CHECK(lps::patternsEqual(processor.patternAtForUi(savedIndex), expected));
    }
    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.patternCatalogErrorForUi().isEmpty());
        CHECK(savedIndex < processor.patternCountForUi());
        CHECK(lps::patternsEqual(processor.patternAtForUi(savedIndex), expected));
        const auto parsed = juce::JSON::parse(catalog.loadFileAsString());
        CHECK(parsed.getDynamicObject() != nullptr);
        CHECK(static_cast<int>(parsed.getDynamicObject()->getProperty(
            "schemaVersion")) == 3);
    }
    CHECK(directory.deleteRecursively());
}

void testSavedPatternsCanBeDeletedAndFallBackToNone()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-delete-pattern-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");

    lps::Pattern deletedPattern;
    deletedPattern.cycleLengthTicks = 1'920;
    deletedPattern.hitCount = 1;
    deletedPattern.hits[0] = {480, 240};

    lps::Pattern retainedPattern;
    retainedPattern.cycleLengthTicks = 1'920;
    retainedPattern.hitCount = 2;
    retainedPattern.hits[0] = {0, 480};
    retainedPattern.hits[1] = {960, 480};

    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(!processor.deletePatternForUi(0));

        const auto saved = processor.savePlayerPattern(0, deletedPattern);
        CHECK(saved.status
            == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
        CHECK(processor.patternCountForUi() == 2);
        CHECK(processor.selectedPatternForPlayer(0) == saved.patternIndex);
        CHECK(processor.deletePatternForUi(saved.patternIndex));
        CHECK(processor.patternCountForUi() == 1);
        CHECK(processor.selectedPatternForPlayer(0) == 0);

        const auto restored = processor.savePlayerPattern(0, deletedPattern);
        CHECK(restored.status
            == LivePatternSequencerProcessor::SavePatternStatus::selectedExisting);
        CHECK(processor.patternCountForUi() == 2);
        CHECK(lps::patternsEqual(
            processor.patternAtForUi(restored.patternIndex), deletedPattern));
        CHECK(processor.deletePatternForUi(restored.patternIndex));

        const auto retained = processor.savePlayerPattern(0, retainedPattern);
        CHECK(retained.status
            == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
        CHECK(processor.patternCountForUi() == 2);
    }

    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.patternCatalogErrorForUi().isEmpty());
        CHECK(processor.patternCountForUi() == 2);
        CHECK(processor.patternNameForUi(0) == "None");
        CHECK(lps::patternsEqual(processor.patternAtForUi(1), retainedPattern));
    }
    CHECK(directory.deleteRecursively());
}

void testPatternOriginMetadataAndSchemaTwoCompatibility()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-origin-catalog-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    std::size_t savedIndex = 0;
    {
        LivePatternSequencerProcessor processor(catalog);
        lps::Pattern pattern;
        pattern.cycleLengthTicks = 1'337;
        pattern.hitCount = 1;
        pattern.hits[0] = {17, 31};
        const auto saved = processor.savePlayerPattern(0, pattern);
        CHECK(saved.status
            == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
        savedIndex = saved.patternIndex;
        CHECK(processor.patternOriginForUi(savedIndex) == "BD1");

        const auto duplicate = processor.savePlayerPattern(11, pattern);
        CHECK(duplicate.status
            == LivePatternSequencerProcessor::SavePatternStatus::selectedExisting);
        CHECK(duplicate.patternIndex == savedIndex);
        CHECK(processor.patternOriginForUi(savedIndex) == "BD1");
    }

    auto parsed = juce::JSON::parse(catalog.loadFileAsString());
    auto* root = parsed.getDynamicObject();
    CHECK(root != nullptr);
    root->setProperty("schemaVersion", 2);
    auto* patterns = root->getProperty("patterns").getArray();
    CHECK(patterns != nullptr);
    for (auto& value : *patterns)
        if (auto* entry = value.getDynamicObject())
            entry->removeProperty("originVoiceName");
    CHECK(catalog.replaceWithText(juce::JSON::toString(parsed)));

    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.patternCatalogErrorForUi().isEmpty());
        CHECK(savedIndex < processor.patternCountForUi());
        CHECK(processor.patternOriginForUi(savedIndex).isEmpty());
    }
    CHECK(directory.deleteRecursively());
}

void testCreateNewPatternDraftDoesNotPublishOrRetargetModulation()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-new-draft-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        const auto patternCount = processor.patternCountForUi();
        const auto selectedPattern = processor.selectedPatternForPlayer(0);
        const auto pitch = processor.selectedModulationForPlayer(
            0, LivePatternSequencerProcessor::ModulationLane::pitch);
        const auto velocity = processor.selectedModulationForPlayer(
            0, LivePatternSequencerProcessor::ModulationLane::velocity);
        const auto gate = processor.selectedModulationForPlayer(
            0, LivePatternSequencerProcessor::ModulationLane::gate);

        CHECK(processor.createNewPlayerPatternDraft(0));
        const auto draft = processor.patternForUi(0);
        CHECK(draft.pattern.cycleLengthTicks
            == 4 * lps::Pattern::ticksPerQuarterNote);
        CHECK(draft.pattern.hitCount == 0);
        CHECK(draft.offsetTicks == 0);
        CHECK(draft.playbackStartTick == 0);
        CHECK(draft.playbackEndTick
            == 4 * lps::Pattern::ticksPerQuarterNote);
        CHECK(processor.playerPlaybackSpeed(0) == 1);
        CHECK(processor.playerPatternModifiedForUi(0));
        CHECK(processor.patternCountForUi() == patternCount);
        CHECK(processor.selectedPatternForPlayer(0) == selectedPattern);
        CHECK(processor.selectedModulationForPlayer(
            0, LivePatternSequencerProcessor::ModulationLane::pitch) == pitch);
        CHECK(processor.selectedModulationForPlayer(
            0, LivePatternSequencerProcessor::ModulationLane::velocity) == velocity);
        CHECK(processor.selectedModulationForPlayer(
            0, LivePatternSequencerProcessor::ModulationLane::gate) == gate);

        CHECK(processor.addPlayerHit(0, 480, 240));
        CHECK(processor.addPlayerHit(0, 3'360, 240));
        processor.offsetPlayerPatternRight(0, 240);
        processor.setPlayerPlaybackWindow(0, 120, 3'000);
        processor.setPlayerPlaybackSpeed(0, 2);
        const auto beforeSave = processor.patternForUi(0);
        const auto saved = processor.savePlayerPattern(0);
        CHECK(saved.status
            == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
        CHECK(!processor.playerPatternChangePendingForUi(0));
        CHECK(!processor.playerPatternModifiedForUi(0));
        CHECK(processor.selectedPatternForPlayer(0) == saved.patternIndex);
        const auto afterSave = processor.patternForUi(0);
        CHECK(afterSave.pattern.cycleLengthTicks == 3'000);
        CHECK(afterSave.pattern.hitCount + 1 == beforeSave.pattern.hitCount);
        CHECK(afterSave.pattern.hits[0].startTick == 720);
        CHECK(lps::patternsEqual(
            processor.patternAtForUi(saved.patternIndex), afterSave.pattern));
        CHECK(beforeSave.offsetTicks == 240);
        CHECK(afterSave.offsetTicks == 0);
        CHECK(afterSave.playbackStartTick == beforeSave.playbackStartTick);
        CHECK(afterSave.playbackEndTick == beforeSave.playbackEndTick);
        CHECK(processor.playerPlaybackSpeed(0) == 2);
    }
    CHECK(directory.deleteRecursively());
}

void testEventPatternProcessorStateRoundTripsExactTicks()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-event-state-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.addPlayerHit(0, 0, 240));
        CHECK(processor.addPlayerHit(0, 31, 17));
        CHECK(processor.resizePlayerHit(0, 0, 2'880));
        processor.offsetPlayerPatternRight(0, 1);
        processor.setPlayerPlaybackWindow(0, 30, 900);

        juce::MemoryBlock state;
        processor.getStateInformation(state);
        CHECK(processor.removePlayerHit(0, 31));
        processor.offsetPlayerPatternRight(0, 100);
        processor.setPlayerPlaybackWindow(0, 0, 240);
        processor.setStateInformation(
            state.getData(), static_cast<int>(state.getSize()));

        const auto restored = processor.patternForUi(0);
        CHECK(restored.pattern.hitCount == 2);
        CHECK((restored.pattern.hits[0] == lps::PatternHit {0, 2'880}));
        CHECK((restored.pattern.hits[1] == lps::PatternHit {31, 17}));
        CHECK(restored.offsetTicks == 1);
        CHECK(restored.playbackStartTick == 30);
        CHECK(restored.playbackEndTick == 900);
    }
    CHECK(directory.deleteRecursively());
}
} // namespace

int main()
{
    testSynthTwoPatternPlayersProduceIndependentNotes();
    testPitchEditingUsesADraftAndPreservesTheLibraryRecord();
    testVolcaSamplePartsUseMidiChannelsOneThroughTen();
    testLegacyProcessorPatternStateMigratesToTicks();
    testLegacyPatternCatalogIsLeftUntouchedAndRejected();
    testNamedLegacyDefaultsMigrateToNoneAndKeepSavedPatterns();
    testEventPatternCatalogRoundTripsExactTicks();
    testSavedPatternsCanBeDeletedAndFallBackToNone();
    testPatternOriginMetadataAndSchemaTwoCompatibility();
    testCreateNewPatternDraftDoesNotPublishOrRetargetModulation();
    testEventPatternProcessorStateRoundTripsExactTicks();
    std::cout << "PluginProcessor tests passed\n";
    return EXIT_SUCCESS;
}
