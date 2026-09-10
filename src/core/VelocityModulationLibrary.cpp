#include "core/VelocityModulationLibrary.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace lps
{
namespace
{
VelocityModulation makeVelocityModulation(
    std::initializer_list<std::uint8_t> values) noexcept
{
    VelocityModulation modulation;
    modulation.length = std::min(values.size(), VelocityModulation::maxLength);
    std::copy_n(values.begin(), modulation.length, modulation.values.begin());
    return modulation;
}

void normalizeTail(VelocityModulation& modulation) noexcept
{
    std::fill(
        modulation.values.begin()
            + static_cast<std::ptrdiff_t>(modulation.length),
        modulation.values.end(),
        std::uint8_t {0});
}
} // namespace

bool velocityModulationsEqual(
    const VelocityModulation& left,
    const VelocityModulation& right) noexcept
{
    if (left.length != right.length
        || left.length > VelocityModulation::maxLength)
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

VelocityModulationLibrary::VelocityModulationLibrary()
{
    entries_[0] = {
        VelocityModulationId {1},
        "Steady 100",
        makeVelocityModulation({201})
    };
    entries_[1] = {
        VelocityModulationId {2},
        "Four-Step",
        makeVelocityModulation({255, 100, 225, 150})
    };

    publishedEntryCount_.store(builtInCount, std::memory_order_release);
}

const VelocityModulationLibraryEntry* VelocityModulationLibrary::recordAt(
    std::size_t index) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);
    return index < publishedCount ? &entries_[index] : nullptr;
}

const VelocityModulationLibraryEntry* VelocityModulationLibrary::find(
    VelocityModulationId id) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (entries_[index].id == id)
            return &entries_[index];
    }

    return nullptr;
}

std::optional<std::size_t> VelocityModulationLibrary::indexOf(
    VelocityModulationId id) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (entries_[index].id == id)
            return index;
    }

    return std::nullopt;
}

const VelocityModulationLibraryEntry* VelocityModulationLibrary::findEquivalent(
    const VelocityModulation& modulation) const noexcept
{
    const auto publishedCount = publishedEntryCount_.load(std::memory_order_acquire);

    for (std::size_t index = 0; index < publishedCount; ++index)
    {
        if (velocityModulationsEqual(entries_[index].modulation, modulation))
            return &entries_[index];
    }

    return nullptr;
}

bool VelocityModulationLibrary::replaceEntriesForStartup(
    const std::vector<VelocityModulationLibraryEntry>& entries)
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
            || candidate.modulation.length > VelocityModulation::maxLength)
        {
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (entries[previous].id == candidate.id
                || velocityModulationsEqual(
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

VelocityModulationId VelocityModulationLibrary::nextAvailableId() const noexcept
{
    for (std::uint64_t value = 1; value <= maxEntryCount; ++value)
    {
        const auto candidate = VelocityModulationId {value};
        if (find(candidate) == nullptr)
            return candidate;
    }

    return {};
}

VelocityModulationLibraryInsertResult VelocityModulationLibrary::addOrFind(
    std::string name,
    const VelocityModulation& modulation,
    const BeforePublish& beforePublish)
{
    if (const auto* existing = findEquivalent(modulation))
        return {existing, false};

    if (name.empty()
        || modulation.length == 0
        || modulation.length > VelocityModulation::maxLength)
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
    entry.name = std::move(name);
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
        || entry.modulation.length > VelocityModulation::maxLength
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
