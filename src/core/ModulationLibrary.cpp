#include "core/ModulationLibrary.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace lps
{
namespace
{
Modulation makeModulation(
    std::initializer_list<std::uint8_t> values) noexcept
{
    Modulation modulation;
    modulation.length = static_cast<std::uint8_t>(
        std::min(values.size(), Modulation::maxLength));
    std::transform(
        values.begin(),
        values.begin() + modulation.length,
        modulation.values.begin(),
        [](std::uint8_t value)
        {
            return NormalizedValue::fromUnipolar8(value);
        });
    return modulation;
}

void normalizeTail(Modulation& modulation) noexcept
{
    std::fill(
        modulation.values.begin()
            + static_cast<std::ptrdiff_t>(modulation.length),
        modulation.values.end(),
        NormalizedValue {});
}
} // namespace

bool modulationsEqual(
    const Modulation& left,
    const Modulation& right) noexcept
{
    if (left.length != right.length
        || left.length > Modulation::maxLength)
    {
        return false;
    }

    for (std::size_t step = 0; step < left.length; ++step)
    {
        if (left.values[step] != right.values[step])
            return false;
    }

    return true;
}

ModulationLibrary::ModulationLibrary()
{
    entries_[0] = {
        ModulationId {1},
        "Constant 201/255",
        makeModulation({201})
    };
    entries_[1] = {
        ModulationId {2},
        "Four Step",
        makeModulation({255, 100, 225, 150})
    };

    publishedEntryCount_.store(builtInCount, std::memory_order_release);
}

const ModulationLibraryEntry* ModulationLibrary::recordAt(
    std::size_t index) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);
    return index < publishedCount ? &entries_[index] : nullptr;
}

const ModulationLibraryEntry* ModulationLibrary::find(
    ModulationId id) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (entries_[index].id == id)
            return &entries_[index];
    }

    return nullptr;
}

std::optional<std::size_t> ModulationLibrary::indexOf(
    ModulationId id) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (entries_[index].id == id)
            return index;
    }

    return std::nullopt;
}

const ModulationLibraryEntry* ModulationLibrary::findEquivalent(
    const Modulation& modulation) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (modulationsEqual(entries_[index].modulation, modulation))
            return &entries_[index];
    }

    return nullptr;
}

bool ModulationLibrary::replaceEntriesForStartup(
    const std::vector<ModulationLibraryEntry>& entries)
{
    if (entries.empty() || entries.size() > entries_.size())
        return false;

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& candidate = entries[index];
        if (!candidate.id.isValid()
            || candidate.id.value() > maxEntryCount
            || candidate.name.empty()
            || candidate.modulation.length == 0
            || candidate.modulation.length > Modulation::maxLength)
        {
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (entries[previous].id == candidate.id
                || modulationsEqual(
                    entries[previous].modulation,
                    candidate.modulation))
            {
                return false;
            }
        }
    }

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        entries_[index] = entries[index];
        normalizeTail(entries_[index].modulation);
    }

    for (std::size_t index = entries.size(); index < entries_.size(); ++index)
        entries_[index] = {};

    publishedEntryCount_.store(entries.size(), std::memory_order_release);
    return true;
}

ModulationId ModulationLibrary::nextAvailableId() const noexcept
{
    for (std::uint64_t value = 1; value <= maxEntryCount; ++value)
    {
        const auto candidate = ModulationId {value};
        if (find(candidate) == nullptr)
            return candidate;
    }

    return {};
}

ModulationLibraryInsertResult ModulationLibrary::addOrFind(
    std::string name,
    const Modulation& modulation,
    const BeforePublish& beforePublish)
{
    if (const auto* existing = findEquivalent(modulation))
        return {existing, false};

    if (modulation.length == 0
        || modulation.length > Modulation::maxLength)
    {
        return {};
    }

    const auto insertionIndex = publishedEntryCount_.load(std::memory_order_acquire);
    if (insertionIndex >= entries_.size())
        return {};

    auto normalizedModulation = modulation;
    normalizeTail(normalizedModulation);

    auto& entry = entries_[insertionIndex];
    entry.id = nextAvailableId();
    entry.name = name.empty()
        ? "Modulation " + std::to_string(entry.id.value())
        : std::move(name);
    entry.modulation = normalizedModulation;

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
        || entry.modulation.length == 0
        || entry.modulation.length > Modulation::maxLength
        || findEquivalent(entry.modulation) != nullptr)
    {
        entry = {};
        return {};
    }

    normalizeTail(entry.modulation);

    publishedEntryCount_.store(insertionIndex + 1, std::memory_order_release);
    return {&entry, true};
}

} // namespace lps
