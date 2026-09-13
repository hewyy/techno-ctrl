#include "core/PatternLibrary.h"
#include "core/PatternPlayer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <thread>
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

constexpr double epsilon = 1.0e-9;

[[nodiscard]] bool close(double left, double right)
{
    return std::abs(left - right) < epsilon;
}

[[nodiscard]] const lps::PatternLibraryEntry& entryAt(
    const lps::PatternLibrary& library,
    std::size_t index)
{
    const auto* entry = library.recordAt(index);
    CHECK(entry != nullptr);
    return *entry;
}

[[nodiscard]] std::vector<double> triggerStartPositions(
    const lps::SequencerEventBuffer& events)
{
    std::vector<double> positions;
    for (const auto& event : events)
        if (event.type == lps::SemanticEventType::triggerStart)
            positions.push_back(event.ppqPosition);
    return positions;
}

[[nodiscard]] std::vector<float> triggerStartValues(
    const lps::SequencerEventBuffer& events)
{
    std::vector<float> values;
    for (const auto& event : events)
        if (event.type == lps::SemanticEventType::triggerStart)
            values.push_back(event.normalizedValue);
    return values;
}

void checkPositions(
    const std::vector<double>& actual,
    std::initializer_list<double> expected)
{
    CHECK(actual.size() == expected.size());
    std::size_t index = 0;
    for (const auto position : expected)
        CHECK(close(actual[index++], position));
}

void checkVelocityValues(
    const std::vector<float>& actual,
    std::initializer_list<std::uint8_t> expectedRawValues)
{
    CHECK(actual.size() == expectedRawValues.size());
    std::size_t index = 0;
    for (const auto rawValue : expectedRawValues)
    {
        const auto expected = static_cast<float>(rawValue) / 255.0f;
        CHECK(std::abs(actual[index++] - expected) < 1.0e-6f);
    }
}

void testBasicKickPlaysTwoFullCycles()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 2.0;
    block.playing = true;
    block.transportDiscontinuity = true;

    const auto result = player.process(block, lps::PlayerDirectives {}, events);

    checkPositions(triggerStartPositions(events), {0.0, 1.0});
    CHECK(result.active);
    CHECK(result.firstCycleBoundaryPpq.has_value());
    CHECK(close(*result.firstCycleBoundaryPpq, 0.0));
    CHECK(!result.eventOverflow);
    const auto capabilities = player.syncCapabilities();
    CHECK(capabilities.providesCycleBoundaries);
    CHECK(capabilities.acceptsExternalCycleBoundaries);
    CHECK(capabilities.acceptsExternalRestart);
}

void testModulationAdvancesOnlyOnHitsAndWraps()
{
    const lps::PatternLibrary patternLibrary;
    const lps::ModulationLibrary modulationLibrary;
    lps::PatternPlayer player { patternLibrary, modulationLibrary };
    player.selectModulation(lps::ModulationId {2});

    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 9.1;
    block.playing = true;
    block.transportDiscontinuity = true;
    player.process(block, events);

    // Basic Kick has one hit followed by three rests. Modulation values still
    // advance consecutively per hit and wrap after the fourth hit.
    checkPositions(
        triggerStartPositions(events),
        {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0});
    checkVelocityValues(
        triggerStartValues(events),
        {255, 100, 225, 150, 255, 100, 225, 150, 255, 100});
    CHECK(player.activeModulationId()
          == lps::ModulationId {2});
}

void testModulationSnapshotTracksLastPlayedHitAcrossRests()
{
    const lps::PatternLibrary patternLibrary;
    const lps::ModulationLibrary modulationLibrary;
    lps::PatternPlayer player { patternLibrary, modulationLibrary };
    player.selectModulation(lps::ModulationId {2});
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstHit;
    firstHit.ppqEnd = 0.1;
    firstHit.playing = true;
    firstHit.transportDiscontinuity = true;
    player.process(firstHit, events);
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);

    lps::TimelineBlock rests;
    rests.ppqStart = firstHit.ppqEnd;
    rests.ppqEnd = 0.9;
    rests.playing = true;
    player.process(rests, events);
    CHECK(triggerStartValues(events).empty());
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);

    lps::TimelineBlock secondHit;
    secondHit.ppqStart = rests.ppqEnd;
    secondHit.ppqEnd = 1.1;
    secondHit.playing = true;
    player.process(secondHit, events);
    checkVelocityValues(triggerStartValues(events), {100});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 1);
}

void testModulationRestartsOnTransportDiscontinuity()
{
    const lps::PatternLibrary patternLibrary;
    const lps::ModulationLibrary modulationLibrary;
    lps::PatternPlayer player { patternLibrary, modulationLibrary };
    player.selectModulation(lps::ModulationId {2});
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstRun;
    firstRun.ppqEnd = 1.1;
    firstRun.playing = true;
    firstRun.transportDiscontinuity = true;
    player.process(firstRun, events);
    checkVelocityValues(triggerStartValues(events), {255, 100});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 1);

    lps::TimelineBlock jumped;
    jumped.ppqStart = 9.3;
    jumped.ppqEnd = 9.4;
    jumped.playing = true;
    jumped.transportDiscontinuity = true;
    player.process(jumped, events);

    checkVelocityValues(triggerStartValues(events), {255});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);
}

void testModulationRestartsOnExplicitSequenceReset()
{
    const lps::PatternLibrary patternLibrary;
    const lps::ModulationLibrary modulationLibrary;
    lps::PatternPlayer player { patternLibrary, modulationLibrary };
    player.selectModulation(lps::ModulationId {2});
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstRun;
    firstRun.ppqEnd = 1.1;
    firstRun.playing = true;
    firstRun.transportDiscontinuity = true;
    player.process(firstRun, events);
    checkVelocityValues(triggerStartValues(events), {255, 100});

    lps::TimelineBlock resetBlock;
    resetBlock.ppqStart = firstRun.ppqEnd;
    resetBlock.ppqEnd = 1.7;
    resetBlock.playing = true;
    lps::PlayerDirectives directives;
    directives.restartAtPpq = 1.5;
    (void) player.process(resetBlock, directives, events);

    checkVelocityValues(triggerStartValues(events), {255});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);
}

void testModulationContinuesAcrossPatternChanges()
{
    const lps::PatternLibrary patternLibrary;
    const lps::ModulationLibrary modulationLibrary;
    const auto* allSteps = patternLibrary.recordAt(1);
    CHECK(allSteps != nullptr);

    lps::PatternPlayer player { patternLibrary, modulationLibrary };
    player.selectModulation(lps::ModulationId {2});
    lps::SequencerEventBuffer events;

    lps::TimelineBlock start;
    start.ppqEnd = 0.1;
    start.playing = true;
    start.transportDiscontinuity = true;
    player.process(start, events);
    checkVelocityValues(triggerStartValues(events), {255});

    player.selectPattern(allSteps->id);
    lps::TimelineBlock crossingPatternBoundary;
    crossingPatternBoundary.ppqStart = start.ppqEnd;
    crossingPatternBoundary.ppqEnd = 1.1;
    crossingPatternBoundary.playing = true;
    player.process(crossingPatternBoundary, events);

    CHECK(player.activePatternId() == allSteps->id);
    checkVelocityValues(triggerStartValues(events), {100});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 1);
}

void testSelectingModulationRestartsItsOwnPhase()
{
    const lps::PatternLibrary patternLibrary;
    const lps::ModulationLibrary modulationLibrary;
    lps::PatternPlayer player { patternLibrary, modulationLibrary };
    player.selectModulation(lps::ModulationId {2});
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstRun;
    firstRun.ppqEnd = 1.1;
    firstRun.playing = true;
    firstRun.transportDiscontinuity = true;
    player.process(firstRun, events);
    checkVelocityValues(triggerStartValues(events), {255, 100});

    player.selectModulation(lps::ModulationId {1});
    lps::TimelineBlock steadyBlock;
    steadyBlock.ppqStart = firstRun.ppqEnd;
    steadyBlock.ppqEnd = 2.1;
    steadyBlock.playing = true;
    player.process(steadyBlock, events);
    checkVelocityValues(triggerStartValues(events), {201});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);

    player.selectModulation(lps::ModulationId {2});
    lps::TimelineBlock fourStepAgain;
    fourStepAgain.ppqStart = steadyBlock.ppqEnd;
    fourStepAgain.ppqEnd = 3.1;
    fourStepAgain.playing = true;
    player.process(fourStepAgain, events);
    checkVelocityValues(triggerStartValues(events), {255});
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);
}

void testSmallBlocksDoNotDuplicateTriggers()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;
    std::vector<double> positions;
    constexpr double blockLength = 0.04;

    for (double start = 0.0; start < 2.0; start += blockLength)
    {
        lps::TimelineBlock block;
        block.ppqStart = start;
        block.ppqEnd = std::min(start + blockLength, 2.0);
        block.playing = true;
        block.transportDiscontinuity = close(start, 0.0);
        player.process(block, events);

        const auto blockPositions = triggerStartPositions(events);
        positions.insert(positions.end(), blockPositions.begin(), blockPositions.end());
    }

    checkPositions(positions, {0.0, 1.0});
}

void testTransportStartAtOffGridPpqBeginsAtFirstStep()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqStart = 2.1;
    block.ppqEnd = 2.2;
    block.playing = true;
    block.transportDiscontinuity = true;

    player.process(block, events);

    CHECK(player.patternPlaybackSnapshot().playing);
    CHECK(player.patternPlaybackSnapshot().currentStep == 0);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerStart);
    CHECK(close(events[0].ppqPosition, block.ppqStart));
}

void testResumeAfterStopRestartsAtFirstStep()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstRun;
    firstRun.ppqEnd = 0.6;
    firstRun.playing = true;
    firstRun.transportDiscontinuity = true;
    player.process(firstRun, events);

    lps::TimelineBlock stopped;
    stopped.ppqStart = firstRun.ppqEnd;
    stopped.ppqEnd = stopped.ppqStart;
    stopped.transportDiscontinuity = true;
    player.process(stopped, events);
    CHECK(!player.patternPlaybackSnapshot().playing);

    lps::TimelineBlock resumed;
    resumed.ppqStart = 6.125;
    resumed.ppqEnd = 6.225;
    resumed.playing = true;
    resumed.transportDiscontinuity = true;
    player.process(resumed, events);

    CHECK(player.patternPlaybackSnapshot().playing);
    CHECK(player.patternPlaybackSnapshot().currentStep == 0);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerStart);
    CHECK(close(events[0].ppqPosition, resumed.ppqStart));
}

void testContiguousPlaybackKeepsAdvancingFromRebasedStart()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstBlock;
    firstBlock.ppqStart = 4.1;
    firstBlock.ppqEnd = 4.2;
    firstBlock.playing = true;
    firstBlock.transportDiscontinuity = true;
    player.process(firstBlock, events);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerStart);
    CHECK(close(events[0].ppqPosition, firstBlock.ppqStart));

    lps::TimelineBlock nextBlock;
    nextBlock.ppqStart = firstBlock.ppqEnd;
    nextBlock.ppqEnd = 4.4;
    nextBlock.playing = true;
    player.process(nextBlock, events);
    CHECK(player.patternPlaybackSnapshot().currentStep == 0);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerEnd);
    CHECK(close(events[0].ppqPosition, 4.225));

    lps::TimelineBlock thirdBlock;
    thirdBlock.ppqStart = nextBlock.ppqEnd;
    thirdBlock.ppqEnd = 4.6;
    thirdBlock.playing = true;
    player.process(thirdBlock, events);
    CHECK(player.patternPlaybackSnapshot().currentStep == 1);
    CHECK(events.empty());
}

void testDiscontinuityTurnsOffHeldTriggerBeforeRestarting()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstBlock;
    firstBlock.ppqEnd = 0.1;
    firstBlock.playing = true;
    firstBlock.transportDiscontinuity = true;
    player.process(firstBlock, events);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerStart);

    lps::TimelineBlock jumpedBlock;
    jumpedBlock.ppqStart = 3.2;
    jumpedBlock.ppqEnd = 3.3;
    jumpedBlock.playing = true;
    jumpedBlock.transportDiscontinuity = true;
    player.process(jumpedBlock, events);

    CHECK(events.size() == 2);
    CHECK(events[0].type == lps::SemanticEventType::triggerEnd);
    CHECK(events[1].type == lps::SemanticEventType::triggerStart);
    CHECK(close(events[0].ppqPosition, jumpedBlock.ppqStart));
    CHECK(close(events[1].ppqPosition, jumpedBlock.ppqStart));
    CHECK(player.patternPlaybackSnapshot().currentStep == 0);
}

void testStopEmitsTriggerOff()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;
    lps::TimelineBlock playing;
    playing.ppqEnd = 0.05;
    playing.playing = true;
    playing.transportDiscontinuity = true;
    player.process(playing, events);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerStart);

    lps::TimelineBlock stopped;
    stopped.ppqStart = 0.05;
    player.process(stopped, events);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerEnd);
    CHECK(player.patternPlaybackSnapshot().currentStep == -1);
    CHECK(!player.patternPlaybackSnapshot().playing);
}

void testSemanticEventsPairStartAndEndByTriggerIdentity()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };

    lps::SequencerEventBuffer events;
    lps::TimelineBlock firstBlock;
    firstBlock.ppqEnd = 0.1;
    firstBlock.playing = true;
    firstBlock.transportDiscontinuity = true;
    player.process(firstBlock, events);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerStart);
    CHECK(!events[0].hasMusicalPitch);
    const auto triggerId = events[0].triggerId;
    CHECK(triggerId.value != 0);

    lps::TimelineBlock laterBlock;
    laterBlock.ppqStart = firstBlock.ppqEnd;
    laterBlock.ppqEnd = 0.2;
    laterBlock.playing = true;
    player.process(laterBlock, events);
    CHECK(events.size() == 1);
    CHECK(events[0].type == lps::SemanticEventType::triggerEnd);
    CHECK(events[0].triggerId == triggerId);
}

void testPatternViewStartsAsBasicKick()
{
    const lps::PatternLibrary library;
    const lps::PatternPlayer player { library };
    const auto pattern = player.patternView();

    CHECK(pattern.stepCount == lps::Pattern::maxLength);
    CHECK(pattern.isHit(0));
    CHECK(!pattern.isHit(1));
    CHECK(!pattern.isHit(2));
    CHECK(!pattern.isHit(3));
    CHECK(!pattern.isHit(31));
    CHECK(pattern.playbackStart == 0);
    CHECK(pattern.playbackEnd == 3);
}

void testInvalidSelectionIsIgnoredAndPrepareLoadsAValidSelection()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    const auto& allSteps = entryAt(library, 1);
    lps::PatternPlayer player { library };

    CHECK(player.selectedPatternId() == kick.id);
    player.selectPattern(lps::PatternId {999});
    CHECK(player.selectedPatternId() == kick.id);

    player.selectPattern(allSteps.id);
    CHECK(player.selectedPatternId() == allSteps.id);
    CHECK(player.activePatternId() == kick.id);
    player.prepare({ 48'000.0, 512 });

    CHECK(player.activePatternId() == allSteps.id);
    const auto view = player.patternView();
    CHECK(view.stepCount == lps::Pattern::maxLength);
    CHECK(view.isHit(0) && view.isHit(1) && view.isHit(2) && view.isHit(3));
    CHECK(view.playbackStart == 0);
    CHECK(view.playbackEnd == 3);
}

void testSelectionDuringPlaybackWaitsForTheOldPatternEnd()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    const auto& allSteps = entryAt(library, 1);
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstBlock;
    firstBlock.ppqEnd = 0.1;
    firstBlock.playing = true;
    firstBlock.transportDiscontinuity = true;
    player.process(firstBlock, events);

    player.setPlaybackWindow(1, 2);
    player.selectPattern(allSteps.id);
    CHECK(player.selectedPatternId() == allSteps.id);
    CHECK(player.activePatternId() == kick.id);

    lps::TimelineBlock beforeBoundary;
    beforeBoundary.ppqStart = 0.1;
    beforeBoundary.ppqEnd = 0.9;
    beforeBoundary.playing = true;
    player.process(beforeBoundary, events);
    CHECK(player.activePatternId() == kick.id);

    lps::TimelineBlock crossingBoundary;
    crossingBoundary.ppqStart = 0.9;
    crossingBoundary.ppqEnd = 1.1;
    crossingBoundary.playing = true;
    player.process(crossingBoundary, events);

    CHECK(player.activePatternId() == allSteps.id);
    CHECK(player.patternView().playbackStart == 0);
    CHECK(player.patternView().playbackEnd == 3);
    checkPositions(triggerStartPositions(events), {1.0});
}

void testStoppedProcessAppliesPendingSelection()
{
    const lps::PatternLibrary library;
    const auto& allSteps = entryAt(library, 1);
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    player.selectPattern(allSteps.id);
    player.process({}, events);

    CHECK(player.activePatternId() == allSteps.id);
    CHECK(player.patternView().isHit(3));
    CHECK(events.empty());
}

void testLoadingVariableLengthPatternsResetsThePlaybackRange()
{
    const lps::PatternLibrary library;
    const auto& backbeat = entryAt(library, 2);
    const auto& nineStepPulse = entryAt(library, 8);
    lps::PatternPlayer player { library };

    player.setPlaybackWindow(0, lps::Pattern::maxLength - 1);
    player.selectPattern(nineStepPulse.id);
    player.prepare({ 48'000.0, 512 });

    auto view = player.patternView();
    CHECK(view.stepCount == lps::Pattern::maxLength);
    CHECK(view.playbackStart == 0);
    CHECK(view.playbackEnd == 8);
    CHECK(view.isHit(0));
    CHECK(!view.isHit(8));

    player.setPlaybackWindow(2, 5);
    player.selectPattern(backbeat.id);
    player.prepare({ 48'000.0, 512 });

    view = player.patternView();
    CHECK(view.stepCount == lps::Pattern::maxLength);
    CHECK(view.playbackStart == 0);
    CHECK(view.playbackEnd == 15);
    CHECK(view.isHit(4));
    CHECK(view.isHit(12));
}

void testLoadedPatternLengthSetsThePlaybackCycleEnd()
{
    const lps::PatternLibrary library;
    const auto& nineStepPulse = entryAt(library, 8);
    lps::PatternPlayer player { library };
    player.selectPattern(nineStepPulse.id);
    player.prepare({ 48'000.0, 512 });

    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 4.6;
    block.playing = true;
    block.transportDiscontinuity = true;
    player.process(block, events);

    checkPositions(triggerStartPositions(events), {0.0, 2.25, 4.5});
}

void testSelectionWaitsForTheActivePlaybackWindowEnd()
{
    const lps::PatternLibrary library;
    const auto& allSteps = entryAt(library, 1);
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    player.setPlaybackWindow(1, 2);
    lps::TimelineBlock establishShortWindow;
    establishShortWindow.ppqEnd = 1.1;
    establishShortWindow.playing = true;
    establishShortWindow.transportDiscontinuity = true;
    player.process(establishShortWindow, events);
    CHECK(player.patternView().playbackStart == 1);
    CHECK(player.patternView().playbackEnd == 2);

    player.selectPattern(allSteps.id);
    lps::TimelineBlock crossShortWindowEnd;
    crossShortWindowEnd.ppqStart = 1.1;
    crossShortWindowEnd.ppqEnd = 1.55;
    crossShortWindowEnd.playing = true;
    player.process(crossShortWindowEnd, events);

    CHECK(player.activePatternId() == allSteps.id);
    checkPositions(triggerStartPositions(events), {1.5});
    CHECK(player.patternView().playbackStart == 0);
    CHECK(player.patternView().playbackEnd == 3);
}

void testPlayerDraftDoesNotModifyLibraryOrAnotherPlayer()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    const auto& allSteps = entryAt(library, 1);
    lps::PatternPlayer first { library };
    lps::PatternPlayer second { library };

    CHECK(!kick.pattern.hits[1]);
    CHECK(!first.patternView().isHit(1));
    CHECK(!second.patternView().isHit(1));

    first.toggleStep(1);
    CHECK(first.patternView().isHit(1));
    CHECK(!second.patternView().isHit(1));
    CHECK(!kick.pattern.hits[1]);

    first.selectPattern(allSteps.id);
    first.prepare({ 48'000.0, 512 });
    first.selectPattern(kick.id);
    first.prepare({ 48'000.0, 512 });
    CHECK(!first.patternView().isHit(1));
}

void testOffsetRotatesPlaybackWithoutChangingDraftLength()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    CHECK(player.patternView().isHit(0));

    player.offsetPatternRight();
    CHECK(player.patternOffset() == 1);
    CHECK(!player.patternView().isHit(0));
    CHECK(player.patternView().isHit(1));

    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 0.4;
    block.playing = true;
    block.transportDiscontinuity = true;
    player.process(block, events);
    checkPositions(triggerStartPositions(events), {0.25});

    player.toggleStep(2);
    const auto edited = player.patternView();
    CHECK(edited.stepCount == lps::Pattern::maxLength);
    CHECK(edited.isHit(1));
    CHECK(edited.isHit(2));
}

void testSpeedChangesTimingWithoutChangingPattern()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    const auto original = player.patternView();
    const auto originalSaveCandidate = player.patternForSave();
    CHECK(!player.hasUnsavedPatternChanges());
    player.setPlaybackSpeed(2);
    CHECK(player.playbackSpeed() == 2);
    CHECK(!player.hasUnsavedPatternChanges());

    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 1.0;
    block.playing = true;
    block.transportDiscontinuity = true;
    player.process(block, events);

    checkPositions(triggerStartPositions(events), {0.0, 0.5});
    const auto afterPlayback = player.patternView();
    CHECK(afterPlayback.stepCount == original.stepCount);
    CHECK(afterPlayback.hitMask == original.hitMask);
    CHECK(patternsEqual(player.patternForSave(), originalSaveCandidate));
}

void testPlaybackWindowWaitsForOldEndThenStartsAtNewStart()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    player.toggleStep(1);
    lps::SequencerEventBuffer events;

    lps::TimelineBlock firstBlock;
    firstBlock.ppqEnd = 0.1;
    firstBlock.playing = true;
    firstBlock.transportDiscontinuity = true;
    player.process(firstBlock, events);

    player.setPlaybackWindow(1, 3);
    const auto requestedView = player.patternView();
    CHECK(!requestedView.isInsidePlaybackWindow(0));
    CHECK(requestedView.isInsidePlaybackWindow(1));
    CHECK(requestedView.isInsidePlaybackWindow(3));

    lps::TimelineBlock finishOldCycle;
    finishOldCycle.ppqStart = 0.1;
    finishOldCycle.ppqEnd = 1.0;
    finishOldCycle.playing = true;
    player.process(finishOldCycle, events);

    lps::TimelineBlock crossOldEnd;
    crossOldEnd.ppqStart = 1.0;
    crossOldEnd.ppqEnd = 1.1;
    crossOldEnd.playing = true;
    player.process(crossOldEnd, events);

    checkPositions(triggerStartPositions(events), {1.0});
    CHECK(player.patternPlaybackSnapshot().playing);
    CHECK(player.patternPlaybackSnapshot().currentStep == 1);
}

void testPlaybackWindowCanExtendAcrossTheFullDraftWithoutChangingThePattern()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    lps::PatternPlayer player { library };
    const auto original = player.patternView();

    player.setPlaybackWindow(0, lps::Pattern::maxLength - 1);
    const auto extended = player.patternView();
    CHECK(extended.playbackStart == 0);
    CHECK(extended.playbackEnd == lps::Pattern::maxLength - 1);
    CHECK(extended.hitMask == original.hitMask);
    CHECK(kick.pattern.length == 4);
    CHECK(!kick.pattern.hits[31]);
    player.toggleStep(31);
    CHECK(player.patternView().isHit(31));
    CHECK(!kick.pattern.hits[31]);

    lps::SequencerEventBuffer events;
    lps::TimelineBlock firstCycle;
    firstCycle.ppqEnd = 1.1;
    firstCycle.playing = true;
    firstCycle.transportDiscontinuity = true;
    player.process(firstCycle, events);
    checkPositions(triggerStartPositions(events), {0.0});

    lps::TimelineBlock extendedCycle;
    extendedCycle.ppqStart = firstCycle.ppqEnd;
    extendedCycle.ppqEnd = 8.1;
    extendedCycle.playing = true;
    player.process(extendedCycle, events);
    checkPositions(triggerStartPositions(events), {7.75, 8.0});

    CHECK(!kick.pattern.hits[31]);
    CHECK(kick.pattern.length == 4);
}

void testPlaybackWindowChosenWhileStoppedIsActiveAtTransportStart()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    player.setPlaybackWindow(4, 7);

    lps::SequencerEventBuffer events;
    lps::TimelineBlock start;
    start.ppqEnd = 0.1;
    start.playing = true;
    start.transportDiscontinuity = true;
    player.process(start, events);

    CHECK(events.empty());
    CHECK(player.patternPlaybackSnapshot().playing);
    CHECK(player.patternPlaybackSnapshot().currentStep == 4);
}

void testUnsavedStateTracksEditsLoopAndOffsetAndCanReturnToClean()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };

    CHECK(!player.hasUnsavedPatternChanges());

    player.toggleStep(1);
    CHECK(player.hasUnsavedPatternChanges());
    player.toggleStep(1);
    CHECK(!player.hasUnsavedPatternChanges());

    player.setPlaybackWindow(1, 3);
    CHECK(player.hasUnsavedPatternChanges());
    player.setPlaybackWindow(0, 3);
    CHECK(!player.hasUnsavedPatternChanges());

    player.offsetPatternRight();
    CHECK(player.hasUnsavedPatternChanges());
    player.offsetPatternLeft();
    CHECK(!player.hasUnsavedPatternChanges());
}

void testPatternForSaveCropsTheRequestedLoopAndRebasesItToZero()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };

    // The library kick at step zero is outside the requested loop. The two
    // draft edits become saved steps zero and two after rebasing [2, 5].
    player.toggleStep(2);
    player.toggleStep(4);
    player.setPlaybackWindow(2, 5);

    const auto saved = player.patternForSave();
    CHECK(saved.length == 4);
    CHECK(saved.hits[0]);
    CHECK(!saved.hits[1]);
    CHECK(saved.hits[2]);
    CHECK(!saved.hits[3]);
    for (std::size_t step = saved.length; step < lps::Pattern::maxLength; ++step)
        CHECK(!saved.hits[step]);
}

void testPatternForSaveExcludesOutsideHitsButTheDraftRemainsModified()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    lps::PatternPlayer player { library };

    player.toggleStep(lps::Pattern::maxLength - 1);

    CHECK(patternsEqual(player.patternForSave(), kick.pattern));
    CHECK(player.hasUnsavedPatternChanges());
    CHECK(player.patternView().isHit(lps::Pattern::maxLength - 1));
}

void testSelectingAnEquivalentSavedPatternDiscardsExcludedDraftEdits()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    player.toggleStep(lps::Pattern::maxLength - 1);
    CHECK(player.hasUnsavedPatternChanges());
    CHECK(patternsEqual(player.patternForSave(), kick.pattern));

    player.selectSavedPattern(kick.id);
    player.process({}, events);

    CHECK(player.activePatternId() == kick.id);
    CHECK(!player.patternView().isHit(lps::Pattern::maxLength - 1));
    CHECK(!player.hasUnsavedPatternChanges());
    CHECK(player.requestedPlaybackStart() == 0);
    CHECK(player.requestedPlaybackEnd() == 3);
}

void testPatternForSaveBakesTheEffectiveOffsetIntoTheSavedHits()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };

    player.offsetPatternRight();
    const auto saved = player.patternForSave();

    CHECK(saved.length == 4);
    CHECK(!saved.hits[0]);
    CHECK(saved.hits[1]);
    CHECK(!saved.hits[2]);
    CHECK(!saved.hits[3]);
    CHECK(player.hasUnsavedPatternChanges());
}

void testPatternForSaveAppliesOffsetBeforeCroppingAndRebasing()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };

    // Basic Kick's hit moves from visible step 0 to step 1. The edit at
    // visible step 3 is translated through the same offset before [1, 4] is
    // cropped and rebased into a four-step saved pattern.
    player.offsetPatternRight();
    player.toggleStep(3);
    player.setPlaybackWindow(1, 4);

    const auto saved = player.patternForSave();
    CHECK(saved.length == 4);
    CHECK(saved.hits[0]);
    CHECK(!saved.hits[1]);
    CHECK(saved.hits[2]);
    CHECK(!saved.hits[3]);
}

void testOrdinaryPatternSelectionPreservesThePlayerOffset()
{
    const lps::PatternLibrary library;
    const auto& allSteps = entryAt(library, 1);
    lps::PatternPlayer player { library };

    player.offsetPatternRight();
    player.selectPattern(allSteps.id);
    player.prepare({ 48'000.0, 512 });

    CHECK(player.activePatternId() == allSteps.id);
    CHECK(player.patternOffset() == 1);
}

void testSavedPatternActivationWaitsForBoundaryThenResetsBakedModifiers()
{
    lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    lps::PatternPlayer player { library };
    lps::SequencerEventBuffer events;

    player.toggleStep(1);
    player.offsetPatternRight();
    player.setPlaybackWindow(1, 2);
    const auto saved = player.patternForSave();
    CHECK(saved.length == 2);
    CHECK(saved.hits[0]);
    CHECK(saved.hits[1]);

    lps::TimelineBlock start;
    start.ppqEnd = 0.1;
    start.playing = true;
    start.transportDiscontinuity = true;
    player.process(start, events);

    const auto insert = library.addOrFind("Saved Two Hits", saved);
    CHECK(insert.entry != nullptr);
    CHECK(insert.inserted);
    player.selectSavedPattern(insert.entry->id);

    lps::TimelineBlock beforeBoundary;
    beforeBoundary.ppqStart = 0.1;
    beforeBoundary.ppqEnd = 0.49;
    beforeBoundary.playing = true;
    player.process(beforeBoundary, events);
    CHECK(player.activePatternId() == kick.id);
    CHECK(player.patternOffset() == 1);

    lps::TimelineBlock crossingBoundary;
    crossingBoundary.ppqStart = 0.49;
    crossingBoundary.ppqEnd = 0.51;
    crossingBoundary.playing = true;
    player.process(crossingBoundary, events);

    CHECK(player.activePatternId() == insert.entry->id);
    CHECK(player.selectedPatternId() == insert.entry->id);
    CHECK(player.patternOffset() == 0);
    CHECK(player.requestedPlaybackStart() == 0);
    CHECK(player.requestedPlaybackEnd() == 1);
    CHECK(patternsEqual(player.patternForSave(), saved));
    CHECK(!player.hasUnsavedPatternChanges());
}

void testSaveSnapshotNeverCombinesConcurrentPatternActivations()
{
    const lps::PatternLibrary library;
    const auto& kick = entryAt(library, 0);
    const auto& backbeat = entryAt(library, 2);
    lps::PatternPlayer player { library };
    std::atomic<bool> start { false };
    std::atomic<bool> done { false };

    std::thread activationThread([&]
    {
        while (!start.load(std::memory_order_acquire))
        {
        }

        for (std::size_t iteration = 0; iteration < 20'000; ++iteration)
        {
            player.selectSavedPattern(
                iteration % 2 == 0 ? backbeat.id : kick.id);
            player.prepare({ 48'000.0, 512 });
        }

        done.store(true, std::memory_order_release);
    });

    start.store(true, std::memory_order_release);
    std::size_t snapshotCount = 0;
    do
    {
        const auto candidate = player.patternForSave();
        CHECK(patternsEqual(candidate, kick.pattern)
            || patternsEqual(candidate, backbeat.pattern));
        ++snapshotCount;
    }
    while (!done.load(std::memory_order_acquire) || snapshotCount < 1'000);

    activationThread.join();
}
} // namespace

int main()
{
    testBasicKickPlaysTwoFullCycles();
    testModulationAdvancesOnlyOnHitsAndWraps();
    testModulationSnapshotTracksLastPlayedHitAcrossRests();
    testModulationRestartsOnTransportDiscontinuity();
    testModulationRestartsOnExplicitSequenceReset();
    testModulationContinuesAcrossPatternChanges();
    testSelectingModulationRestartsItsOwnPhase();
    testSmallBlocksDoNotDuplicateTriggers();
    testTransportStartAtOffGridPpqBeginsAtFirstStep();
    testResumeAfterStopRestartsAtFirstStep();
    testContiguousPlaybackKeepsAdvancingFromRebasedStart();
    testDiscontinuityTurnsOffHeldTriggerBeforeRestarting();
    testStopEmitsTriggerOff();
    testSemanticEventsPairStartAndEndByTriggerIdentity();
    testPatternViewStartsAsBasicKick();
    testInvalidSelectionIsIgnoredAndPrepareLoadsAValidSelection();
    testSelectionDuringPlaybackWaitsForTheOldPatternEnd();
    testStoppedProcessAppliesPendingSelection();
    testLoadingVariableLengthPatternsResetsThePlaybackRange();
    testLoadedPatternLengthSetsThePlaybackCycleEnd();
    testSelectionWaitsForTheActivePlaybackWindowEnd();
    testPlayerDraftDoesNotModifyLibraryOrAnotherPlayer();
    testOffsetRotatesPlaybackWithoutChangingDraftLength();
    testSpeedChangesTimingWithoutChangingPattern();
    testPlaybackWindowWaitsForOldEndThenStartsAtNewStart();
    testPlaybackWindowCanExtendAcrossTheFullDraftWithoutChangingThePattern();
    testPlaybackWindowChosenWhileStoppedIsActiveAtTransportStart();
    testUnsavedStateTracksEditsLoopAndOffsetAndCanReturnToClean();
    testPatternForSaveCropsTheRequestedLoopAndRebasesItToZero();
    testPatternForSaveExcludesOutsideHitsButTheDraftRemainsModified();
    testSelectingAnEquivalentSavedPatternDiscardsExcludedDraftEdits();
    testPatternForSaveBakesTheEffectiveOffsetIntoTheSavedHits();
    testPatternForSaveAppliesOffsetBeforeCroppingAndRebasing();
    testOrdinaryPatternSelectionPreservesThePlayerOffset();
    testSavedPatternActivationWaitsForBoundaryThenResetsBakedModifiers();
    testSaveSnapshotNeverCombinesConcurrentPatternActivations();

    std::cout << "All pattern player tests passed.\n";
    return EXIT_SUCCESS;
}
