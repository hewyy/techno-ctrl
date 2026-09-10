#include "core/PatternLibrary.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

namespace lps
{
namespace
{
// One character is one player step: 'x' is a hit and '-' is a rest.
Pattern makePattern(std::string_view steps) noexcept
{
    Pattern pattern;
    pattern.length = std::min(steps.size(), Pattern::maxLength);

    for (std::size_t step = 0; step < pattern.length; ++step)
        pattern.hits[step] = steps[step] == 'x';

    return pattern;
}
} // namespace

bool patternsEqual(const Pattern& left, const Pattern& right) noexcept
{
    if (left.length != right.length || left.length > Pattern::maxLength)
        return false;

    for (std::size_t step = 0; step < left.length; ++step)
    {
        if (left.hits[step] != right.hits[step])
            return false;
    }

    return true;
}

PatternLibrary::PatternLibrary()
{
    entries_[0] = {PatternId {1}, "Basic Kick", makePattern("x---")};
    entries_[1] = {PatternId {2}, "All Steps", makePattern("xxxx")};
    entries_[2] = {PatternId {3}, "Backbeat", makePattern("----x-------x---")};
    entries_[3] = {PatternId {4}, "Offbeat Hats", makePattern("--x---x-")};
    entries_[4] = {PatternId {5}, "Tresillo", makePattern("x--x--x-")};
    entries_[5] = {PatternId {6}, "3-Step Pulse", makePattern("x--")};
    entries_[6] = {PatternId {7}, "5-Step Pulse", makePattern("x----")};
    entries_[7] = {PatternId {8}, "7-Step Pulse", makePattern("x------")};
    entries_[8] = {PatternId {9}, "9-Step Pulse", makePattern("x--------")};
    entries_[9] = {PatternId {10}, "Euclidean 5/12", makePattern("x-x--x-x--x-")};

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
            || candidate.name.empty()
            || candidate.pattern.length == 0
            || candidate.pattern.length > Pattern::maxLength)
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
        auto& pattern = entries_[index].pattern;
        std::fill(pattern.hits.begin() + static_cast<std::ptrdiff_t>(pattern.length),
                  pattern.hits.end(),
                  false);
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

    if (name.empty() || pattern.length == 0 || pattern.length > Pattern::maxLength)
        return {};

    const auto insertionIndex = publishedEntryCount_.load(std::memory_order_acquire);
    if (insertionIndex >= entries_.size())
        return {};

    auto normalizedPattern = pattern;
    std::fill(normalizedPattern.hits.begin()
                  + static_cast<std::ptrdiff_t>(normalizedPattern.length),
              normalizedPattern.hits.end(),
              false);

    auto& entry = entries_[insertionIndex];
    entry.id = nextAvailableId();
    entry.name = std::move(name);
    entry.pattern = normalizedPattern;

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
        || entry.name.empty()
        || entry.pattern.length == 0
        || entry.pattern.length > Pattern::maxLength
        || findEquivalent(entry.pattern) != nullptr)
    {
        entry = {};
        return {};
    }

    std::fill(entry.pattern.hits.begin()
                  + static_cast<std::ptrdiff_t>(entry.pattern.length),
              entry.pattern.hits.end(),
              false);

    publishedEntryCount_.store(insertionIndex + 1, std::memory_order_release);
    return {&entry, true};
}

} // namespace lps
