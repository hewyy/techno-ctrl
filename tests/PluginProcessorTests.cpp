#include "plugin/PluginProcessor.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

class TemporaryPatternCatalog
{
public:
    TemporaryPatternCatalog()
        : directory_(juce::File::getSpecialLocation(juce::File::tempDirectory)
              .getNonexistentChildFile(
                  "live-pattern-sequencer-tests", juce::String {}, false)),
          catalogFile_(directory_.getChildFile("patterns.json"))
    {
        CHECK(directory_.createDirectory().wasOk());
    }

    ~TemporaryPatternCatalog()
    {
        (void) directory_.deleteRecursively();
    }

    TemporaryPatternCatalog(const TemporaryPatternCatalog&) = delete;
    TemporaryPatternCatalog& operator=(const TemporaryPatternCatalog&) = delete;

    [[nodiscard]] const juce::File& file() const noexcept { return catalogFile_; }

private:
    juce::File directory_;
    juce::File catalogFile_;
};

lps::Pattern makePattern(std::string_view steps)
{
    lps::Pattern pattern;
    pattern.length = steps.size();
    for (std::size_t step = 0;
         step < steps.size() && step < lps::Pattern::maxLength;
         ++step)
    {
        pattern.hits[step] = steps[step] == 'x';
    }
    return pattern;
}

std::optional<std::size_t> patternIndexNamed(
    const LivePatternSequencerProcessor& processor,
    const juce::String& name)
{
    for (std::size_t index = 0; index < processor.patternCountForUi(); ++index)
    {
        if (processor.patternNameForUi(index) == name)
            return index;
    }

    return std::nullopt;
}

std::optional<std::size_t> velocityModulationIndexNamed(
    const LivePatternSequencerProcessor& processor,
    const juce::String& name)
{
    for (std::size_t index = 0;
         index < processor.velocityModulationCountForUi();
         ++index)
    {
        if (processor.velocityModulationNameForUi(index) == name)
            return index;
    }

    return std::nullopt;
}

void testStaticMasterConfigurationAndResetRequestForwarding()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());
    const auto masterIndex = processor.masterPlayerIndexForUi();

    CHECK(masterIndex.has_value());
    CHECK(*masterIndex == 0);
    CHECK(processor.playerIsMasterForUi(0));
    CHECK(!processor.playerIsMasterForUi(1));
    CHECK(!processor.resetPlayerToMaster(0));
    CHECK(!processor.resetPlayerToMaster(processor.playerCountForUi()));
    CHECK(!processor.playerResetToMasterPendingForUi(0));

    CHECK(processor.resetPlayerToMaster(1));
    CHECK(processor.playerResetToMasterPendingForUi(1));
}

void testPlayerMuteStateForwardingAndInvalidIndexSafety()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());
    const auto playerCount = processor.playerCountForUi();

    CHECK(playerCount >= 2);
    for (std::size_t playerIndex = 0; playerIndex < playerCount; ++playerIndex)
        CHECK(!processor.playerMutedForUi(playerIndex));

    processor.setPlayerMuted(1, true);
    CHECK(!processor.playerMutedForUi(0));
    CHECK(processor.playerMutedForUi(1));

    processor.setPlayerMuted(playerCount, true);
    CHECK(!processor.playerMutedForUi(playerCount));
    CHECK(processor.playerMutedForUi(1));

    processor.setPlayerMuted(1, false);
    CHECK(!processor.playerMutedForUi(1));
}

void testVelocityModulationBuiltInsAndSelectionsAreIndependent()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());

    CHECK(processor.velocityModulationCountForUi()
        == lps::VelocityModulationLibrary::builtInCount);
    CHECK(processor.velocityModulationNameForUi(0) == "Steady 100");
    CHECK(processor.velocityModulationNameForUi(1) == "Four-Step");
    CHECK(processor.velocityModulationCatalogErrorForUi().isEmpty());

    CHECK(processor.playerCountForUi() >= 2);
    CHECK(processor.selectedVelocityModulationForPlayer(0) == 0);
    CHECK(processor.selectedVelocityModulationForPlayer(1) == 0);

    const auto originalPattern = processor.selectedPatternForPlayer(0);
    processor.selectVelocityModulationForPlayer(0, 1);
    CHECK(processor.selectedVelocityModulationForPlayer(0) == 1);
    CHECK(processor.selectedVelocityModulationForPlayer(1) == 0);
    CHECK(processor.selectedPatternForPlayer(0) == originalPattern);

    processor.selectPatternForPlayer(0, 1);
    CHECK(processor.selectedPatternForPlayer(0) == 1);
    CHECK(processor.selectedVelocityModulationForPlayer(0) == 1);

    processor.prepareToPlay(48'000.0, 512);
    const auto first = processor.velocityModulationForUi(0);
    CHECK(first.length == 4);
    CHECK(first.values[0] == 255);
    CHECK(first.values[1] == 100);
    CHECK(first.values[2] == 225);
    CHECK(first.values[3] == 150);

    const auto second = processor.velocityModulationForUi(1);
    CHECK(second.length == 1);
    CHECK(second.values[0] == 201);
}

void testVelocityModulationDraftSaveWorkflow()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());
    processor.prepareToPlay(48'000.0, 512);

    using Status =
        LivePatternSequencerProcessor::SaveVelocityModulationStatus;
    const auto initialCount = processor.velocityModulationCountForUi();
    CHECK(!processor.playerVelocityModulationModifiedForUi(0));

    processor.setPlayerVelocityModulationLength(0, 3);
    auto draft = processor.velocityModulationForUi(0);
    CHECK(draft.length == 3);
    CHECK(draft.values[0] == 201);
    CHECK(draft.values[1] == 201);
    CHECK(draft.values[2] == 201);

    processor.setPlayerVelocityModulationValue(0, 0, 255);
    processor.setPlayerVelocityModulationValue(0, 1, 100);
    processor.setPlayerVelocityModulationValue(0, 2, 225);
    CHECK(processor.playerVelocityModulationModifiedForUi(0));

    const auto needsName = processor.savePlayerVelocityModulation(0);
    CHECK(needsName.status == Status::needsName);
    CHECK(needsName.candidateModulation.length == 3);
    CHECK(needsName.candidateModulation.values[0] == 255);
    CHECK(needsName.candidateModulation.values[1] == 100);
    CHECK(needsName.candidateModulation.values[2] == 225);
    CHECK(processor.velocityModulationCountForUi() == initialCount);

    // The candidate is a frozen copy, independent of subsequent knob edits.
    processor.setPlayerVelocityModulationValue(0, 0, 42);
    const auto saved = processor.savePlayerVelocityModulation(
        0, needsName.candidateModulation, "Three-Step Accent");
    CHECK(saved.status == Status::savedNew);
    CHECK(saved.modulationIndex == initialCount);
    CHECK(processor.velocityModulationCountForUi() == initialCount + 1);
    CHECK(processor.velocityModulationNameForUi(saved.modulationIndex)
        == "Three-Step Accent");
    CHECK(processor.selectedVelocityModulationForPlayer(0)
        == saved.modulationIndex);

    // Applying the saved selection reloads its canonical values and clears
    // the unsaved draft edit made after the naming request.
    processor.prepareToPlay(48'000.0, 512);
    draft = processor.velocityModulationForUi(0);
    CHECK(draft.length == 3);
    CHECK(draft.values[0] == 255);
    CHECK(draft.values[1] == 100);
    CHECK(draft.values[2] == 225);
    CHECK(!processor.playerVelocityModulationModifiedForUi(0));

    const auto duplicate = processor.savePlayerVelocityModulation(0);
    CHECK(duplicate.status == Status::selectedExisting);
    CHECK(duplicate.modulationIndex == saved.modulationIndex);
    CHECK(processor.velocityModulationCountForUi() == initialCount + 1);
}

void testSavedVelocityModulationIsRestoredFromSiblingCatalog()
{
    TemporaryPatternCatalog catalog;
    const auto velocityCatalog = catalog.file().getSiblingFile(
        "velocity-modulations.json");
    constexpr auto savedName = "Persistent Velocity Curve";
    std::size_t savedIndex = 0;

    {
        LivePatternSequencerProcessor writer(catalog.file());
        writer.setPlayerVelocityModulationLength(0, 2);
        writer.setPlayerVelocityModulationValue(0, 0, 9);
        writer.setPlayerVelocityModulationValue(0, 1, 240);

        const auto saved = writer.savePlayerVelocityModulation(0, savedName);
        CHECK(saved.status
            == LivePatternSequencerProcessor::SaveVelocityModulationStatus::savedNew);
        CHECK(saved.modulationIndex
            == lps::VelocityModulationLibrary::builtInCount);
        savedIndex = saved.modulationIndex;
    }

    CHECK(velocityCatalog.existsAsFile());
    CHECK(velocityCatalog.getSize() > 0);
    CHECK(!juce::JSON::parse(velocityCatalog).isVoid());

    LivePatternSequencerProcessor reader(catalog.file());
    CHECK(reader.patternCountForUi() == lps::PatternLibrary::builtInCount);
    CHECK(reader.velocityModulationCountForUi()
        == lps::VelocityModulationLibrary::builtInCount + 1);
    CHECK(reader.velocityModulationNameForUi(savedIndex) == savedName);
    CHECK(velocityModulationIndexNamed(reader, savedName) == savedIndex);

    reader.selectVelocityModulationForPlayer(0, savedIndex);
    reader.prepareToPlay(48'000.0, 512);
    const auto restored = reader.velocityModulationForUi(0);
    CHECK(restored.length == 2);
    CHECK(restored.values[0] == 9);
    CHECK(restored.values[1] == 240);
    CHECK(!reader.playerVelocityModulationModifiedForUi(0));
}

void testAuthoredVelocityModulationCatalogValidation()
{
    {
        TemporaryPatternCatalog catalog;
        const auto velocityCatalog = catalog.file().getSiblingFile(
            "velocity-modulations.json");
        const juce::String authoredJson = R"json({
  "format": "live-pattern-sequencer-velocity-modulation-library",
  "schemaVersion": 1,
  "modulations": [
    { "id": 7, "name": "Authored Curve", "values": [0, 64, 255] }
  ]
})json";
        CHECK(velocityCatalog.replaceWithText(authoredJson));

        LivePatternSequencerProcessor processor(catalog.file());
        CHECK(processor.velocityModulationCatalogErrorForUi().isEmpty());
        CHECK(processor.velocityModulationCountForUi() == 1);
        CHECK(processor.velocityModulationNameForUi(0) == "Authored Curve");
        const auto authored = processor.velocityModulationForUi(0);
        CHECK(authored.length == 3);
        CHECK(authored.values[0] == 0);
        CHECK(authored.values[1] == 64);
        CHECK(authored.values[2] == 255);
    }

    {
        TemporaryPatternCatalog catalog;
        const auto velocityCatalog = catalog.file().getSiblingFile(
            "velocity-modulations.json");
        const juce::String malformedJson = R"json({
  "format": "live-pattern-sequencer-velocity-modulation-library",
  "schemaVersion": 1,
  "modulations": [
    { "id": 1, "name": "Out Of Range", "values": [256] }
  ]
})json";
        CHECK(velocityCatalog.replaceWithText(malformedJson));
        const auto originalContents = velocityCatalog.loadFileAsString();

        LivePatternSequencerProcessor processor(catalog.file());
        CHECK(processor.velocityModulationCatalogErrorForUi().isNotEmpty());
        CHECK(processor.velocityModulationCountForUi()
            == lps::VelocityModulationLibrary::builtInCount);
        CHECK(processor.velocityModulationNameForUi(0) == "Steady 100");
        CHECK(velocityCatalog.loadFileAsString() == originalContents);
    }
}

void testVelocityModulationInvalidIndexSafety()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());
    const auto playerCount = processor.playerCountForUi();
    const auto modulationCount = processor.velocityModulationCountForUi();
    const auto original = processor.velocityModulationForUi(0);

    CHECK(processor.velocityModulationNameForUi(modulationCount).isEmpty());
    CHECK(processor.velocityModulationForUi(playerCount).length == 0);
    CHECK(!processor.playerVelocityModulationModifiedForUi(playerCount));
    CHECK(processor.selectedVelocityModulationForPlayer(playerCount) == 0);
    CHECK(processor.currentVelocityModulationStepForUi(playerCount) == -1);

    processor.selectVelocityModulationForPlayer(0, modulationCount);
    processor.selectVelocityModulationForPlayer(playerCount, 1);
    processor.setPlayerVelocityModulationValue(playerCount, 0, 1);
    processor.setPlayerVelocityModulationLength(playerCount, 2);
    processor.setPlayerVelocityModulationValue(
        0, lps::VelocityModulation::maxLength, 1);
    processor.setPlayerVelocityModulationLength(0, 0);
    processor.setPlayerVelocityModulationLength(
        0, lps::VelocityModulation::maxLength + 1);

    CHECK(processor.selectedVelocityModulationForPlayer(0) == 0);
    CHECK(lps::velocityModulationsEqual(
        processor.velocityModulationForUi(0), original));
    CHECK(!processor.playerVelocityModulationModifiedForUi(0));

    const auto invalidPlayerSave =
        processor.savePlayerVelocityModulation(playerCount, "Invalid");
    CHECK(invalidPlayerSave.status
        == LivePatternSequencerProcessor::SaveVelocityModulationStatus::failed);

    lps::VelocityModulation tooLong;
    tooLong.length = lps::VelocityModulation::maxLength + 1;
    const auto invalidModulationSave = processor.savePlayerVelocityModulation(
        0, tooLong, "Too Long");
    CHECK(invalidModulationSave.status
        == LivePatternSequencerProcessor::SaveVelocityModulationStatus::failed);
}

void testSaveWorkflowDeduplicatesFreezesAndLoadsTheSavedLoop()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());
    processor.prepareToPlay(48'000.0, 512);

    const auto initialPatternCount = processor.patternCountForUi();
    CHECK(initialPatternCount == lps::PatternLibrary::builtInCount);
    CHECK(!processor.playerPatternModifiedForUi(0));

    // Exact content is selected immediately without requiring a new name.
    const auto existing = processor.savePlayerPattern(0);
    CHECK(existing.status
        == LivePatternSequencerProcessor::SavePatternStatus::selectedExisting);
    CHECK(existing.patternIndex == 0);
    CHECK(processor.patternCountForUi() == initialPatternCount);

    // Shorten Basic Kick from x--- to x-. The first request snapshots that
    // exact bracketed loop and asks for a name because it is new content.
    processor.setPlayerPlaybackWindow(0, 0, 1);
    CHECK(processor.playerPatternModifiedForUi(0));
    const auto needsName = processor.savePlayerPattern(0);
    CHECK(needsName.status
        == LivePatternSequencerProcessor::SavePatternStatus::needsName);
    CHECK(needsName.candidatePattern.length == 2);
    CHECK(needsName.candidatePattern.hits[0]);
    CHECK(!needsName.candidatePattern.hits[1]);
    CHECK(processor.patternCountForUi() == initialPatternCount);

    // Changes made after opening the naming dialog must not change the
    // candidate that receives the entered name.
    processor.setPlayerPlaybackWindow(0, 0, 3);
    processor.togglePlayerStep(0, 1);
    const auto saved = processor.savePlayerPattern(
        0, needsName.candidatePattern, "Short Kick");
    CHECK(saved.status
        == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
    CHECK(saved.patternIndex == initialPatternCount);
    CHECK(processor.patternCountForUi() == initialPatternCount + 1);
    CHECK(processor.patternNameForUi(saved.patternIndex) == "Short Kick");

    // Saved selections follow the normal boundary policy. prepareToPlay()
    // applies the pending selection here so its canonical draft can be read.
    processor.prepareToPlay(48'000.0, 512);
    const auto savedView = processor.patternForUi(0);
    CHECK(savedView.isHit(0));
    CHECK(!savedView.isHit(1));
    CHECK(savedView.playbackStart == 0);
    CHECK(savedView.playbackEnd == 1);
    CHECK(!processor.playerPatternModifiedForUi(0));

    // Saving identical content again selects the existing entry and does not
    // grow the library or ask for another name.
    const auto duplicate = processor.savePlayerPattern(0);
    CHECK(duplicate.status
        == LivePatternSequencerProcessor::SavePatternStatus::selectedExisting);
    CHECK(duplicate.patternIndex == saved.patternIndex);
    CHECK(processor.patternCountForUi() == initialPatternCount + 1);

    const auto invalid = processor.savePlayerPattern(processor.playerCountForUi());
    CHECK(invalid.status
        == LivePatternSequencerProcessor::SavePatternStatus::failed);
}

void testSavedPatternIsRestoredFromJsonCatalogOnNextStartup()
{
    TemporaryPatternCatalog catalog;
    constexpr auto persistedPatternName = "Persistent \"Thirty-Two\"";
    std::size_t persistedPatternIndex = 0;

    {
        LivePatternSequencerProcessor writer(catalog.file());
        writer.prepareToPlay(48'000.0, 512);

        writer.setPlayerPlaybackWindow(0, 0, lps::Pattern::maxLength - 1);
        writer.togglePlayerStep(0, 15);
        writer.togglePlayerStep(0, lps::Pattern::maxLength - 1);

        const auto saved = writer.savePlayerPattern(0, persistedPatternName);
        CHECK(saved.status
            == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
        CHECK(saved.patternIndex == lps::PatternLibrary::builtInCount);
        CHECK(saved.candidatePattern.length == lps::Pattern::maxLength);
        CHECK(saved.candidatePattern.hits[0]);
        CHECK(saved.candidatePattern.hits[15]);
        CHECK(saved.candidatePattern.hits[lps::Pattern::maxLength - 1]);
        CHECK(writer.patternNameForUi(saved.patternIndex) == persistedPatternName);
        persistedPatternIndex = saved.patternIndex;
    }

    CHECK(catalog.file().existsAsFile());
    CHECK(catalog.file().getSize() > 0);
    CHECK(!juce::JSON::parse(catalog.file()).isVoid());

    LivePatternSequencerProcessor reader(catalog.file());
    CHECK(reader.patternCountForUi() == lps::PatternLibrary::builtInCount + 1);
    CHECK(reader.patternNameForUi(persistedPatternIndex) == persistedPatternName);

    reader.selectPatternForPlayer(0, persistedPatternIndex);
    CHECK(reader.selectedPatternForPlayer(0) == persistedPatternIndex);
    reader.prepareToPlay(48'000.0, 512);

    const auto restored = reader.patternForUi(0);
    CHECK(restored.stepCount == lps::Pattern::maxLength);
    CHECK(restored.playbackStart == 0);
    CHECK(restored.playbackEnd == lps::Pattern::maxLength - 1);
    CHECK(restored.isHit(0));
    CHECK(restored.isHit(15));
    CHECK(restored.isHit(lps::Pattern::maxLength - 1));
    CHECK(!restored.isHit(1));
    CHECK(!reader.playerPatternModifiedForUi(0));
}

void testManuallyAuthoredVersionOneJsonCatalogLoadsAtConstruction()
{
    TemporaryPatternCatalog catalog;
    const juce::String authoredJson = R"json({
  "format": "live-pattern-sequencer-pattern-library",
  "schemaVersion": 1,
  "patterns": [
    { "id": 7, "name": "Authored First", "steps": "x--x-" },
    { "id": 2, "name": "Authored Second", "steps": "-xx" }
  ]
})json";
    CHECK(catalog.file().replaceWithText(authoredJson));

    LivePatternSequencerProcessor processor(catalog.file());
    CHECK(processor.patternCountForUi() == 2);
    CHECK(processor.patternCatalogErrorForUi().isEmpty());
    CHECK(processor.patternNameForUi(0) == "Authored First");
    CHECK(processor.patternNameForUi(1) == "Authored Second");

    processor.prepareToPlay(48'000.0, 512);
    const auto first = processor.patternForUi(0);
    CHECK(first.playbackStart == 0);
    CHECK(first.playbackEnd == 4);
    CHECK(first.isHit(0));
    CHECK(!first.isHit(1));
    CHECK(!first.isHit(2));
    CHECK(first.isHit(3));
    CHECK(!first.isHit(4));

    processor.selectPatternForPlayer(0, 1);
    processor.prepareToPlay(48'000.0, 512);
    const auto second = processor.patternForUi(0);
    CHECK(second.playbackStart == 0);
    CHECK(second.playbackEnd == 2);
    CHECK(!second.isHit(0));
    CHECK(second.isHit(1));
    CHECK(second.isHit(2));
}

void testMalformedExistingJsonIsPreservedAndBlocksNovelSave()
{
    TemporaryPatternCatalog catalog;
    CHECK(catalog.file().replaceWithText("{ this is not valid JSON\n"));
    const auto malformedContents = catalog.file().loadFileAsString();

    LivePatternSequencerProcessor processor(catalog.file());
    const auto initialCount = processor.patternCountForUi();
    CHECK(initialCount == lps::PatternLibrary::builtInCount);
    CHECK(processor.patternCatalogErrorForUi().isNotEmpty());

    const auto saveResult = processor.savePlayerPattern(
        0, makePattern("-x"), "Must Not Publish");
    CHECK(saveResult.status
        == LivePatternSequencerProcessor::SavePatternStatus::failed);
    CHECK(processor.patternCountForUi() == initialCount);
    CHECK(processor.patternNameForUi(initialCount).isEmpty());
    CHECK(processor.patternCatalogErrorForUi().isNotEmpty());
    CHECK(catalog.file().loadFileAsString() == malformedContents);
}

void testCatalogWriteFailureDoesNotPublishNewPattern()
{
    TemporaryPatternCatalog catalog;
    const auto blockingParent = catalog.file().getSiblingFile("not-a-directory");
    CHECK(blockingParent.replaceWithText("This regular file blocks a child catalog."));
    const auto blockingContents = blockingParent.loadFileAsString();
    const auto impossibleCatalog = blockingParent.getChildFile("patterns.json");

    LivePatternSequencerProcessor processor(impossibleCatalog);
    const auto initialCount = processor.patternCountForUi();
    CHECK(initialCount == lps::PatternLibrary::builtInCount);
    CHECK(processor.patternCatalogErrorForUi().isNotEmpty());

    const auto saveResult = processor.savePlayerPattern(
        0, makePattern("-x"), "Cannot Be Written");
    CHECK(saveResult.status
        == LivePatternSequencerProcessor::SavePatternStatus::failed);
    CHECK(processor.patternCountForUi() == initialCount);
    CHECK(!patternIndexNamed(processor, "Cannot Be Written").has_value());
    CHECK(!impossibleCatalog.exists());
    CHECK(blockingParent.loadFileAsString() == blockingContents);
}

void testConcurrentProcessorSnapshotsMergeDistinctSavesWithoutLoss()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor firstWriter(catalog.file());
    LivePatternSequencerProcessor secondWriter(catalog.file());

    const auto firstSave = firstWriter.savePlayerPattern(
        0, makePattern("x-"), "From First Processor");
    CHECK(firstSave.status
        == LivePatternSequencerProcessor::SavePatternStatus::savedNew);

    const auto secondSave = secondWriter.savePlayerPattern(
        0, makePattern("-x"), "From Second Processor");
    CHECK(secondSave.status
        == LivePatternSequencerProcessor::SavePatternStatus::savedNew);

    LivePatternSequencerProcessor reloaded(catalog.file());
    CHECK(reloaded.patternCountForUi() == lps::PatternLibrary::builtInCount + 2);
    CHECK(reloaded.patternNameForUi(lps::PatternLibrary::builtInCount)
        == "From First Processor");
    CHECK(reloaded.patternNameForUi(lps::PatternLibrary::builtInCount + 1)
        == "From Second Processor");

    reloaded.selectPatternForPlayer(0, lps::PatternLibrary::builtInCount);
    reloaded.prepareToPlay(48'000.0, 512);
    const auto firstRestored = reloaded.patternForUi(0);
    CHECK(firstRestored.playbackEnd == 1);
    CHECK(firstRestored.isHit(0));
    CHECK(!firstRestored.isHit(1));

    reloaded.selectPatternForPlayer(0, lps::PatternLibrary::builtInCount + 1);
    reloaded.prepareToPlay(48'000.0, 512);
    const auto secondRestored = reloaded.patternForUi(0);
    CHECK(secondRestored.playbackEnd == 1);
    CHECK(!secondRestored.isHit(0));
    CHECK(secondRestored.isHit(1));
}

void testSimultaneousDistinctSavesFromStaleProcessorsAreBothRestored()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor firstWriter(catalog.file());
    LivePatternSequencerProcessor secondWriter(catalog.file());

    std::atomic<int> readyWriterCount {0};
    std::atomic<bool> startSaving {false};
    LivePatternSequencerProcessor::SavePatternResult firstSave;
    LivePatternSequencerProcessor::SavePatternResult secondSave;

    std::thread firstThread([&]
    {
        readyWriterCount.fetch_add(1, std::memory_order_release);
        while (!startSaving.load(std::memory_order_acquire))
            std::this_thread::yield();

        firstSave = firstWriter.savePlayerPattern(
            0, makePattern("x-"), "Simultaneous First");
    });

    std::thread secondThread([&]
    {
        readyWriterCount.fetch_add(1, std::memory_order_release);
        while (!startSaving.load(std::memory_order_acquire))
            std::this_thread::yield();

        secondSave = secondWriter.savePlayerPattern(
            0, makePattern("-x"), "Simultaneous Second");
    });

    while (readyWriterCount.load(std::memory_order_acquire) != 2)
        std::this_thread::yield();
    startSaving.store(true, std::memory_order_release);

    firstThread.join();
    secondThread.join();

    CHECK(firstSave.status
        == LivePatternSequencerProcessor::SavePatternStatus::savedNew);
    CHECK(secondSave.status
        == LivePatternSequencerProcessor::SavePatternStatus::savedNew);

    LivePatternSequencerProcessor reloaded(catalog.file());
    CHECK(reloaded.patternCatalogErrorForUi().isEmpty());
    CHECK(reloaded.patternCountForUi() == lps::PatternLibrary::builtInCount + 2);

    const auto firstIndex = patternIndexNamed(reloaded, "Simultaneous First");
    const auto secondIndex = patternIndexNamed(reloaded, "Simultaneous Second");
    CHECK(firstIndex.has_value());
    CHECK(secondIndex.has_value());
    CHECK(*firstIndex != *secondIndex);

    reloaded.selectPatternForPlayer(0, *firstIndex);
    reloaded.prepareToPlay(48'000.0, 512);
    const auto firstRestored = reloaded.patternForUi(0);
    CHECK(firstRestored.playbackEnd == 1);
    CHECK(firstRestored.isHit(0));
    CHECK(!firstRestored.isHit(1));

    reloaded.selectPatternForPlayer(0, *secondIndex);
    reloaded.prepareToPlay(48'000.0, 512);
    const auto secondRestored = reloaded.patternForUi(0);
    CHECK(secondRestored.playbackEnd == 1);
    CHECK(!secondRestored.isHit(0));
    CHECK(secondRestored.isHit(1));
}

void testOversizedInt64SchemaVersionIsRejectedAndPreserved()
{
    TemporaryPatternCatalog catalog;
    const juce::String oversizedVersionJson = R"json({
  "format": "live-pattern-sequencer-pattern-library",
  "schemaVersion": 4294967297,
  "patterns": [
    { "id": 1, "name": "Must Not Load", "steps": "x-" }
  ]
})json";
    CHECK(catalog.file().replaceWithText(oversizedVersionJson));
    const auto originalContents = catalog.file().loadFileAsString();

    LivePatternSequencerProcessor processor(catalog.file());
    CHECK(processor.patternCountForUi() == lps::PatternLibrary::builtInCount);
    CHECK(processor.patternNameForUi(0) == "Basic Kick");
    CHECK(!patternIndexNamed(processor, "Must Not Load").has_value());
    CHECK(processor.patternCatalogErrorForUi().isNotEmpty());
    CHECK(catalog.file().loadFileAsString() == originalContents);
}

void testMixedPlayerCapabilitiesAreExposedWithoutConcreteAssumptions()
{
    TemporaryPatternCatalog catalog;
    LivePatternSequencerProcessor processor(catalog.file());
    CHECK(processor.playerCountForUi() > 1);

    const auto patternIndex = std::size_t { 0 };
    const auto pulseIndex = processor.playerCountForUi() - 1;
    CHECK(processor.playerSupportsPatternEditingForUi(patternIndex));
    CHECK(processor.playerSupportsVelocityEditingForUi(patternIndex));
    CHECK(processor.playerCanResetToMasterForUi(patternIndex));

    CHECK(processor.playerNameForUi(pulseIndex) == "Pulse");
    CHECK(!processor.playerSupportsPatternEditingForUi(pulseIndex));
    CHECK(!processor.playerSupportsVelocityEditingForUi(pulseIndex));
    CHECK(!processor.playerCanResetToMasterForUi(pulseIndex));
    CHECK(processor.patternForUi(pulseIndex).stepCount == 0);
    CHECK(processor.savePlayerPattern(pulseIndex).status
        == LivePatternSequencerProcessor::SavePatternStatus::failed);
    CHECK(!processor.resetPlayerToMaster(pulseIndex));
}
} // namespace

int main()
{
    testStaticMasterConfigurationAndResetRequestForwarding();
    testPlayerMuteStateForwardingAndInvalidIndexSafety();
    testVelocityModulationBuiltInsAndSelectionsAreIndependent();
    testVelocityModulationDraftSaveWorkflow();
    testSavedVelocityModulationIsRestoredFromSiblingCatalog();
    testAuthoredVelocityModulationCatalogValidation();
    testVelocityModulationInvalidIndexSafety();
    testSaveWorkflowDeduplicatesFreezesAndLoadsTheSavedLoop();
    testSavedPatternIsRestoredFromJsonCatalogOnNextStartup();
    testManuallyAuthoredVersionOneJsonCatalogLoadsAtConstruction();
    testMalformedExistingJsonIsPreservedAndBlocksNovelSave();
    testCatalogWriteFailureDoesNotPublishNewPattern();
    testConcurrentProcessorSnapshotsMergeDistinctSavesWithoutLoss();
    testSimultaneousDistinctSavesFromStaleProcessorsAreBothRestored();
    testOversizedInt64SchemaVersionIsRejectedAndPreserved();
    testMixedPlayerCapabilitiesAreExposedWithoutConcreteAssumptions();
    std::cout << "All plugin processor tests passed.\n";
    return EXIT_SUCCESS;
}
