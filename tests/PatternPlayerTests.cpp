#include "core/PatternPlayer.h"

#include <cmath>
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

lps::TimelineBlock block(double start, double end, bool discontinuity = false)
{
    return {start, end, 120.0, 48'000.0, 4096, true, discontinuity};
}

lps::Pattern pattern(std::initializer_list<lps::PatternHit> hits,
                     lps::PatternTick cycle = 960)
{
    lps::Pattern result;
    result.cycleLengthTicks = cycle;
    for (const auto hit : hits)
        result.hits[result.hitCount++] = hit;
    return result;
}

lps::PatternId addAndSelect(
    lps::PatternLibrary& library,
    lps::PatternPlayer& player,
    const lps::Pattern& value)
{
    const auto added = library.addOrFind("Test", value);
    CHECK(added.entry != nullptr);
    player.selectPattern(added.entry->id);
    player.prepare({});
    return added.entry->id;
}

void testHighResolutionHitsAndHalfOpenBlocks()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    player.setRuntimeId(3);
    addAndSelect(library, player, pattern({
        {0, 30}, {1, 1}, {30, 60}, {60, 120}, {120, 240}, {240, 30}}));

    lps::PlayerSignalBuffer output;
    auto result = player.process(block(0.0, 0.25, true), output);
    CHECK(result.active);
    CHECK(output.size() == 6); // boundary plus ticks 0, 1, 30, 60, 120
    CHECK(output[0].type == lps::PlayerSignalType::patternCycleBoundary);
    CHECK(output[1].ppqPosition == 0.0);
    CHECK(std::abs(output[2].ppqPosition - 1.0 / 960.0) < 1.0e-12);
    CHECK(std::abs(output[3].ppqPosition - 30.0 / 960.0) < 1.0e-12);
    CHECK(std::abs(output[3].baseDurationPpq - 60.0 / 960.0) < 1.0e-12);

    result = player.process(block(0.25, 0.5), output);
    CHECK(result.active);
    CHECK(output.size() == 1);
    CHECK(output[0].ppqPosition == 0.25);
}

void testSeveralCyclesAndCycleBoundaryOrdering()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player, pattern({{0, 240}, {240, 240}}, 480));

    lps::PlayerSignalBuffer output;
    (void) player.process(block(0.0, 1.25, true), output);
    CHECK(output.size() == 8); // 3 boundaries, 5 hits
    CHECK(output[0].type == lps::PlayerSignalType::patternCycleBoundary);
    CHECK(output[1].type == lps::PlayerSignalType::patternHit);
    CHECK(output[2].ppqPosition == 0.25);
    CHECK(output[3].type == lps::PlayerSignalType::patternCycleBoundary);
    CHECK(output[3].ppqPosition == 0.5);
    CHECK(output[4].ppqPosition == 0.5);
}

void testPlaybackSpeedScalesPositionsAndDurations()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player, pattern({{240, 480}}));
    lps::PlayerSignalBuffer output;

    player.setPlaybackSpeed(0);
    (void) player.process(block(0.0, 1.0, true), output);
    CHECK(output.size() == 2);
    CHECK(output[1].ppqPosition == 0.5);
    CHECK(output[1].baseDurationPpq == 1.0);

    player.setPlaybackSpeed(1);
    (void) player.process(block(0.0, 1.0, true), output);
    CHECK(output.size() == 2);
    CHECK(output[1].ppqPosition == 0.25);
    CHECK(output[1].baseDurationPpq == 0.5);

    player.setPlaybackSpeed(2);
    (void) player.process(block(0.0, 0.5, true), output);
    CHECK(output.size() == 2);
    CHECK(output[1].ppqPosition == 0.125);
    CHECK(output[1].baseDurationPpq == 0.25);
}

void testDensePatternsRemainBoundedAndReportOverflow()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    lps::Pattern dense;
    dense.cycleLengthTicks = lps::Pattern::maximumHitCount;
    dense.hitCount = lps::Pattern::maximumHitCount;
    for (std::size_t index = 0; index < dense.hitCount; ++index)
        dense.hits[index] = {static_cast<lps::PatternTick>(index), 1};
    addAndSelect(library, player, dense);

    lps::PlayerSignalBuffer output;
    auto result = player.process(block(0.0,
        static_cast<double>(dense.cycleLengthTicks)
            / lps::Pattern::ticksPerQuarterNote,
        true), output);
    CHECK(!result.eventOverflow);
    CHECK(output.size() == dense.hitCount + 1); // boundary plus every hit

    result = player.process(block(0.0,
        4.0 * dense.cycleLengthTicks / lps::Pattern::ticksPerQuarterNote,
        true), output);
    CHECK(result.eventOverflow);
    CHECK(output.size() == lps::PlayerSignalBuffer::capacity);
}

void testOffsetRotatesAcrossFullCycleBeforeWindowFiltering()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player, pattern({{0, 240}, {720, 240}}));
    player.setPatternOffsetTicks(240);
    player.setPlaybackWindow(240, 720);

    lps::PlayerSignalBuffer output;
    (void) player.process(block(0.0, 0.5, true), output);
    CHECK(output.size() == 2);
    CHECK(output[0].type == lps::PlayerSignalType::patternCycleBoundary);
    CHECK(output[1].type == lps::PlayerSignalType::patternHit);
    CHECK(output[1].ppqPosition == 0.0); // source tick 0 rotated to window start
}

void testEditingMaintainsOrderingAndExactTicks()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player, pattern({{0, 240}}, 960));
    CHECK(player.addHit(31, 17));
    CHECK(player.addHit(30, 1));
    CHECK(!player.addHit(31, 99));
    CHECK(player.moveHit(31, 32));
    CHECK(player.resizeHit(32, 2880));
    CHECK(player.removeHit(30));

    const auto saved = player.patternForSave();
    CHECK(saved.hitCount == 2);
    CHECK((saved.hits[0] == lps::PatternHit {0, 240}));
    CHECK((saved.hits[1] == lps::PatternHit {32, 2880}));
    CHECK(player.hasUnsavedPatternChanges());
    CHECK(!player.setCycleLength(32));
    CHECK(player.setCycleLength(33));
}

void testPatternForSaveEndsAtPlaybackEndpoint()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player,
        pattern({{0, 240}, {479, 480}, {480, 240}, {720, 240}}, 960));
    player.setPlaybackWindow(120, 480);
    CHECK(player.hasUnsavedPatternChanges());

    const auto saved = player.patternForSave();
    CHECK(saved.cycleLengthTicks == 480);
    CHECK(saved.hitCount == 2);
    CHECK((saved.hits[0] == lps::PatternHit {0, 240}));
    CHECK((saved.hits[1] == lps::PatternHit {479, 480}));
    CHECK((saved.hits[2] == lps::PatternHit {}));

    const auto inserted = library.addOrFind("Trimmed", saved);
    CHECK(inserted.entry != nullptr);
    CHECK(player.adoptSavedDraft(inserted.entry->id));
    const auto adopted = player.patternView();
    CHECK(lps::patternsEqual(adopted.pattern, saved));
    CHECK(adopted.playbackStartTick == 120);
    CHECK(adopted.playbackEndTick == 480);
    CHECK(!player.hasUnsavedPatternChanges());
}

void testPatternForSaveBakesInRotation()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player,
        pattern({{0, 30}, {720, 240}}, 960));

    player.setPatternOffsetTicks(240);
    CHECK(player.hasUnsavedPatternChanges());

    const auto saved = player.patternForSave();
    CHECK(saved.hitCount == 2);
    CHECK((saved.hits[0] == lps::PatternHit {0, 240}));
    CHECK((saved.hits[1] == lps::PatternHit {240, 30}));

    const auto inserted = library.addOrFind("Rotated", saved);
    CHECK(inserted.entry != nullptr);
    CHECK(player.adoptSavedDraft(inserted.entry->id));
    CHECK(player.patternOffsetTicks() == 0);
    CHECK(lps::patternsEqual(player.patternView().pattern, saved));
    CHECK(!player.hasUnsavedPatternChanges());
}

void testOneShotEndsAfterWindowButKeepsHitDuration()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    addAndSelect(library, player, pattern({{0, 2880}}, 960));
    player.setPlaybackWindow(0, 240);
    player.setPlayMode(lps::PlayMode::oneShot);

    lps::PlayerSignalBuffer output;
    const auto result = player.process(block(0.0, 1.0, true), output);
    CHECK(!result.active);
    CHECK(output.size() == 2);
    CHECK(output[1].baseDurationPpq == 3.0);
}

void testHitAdvanceUsesLegacySlices()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    player.setRuntimeId(8);
    addAndSelect(library, player, pattern({{30, 30}, {120, 60}, {240, 240}}, 480));
    player.setAdvanceSource(lps::PatternHitAdvance {lps::PatternPlayerId {2}});
    player.prepare({});

    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::resetAndPlay, 1.0, output);
    CHECK(output.size() == 3); // boundary plus both hits in first 240-tick slice
    output.clear();
    player.advanceFromPatternHit(lps::PlayerSignal::patternHit(
        1.25, lps::PatternPlayerId {2}, lps::TriggerId {1}, 0.25), output);
    CHECK(output.size() == 1);
    CHECK(output[0].type == lps::PlayerSignalType::patternHit);
    CHECK(output[0].ppqPosition == 1.25);
}

void testExternalCycleActivatesPendingSelection()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    const auto target = library.addOrFind(
        "Target", pattern({{0, 240}}, 960));
    CHECK(target.entry != nullptr);
    player.setRuntimeId(8);
    player.setTransitionPolicy(lps::PatternTransitionPolicy::externalCycle(
        lps::PatternPlayerId {2}));
    player.prepare({});
    player.selectPattern(target.entry->id);
    lps::PlayerSignalBuffer output;
    CHECK(player.observeCycleBoundary(lps::PlayerSignal::patternCycleBoundary(
        1.0, lps::PatternPlayerId {2}), output));
    CHECK(player.activePatternId() == target.entry->id);
    CHECK(!output.empty());
}

void testMissingSavedPatternFallsBackToNone()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    lps::PatternPlayerPersistentState missing;
    missing.patternId = lps::PatternId {999};
    missing.pattern = pattern({{0, 240}}, 960);
    missing.patternOffsetTicks = 240;
    missing.playbackStartTick = 0;
    missing.playbackEndTick = 960;
    missing.playbackSpeed = 2;

    CHECK(player.restorePersistentState(missing));
    const auto restored = player.capturePersistentState();
    CHECK(restored.patternId == lps::PatternId {1});
    CHECK(restored.pattern.hitCount == 0);
    CHECK(restored.pattern.cycleLengthTicks
        == 8 * lps::Pattern::ticksPerQuarterNote);
    CHECK(restored.patternOffsetTicks == 0);
    CHECK(restored.playbackStartTick == 0);
    CHECK(restored.playbackEndTick == restored.pattern.cycleLengthTicks);
    CHECK(restored.playbackSpeed == 1);
}
} // namespace

int main()
{
    testHighResolutionHitsAndHalfOpenBlocks();
    testSeveralCyclesAndCycleBoundaryOrdering();
    testPlaybackSpeedScalesPositionsAndDurations();
    testDensePatternsRemainBoundedAndReportOverflow();
    testOffsetRotatesAcrossFullCycleBeforeWindowFiltering();
    testEditingMaintainsOrderingAndExactTicks();
    testPatternForSaveEndsAtPlaybackEndpoint();
    testPatternForSaveBakesInRotation();
    testOneShotEndsAfterWindowButKeepsHitDuration();
    testHitAdvanceUsesLegacySlices();
    testExternalCycleActivatesPendingSelection();
    testMissingSavedPatternFallsBackToNone();
    std::cout << "PatternPlayer timing tests passed\n";
    return EXIT_SUCCESS;
}
