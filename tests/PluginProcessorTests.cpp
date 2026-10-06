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
            parsed.getDynamicObject()->getProperty("schemaVersion")) == 6);
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

void testDefaultMidiMappingsEditOnlyTheirRegisteredTargets()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-midi-mapping-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(processor.acceptsMidi());
        CHECK(processor.activeMappingCount() == 2);
        processor.prepareToPlay(48'000.0, 512);

        const auto targetPlayer = processor.synthTwoPlayerIndexForUi(0);
        const auto otherPlayer = processor.synthTwoPlayerIndexForUi(1);
        processor.setPlayerModulationValue(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            0,
            120);
        processor.setPlayerModulationValue(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity,
            0,
            100);
        const auto before = processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        const auto otherBefore = processor.modulationForUi(
            otherPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        const auto velocityBefore = processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity);

        juce::AudioBuffer<float> audio(
            processor.getTotalNumOutputChannels(), 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 23);
        processor.processBlock(audio, midi);
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == before.values[0]);
        for (const auto metadata : midi)
        {
            const auto& message = metadata.getMessage();
            CHECK(!(message.isController()
                && message.getChannel() == 15
                && message.getControllerNumber() == 20));
        }

        // The processor-owned non-audio dispatcher performs the target edit.
        processor.dispatchPendingMidiMappingEdits();
        auto after = processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(after.values[0] == lps::NormalizedValue::fromUnipolar8(122));
        CHECK(processor.modulationForUi(
            otherPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values == otherBefore.values);
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity)
                .values[0] == velocityBefore.values[0]);

        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 127), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        after = processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch);
        CHECK(after.values[0] == before.values[0]);

        midi.addEvent(juce::MidiMessage::controllerEvent(15, 21, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        auto velocityAfter = processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity);
        CHECK(velocityAfter.values[0]
            == lps::NormalizedValue::fromUnipolar8(101));
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == before.values[0]);

        midi.addEvent(juce::MidiMessage::controllerEvent(15, 21, 127), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        velocityAfter = processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity);
        CHECK(velocityAfter.values[0] == velocityBefore.values[0]);

        processor.setPlayerModulationLocked(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity,
            true);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 21, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity)
                .values[0] == velocityBefore.values[0]);
        processor.setPlayerModulationLocked(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::velocity,
            false);

        // Matching but undecoded values are consumed and do not edit.
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 2), 0);
        processor.processBlock(audio, midi);
        CHECK(midi.isEmpty());
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == before.values[0]);

        // Other sources follow the no-pass-through input policy.
        midi.addEvent(juce::MidiMessage::controllerEvent(14, 20, 1), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 22, 1), 1);
        midi.addEvent(juce::MidiMessage::noteOn(15, 20, (juce::uint8) 100), 2);
        processor.processBlock(audio, midi);
        CHECK(midi.isEmpty());
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == before.values[0]);

        processor.setPlayerModulationLocked(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            true);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == before.values[0]);

        // Input remains active during playback and without an editor instance.
        processor.setPlayerModulationLocked(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            false);
        processor.setInternalTransportPlayingForUi(true);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(122));
    }
    CHECK(directory.deleteRecursively());
}

void testMidiMappingReplacementValidationAndAtomicFailure()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-midi-config-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        const juce::String custom = R"json({
  "version": 1,
  "futureDocumentField": "preserved",
  "mappings": [{
    "id": "custom",
    "enabled": true,
    "source": {"type":"cc","channel":16,"control":21,"valueMode":"inc-dec"},
    "when": {},
    "routes": [{
      "target":"synth.2.pattern.1.pitch.step.1",
      "operation":"increment",
      "scale":-2,
      "futureRouteField": true
    }]
  }]
})json";
        CHECK(processor.replaceMappingConfiguration(custom).wasOk());
        CHECK(processor.mappingConfiguration() == custom);
        CHECK(processor.activeMappingCount() == 1);

        processor.prepareToPlay(48'000.0, 64);
        const auto targetPlayer = processor.synthTwoPlayerIndexForUi(0);
        processor.setPlayerModulationValue(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch,
            0,
            120);
        juce::AudioBuffer<float> audio(
            processor.getTotalNumOutputChannels(), 64);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(16, 21, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            targetPlayer,
            LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(116));

        const auto previous = processor.mappingConfiguration();
        CHECK(processor.replaceMappingConfiguration("{").failed());
        CHECK(processor.mappingConfiguration() == previous);
        CHECK(processor.activeMappingCount() == 1);
        CHECK(!processor.mappingDiagnostics().empty());
        CHECK(processor.mappingDiagnostics()[0].code == "malformed-document");
        CHECK(processor.replaceMappingConfiguration(
            R"json({"version":3,"mappings":[]})json").failed());
        CHECK(processor.mappingConfiguration() == previous);
        CHECK(processor.mappingDiagnostics()[0].code == "unsupported-version");

        CHECK(processor.replaceMappingConfiguration(R"json({
          "version":1,
          "mappings":[
            {"id":"duplicate","enabled":true,"source":{"type":"cc","channel":1,"control":1,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]},
            {"id":"duplicate","enabled":true,"source":{"type":"cc","channel":2,"control":2,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]}
          ]
        })json").wasOk());
        CHECK(processor.activeMappingCount() == 0);

        CHECK(processor.replaceMappingConfiguration(R"json({
          "version":1,
          "mappings":[
            {"id":"valid","enabled":true,"source":{"type":"cc","channel":15,"control":20,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]},
            {"id":"bad-range","enabled":true,"source":{"type":"cc","channel":17,"control":128,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":0}]}
          ]
        })json").wasOk());
        CHECK(processor.activeMappingCount() == 1);

        CHECK(processor.replaceMappingConfiguration(R"json({
          "version":1,
          "mappings":[
            {"id":"conflict-a","enabled":true,"source":{"type":"cc","channel":1,"control":1,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]},
            {"id":"conflict-b","enabled":true,"source":{"type":"cc","channel":1,"control":1,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]}
          ]
        })json").wasOk());
        CHECK(processor.activeMappingCount() == 0);

        CHECK(processor.replaceMappingConfiguration(R"json({
          "version":1,
          "mappings":[
            {"id":"condition","enabled":true,"source":{"type":"cc","channel":1,"control":1,"valueMode":"inc-dec"},"when":{"shift":true},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]},
            {"id":"fanout","enabled":true,"source":{"type":"cc","channel":2,"control":2,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1},{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]},
            {"id":"unknown-target","enabled":true,"source":{"type":"cc","channel":3,"control":3,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"not.registered","operation":"increment","scale":1}]}
          ]
        })json").wasOk());
        CHECK(processor.activeMappingCount() == 0);
    }
    CHECK(directory.deleteRecursively());
}

void testMidiMappingStateRestorationAndLegacyStateBehavior()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-midi-state-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    const juce::String custom = R"json({"version":1,"mappings":[{"id":"restored","enabled":true,"source":{"type":"cc","channel":16,"control":21,"valueMode":"inc-dec"},"when":{},"routes":[{"target":"synth.2.pattern.1.pitch.step.1","operation":"increment","scale":1}]}]})json";
    juce::MemoryBlock savedState;
    {
        LivePatternSequencerProcessor source(catalog);
        CHECK(source.replaceMappingConfiguration(custom).wasOk());
        source.getStateInformation(savedState);
    }
    {
        LivePatternSequencerProcessor restored(catalog);
        restored.setStateInformation(
            savedState.getData(), static_cast<int>(savedState.getSize()));
        CHECK(restored.activeMappingCount() == 1);
        CHECK(restored.mappingConfiguration() == custom);
    }

    auto state = juce::JSON::parse(juce::String::fromUTF8(
        static_cast<const char*>(savedState.getData()),
        static_cast<int>(savedState.getSize())));
    auto* root = state.getDynamicObject();
    CHECK(root != nullptr);
    root->removeProperty("midiMappingConfiguration");
    root->setProperty("schemaVersion", 4);
    auto text = juce::JSON::toString(state, true);
    {
        LivePatternSequencerProcessor restored(catalog);
        CHECK(restored.activeMappingCount() == 2);
        restored.setStateInformation(
            text.toRawUTF8(), static_cast<int>(text.getNumBytesAsUTF8()));
        CHECK(restored.activeMappingCount() == 0);
    }

    root->setProperty("schemaVersion", 5);
    root->setProperty("midiMappingConfiguration", "{");
    auto* players = root->getProperty("players").getArray();
    CHECK(players != nullptr);
    auto* firstPlayer = players->getReference(0).getDynamicObject();
    CHECK(firstPlayer != nullptr);
    firstPlayer->setProperty("muted", true);
    text = juce::JSON::toString(state, true);
    {
        LivePatternSequencerProcessor restored(catalog);
        restored.setStateInformation(
            text.toRawUTF8(), static_cast<int>(text.getNumBytesAsUTF8()));
        CHECK(restored.activeMappingCount() == 0);
        CHECK(restored.playerMutedForUi(0));
    }
    CHECK(directory.deleteRecursively());
}

void testPageRelativeMidiContextAndEventTimeBinding()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-page-midi-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        CHECK(static_cast<int>(juce::JSON::parse(
            processor.mappingConfiguration()).getProperty("version", 0)) == 2);
        CHECK(processor.midiTargetCountForTests() >= 6656);
        CHECK(processor.midiPageBindingSnapshotForTests().valid);
        processor.prepareToPlay(48'000.0, 64);

        const auto first = processor.synthTwoPlayerIndexForUi(0);
        const std::size_t second = 0; // BD1
        processor.setPlayerModulationValue(
            first, LivePatternSequencerProcessor::ModulationLane::pitch,
            0, 120);
        processor.setPlayerModulationValue(
            second, LivePatternSequencerProcessor::ModulationLane::pitch,
            0, 100);
        juce::AudioBuffer<float> audio(
            processor.getTotalNumOutputChannels(), 64);
        juce::MidiBuffer midi;

        // A Synth 2 Pattern 1 event remains attached to that subject even if
        // the UI changes to the BD1 pattern before the dispatcher drains it.
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        CHECK(processor.setMidiPatternOverviewContext(second));
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            first, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(122));
        CHECK(processor.modulationForUi(
            second, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(100));

        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            second, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(102));

        // A step page uses the selected parameter and its ten-step bank.
        processor.setPlayerModulationLength(
            second, LivePatternSequencerProcessor::ModulationLane::pitch, 12);
        processor.setPlayerModulationValue(
            second, LivePatternSequencerProcessor::ModulationLane::pitch,
            10, 110);
        CHECK(processor.setMidiParameterStepsContext(
            LivePatternSequencerProcessor::patternMidiPageProfile,
            LivePatternSequencerProcessor::patternMidiSubject(second),
            1, 1));
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            second, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[10] == lps::NormalizedValue::fromUnipolar8(112));

        CHECK(!processor.setMidiParameterStepsContext(
            LivePatternSequencerProcessor::patternMidiPageProfile,
            LivePatternSequencerProcessor::patternMidiSubject(second),
            1, 4));
        CHECK(!processor.midiPageBindingSnapshotForTests().valid);
        CHECK(!processor.midiPageBindingDiagnostics().empty());
        CHECK(processor.midiPageBindingDiagnostics()[0].code
            == "invalid-step-bank");
    }
    CHECK(directory.deleteRecursively());
}

void testVersionTwoFanOutPredicatesAndGenericParameters()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-v2-midi-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    {
        LivePatternSequencerProcessor processor(catalog);
        processor.prepareToPlay(48'000.0, 64);
        const auto player = processor.synthTwoPlayerIndexForUi(0);
        processor.setPlayerModulationValue(
            player, LivePatternSequencerProcessor::ModulationLane::pitch,
            0, 100);
        processor.setPlayerModulationValue(
            player, LivePatternSequencerProcessor::ModulationLane::velocity,
            0, 100);
        CHECK(processor.setMidiPatternOverviewContext(player));
        const juce::String fanout = R"json({"version":2,"mappings":[
          {"id":"fanout","enabled":true,"source":{"type":"cc","channel":15,"control":30,"valueMode":"inc-dec"},"when":{},"routes":[
            {"controlSlot":1,"operation":"increment","scale":1},
            {"controlSlot":2,"operation":"increment","scale":1}]}
        ]})json";
        CHECK(processor.replaceMappingConfiguration(fanout).wasOk());
        CHECK(processor.mappingConfiguration() == fanout);
        CHECK(processor.activeMappingCount() == 1);
        juce::AudioBuffer<float> audio(
            processor.getTotalNumOutputChannels(), 64);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 30, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            player, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(102));
        CHECK(processor.modulationForUi(
            player, LivePatternSequencerProcessor::ModulationLane::velocity)
                .values[0] == lps::NormalizedValue::fromUnipolar8(101));

        const juce::String predicates = R"json({"version":2,"mappings":[
          {"id":"plain","enabled":true,"source":{"type":"cc","channel":15,"control":31,"valueMode":"inc-dec"},"when":{"modifiers":{"shift":false}},"routes":[{"controlSlot":2,"operation":"increment","scale":1}]},
          {"id":"shift","enabled":true,"source":{"type":"cc","channel":15,"control":31,"valueMode":"inc-dec"},"when":{"modifiers":{"shift":true}},"routes":[{"controlSlot":1,"operation":"increment","scale":1}]},
          {"id":"released","enabled":true,"source":{"type":"cc","channel":15,"control":32,"valueMode":"inc-dec"},"when":{"notes":[{"note":60,"held":false}]},"routes":[{"controlSlot":2,"operation":"increment","scale":1}]},
          {"id":"held","enabled":true,"source":{"type":"cc","channel":15,"control":32,"valueMode":"inc-dec"},"when":{"notes":[{"note":60,"held":true}]},"routes":[{"controlSlot":1,"operation":"increment","scale":1}]},
          {"id":"bank1","enabled":true,"source":{"type":"cc","channel":15,"control":33,"valueMode":"inc-dec"},"when":{"bank":1},"routes":[{"controlSlot":1,"operation":"increment","scale":1}]},
          {"id":"bank2","enabled":true,"source":{"type":"cc","channel":15,"control":33,"valueMode":"inc-dec"},"when":{"bank":2},"routes":[{"controlSlot":2,"operation":"increment","scale":1}]}
        ]})json";
        CHECK(processor.replaceMappingConfiguration(predicates).wasOk());
        CHECK(processor.activeMappingCount() == 6);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 31, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        auto velocity = processor.modulationForUi(
            player, LivePatternSequencerProcessor::ModulationLane::velocity);
        CHECK(velocity.values[0]
            == lps::NormalizedValue::fromUnipolar8(102));
        CHECK(processor.setMidiControllerModifier("shift", true));
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 31, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            player, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(104));

        midi.addEvent(juce::MidiMessage::noteOn(
            15, 60, static_cast<juce::uint8>(100)), 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 32, 1), 1);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            player, LivePatternSequencerProcessor::ModulationLane::pitch)
                .values[0] == lps::NormalizedValue::fromUnipolar8(106));
        CHECK(processor.setMidiControllerBank(2));
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 33, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        CHECK(processor.modulationForUi(
            player, LivePatternSequencerProcessor::ModulationLane::velocity)
                .values[0] == lps::NormalizedValue::fromUnipolar8(103));

        // Generic parameter adapters preserve each definition's range.
        CHECK(processor.replaceMappingConfiguration(
            lps::bundledMidiMappingConfiguration()).wasOk());
        CHECK(processor.setMidiSynthOverviewContext());
        processor.setSynthLaneValue(
            LivePatternSequencerProcessor::synthTwoPatternCount, 0, 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        const auto synthValue = processor.synthLaneModulationForUi(
            LivePatternSequencerProcessor::synthTwoPatternCount);
        CHECK(juce::roundToInt(synthValue.values[0].toFloat() * 127.0f) == 1);

        CHECK(processor.setMidiSampleOverviewContext(1));
        const auto sampleLane = LivePatternSequencerProcessor::samplePartCount
            + 12;
        processor.setSampleLaneValue(sampleLane, 0, 0);
        midi.addEvent(juce::MidiMessage::controllerEvent(15, 20, 1), 0);
        processor.processBlock(audio, midi);
        processor.dispatchPendingMidiMappingEdits();
        const auto sampleValue = processor.sampleLaneModulationForUi(sampleLane);
        CHECK(juce::roundToInt(sampleValue.values[0].toFloat() * 199.0f) == 1);
    }
    CHECK(directory.deleteRecursively());
}

void testMidiEditingContextPersistence()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-midi-context-state-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    juce::MemoryBlock state;
    lps::MidiEditingContext expected;
    {
        LivePatternSequencerProcessor source(catalog);
        CHECK(source.setMidiParameterStepsContext(
            LivePatternSequencerProcessor::sampleMidiPageProfile,
            LivePatternSequencerProcessor::sampleMidiSubject(3), 2, 0));
        expected = source.midiEditingContext();
        source.getStateInformation(state);
    }
    {
        LivePatternSequencerProcessor restored(catalog);
        restored.setStateInformation(
            state.getData(), static_cast<int>(state.getSize()));
        const auto actual = restored.midiEditingContext();
        CHECK(actual.valid == expected.valid);
        CHECK(actual.profile == expected.profile);
        CHECK(actual.subject == expected.subject);
        CHECK(actual.pageKind == expected.pageKind);
        CHECK(actual.selectedParameterSlot == expected.selectedParameterSlot);
        CHECK(actual.stepBank == expected.stepBank);
        CHECK(restored.midiPageBindingSnapshotForTests().valid);
    }
    CHECK(directory.deleteRecursively());
}

void testMixerTrackParameterMatrixPersists()
{
    auto directory = juce::File::getSpecialLocation(
        juce::File::tempDirectory).getNonexistentChildFile(
            "live-pattern-sequencer-mixer-state-test", {}, false);
    CHECK(directory.createDirectory());
    const auto catalog = directory.getChildFile("patterns.json");
    juce::MemoryBlock state;
    {
        LivePatternSequencerProcessor source(catalog);
        CHECK(LivePatternSequencerProcessor::mixerTrackCount == 12);
        CHECK(LivePatternSequencerProcessor::mixerTrackParameterCount == 7);
        CHECK(source.setMixerTrackParameterValue(0, 0, 96));
        CHECK(source.setMixerTrackParameterValue(11, 6, 200));
        CHECK(source.mixerTrackParameterValueForUi(0, 0) == 96);
        CHECK(source.mixerTrackParameterValueForUi(11, 6) == 127);
        CHECK(!source.setMixerTrackParameterValue(12, 0, 64));
        CHECK(!source.setMixerTrackParameterValue(0, 7, 64));
        source.getStateInformation(state);

        const auto parsed = juce::JSON::parse(juce::String::fromUTF8(
            static_cast<const char*>(state.getData()),
            static_cast<int>(state.getSize())));
        const auto* root = parsed.getDynamicObject();
        CHECK(root != nullptr);
        const auto* tracks = root->getProperty(
            "mixerTrackParameterValues").getArray();
        CHECK(tracks != nullptr);
        CHECK(tracks->size() == 12);
        CHECK(tracks->getReference(0).getArray()->size() == 7);
    }
    {
        LivePatternSequencerProcessor restored(catalog);
        restored.setStateInformation(
            state.getData(), static_cast<int>(state.getSize()));
        CHECK(restored.mixerTrackParameterValueForUi(0, 0) == 96);
        CHECK(restored.mixerTrackParameterValueForUi(11, 6) == 127);
        CHECK(restored.mixerTrackParameterValueForUi(6, 3) == 0);
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
    testDefaultMidiMappingsEditOnlyTheirRegisteredTargets();
    testMidiMappingReplacementValidationAndAtomicFailure();
    testMidiMappingStateRestorationAndLegacyStateBehavior();
    testPageRelativeMidiContextAndEventTimeBinding();
    testVersionTwoFanOutPredicatesAndGenericParameters();
    testMidiEditingContextPersistence();
    testMixerTrackParameterMatrixPersists();
    std::cout << "PluginProcessor tests passed\n";
    return EXIT_SUCCESS;
}
