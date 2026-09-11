#include "core/ModulationLane.h"

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

lps::ModulationLaneDefinition definition(lps::LaneId id)
{
    lps::ModulationLaneDefinition result;
    result.id = id;
    result.target = lps::ModulationTarget::intensity;
    result.advanceOn = lps::ModulationAdvancePoint::candidateTrigger;
    result.resetOn = lps::ModulationResetReason::sourceSelection
        | lps::ModulationResetReason::transportDiscontinuity
        | lps::resetMask(lps::ModulationResetReason::explicitRestart);
    return result;
}

lps::ModulationLaneState state(
    std::initializer_list<std::uint8_t> values)
{
    lps::ModulationLaneState result;
    result.length = static_cast<std::uint8_t>(values.size());
    std::size_t index = 0;
    for (const auto value : values)
        result.values[index++] = lps::NormalizedValue::fromUnipolar8(value);
    return result;
}

void testLegacyLevelsHaveExactNormalizedMapping()
{
    constexpr std::uint8_t level = 100;
    const auto normalized = lps::NormalizedValue::fromUnipolar8(level);
    CHECK(normalized.raw == level * 257u);
    CHECK(std::abs(normalized.toFloat()
        - static_cast<float>(level) / 255.0f) < 1.0e-7f);
}

void testLaneOwnsAdvanceAndResetPolicy()
{
    lps::ModulationLaneRuntime lane { definition({ 1 }) };
    lane.publishState(state({ 255, 100 }));

    CHECK(!lane.advance(lps::ModulationAdvancePoint::sourceStep).has_value());
    const auto first = lane.advance(lps::ModulationAdvancePoint::candidateTrigger);
    const auto second = lane.advance(lps::ModulationAdvancePoint::candidateTrigger);
    CHECK(first && first->sourceStep == 0 && first->mappedValue == 1.0f);
    CHECK(second && second->sourceStep == 1);

    lane.reset(lps::ModulationResetReason::patternChange);
    const auto wrapped = lane.advance(lps::ModulationAdvancePoint::candidateTrigger);
    CHECK(wrapped && wrapped->sourceStep == 0);

    (void) lane.advance(lps::ModulationAdvancePoint::candidateTrigger);
    lane.reset(lps::ModulationResetReason::explicitRestart);
    const auto restarted = lane.advance(lps::ModulationAdvancePoint::candidateTrigger);
    CHECK(restarted && restarted->sourceStep == 0);
}

void testBankKeepsLaneCursorsIndependent()
{
    lps::ModulationBank bank;
    auto* twoStep = bank.addLane(definition({ 1 }));
    auto* threeStep = bank.addLane(definition({ 2 }));
    CHECK(twoStep != nullptr);
    CHECK(threeStep != nullptr);
    CHECK(bank.size() == 2);
    CHECK(bank.addLane(definition({ 1 })) == nullptr);

    twoStep->publishState(state({ 0, 255 }));
    threeStep->publishState(state({ 0, 128, 255 }));
    for (int iteration = 0; iteration < 5; ++iteration)
    {
        (void) twoStep->advance(lps::ModulationAdvancePoint::candidateTrigger);
        (void) threeStep->advance(lps::ModulationAdvancePoint::candidateTrigger);
    }
    CHECK(twoStep->currentStep() == 0);
    CHECK(threeStep->currentStep() == 1);
}
} // namespace

int main()
{
    testLegacyLevelsHaveExactNormalizedMapping();
    testLaneOwnsAdvanceAndResetPolicy();
    testBankKeepsLaneCursorsIndependent();
    std::cout << "All modulation lane tests passed.\n";
    return EXIT_SUCCESS;
}
