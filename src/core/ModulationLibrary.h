#pragma once

#include "core/NormalizedValue.h"

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

struct Modulation
{
    static constexpr std::size_t maxLength = 32;

    std::array<NormalizedValue, maxLength> values {};
    std::uint8_t length = 0;
};

static_assert(std::is_trivially_copyable_v<Modulation>);

class ModulationId
{
public:
    constexpr ModulationId() noexcept = default;
    explicit constexpr ModulationId(std::uint64_t value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(
        ModulationId left,
        ModulationId right) noexcept
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(
        ModulationId left,
        ModulationId right) noexcept
    {
        return !(left == right);
    }

private:
    std::uint64_t value_ = 0;
};

struct ModulationLibraryEntry
{
    ModulationId id;
    std::string name;
    Modulation modulation;
};

[[nodiscard]] bool modulationsEqual(
    const Modulation& left,
    const Modulation& right) noexcept;

struct ModulationLibraryInsertResult
{
    const ModulationLibraryEntry* entry = nullptr;
    bool inserted = false;
};

class ModulationLibrary
{
public:
    using BeforePublish = std::function<bool(ModulationLibraryEntry&)>;

    static constexpr std::size_t builtInCount = 2;
    static constexpr std::size_t maxEntryCount = 256;

    ModulationLibrary();

    ModulationLibrary(const ModulationLibrary&) = delete;
    ModulationLibrary& operator=(const ModulationLibrary&) = delete;

    // Published entries are immutable and retain stable addresses for the
    // lifetime of the library. Readers may call these methods on the audio
    // thread while one UI thread appends entries with addOrFind().
    [[nodiscard]] std::size_t size() const noexcept
    {
        return publishedEntryCount_.load(std::memory_order_acquire);
    }
    [[nodiscard]] const ModulationLibraryEntry* recordAt(
        std::size_t index) const noexcept;
    [[nodiscard]] const ModulationLibraryEntry* find(
        ModulationId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> indexOf(
        ModulationId id) const noexcept;
    [[nodiscard]] const ModulationLibraryEntry* findEquivalent(
        const Modulation& modulation) const noexcept;

    // Replaces the constructor defaults with a validated catalog. This is a
    // startup-only operation and must run before any reader is given access to
    // the library.
    [[nodiscard]] bool replaceEntriesForStartup(
        const std::vector<ModulationLibraryEntry>& entries);

    // Single-writer operation intended for the UI thread. Duplicate content
    // returns the existing entry regardless of name. New content must have a
    // non-empty name and a length from one through Modulation::maxLength.
    // beforePublish may persist and adjust the staged entry; returning false
    // leaves the library unchanged.
    [[nodiscard]] ModulationLibraryInsertResult addOrFind(
        std::string name,
        const Modulation& modulation,
        const BeforePublish& beforePublish = {});

private:
    [[nodiscard]] ModulationId nextAvailableId() const noexcept;

    std::array<ModulationLibraryEntry, maxEntryCount> entries_ {};
    std::atomic<std::size_t> publishedEntryCount_ {0};
};

} // namespace lps
