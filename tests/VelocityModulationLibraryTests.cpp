#include "core/VelocityModulationLibrary.h"

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line
              << ": test assertion failed: " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

lps::VelocityModulation makeModulation(
    std::initializer_list<std::uint8_t> values)
{
    lps::VelocityModulation modulation;
    modulation.length = values.size();

    std::size_t step = 0;
    for (const auto value : values)
    {
        if (step >= lps::VelocityModulation::maxLength)
            break;
        modulation.values[step++] = value;
    }

    return modulation;
}

void checkModulation(
    const lps::VelocityModulation& modulation,
    std::initializer_list<std::uint8_t> expectedValues)
{
    CHECK(modulation.length == expectedValues.size());
    CHECK(modulation.length <= lps::VelocityModulation::maxLength);

    auto expected = expectedValues.begin();
    for (std::size_t step = 0; step < lps::VelocityModulation::maxLength; ++step)
    {
        const auto expectedValue = step < expectedValues.size()
            ? *expected++
            : std::uint8_t {0};
        CHECK(modulation.values[step] == expectedValue);
    }
}

void testBuiltInsHaveStableOrderAndContent()
{
    const lps::VelocityModulationLibrary library;
    const lps::VelocityModulationLibrary anotherLibrary;

    static_assert(lps::VelocityModulationLibrary::builtInCount == 2);
    static_assert(
        lps::VelocityModulationLibrary::maxEntryCount
        > lps::VelocityModulationLibrary::builtInCount);
    CHECK(library.size() == lps::VelocityModulationLibrary::builtInCount);

    const auto* steady = library.recordAt(0);
    const auto* fourStep = library.recordAt(1);
    CHECK(steady != nullptr);
    CHECK(fourStep != nullptr);
    CHECK(steady->id == lps::VelocityModulationId {1});
    CHECK(steady->id.isValid());
    CHECK(steady->name == "Steady 100");
    checkModulation(steady->modulation, {201});
    CHECK(fourStep->id == lps::VelocityModulationId {2});
    CHECK(fourStep->name == "Four-Step");
    checkModulation(fourStep->modulation, {255, 100, 225, 150});

    for (std::size_t index = 0; index < library.size(); ++index)
    {
        const auto* duplicate = anotherLibrary.recordAt(index);
        CHECK(duplicate != nullptr);
        CHECK(duplicate->id == library.recordAt(index)->id);
        CHECK(duplicate->name == library.recordAt(index)->name);
        CHECK(lps::velocityModulationsEqual(
            duplicate->modulation,
            library.recordAt(index)->modulation));
    }
}

void testLookupAndInvalidLookup()
{
    const lps::VelocityModulationLibrary library;

    for (std::size_t index = 0; index < library.size(); ++index)
    {
        const auto id = lps::VelocityModulationId {index + 1};
        CHECK(library.find(id) == library.recordAt(index));
        CHECK(library.indexOf(id) == index);
    }

    const lps::VelocityModulationId invalid;
    CHECK(!invalid.isValid());
    CHECK(invalid.value() == 0);
    CHECK(library.recordAt(library.size()) == nullptr);
    CHECK(library.find(invalid) == nullptr);
    CHECK(library.find(lps::VelocityModulationId {999}) == nullptr);
    CHECK(!library.indexOf(invalid).has_value());
    CHECK(!library.indexOf(lps::VelocityModulationId {999}).has_value());
}

void testEqualityUsesLengthAndMeaningfulPrefixOnly()
{
    auto clean = makeModulation({255, 100, 225, 150});
    auto dirtyTail = clean;
    dirtyTail.values[clean.length] = 17;
    dirtyTail.values[lps::VelocityModulation::maxLength - 1] = 91;

    CHECK(lps::velocityModulationsEqual(clean, dirtyTail));

    auto differentValue = clean;
    differentValue.values[1] = 101;
    CHECK(!lps::velocityModulationsEqual(clean, differentValue));

    auto differentLength = clean;
    ++differentLength.length;
    CHECK(!lps::velocityModulationsEqual(clean, differentLength));

    auto invalidLeft = clean;
    auto invalidRight = clean;
    invalidLeft.length = lps::VelocityModulation::maxLength + 1;
    invalidRight.length = invalidLeft.length;
    CHECK(!lps::velocityModulationsEqual(invalidLeft, invalidRight));
}

void testDuplicateContentReturnsExistingEntry()
{
    lps::VelocityModulationLibrary library;
    auto duplicate = makeModulation({201});
    duplicate.values[duplicate.length] = 255;

    CHECK(library.findEquivalent(duplicate) == library.recordAt(0));

    const auto result = library.addOrFind({}, duplicate);
    CHECK(result.entry == library.recordAt(0));
    CHECK(!result.inserted);
    CHECK(library.size() == lps::VelocityModulationLibrary::builtInCount);
}

void testInsertionNormalizesTailAndPreservesStableOrder()
{
    lps::VelocityModulationLibrary library;
    auto modulation = makeModulation({1, 2, 3, 4, 5, 6});
    for (std::size_t step = modulation.length;
         step < lps::VelocityModulation::maxLength;
         ++step)
    {
        modulation.values[step] = 255;
    }

    const auto result = library.addOrFind("Six Step", modulation);
    CHECK(result.inserted);
    CHECK(result.entry != nullptr);
    CHECK(result.entry
          == library.recordAt(lps::VelocityModulationLibrary::builtInCount));
    CHECK(result.entry->id == lps::VelocityModulationId {3});
    CHECK(result.entry->name == "Six Step");
    checkModulation(result.entry->modulation, {1, 2, 3, 4, 5, 6});
    CHECK(library.find(lps::VelocityModulationId {3}) == result.entry);
    CHECK(library.indexOf(lps::VelocityModulationId {3})
          == lps::VelocityModulationLibrary::builtInCount);
    CHECK(library.findEquivalent(modulation) == result.entry);

    const auto duplicate = library.addOrFind(
        "Different Name", makeModulation({1, 2, 3, 4, 5, 6}));
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == result.entry);
    CHECK(duplicate.entry->name == "Six Step");

    const auto* stableAddress = result.entry;
    const auto second = library.addOrFind(
        "Two Step", makeModulation({31, 63}));
    CHECK(second.inserted);
    CHECK(second.entry != nullptr);
    CHECK(second.entry->id == lps::VelocityModulationId {4});
    CHECK(library.recordAt(lps::VelocityModulationLibrary::builtInCount)
          == stableAddress);
    CHECK(stableAddress->name == "Six Step");
}

void testSameValuesWithDifferentLengthsAreDistinct()
{
    lps::VelocityModulationLibrary library;
    const auto shortResult = library.addOrFind(
        "Short", makeModulation({42}));
    const auto longResult = library.addOrFind(
        "Long", makeModulation({42, 0}));

    CHECK(shortResult.inserted);
    CHECK(longResult.inserted);
    CHECK(shortResult.entry != nullptr);
    CHECK(longResult.entry != nullptr);
    CHECK(shortResult.entry != longResult.entry);
    CHECK(shortResult.entry->modulation.length == 1);
    CHECK(longResult.entry->modulation.length == 2);
}

void testInvalidNewModulationsAreRejected()
{
    lps::VelocityModulationLibrary library;
    const auto initialSize = library.size();

    CHECK(library.addOrFind({}, makeModulation({1, 2})).entry == nullptr);

    lps::VelocityModulation empty;
    CHECK(library.addOrFind("Empty", empty).entry == nullptr);

    auto tooLong = makeModulation({1});
    tooLong.length = lps::VelocityModulation::maxLength + 1;
    CHECK(library.addOrFind("Too Long", tooLong).entry == nullptr);
    CHECK(library.findEquivalent(tooLong) == nullptr);
    CHECK(library.size() == initialSize);
}

void testCapacityAndDuplicateLookupWhenFull()
{
    lps::VelocityModulationLibrary library;
    const lps::VelocityModulationLibraryEntry* firstInserted = nullptr;

    for (std::size_t serial = 0;
         serial < lps::VelocityModulationLibrary::maxEntryCount
             - lps::VelocityModulationLibrary::builtInCount;
         ++serial)
    {
        lps::VelocityModulation modulation;
        modulation.length = lps::VelocityModulation::maxLength;
        modulation.values[0] = static_cast<std::uint8_t>(serial & 0xffu);
        modulation.values[1] = static_cast<std::uint8_t>((serial >> 8u) & 0xffu);
        modulation.values[lps::VelocityModulation::maxLength - 1] = 17;

        const auto result = library.addOrFind(
            "Generated " + std::to_string(serial), modulation);
        CHECK(result.inserted);
        CHECK(result.entry != nullptr);
        CHECK(result.entry->id == lps::VelocityModulationId {
            lps::VelocityModulationLibrary::builtInCount + serial + 1});

        if (serial == 0)
            firstInserted = result.entry;
    }

    CHECK(library.size() == lps::VelocityModulationLibrary::maxEntryCount);
    CHECK(library.recordAt(lps::VelocityModulationLibrary::maxEntryCount) == nullptr);
    CHECK(firstInserted != nullptr);
    CHECK(firstInserted
          == library.recordAt(lps::VelocityModulationLibrary::builtInCount));

    const auto duplicate = library.addOrFind({}, firstInserted->modulation);
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == firstInserted);

    lps::VelocityModulation newModulation;
    newModulation.length = lps::VelocityModulation::maxLength;
    newModulation.values[0] = 254;
    newModulation.values[lps::VelocityModulation::maxLength - 1] = 17;
    const auto fullResult = library.addOrFind("Cannot Fit", newModulation);
    CHECK(!fullResult.inserted);
    CHECK(fullResult.entry == nullptr);
}

void testStartupReplacementPublishesValidatedCatalogInFileOrder()
{
    lps::VelocityModulationLibrary library;
    auto firstModulation = makeModulation({9, 8, 7});
    firstModulation.values[firstModulation.length] = 55;
    firstModulation.values[lps::VelocityModulation::maxLength - 1] = 99;

    const std::vector<lps::VelocityModulationLibraryEntry> loadedEntries {
        {lps::VelocityModulationId {5}, "Loaded First", firstModulation},
        {lps::VelocityModulationId {2}, "Loaded Second", makeModulation({3, 1})},
        {lps::VelocityModulationId {256}, "Loaded Ten Steps",
            makeModulation({0, 25, 50, 75, 100, 125, 150, 175, 200, 255})}
    };

    CHECK(library.replaceEntriesForStartup(loadedEntries));
    CHECK(library.size() == loadedEntries.size());

    for (std::size_t index = 0; index < loadedEntries.size(); ++index)
    {
        const auto* entry = library.recordAt(index);
        CHECK(entry != nullptr);
        CHECK(entry->id == loadedEntries[index].id);
        CHECK(entry->name == loadedEntries[index].name);
        CHECK(lps::velocityModulationsEqual(
            entry->modulation, loadedEntries[index].modulation));
        CHECK(library.find(entry->id) == entry);
        CHECK(library.indexOf(entry->id) == index);
    }

    checkModulation(library.recordAt(0)->modulation, {9, 8, 7});

    const auto appended = library.addOrFind(
        "Loaded Next", makeModulation({11, 22, 33, 44}));
    CHECK(appended.inserted);
    CHECK(appended.entry != nullptr);
    CHECK(appended.entry->id == lps::VelocityModulationId {1});
}

void checkStartupReplacementRejected(
    const std::vector<lps::VelocityModulationLibraryEntry>& loadedEntries)
{
    lps::VelocityModulationLibrary library;
    const auto* originalFirst = library.recordAt(0);
    CHECK(originalFirst != nullptr);
    const auto originalFirstId = originalFirst->id;
    const auto originalFirstName = originalFirst->name;
    const auto originalFirstModulation = originalFirst->modulation;

    CHECK(!library.replaceEntriesForStartup(loadedEntries));
    CHECK(library.size() == lps::VelocityModulationLibrary::builtInCount);

    const auto* firstAfterRejection = library.recordAt(0);
    CHECK(firstAfterRejection == originalFirst);
    CHECK(firstAfterRejection->id == originalFirstId);
    CHECK(firstAfterRejection->name == originalFirstName);
    CHECK(lps::velocityModulationsEqual(
        firstAfterRejection->modulation,
        originalFirstModulation));
}

void testStartupReplacementRejectsInvalidCatalogAtomically()
{
    checkStartupReplacementRejected({});

    checkStartupReplacementRejected({
        {lps::VelocityModulationId {}, "Invalid ID", makeModulation({1})}
    });

    checkStartupReplacementRejected({
        {lps::VelocityModulationId {257}, "Oversized ID", makeModulation({1})}
    });

    checkStartupReplacementRejected({
        {lps::VelocityModulationId {1}, "First", makeModulation({1})},
        {lps::VelocityModulationId {1}, "Duplicate ID", makeModulation({2})}
    });

    checkStartupReplacementRejected({
        {lps::VelocityModulationId {1}, "", makeModulation({1})}
    });

    checkStartupReplacementRejected({
        {lps::VelocityModulationId {1}, "Empty Modulation", {}}
    });

    auto tooLong = makeModulation({1});
    tooLong.length = lps::VelocityModulation::maxLength + 1;
    checkStartupReplacementRejected({
        {lps::VelocityModulationId {1}, "Too Long", tooLong}
    });

    auto dirtyDuplicate = makeModulation({1, 2, 3});
    dirtyDuplicate.values[dirtyDuplicate.length] = 99;
    checkStartupReplacementRejected({
        {lps::VelocityModulationId {1}, "First", makeModulation({1, 2, 3})},
        {lps::VelocityModulationId {2}, "Duplicate Content", dirtyDuplicate}
    });

    std::vector<lps::VelocityModulationLibraryEntry> tooManyEntries;
    tooManyEntries.reserve(lps::VelocityModulationLibrary::maxEntryCount + 1);
    for (std::size_t index = 0;
         index <= lps::VelocityModulationLibrary::maxEntryCount;
         ++index)
    {
        lps::VelocityModulation modulation;
        modulation.length = lps::VelocityModulation::maxLength;
        modulation.values[0] = static_cast<std::uint8_t>(index & 0xffu);
        modulation.values[1] = static_cast<std::uint8_t>((index >> 8u) & 0xffu);
        tooManyEntries.push_back({
            lps::VelocityModulationId {index + 1},
            "Loaded " + std::to_string(index),
            modulation
        });
    }
    checkStartupReplacementRejected(tooManyEntries);
}

void testInsertionIsPublishedOnlyAfterPersistenceCallbackSucceeds()
{
    lps::VelocityModulationLibrary library;
    const auto initialSize = library.size();
    auto modulation = makeModulation({12, 34, 56});
    modulation.values[modulation.length] = 255;
    bool rejectionCallbackCalled = false;

    const auto rejected = library.addOrFind(
        "Not Persisted", modulation,
        [&](lps::VelocityModulationLibraryEntry& candidate)
        {
            rejectionCallbackCalled = true;
            CHECK(candidate.id == lps::VelocityModulationId {initialSize + 1});
            CHECK(candidate.name == "Not Persisted");
            checkModulation(candidate.modulation, {12, 34, 56});
            CHECK(library.size() == initialSize);
            CHECK(library.find(candidate.id) == nullptr);
            return false;
        });

    CHECK(rejectionCallbackCalled);
    CHECK(rejected.entry == nullptr);
    CHECK(!rejected.inserted);
    CHECK(library.size() == initialSize);
    CHECK(library.findEquivalent(modulation) == nullptr);

    bool successCallbackCalled = false;
    const auto inserted = library.addOrFind(
        "Persisted", modulation,
        [&](lps::VelocityModulationLibraryEntry& candidate)
        {
            successCallbackCalled = true;
            CHECK(library.size() == initialSize);
            CHECK(library.find(candidate.id) == nullptr);
            candidate.id = lps::VelocityModulationId {200};
            candidate.name = "Persisted With Allocated ID";
            candidate.modulation.values[candidate.modulation.length] = 77;
            return true;
        });

    CHECK(successCallbackCalled);
    CHECK(inserted.inserted);
    CHECK(inserted.entry != nullptr);
    CHECK(inserted.entry == library.recordAt(initialSize));
    CHECK(inserted.entry->id == lps::VelocityModulationId {200});
    CHECK(inserted.entry->name == "Persisted With Allocated ID");
    checkModulation(inserted.entry->modulation, {12, 34, 56});
    CHECK(library.size() == initialSize + 1);

    bool duplicateCallbackCalled = false;
    const auto duplicate = library.addOrFind(
        "Duplicate", modulation,
        [&](lps::VelocityModulationLibraryEntry&)
        {
            duplicateCallbackCalled = true;
            return true;
        });
    CHECK(!duplicateCallbackCalled);
    CHECK(!duplicate.inserted);
    CHECK(duplicate.entry == inserted.entry);
}

void testInvalidCallbackMutationAndExceptionRemainUnpublished()
{
    lps::VelocityModulationLibrary library;
    const auto initialSize = library.size();

    const auto invalid = library.addOrFind(
        "Invalidated", makeModulation({88, 99}),
        [](lps::VelocityModulationLibraryEntry& candidate)
        {
            candidate.id = lps::VelocityModulationId {1};
            return true;
        });
    CHECK(invalid.entry == nullptr);
    CHECK(!invalid.inserted);
    CHECK(library.size() == initialSize);
    CHECK(library.findEquivalent(makeModulation({88, 99})) == nullptr);

    bool threw = false;
    try
    {
        (void) library.addOrFind(
            "Throws", makeModulation({77, 66}),
            [](lps::VelocityModulationLibraryEntry&) -> bool
            {
                throw std::runtime_error("persistence failed");
            });
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    CHECK(threw);
    CHECK(library.size() == initialSize);
    CHECK(library.findEquivalent(makeModulation({77, 66})) == nullptr);

    const auto recovered = library.addOrFind(
        "Recovered", makeModulation({77, 66}));
    CHECK(recovered.inserted);
    CHECK(recovered.entry != nullptr);
    CHECK(recovered.entry->id == lps::VelocityModulationId {initialSize + 1});
}
} // namespace

int main()
{
    testBuiltInsHaveStableOrderAndContent();
    testLookupAndInvalidLookup();
    testEqualityUsesLengthAndMeaningfulPrefixOnly();
    testDuplicateContentReturnsExistingEntry();
    testInsertionNormalizesTailAndPreservesStableOrder();
    testSameValuesWithDifferentLengthsAreDistinct();
    testInvalidNewModulationsAreRejected();
    testCapacityAndDuplicateLookupWhenFull();
    testStartupReplacementPublishesValidatedCatalogInFileOrder();
    testStartupReplacementRejectsInvalidCatalogAtomically();
    testInsertionIsPublishedOnlyAfterPersistenceCallbackSucceeds();
    testInvalidCallbackMutationAndExceptionRemainUnpublished();

    std::cout << "VelocityModulationLibrary tests passed\n";
    return EXIT_SUCCESS;
}
