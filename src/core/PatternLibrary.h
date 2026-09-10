#pragma once

#include "core/SequencerTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lps
{

class PatternId
{
public:
    constexpr PatternId() noexcept = default;
    explicit constexpr PatternId(std::uint64_t value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(PatternId left, PatternId right) noexcept
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(PatternId left, PatternId right) noexcept
    {
        return !(left == right);
    }

private:
    std::uint64_t value_ = 0;
};

struct PatternLibraryEntry
{
    PatternId id;
    std::string name;
    Pattern pattern;
};

[[nodiscard]] bool patternsEqual(const Pattern& left, const Pattern& right) noexcept;

struct PatternLibraryInsertResult
{
    const PatternLibraryEntry* entry = nullptr;
    bool inserted = false;
};

class PatternLibrary
{
public:
    using BeforePublish = std::function<bool(PatternLibraryEntry&)>;

    static constexpr std::size_t builtInCount = 10;
    static constexpr std::size_t maxEntryCount = 256;

    PatternLibrary();

    PatternLibrary(const PatternLibrary&) = delete;
    PatternLibrary& operator=(const PatternLibrary&) = delete;

    // Published entries are immutable and retain stable addresses for the
    // lifetime of the library. Readers may call these methods on the audio
    // thread while one UI thread appends entries with addOrFind().
    [[nodiscard]] std::size_t size() const noexcept
    {
        return publishedEntryCount_.load(std::memory_order_acquire);
    }
    [[nodiscard]] const PatternLibraryEntry* recordAt(std::size_t index) const noexcept;
    [[nodiscard]] const PatternLibraryEntry* find(PatternId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> indexOf(PatternId id) const noexcept;
    [[nodiscard]] const PatternLibraryEntry* findEquivalent(const Pattern& pattern) const noexcept;

    // Replaces the constructor defaults with a validated catalog. This is a
    // startup-only operation and must run before any PatternPlayer or other
    // reader is given access to the library.
    [[nodiscard]] bool replaceEntriesForStartup(
        const std::vector<PatternLibraryEntry>& entries);

    // Single-writer operation intended for the UI thread. Duplicate content
    // returns the existing entry regardless of name. New content must have a
    // non-empty name and a valid length. beforePublish may persist and adjust
    // the staged entry (for example, to resolve an ID allocated by another
    // process); returning false leaves the library unchanged.
    [[nodiscard]] PatternLibraryInsertResult addOrFind(std::string name,
                                                       const Pattern& pattern,
                                                       const BeforePublish& beforePublish = {});

private:
    [[nodiscard]] PatternId nextAvailableId() const noexcept;

    std::array<PatternLibraryEntry, maxEntryCount> entries_ {};
    std::atomic<std::size_t> publishedEntryCount_ {0};
};

} // namespace lps
