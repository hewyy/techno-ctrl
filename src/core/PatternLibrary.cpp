#include "core/PatternLibrary.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace lps
{
bool patternIsValid(const Pattern& pattern) noexcept
{
    if (pattern.cycleLengthTicks == 0
        || pattern.cycleLengthTicks > Pattern::maximumCycleLengthTicks
        || pattern.hitCount > Pattern::maximumHitCount)
    {
        return false;
    }

    PatternTick previousStart = 0;
    for (std::size_t index = 0; index < pattern.hitCount; ++index)
    {
        const auto& hit = pattern.hits[index];
        if (hit.startTick >= pattern.cycleLengthTicks
            || hit.durationTicks == 0
            || (index != 0 && hit.startTick <= previousStart))
        {
            return false;
        }
        previousStart = hit.startTick;
    }
    return true;
}

Pattern normalizedPattern(Pattern pattern) noexcept
{
    if (pattern.hitCount <= Pattern::maximumHitCount)
    {
        std::fill(
            pattern.hits.begin() + static_cast<std::ptrdiff_t>(pattern.hitCount),
            pattern.hits.end(),
            PatternHit {});
    }
    return pattern;
}

bool patternsEqual(const Pattern& left, const Pattern& right) noexcept
{
    if (left.cycleLengthTicks != right.cycleLengthTicks
        || left.hitCount != right.hitCount
        || left.hitCount > Pattern::maximumHitCount)
        return false;

    for (std::size_t index = 0; index < left.hitCount; ++index)
    {
        if (!(left.hits[index] == right.hits[index]))
            return false;
    }

    return true;
}

PatternLibrary::PatternLibrary()
{
    Pattern none;
    none.cycleLengthTicks = 8 * Pattern::ticksPerQuarterNote;
    entries_[0] = {PatternId {1}, "None", none};

    publishedEntryCount_.store(builtInCount, std::memory_order_release);
}

const PatternLibraryEntry* PatternLibrary::recordAt(std::size_t index) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);
    return index < publishedCount ? &entries_[index] : nullptr;
}

const PatternLibraryEntry* PatternLibrary::find(PatternId id) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (entries_[index].id == id)
            return &entries_[index];
    }

    return nullptr;
}

std::optional<std::size_t> PatternLibrary::indexOf(PatternId id) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (entries_[index].id == id)
            return index;
    }

    return std::nullopt;
}

const PatternLibraryEntry* PatternLibrary::findEquivalent(const Pattern& pattern) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (patternsEqual(entries_[index].pattern, pattern))
            return &entries_[index];
    }

    return nullptr;
}

bool PatternLibrary::replaceEntriesForStartup(
    const std::vector<PatternLibraryEntry>& entries)
{
    if (entries.empty() || entries.size() > entries_.size())
        return false;

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& candidate = entries[index];
        if (!candidate.id.isValid()
            || candidate.id.value() > maxEntryCount
            || !patternIsValid(candidate.pattern))
        {
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (entries[previous].id == candidate.id
                || patternsEqual(entries[previous].pattern, candidate.pattern))
            {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        entries_[index] = entries[index];
        entries_[index].pattern = normalizedPattern(entries_[index].pattern);
    }

    for (std::size_t index = entries.size(); index < entries_.size(); ++index)
        entries_[index] = {};

    publishedEntryCount_.store(entries.size(), std::memory_order_release);
    return true;
}

PatternId PatternLibrary::nextAvailableId() const noexcept
{
    for (std::uint64_t value = 1; value <= maxEntryCount; ++value)
    {
        const auto candidate = PatternId {value};
        if (find(candidate) == nullptr)
            return candidate;
    }

    return {};
}

PatternLibraryInsertResult PatternLibrary::addOrFind(std::string name,
                                                     const Pattern& pattern,
                                                     const BeforePublish& beforePublish)
{
    if (const auto* existing = findEquivalent(pattern))
        return {existing, false};

    if (!patternIsValid(pattern))
        return {};

    const auto insertionIndex = publishedEntryCount_.load(std::memory_order_acquire);
    if (insertionIndex >= entries_.size())
        return {};

    auto normalized = normalizedPattern(pattern);

    auto& entry = entries_[insertionIndex];
    entry.id = nextAvailableId();
    entry.name = std::move(name);
    entry.pattern = normalized;

    try
    {
        if (beforePublish && !beforePublish(entry))
        {
            entry = {};
            return {};
        }
    }
    catch (...)
    {
        entry = {};
        throw;
    }

    if (!entry.id.isValid()
        || entry.id.value() > maxEntryCount
        || find(entry.id) != nullptr
        || !patternIsValid(entry.pattern)
        || findEquivalent(entry.pattern) != nullptr)
    {
        entry = {};
        return {};
    }

    entry.pattern = normalizedPattern(entry.pattern);

    publishedEntryCount_.store(insertionIndex + 1, std::memory_order_release);
    return {&entry, true};
}

} // namespace lps
