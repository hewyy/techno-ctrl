#include "core/MidiControlMapping.h"

#include <algorithm>

namespace lps
{
namespace
{
bool conditionsMatch(
    const MidiMappingConditions& conditions,
    const MidiMappingContext& context) noexcept
{
    if (conditions.count > conditions.values.size())
        return false;
    for (std::size_t index = 0; index < conditions.count; ++index)
    {
        const auto& condition = conditions.values[index];
        switch (condition.kind)
        {
            case MidiConditionKind::modifier:
                if (condition.key >= context.modifiers.size()
                    || context.modifiers[condition.key]
                        != (condition.value != 0))
                    return false;
                break;
            case MidiConditionKind::heldNote:
                if (condition.key >= context.heldNotes.size()
                    || context.heldNotes[condition.key]
                        != (condition.value != 0))
                    return false;
                break;
            case MidiConditionKind::mode:
                if (context.mode != condition.value)
                    return false;
                break;
            case MidiConditionKind::bank:
                if (context.bank != condition.value)
                    return false;
                break;
        }
    }
    return true;
}
}

bool decodeMidiControlEvent(
    const MidiControlEvent& event,
    MidiValueMode mode,
    DecodedMidiControlEvent& decoded) noexcept
{
    if (mode != MidiValueMode::incDec)
        return false;
    int movement = 0;
    if (event.rawValue == 1)
        movement = 1;
    else if (event.rawValue == 127)
        movement = -1;
    else
        return false;
    decoded = {event, movement};
    return true;
}

void compileMidiSourceIndex(RuntimeMidiMappingTable& table) noexcept
{
    const auto count = std::min<std::size_t>(
        table.mappingCount, table.mappings.size());
    std::stable_sort(
        table.mappings.begin(), table.mappings.begin() + count,
        [](const RuntimeMidiMapping& left, const RuntimeMidiMapping& right)
        {
            return midiCcSourceIndex(left.channel, left.control)
                < midiCcSourceIndex(right.channel, right.control);
        });
    table.ccBuckets.fill({});
    std::size_t position = 0;
    while (position < count)
    {
        const auto source = midiCcSourceIndex(
            table.mappings[position].channel,
            table.mappings[position].control);
        if (source >= table.ccBuckets.size())
        {
            ++position;
            continue;
        }
        const auto begin = position;
        while (position < count
            && midiCcSourceIndex(
                table.mappings[position].channel,
                table.mappings[position].control) == source)
            ++position;
        table.ccBuckets[source] = {
            static_cast<std::uint16_t>(begin),
            static_cast<std::uint16_t>(position - begin)
        };
    }
    table.mappingCount = static_cast<std::uint16_t>(count);
    table.sourceIndexValid = true;
}

bool MidiTargetCommandBuffer::push(MidiTargetCommand command) noexcept
{
    if (count >= commands.size())
    {
        overflowed = true;
        return false;
    }
    commands[count++] = command;
    return true;
}

MidiMappingResolution resolveMidiControlEvent(
    const MidiControlEvent& event,
    const MidiMappingContext& context,
    const RuntimeMidiMappingTable& table,
    const MidiPageBindingSnapshot& pageBindings) noexcept
{
    MidiMappingResolution result;
    if (event.type != MidiControlMessageType::controlChange)
        return result;
    const auto source = midiCcSourceIndex(event.channel, event.control);
    if (source >= midiCcSourceBucketCount)
        return result;

    std::size_t first = 0;
    std::size_t count = std::min<std::size_t>(
        table.mappingCount, table.mappings.size());
    if (table.sourceIndexValid)
    {
        first = table.ccBuckets[source].offset;
        count = table.ccBuckets[source].count;
        if (first > table.mappingCount || count > table.mappingCount - first)
            return result;
    }

    const RuntimeMidiMapping* match = nullptr;
    for (std::size_t relative = 0; relative < count; ++relative)
    {
        const auto& mapping = table.mappings[first + relative];
        ++result.mappingsInspected;
        if (mapping.sourceType != event.type
            || mapping.channel != event.channel
            || mapping.control != event.control
            || !conditionsMatch(mapping.conditions, context))
            continue;
        if (match != nullptr)
        {
            result.consumed = true;
            result.ambiguous = true;
            return result;
        }
        match = &mapping;
    }
    if (match == nullptr)
        return result;
    result.consumed = true;

    DecodedMidiControlEvent decoded;
    if (!decodeMidiControlEvent(event, match->valueMode, decoded))
        return result;
    const auto routeCount = std::min<std::size_t>(
        match->routeCount, match->routes.size());
    for (std::size_t routeIndex = 0; routeIndex < routeCount; ++routeIndex)
    {
        const auto& route = match->routes[routeIndex];
        auto target = route.target;
        if (route.destination == MidiRouteDestination::controlSlot)
        {
            if (!pageBindings.valid
                || route.controlSlot >= pageBindings.controlTargets.size())
                continue;
            target = pageBindings.controlTargets[route.controlSlot];
        }
        if (!target.valid())
            continue;
        (void) result.targetCommands.push({
            target,
            route.operation,
            decoded.logicalMovement * static_cast<int>(route.scale)
        });
    }
    return result;
}

MidiMappingResolution resolveMidiControlEvent(
    const MidiControlEvent& event,
    const MidiMappingContext& context,
    const RuntimeMidiMappingTable& table) noexcept
{
    const MidiPageBindingSnapshot emptyBindings;
    return resolveMidiControlEvent(event, context, table, emptyBindings);
}

MidiTargetMailbox::MidiTargetMailbox() noexcept { clear(); }

void MidiTargetMailbox::accumulate(
    MidiTargetHandle target, int movement) noexcept
{
    if (!target.valid() || target.value >= pending_.size() || movement == 0)
        return;
    auto& pending = pending_[target.value];
    auto current = pending.load(std::memory_order_relaxed);
    constexpr int maximumCompareExchangeAttempts = 16;
    for (int attempt = 0; attempt < maximumCompareExchangeAttempts; ++attempt)
    {
        const auto sum = static_cast<long long>(current) + movement;
        const auto saturated = static_cast<int>(std::clamp<long long>(
            sum, -maximumPendingMidiIncrement, maximumPendingMidiIncrement));
        if (pending.compare_exchange_weak(
                current, saturated,
                std::memory_order_release, std::memory_order_relaxed))
        {
            const auto word = target.value / bitsPerDirtyWord;
            const auto bit = target.value % bitsPerDirtyWord;
            dirty_[word].fetch_or(
                std::uint64_t {1} << bit, std::memory_order_release);
            return;
        }
    }
}

int MidiTargetMailbox::take(MidiTargetHandle target) noexcept
{
    return target.valid() && target.value < pending_.size()
        ? pending_[target.value].exchange(0, std::memory_order_acq_rel) : 0;
}

void MidiTargetMailbox::clear() noexcept
{
    for (auto& pending : pending_)
        pending.store(0, std::memory_order_relaxed);
    for (auto& dirty : dirty_)
        dirty.store(0, std::memory_order_relaxed);
}

std::size_t MidiTargetMailbox::trailingZeroCount(
    std::uint64_t value) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    return static_cast<std::size_t>(__builtin_ctzll(value));
#else
    std::size_t result = 0;
    while ((value & 1u) == 0) { value >>= 1u; ++result; }
    return result;
#endif
}

static_assert(std::atomic<int>::is_always_lock_free,
    "The MIDI target mailbox must be lock-free");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
    "The MIDI dirty-target bitset must be lock-free");

} // namespace lps
