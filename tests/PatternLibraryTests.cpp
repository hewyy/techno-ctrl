#include "core/PatternLibrary.h"

#include <array>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
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

struct ExpectedPattern
{
    std::uint64_t id;
    std::string_view name;
    std::string_view steps;
};

constexpr std::array expectedPatterns {
    ExpectedPattern {1, "Basic Kick", "x---"},
    ExpectedPattern {2, "All Steps", "xxxx"},
    ExpectedPattern {3, "Backbeat", "----x-------x---"},
    ExpectedPattern {4, "Offbeat Hats", "--x---x-"},
    ExpectedPattern {5, "Tresillo", "x--x--x-"},
    ExpectedPattern {6, "3-Step Pulse", "x--"},
    ExpectedPattern {7, "5-Step Pulse", "x----"},
    ExpectedPattern {8, "7-Step Pulse", "x------"},
    ExpectedPattern {9, "9-Step Pulse", "x--------"},
    ExpectedPattern {10, "Euclidean 5/12", "x-x--x-x--x-"}
};

static_assert(expectedPatterns.size() == lps::PatternLibrary::builtInCount);
static_assert(lps::PatternLibrary::maxEntryCount > lps::PatternLibrary::builtInCount);

lps::Pattern makePattern(std::string_view steps)
{
    lps::Pattern pattern;
    pattern.length = steps.size();

    for (std::size_t step = 0; step < steps.size() && step < lps::Pattern::maxLength; ++step)
        pattern.hits[step] = steps[step] == 'x';

    return pattern;
}

void checkPattern(const lps::Pattern& pattern, std::string_view expectedSteps)
{
    CHECK(pattern.length == expectedSteps.size());
    CHECK(pattern.length <= lps::Pattern::maxLength);

    for (std::size_t step = 0; step < lps::Pattern::maxLength; ++step)
    {
        const bool expectedHit = step < expectedSteps.size() && expectedSteps[step] == 'x';
        CHECK(pattern.hits[step] == expectedHit);
    }
}

void testBuiltInsHaveStableOrderAndContent()
{
    const lps::PatternLibrary library;
    const lps::PatternLibrary anotherLibrary;
    CHECK(library.size() == expectedPatterns.size());

    for (std::size_t index = 0; index < expectedPatterns.size(); ++index)
    {
        const auto& expected = expectedPatterns[index];
        const auto* entry = library.recordAt(index);
        const auto* duplicate = anotherLibrary.recordAt(index);

        CHECK(entry != nullptr);
        CHECK(duplicate != nullptr);
        CHECK(entry->id == lps::PatternId {expected.id});
        CHECK(entry->id.isValid());
        CHECK(entry->name == expected.name);
        CHECK(duplicate->id == entry->id);
        checkPattern(entry->pattern, expected.steps);
    }
}

void testLookupAndReverseLookup()
{
    const lps::PatternLibrary library;

    for (std::size_t index = 0; index < expectedPatterns.size(); ++index)
    {
        const auto id = lps::PatternId {expectedPatterns[index].id};
        CHECK(library.find(id) == library.recordAt(index));

        const auto foundIndex = library.indexOf(id);
        CHECK(foundIndex.has_value());
        CHECK(*foundIndex == index);
    }
}

void testInvalidLookup()
{
    const lps::PatternLibrary library;
    const lps::PatternId invalid;

    CHECK(!invalid.isValid());
    CHECK(invalid.value() == 0);
    CHECK(library.recordAt(library.size()) == nullptr);
    CHECK(library.find(invalid) == nullptr);
    CHECK(library.find(lps::PatternId {999}) == nullptr);
    CHECK(!library.indexOf(invalid).has_value());
    CHECK(!library.indexOf(lps::PatternId {999}).has_value());
}

void testPatternEqualityUsesLengthAndMeaningfulHitsOnly()
{
    auto clean = makePattern("x--x-");
    auto dirtyTail = clean;
    dirtyTail.hits[clean.length] = true;
    dirtyTail.hits[lps::Pattern::maxLength - 1] = true;

    CHECK(lps::patternsEqual(clean, dirtyTail));

    auto differentHit = clean;
    differentHit.hits[1] = true;
    CHECK(!lps::patternsEqual(clean, differentHit));

    auto differentLength = clean;
    ++differentLength.length;
    CHECK(!lps::patternsEqual(clean, differentLength));
}

void testDuplicateContentReturnsExistingEntry()
{
    lps::PatternLibrary library;
    auto duplicate = makePattern("x---");
    duplicate.hits[duplicate.length] = true;

    CHECK(library.findEquivalent(duplicate) == library.recordAt(0));

    const auto result = library.addOrFind({}, duplicate);
    CHECK(result.entry == library.recordAt(0));
    CHECK(!result.inserted);
    CHECK(library.size() == lps::PatternLibrary::builtInCount);
}

void testInsertionNormalizesTailAndPreservesStableOrder()
{
    lps::PatternLibrary library;
    auto pattern = makePattern("x-x--x");

    for (std::size_t step = pattern.length; step < lps::Pattern::maxLength; ++step)
        pattern.hits[step] = true;

    const auto result = library.addOrFind("Six Step", pattern);
    CHECK(result.inserted);
    CHECK(result.entry != nullptr);
    CHECK(result.entry == library.recordAt(lps::PatternLibrary::builtInCount));
    CHECK(result.entry->id == lps::PatternId {11});
    CHECK(result.entry->name == "Six Step");
    CHECK(result.entry->pattern.length == 6);

    for (std::size_t step = result.entry->pattern.length;
         step < lps::Pattern::maxLength;
         ++step)
    {
        CHECK(!result.entry->pattern.hits[step]);
    }

    CHECK(library.find(lps::PatternId {11}) == result.entry);
    CHECK(library.indexOf(lps::PatternId {11}) == lps::PatternLibrary::builtInCount);
    CHECK(library.findEquivalent(pattern) == result.entry);

    const auto duplicate = library.addOrFind("Different Name", makePattern("x-x--x"));
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == result.entry);
    CHECK(duplicate.entry->name == "Six Step");
    CHECK(library.size() == lps::PatternLibrary::builtInCount + 1);

    const auto* stableAddress = result.entry;
    const auto second = library.addOrFind("Two Step", makePattern("x-"));
    CHECK(second.inserted);
    CHECK(second.entry != nullptr);
    CHECK(second.entry->id == lps::PatternId {12});
    CHECK(library.recordAt(lps::PatternLibrary::builtInCount) == stableAddress);
    CHECK(stableAddress->name == "Six Step");
}

void testSameHitsWithDifferentLengthsAreDistinct()
{
    lps::PatternLibrary library;
    const auto shortResult = library.addOrFind("Short", makePattern("-x"));
    const auto longResult = library.addOrFind("Long", makePattern("-x-"));

    CHECK(shortResult.inserted);
    CHECK(longResult.inserted);
    CHECK(shortResult.entry != nullptr);
    CHECK(longResult.entry != nullptr);
    CHECK(shortResult.entry != longResult.entry);
    CHECK(shortResult.entry->pattern.length == 2);
    CHECK(longResult.entry->pattern.length == 3);
}

void testUnnamedPatternsAreAcceptedAndInvalidPatternsAreRejected()
{
    lps::PatternLibrary library;
    const auto initialSize = library.size();

    const auto unnamed = library.addOrFind(makePattern("-x"));
    CHECK(unnamed.inserted);
    CHECK(unnamed.entry != nullptr);
    CHECK(unnamed.entry->name.empty());
    CHECK(unnamed.entry->id == lps::PatternId {11});

    lps::Pattern empty;
    CHECK(library.addOrFind("Empty", empty).entry == nullptr);

    auto tooLong = makePattern("x");
    tooLong.length = lps::Pattern::maxLength + 1;
    CHECK(library.addOrFind("Too Long", tooLong).entry == nullptr);
    CHECK(library.findEquivalent(tooLong) == nullptr);
    CHECK(library.size() == initialSize + 1);
}

void testCapacityAndDuplicateLookupWhenFull()
{
    lps::PatternLibrary library;
    const lps::PatternLibraryEntry* firstInserted = nullptr;

    for (std::size_t serial = 0;
         serial < lps::PatternLibrary::maxEntryCount - lps::PatternLibrary::builtInCount;
         ++serial)
    {
        lps::Pattern pattern;
        pattern.length = lps::Pattern::maxLength;
        pattern.hits[lps::Pattern::maxLength - 1] = true;

        for (std::size_t bit = 0; bit < 8; ++bit)
            pattern.hits[bit] = (serial & (std::size_t {1} << bit)) != 0;

        const auto result = library.addOrFind("Generated " + std::to_string(serial), pattern);
        CHECK(result.inserted);
        CHECK(result.entry != nullptr);
        CHECK(result.entry->id
              == lps::PatternId {lps::PatternLibrary::builtInCount + serial + 1});

        if (serial == 0)
            firstInserted = result.entry;
    }

    CHECK(library.size() == lps::PatternLibrary::maxEntryCount);
    CHECK(library.recordAt(lps::PatternLibrary::maxEntryCount) == nullptr);
    CHECK(firstInserted != nullptr);
    CHECK(firstInserted == library.recordAt(lps::PatternLibrary::builtInCount));

    const auto duplicate = library.addOrFind({}, firstInserted->pattern);
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == firstInserted);

    lps::Pattern newPattern;
    newPattern.length = lps::Pattern::maxLength;
    newPattern.hits[lps::Pattern::maxLength - 2] = true;
    const auto fullResult = library.addOrFind("Cannot Fit", newPattern);
    CHECK(!fullResult.inserted);
    CHECK(fullResult.entry == nullptr);
}

void testStartupReplacementPublishesValidatedCatalogInFileOrder()
{
    lps::PatternLibrary library;
    auto firstPattern = makePattern("x--x-x");
    firstPattern.hits[firstPattern.length] = true;
    firstPattern.hits[lps::Pattern::maxLength - 1] = true;

    const std::vector<lps::PatternLibraryEntry> loadedEntries {
        {lps::PatternId {1}, "Loaded First", firstPattern},
        {lps::PatternId {2}, {}, makePattern("-x-x-")},
        {lps::PatternId {3}, "Loaded 32 Steps",
            makePattern("x------------------------------x")}
    };

    CHECK(library.replaceEntriesForStartup(loadedEntries));
    CHECK(library.size() == loadedEntries.size());

    for (std::size_t index = 0; index < loadedEntries.size(); ++index)
    {
        const auto* entry = library.recordAt(index);
        CHECK(entry != nullptr);
        CHECK(entry->id == loadedEntries[index].id);
        CHECK(entry->name == loadedEntries[index].name);
        CHECK(lps::patternsEqual(entry->pattern, loadedEntries[index].pattern));
        CHECK(library.find(entry->id) == entry);
        CHECK(library.indexOf(entry->id) == index);
    }

    const auto* first = library.recordAt(0);
    CHECK(first != nullptr);
    for (std::size_t step = first->pattern.length;
         step < lps::Pattern::maxLength;
         ++step)
    {
        CHECK(!first->pattern.hits[step]);
    }

    const auto appended = library.addOrFind("Loaded Next", makePattern("--x--x-"));
    CHECK(appended.inserted);
    CHECK(appended.entry != nullptr);
    CHECK(appended.entry->id == lps::PatternId {loadedEntries.size() + 1});
}

void checkStartupReplacementRejected(
    const std::vector<lps::PatternLibraryEntry>& loadedEntries)
{
    lps::PatternLibrary library;
    const auto* originalFirst = library.recordAt(0);
    CHECK(originalFirst != nullptr);
    const auto originalFirstId = originalFirst->id;
    const auto originalFirstName = originalFirst->name;
    const auto originalFirstPattern = originalFirst->pattern;

    CHECK(!library.replaceEntriesForStartup(loadedEntries));
    CHECK(library.size() == lps::PatternLibrary::builtInCount);

    const auto* firstAfterRejection = library.recordAt(0);
    CHECK(firstAfterRejection == originalFirst);
    CHECK(firstAfterRejection->id == originalFirstId);
    CHECK(firstAfterRejection->name == originalFirstName);
    CHECK(lps::patternsEqual(firstAfterRejection->pattern, originalFirstPattern));
}

void testStartupReplacementRejectsInvalidCatalogAtomically()
{
    checkStartupReplacementRejected({});

    checkStartupReplacementRejected({
        {lps::PatternId {}, "Invalid ID", makePattern("x-")}
    });

    checkStartupReplacementRejected({
        {lps::PatternId {1}, "First", makePattern("x-")},
        {lps::PatternId {1}, "Duplicate ID", makePattern("-x")}
    });

    checkStartupReplacementRejected({
        {lps::PatternId {1}, "Empty Pattern", {}}
    });

    auto tooLong = makePattern("x");
    tooLong.length = lps::Pattern::maxLength + 1;
    checkStartupReplacementRejected({
        {lps::PatternId {1}, "Too Long", tooLong}
    });

    checkStartupReplacementRejected({
        {lps::PatternId {1}, "First", makePattern("x--x")},
        {lps::PatternId {2}, "Duplicate Content", makePattern("x--x")}
    });

    std::vector<lps::PatternLibraryEntry> tooManyEntries;
    tooManyEntries.reserve(lps::PatternLibrary::maxEntryCount + 1);
    for (std::size_t index = 0;
         index <= lps::PatternLibrary::maxEntryCount;
         ++index)
    {
        lps::Pattern pattern;
        pattern.length = lps::Pattern::maxLength;
        for (std::size_t bit = 0; bit < 9; ++bit)
            pattern.hits[bit] = (index & (std::size_t {1} << bit)) != 0;

        tooManyEntries.push_back({
            lps::PatternId {index + 1},
            "Loaded " + std::to_string(index),
            pattern
        });
    }
    checkStartupReplacementRejected(tooManyEntries);
}

void testInsertionIsPublishedOnlyAfterPersistenceCallbackSucceeds()
{
    lps::PatternLibrary library;
    const auto initialSize = library.size();
    const auto pattern = makePattern("x-x--x-");
    bool rejectionCallbackCalled = false;

    const auto rejected = library.addOrFind(
        "Not Persisted", pattern,
        [&](lps::PatternLibraryEntry& candidate)
        {
            rejectionCallbackCalled = true;
            CHECK(candidate.id == lps::PatternId {initialSize + 1});
            CHECK(candidate.name == "Not Persisted");
            CHECK(lps::patternsEqual(candidate.pattern, pattern));
            CHECK(library.size() == initialSize);
            CHECK(library.find(candidate.id) == nullptr);
            return false;
        });

    CHECK(rejectionCallbackCalled);
    CHECK(rejected.entry == nullptr);
    CHECK(!rejected.inserted);
    CHECK(library.size() == initialSize);
    CHECK(library.findEquivalent(pattern) == nullptr);

    bool successCallbackCalled = false;
    const auto inserted = library.addOrFind(
        "Persisted", pattern,
        [&](lps::PatternLibraryEntry& candidate)
        {
            successCallbackCalled = true;
            CHECK(candidate.id == lps::PatternId {initialSize + 1});
            CHECK(candidate.name == "Persisted");
            CHECK(library.size() == initialSize);
            CHECK(library.find(candidate.id) == nullptr);
            return true;
        });

    CHECK(successCallbackCalled);
    CHECK(inserted.inserted);
    CHECK(inserted.entry != nullptr);
    CHECK(inserted.entry == library.recordAt(initialSize));
    CHECK(library.size() == initialSize + 1);

    bool duplicateCallbackCalled = false;
    const auto duplicate = library.addOrFind(
        "Duplicate", pattern,
        [&](lps::PatternLibraryEntry&)
        {
            duplicateCallbackCalled = true;
            return true;
        });
    CHECK(!duplicateCallbackCalled);
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == inserted.entry);
}
} // namespace

int main()
{
    testBuiltInsHaveStableOrderAndContent();
    testLookupAndReverseLookup();
    testInvalidLookup();
    testPatternEqualityUsesLengthAndMeaningfulHitsOnly();
    testDuplicateContentReturnsExistingEntry();
    testInsertionNormalizesTailAndPreservesStableOrder();
    testSameHitsWithDifferentLengthsAreDistinct();
    testUnnamedPatternsAreAcceptedAndInvalidPatternsAreRejected();
    testCapacityAndDuplicateLookupWhenFull();
    testStartupReplacementPublishesValidatedCatalogInFileOrder();
    testStartupReplacementRejectsInvalidCatalogAtomically();
    testInsertionIsPublishedOnlyAfterPersistenceCallbackSucceeds();

    std::cout << "PatternLibrary tests passed\n";
    return EXIT_SUCCESS;
}
