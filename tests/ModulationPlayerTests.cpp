#include "core/ModulationPlayer.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

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

void testHitAdvanceAndCoincidentReset()
{
    lps::ModulationLibrary library;
    lps::ModulationPlayer player(library, lps::ModulationPlayerId {7});
    player.selectModulation(lps::ModulationId {2});
    player.setAdvanceSource(lps::PatternHitAdvance {lps::PatternPlayerId {3}});
    player.prepare({});

    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::resetAndPlay, 4.0, output);
    CHECK(output.size() == 1);
    CHECK(output[0].type == lps::PlayerSignalType::modulationValue);
    CHECK(output[0].modulationPlayerId == lps::ModulationPlayerId {7});
    CHECK(output[0].sourceStep == 0);
    CHECK(output[0].normalizedValue
        == lps::NormalizedValue::fromUnipolar8(255));

    player.advanceFromPatternHit(lps::PatternPlayerId {3}, 4.0, output);
    CHECK(output.size() == 1);
    player.advanceFromPatternHit(lps::PatternPlayerId {2}, 4.25, output);
    CHECK(output.size() == 1);
    player.advanceFromPatternHit(lps::PatternPlayerId {3}, 4.25, output);
    CHECK(output.size() == 2);
    CHECK(output[1].sourceStep == 1);
    CHECK(output[1].normalizedValue
        == lps::NormalizedValue::fromUnipolar8(100));
}

void testPlayersSharingARecordKeepIndependentCursors()
{
    lps::ModulationLibrary library;
    lps::ModulationPlayer first(library, lps::ModulationPlayerId {1});
    lps::ModulationPlayer second(library, lps::ModulationPlayerId {2});
    first.selectModulation(lps::ModulationId {2});
    second.selectModulation(lps::ModulationId {2});
    first.setAdvanceSource(lps::PatternHitAdvance {lps::PatternPlayerId {1}});
    second.setAdvanceSource(lps::PatternHitAdvance {lps::PatternPlayerId {1}});
    first.prepare({});
    second.prepare({});

    lps::PlayerSignalBuffer firstOutput;
    lps::PlayerSignalBuffer secondOutput;
    first.command(lps::PlayerCommand::resetAndPlay, 0.0, firstOutput);
    second.command(lps::PlayerCommand::resetAndPlay, 0.0, secondOutput);
    first.advanceFromPatternHit(lps::PatternPlayerId {1}, 0.25, firstOutput);
    first.advanceFromPatternHit(lps::PatternPlayerId {1}, 0.5, firstOutput);
    second.advanceFromPatternHit(lps::PatternPlayerId {1}, 0.25, secondOutput);

    CHECK(first.status().currentStep == 2);
    CHECK(second.status().currentStep == 1);
    CHECK(first.activeModulationId() == second.activeModulationId());
}

void testSelectionCanWaitForOwningPatternCycle()
{
    lps::ModulationLibrary library;
    lps::ModulationPlayer player(library, lps::ModulationPlayerId {5});
    player.setAdvanceSource(
        lps::PatternHitAdvance {lps::PatternPlayerId {3}});
    player.setSelectionQuantizedToPatternCycle(true);
    player.prepare({});

    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::resetAndPlay, 0.0, output);
    CHECK(player.activeModulationId() == lps::ModulationId {1});

    player.selectModulation(lps::ModulationId {2});
    player.advanceFromPatternHit(
        lps::PatternPlayerId {3}, 0.25, output);
    CHECK(player.activeModulationId() == lps::ModulationId {1});
    CHECK(!player.observeCycleBoundary(
        lps::PlayerSignal::patternCycleBoundary(
            1.0, lps::PatternPlayerId {2})));
    CHECK(player.observeCycleBoundary(
        lps::PlayerSignal::patternCycleBoundary(
            1.0, lps::PatternPlayerId {3})));
    CHECK(player.activeModulationId() == lps::ModulationId {2});

    player.advanceFromPatternHit(
        lps::PatternPlayerId {3}, 1.0, output);
    CHECK(output[output.size() - 1].sourceStep == 0);
}

void testOneShotAndCommandSemantics()
{
    lps::ModulationLibrary library;
    lps::ModulationPlayer player(library, lps::ModulationPlayerId {1});
    player.selectModulation(lps::ModulationId {2});
    player.setAdvanceSource(lps::PatternHitAdvance {lps::PatternPlayerId {1}});
    player.setPlayMode(lps::PlayMode::oneShot);
    player.prepare({});

    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::play, 0.0, output);
    CHECK(output.size() == 1);
    for (int hit = 1; hit < 4; ++hit)
        player.advanceFromPatternHit(
            lps::PatternPlayerId {1}, hit * 0.25, output);
    CHECK(output.size() == 4);
    CHECK(!player.status().playing);

    player.command(lps::PlayerCommand::play, 2.0, output);
    CHECK(output.size() == 4);
    player.command(lps::PlayerCommand::resetAndPlay, 2.0, output);
    CHECK(output.size() == 5);
    CHECK(output[4].sourceStep == 0);
    player.command(lps::PlayerCommand::stop, 2.1, output);
    player.advanceFromPatternHit(lps::PatternPlayerId {1}, 2.25, output);
    CHECK(output.size() == 5);
}

void testClockUsesHalfOpenBlocks()
{
    lps::ModulationLibrary library;
    lps::ModulationPlayer player(library, lps::ModulationPlayerId {4});
    player.selectModulation(lps::ModulationId {2});
    player.setAdvanceSource(lps::ClockAdvance {0.25});
    player.prepare({});

    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::resetAndPlay, 0.0, output);
    player.processClock({0.0, 0.5, 120.0, 48'000.0, 100, true, false}, output);
    CHECK(output.size() == 2);
    CHECK(std::abs(output[1].ppqPosition - 0.25) < 1.0e-12);
    player.processClock({0.5, 0.75, 120.0, 48'000.0, 100, true, false}, output);
    CHECK(output.size() == 3);
    CHECK(std::abs(output[2].ppqPosition - 0.5) < 1.0e-12);
}

void testSwitchingToClockWhilePlayingSchedulesNextGridStep()
{
    lps::ModulationLibrary library;
    lps::ModulationPlayer player(library, lps::ModulationPlayerId {4});
    player.setAdvanceSource(
        lps::PatternHitAdvance {lps::PatternPlayerId {1}});
    player.prepare({});
    lps::PlayerSignalBuffer output;
    player.command(lps::PlayerCommand::resetAndPlay, 0.0, output);
    CHECK(output.size() == 1);

    player.setAdvanceSource(lps::ClockAdvance {0.25});
    player.processClock(
        {0.10, 0.30, 120.0, 48'000.0, 4'800, true, false}, output);
    CHECK(output.size() == 2);
    CHECK(std::abs(output[1].ppqPosition - 0.25) < 1.0e-12);
}

void testNonFiniteNormalizedInputIsDeterministic()
{
    CHECK(lps::NormalizedValue::fromFloat(
        std::numeric_limits<float>::quiet_NaN()).raw == 0);
    CHECK(lps::NormalizedValue::fromFloat(
        -std::numeric_limits<float>::infinity()).raw == 0);
    CHECK(lps::NormalizedValue::fromFloat(
        std::numeric_limits<float>::infinity()).raw
        == lps::NormalizedValue::maximum);
}
} // namespace

int main()
{
    testHitAdvanceAndCoincidentReset();
    testPlayersSharingARecordKeepIndependentCursors();
    testSelectionCanWaitForOwningPatternCycle();
    testOneShotAndCommandSemantics();
    testClockUsesHalfOpenBlocks();
    testSwitchingToClockWhilePlayingSchedulesNextGridStep();
    testNonFiniteNormalizedInputIsDeterministic();
    std::cout << "ModulationPlayer tests passed\n";
    return EXIT_SUCCESS;
}
