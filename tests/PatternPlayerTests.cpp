#include "core/PatternPlayer.h"

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

void testClockEmitsRhythmOnlySignals()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    player.setRuntimeId(3);
    player.prepare({});

    lps::PlayerSignalBuffer output;
    const auto result = player.process(block(0.0, 0.25, true), output);
    CHECK(result.active);
    CHECK(output.size() == 2);
    CHECK(output[0].type == lps::PlayerSignalType::patternCycleBoundary);
    CHECK(output[1].type == lps::PlayerSignalType::patternHit);
    CHECK(output[1].patternPlayerId == lps::PatternPlayerId {3});
    CHECK(output[1].nominalStepLengthPpq == 0.25);
}

void testCycleBoundaryExistsWhenFirstStepIsRest()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    player.toggleStep(0);
    player.prepare({});

    lps::PlayerSignalBuffer output;
    (void) player.process(block(0.0, 0.25, true), output);
    CHECK(output.size() == 1);
    CHECK(output[0].type == lps::PlayerSignalType::patternCycleBoundary);
}

void testOneShotCountsRestsAndStops()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    player.setPlaybackWindow(0, 3);
    player.setPlayMode(lps::PlayMode::oneShot);
    player.prepare({});

    lps::PlayerSignalBuffer output;
    const auto result = player.process(block(0.0, 2.0, true), output);
    CHECK(!result.active);
    CHECK(!player.patternPlaybackSnapshot().playing);
    CHECK(output.size() == 2);
}

void testHitAdvanceAndCommands()
{
    lps::PatternLibrary library;
    lps::PatternPlayer player(library);
    player.setRuntimeId(8);
    player.toggleStep(1);
    player.setPlaybackWindow(0, 3);
    player.setAdvanceSource(
        lps::PatternHitAdvance {lps::PatternPlayerId {2}});
    player.prepare({});

    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::resetAndPlay, 1.0, output);
    CHECK(output.size() == 2);
    const auto sourceHit = lps::PlayerSignal::patternHit(
        1.0, lps::PatternPlayerId {2}, lps::TriggerId {1}, 0.25);
    player.advanceFromPatternHit(sourceHit, output);
    CHECK(output.size() == 2);

    auto nextHit = sourceHit;
    nextHit.ppqPosition = 1.25;
    player.advanceFromPatternHit(nextHit, output);
    CHECK(output.size() == 3);
    CHECK(output[2].type == lps::PlayerSignalType::patternHit);
    CHECK(output[2].patternPlayerId == lps::PatternPlayerId {8});

    player.command(lps::PlayerCommand::stop, 1.5, output);
    nextHit.ppqPosition = 1.5;
    player.advanceFromPatternHit(nextHit, output);
    CHECK(output.size() == 3);
}
} // namespace

int main()
{
    testClockEmitsRhythmOnlySignals();
    testCycleBoundaryExistsWhenFirstStepIsRest();
    testOneShotCountsRestsAndStops();
    testHitAdvanceAndCommands();
    std::cout << "PatternPlayer signal tests passed\n";
    return EXIT_SUCCESS;
}
