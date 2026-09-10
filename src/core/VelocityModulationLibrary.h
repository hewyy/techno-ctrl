#pragma once

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

struct VelocityModulation
{
    static constexpr std::size_t maxLength = 10;

    // The editor/library use an 8-bit scale. PatternPlayer normalizes it for
    // transports such as MIDI, whose final velocity range is 0 through 127.
    std::array<std::uint8_t, maxLength> values {};
    std::size_t length = 0;
};

class VelocityModulationId
{
public:
    constexpr VelocityModulationId() noexcept = default;
    explicit constexpr VelocityModulationId(std::uint64_t value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != 0; }

    friend constexpr bool operator==(
        VelocityModulationId left,
        VelocityModulationId right) noexcept
    {
        return left.value_ == right.value_;
    }

    friend constexpr bool operator!=(
        VelocityModulationId left,
        VelocityModulationId right) noexcept
    {
        return !(left == right);
    }

private:
    std::uint64_t value_ = 0;
};

struct VelocityModulationLibraryEntry
{
    VelocityModulationId id;
    std::string name;
    VelocityModulation modulation;
};

[[nodiscard]] bool velocityModulationsEqual(
    const VelocityModulation& left,
    const VelocityModulation& right) noexcept;

struct VelocityModulationLibraryInsertResult
{
    const VelocityModulationLibraryEntry* entry = nullptr;
    bool inserted = false;
};

class VelocityModulationLibrary
{
public:
    using BeforePublish = std::function<bool(VelocityModulationLibraryEntry&)>;

    static constexpr std::size_t builtInCount = 2;
    static constexpr std::size_t maxEntryCount = 256;

    VelocityModulationLibrary();

    VelocityModulationLibrary(const VelocityModulationLibrary&) = delete;
    VelocityModulationLibrary& operator=(const VelocityModulationLibrary&) = delete;

    // Published entries are immutable and retain stable addresses for the
    // lifetime of the library. Readers may call these methods on the audio
    // thread while one UI thread appends entries with addOrFind().
    [[nodiscard]] std::size_t size() const noexcept
    {
        return publishedEntryCount_.load(std::memory_order_acquire);
    }
    [[nodiscard]] const VelocityModulationLibraryEntry* recordAt(
        std::size_t index) const noexcept;
    [[nodiscard]] const VelocityModulationLibraryEntry* find(
        VelocityModulationId id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> indexOf(
        VelocityModulationId id) const noexcept;
    [[nodiscard]] const VelocityModulationLibraryEntry* findEquivalent(
        const VelocityModulation& modulation) const noexcept;

    // Replaces the constructor defaults with a validated catalog. This is a
    // startup-only operation and must run before any reader is given access to
    // the library.
    [[nodiscard]] bool replaceEntriesForStartup(
        const std::vector<VelocityModulationLibraryEntry>& entries);

    // Single-writer operation intended for the UI thread. Duplicate content
    // returns the existing entry regardless of name. New content must have a
    // non-empty name and a length from one through VelocityModulation::maxLength.
    // beforePublish may persist and adjust the staged entry; returning false
    // leaves the library unchanged.
    [[nodiscard]] VelocityModulationLibraryInsertResult addOrFind(
        std::string name,
        const VelocityModulation& modulation,
        const BeforePublish& beforePublish = {});

private:
    [[nodiscard]] VelocityModulationId nextAvailableId() const noexcept;

    std::array<VelocityModulationLibraryEntry, maxEntryCount> entries_ {};
    std::atomic<std::size_t> publishedEntryCount_ {0};
};

} // namespace lps
