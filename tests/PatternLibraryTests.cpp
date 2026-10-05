#include "core/PatternLibrary.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

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

lps::Pattern legacyPattern(std::string_view steps)
{
    lps::Pattern pattern;
    pattern.cycleLengthTicks = static_cast<lps::PatternTick>(
        steps.size() * lps::Pattern::legacyStepTicks);
    for (std::size_t step = 0; step < steps.size(); ++step)
        if (steps[step] == 'x')
            pattern.hits[pattern.hitCount++] = {
                static_cast<lps::PatternTick>(
                    step * lps::Pattern::legacyStepTicks),
                lps::Pattern::legacyStepTicks};
    return pattern;
}

void testDefaultIsAnEmptyFourBarPattern()
{
    const lps::PatternLibrary library;
    CHECK(library.size() == 1);
    const auto* entry = library.recordAt(0);
    CHECK(entry != nullptr);
    CHECK(entry->id == lps::PatternId {1});
    CHECK(entry->name == "None");
    CHECK(entry->pattern.hitCount == 0);
    CHECK(entry->pattern.cycleLengthTicks
        == 8 * lps::Pattern::ticksPerQuarterNote);
    CHECK(lps::patternIsValid(entry->pattern));
}

void testValidationAndOrdering()
{
    lps::Pattern pattern;
    pattern.cycleLengthTicks = lps::Pattern::ticksPerQuarterNote;
    pattern.hitCount = 4;
    pattern.hits[0] = {0, 30};
    pattern.hits[1] = {120, 240};
    pattern.hits[2] = {240, 2880};
    pattern.hits[3] = {959, 1};
    CHECK(lps::patternIsValid(pattern));
    auto maximumCycle = pattern;
    maximumCycle.cycleLengthTicks = lps::Pattern::maximumCycleLengthTicks;
    CHECK(lps::patternIsValid(maximumCycle));

    auto invalid = pattern;
    invalid.cycleLengthTicks = 0;
    CHECK(!lps::patternIsValid(invalid));
    invalid = pattern;
    invalid.cycleLengthTicks = lps::Pattern::maximumCycleLengthTicks + 1;
    CHECK(!lps::patternIsValid(invalid));
    invalid = pattern;
    invalid.hitCount = lps::Pattern::maximumHitCount + 1;
    CHECK(!lps::patternIsValid(invalid));
    invalid = pattern;
    invalid.hits[1].durationTicks = 0;
    CHECK(!lps::patternIsValid(invalid));
    invalid = pattern;
    invalid.hits[1].startTick = invalid.hits[0].startTick;
    CHECK(!lps::patternIsValid(invalid));
    invalid = pattern;
    invalid.hits[2].startTick = 100;
    CHECK(!lps::patternIsValid(invalid));
    invalid = pattern;
    invalid.hits[3].startTick = pattern.cycleLengthTicks;
    CHECK(!lps::patternIsValid(invalid));
}

void testEqualityIgnoresUnusedStorageAndUsesAllMeaningfulFields()
{
    const auto clean = legacyPattern("x--x-");
    auto dirtyTail = clean;
    dirtyTail.hits[dirtyTail.hitCount] = {777, 888};
    dirtyTail.hits.back() = {999, 111};
    CHECK(lps::patternsEqual(clean, dirtyTail));

    auto changed = clean;
    ++changed.hits[0].durationTicks;
    CHECK(!lps::patternsEqual(clean, changed));
    changed = clean;
    ++changed.hits[1].startTick;
    CHECK(!lps::patternsEqual(clean, changed));
    changed = clean;
    ++changed.cycleLengthTicks;
    CHECK(!lps::patternsEqual(clean, changed));
}

void testDeduplicationAndStableStorage()
{
    lps::PatternLibrary library;
    auto pattern = legacyPattern("-x-x-");
    const auto inserted = library.addOrFind(
        "Custom", pattern,
        [](lps::PatternLibraryEntry& entry)
        {
            entry.originVoiceName = "BD1";
            return true;
        });
    CHECK(inserted.inserted);
    CHECK(inserted.entry != nullptr);
    CHECK(inserted.entry->id == lps::PatternId {2});

    pattern.hits[pattern.hitCount] = {900, 3};
    const auto duplicate = library.addOrFind("Other", pattern);
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == inserted.entry);
    CHECK(duplicate.entry->originVoiceName.has_value());
    CHECK(*duplicate.entry->originVoiceName == "BD1");

    const auto* stable = inserted.entry;
    CHECK(library.addOrFind("Second", legacyPattern("--x--x-")).inserted);
    CHECK(library.recordAt(lps::PatternLibrary::builtInCount) == stable);
}

void testMaximumHitCapacity()
{
    lps::Pattern pattern;
    pattern.cycleLengthTicks = lps::Pattern::maximumCycleLengthTicks;
    pattern.hitCount = lps::Pattern::maximumHitCount;
    for (std::size_t index = 0; index < pattern.hitCount; ++index)
        pattern.hits[index] = {
            static_cast<lps::PatternTick>(index),
            static_cast<lps::PatternTick>(index + 1)};
    CHECK(lps::patternIsValid(pattern));

    lps::PatternLibrary library;
    const auto result = library.addOrFind("Dense", pattern);
    CHECK(result.inserted);
    CHECK(result.entry->pattern.hitCount == lps::Pattern::maximumHitCount);
}

void testStartupReplacementIsAtomic()
{
    lps::PatternLibrary library;
    const std::vector<lps::PatternLibraryEntry> valid {
        {lps::PatternId {1}, "First", legacyPattern("x--x")},
        {lps::PatternId {2}, "Second", legacyPattern("-x-x-")}
    };
    CHECK(library.replaceEntriesForStartup(valid));
    CHECK(library.size() == valid.size());

    auto invalid = legacyPattern("x-");
    invalid.hits[0].durationTicks = 0;
    CHECK(!library.replaceEntriesForStartup({
        {lps::PatternId {1}, "Invalid", invalid}}));
    CHECK(library.size() == valid.size());
    CHECK(library.recordAt(0)->name == "First");

    CHECK(!library.replaceEntriesForStartup({
        {lps::PatternId {1}, "A", legacyPattern("x-")},
        {lps::PatternId {1}, "B", legacyPattern("-x")}}));
    CHECK(!library.replaceEntriesForStartup({
        {lps::PatternId {1}, "A", legacyPattern("x-")},
        {lps::PatternId {2}, "B", legacyPattern("x-")}}));
}

void testPersistenceCallbackControlsPublication()
{
    lps::PatternLibrary library;
    const auto initialSize = library.size();
    const auto pattern = legacyPattern("x-x--x-");
    bool called = false;
    const auto rejected = library.addOrFind(
        "Rejected", pattern,
        [&](lps::PatternLibraryEntry& candidate)
        {
            called = true;
            CHECK(candidate.id == lps::PatternId {initialSize + 1});
            return false;
        });
    CHECK(called);
    CHECK(rejected.entry == nullptr);
    CHECK(library.size() == initialSize);

    const auto inserted = library.addOrFind(
        "Accepted", pattern,
        [](lps::PatternLibraryEntry&) { return true; });
    CHECK(inserted.inserted);
    CHECK(library.size() == initialSize + 1);
}
} // namespace

int main()
{
    testDefaultIsAnEmptyFourBarPattern();
    testValidationAndOrdering();
    testEqualityIgnoresUnusedStorageAndUsesAllMeaningfulFields();
    testDeduplicationAndStableStorage();
    testMaximumHitCapacity();
    testStartupReplacementIsAtomic();
    testPersistenceCallbackControlsPublication();
    std::cout << "PatternLibrary tests passed\n";
    return EXIT_SUCCESS;
}
