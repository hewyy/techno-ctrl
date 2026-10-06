#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace lps
{

constexpr std::size_t maximumMidiMappings = 1024;
constexpr std::size_t maximumMidiRoutesPerMapping = 8;
constexpr std::size_t maximumMidiControlSlots = 64;
constexpr std::size_t maximumMidiTargetCount = 8192;
constexpr std::size_t midiCcSourceBucketCount = 16 * 128;
constexpr std::size_t maximumMidiConditionsPerMapping = 32;
constexpr int maximumPendingMidiIncrement = 127;

enum class MidiControlMessageType : std::uint8_t
{
    controlChange,
    noteOn,
    noteOff,
    other
};

struct MidiControlEvent
{
    MidiControlMessageType type = MidiControlMessageType::other;
    std::uint8_t channel = 0;
    std::uint8_t control = 0;
    std::uint8_t rawValue = 0;
    std::int32_t samplePosition = 0;
    std::uint32_t eventOrder = 0;
};

enum class MidiValueMode : std::uint8_t { incDec };

struct DecodedMidiControlEvent
{
    MidiControlEvent source;
    int logicalMovement = 0;
};

[[nodiscard]] bool decodeMidiControlEvent(
    const MidiControlEvent&, MidiValueMode, DecodedMidiControlEvent&) noexcept;

struct MidiMappingContext
{
    std::array<bool, 16> modifiers {};
    std::array<bool, 128> heldNotes {};
    int mode = 0; // Retained for source compatibility with version 1 clients.
    int bank = 0;
};

enum class MidiConditionKind : std::uint8_t
{
    modifier,
    heldNote,
    mode,
    bank
};

struct MidiMappingCondition
{
    MidiConditionKind kind = MidiConditionKind::modifier;
    std::uint8_t key = 0;
    int value = 0;
};

struct MidiMappingConditions
{
    static constexpr std::size_t maximumConditionCount =
        maximumMidiConditionsPerMapping;
    std::array<MidiMappingCondition, maximumConditionCount> values {};
    std::uint8_t count = 0;
};

struct MidiTargetHandle
{
    static constexpr std::uint16_t invalidValue =
        std::numeric_limits<std::uint16_t>::max();
    std::uint16_t value = invalidValue;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return value != invalidValue;
    }
    friend constexpr bool operator==(
        MidiTargetHandle left, MidiTargetHandle right) noexcept
    {
        return left.value == right.value;
    }
    friend constexpr bool operator!=(
        MidiTargetHandle left, MidiTargetHandle right) noexcept
    {
        return !(left == right);
    }
};

struct MidiStableId
{
    std::uint32_t value = 0;
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(
        MidiStableId left, MidiStableId right) noexcept
    {
        return left.value == right.value;
    }
    friend constexpr bool operator!=(
        MidiStableId left, MidiStableId right) noexcept
    {
        return !(left == right);
    }
};

using MidiPageProfileId = MidiStableId;
using MidiSubjectId = MidiStableId;
using MidiParameterDefinitionId = MidiStableId;

enum class MidiPageKind : std::uint8_t
{
    parameterOverview,
    parameterSteps
};

struct MidiEditingContext
{
    MidiPageProfileId profile;
    MidiPageKind pageKind = MidiPageKind::parameterOverview;
    MidiSubjectId subject;
    std::uint16_t selectedParameterSlot = 0; // One based. Zero means none.
    std::uint16_t stepBank = 0;
    bool valid = false;
};

struct MidiPageBindingSnapshot
{
    std::array<MidiTargetHandle, maximumMidiControlSlots> controlTargets {};
    MidiPageProfileId profile;
    MidiPageKind pageKind = MidiPageKind::parameterOverview;
    std::uint64_t generation = 0;
    bool valid = false;

    MidiPageBindingSnapshot() noexcept { controlTargets.fill({}); }
};

enum class MidiRouteOperation : std::uint8_t { increment };
enum class MidiRouteDestination : std::uint8_t
{
    directTarget,
    controlSlot
};

struct RuntimeMidiRoute
{
    MidiTargetHandle target;
    MidiRouteOperation operation = MidiRouteOperation::increment;
    std::int16_t scale = 1;
    std::uint8_t controlSlot = 0; // Zero based at runtime.
    MidiRouteDestination destination = MidiRouteDestination::directTarget;
};

struct RuntimeMidiMapping
{
    MidiControlMessageType sourceType = MidiControlMessageType::controlChange;
    std::uint8_t channel = 1;
    std::uint8_t control = 0;
    MidiValueMode valueMode = MidiValueMode::incDec;
    MidiMappingConditions conditions;
    std::array<RuntimeMidiRoute, maximumMidiRoutesPerMapping> routes {};
    std::uint8_t routeCount = 0;
};

struct MidiSourceBucket
{
    std::uint16_t offset = 0;
    std::uint16_t count = 0;
};

struct RuntimeMidiMappingTable
{
    std::array<RuntimeMidiMapping, maximumMidiMappings> mappings {};
    std::array<MidiSourceBucket, midiCcSourceBucketCount> ccBuckets {};
    std::uint16_t mappingCount = 0;
    bool sourceIndexValid = false;
};

[[nodiscard]] constexpr std::size_t midiCcSourceIndex(
    std::uint8_t channel, std::uint8_t control) noexcept
{
    return channel >= 1 && channel <= 16 && control <= 127
        ? (static_cast<std::size_t>(channel) - 1u) * 128u + control
        : midiCcSourceBucketCount;
}

// Non-realtime: orders mappings by source and builds direct CC buckets.
void compileMidiSourceIndex(RuntimeMidiMappingTable&) noexcept;

struct MidiTargetCommand
{
    MidiTargetHandle target;
    MidiRouteOperation operation = MidiRouteOperation::increment;
    int value = 0;
};

struct MidiTargetCommandBuffer
{
    static constexpr std::size_t capacity = maximumMidiRoutesPerMapping;
    std::array<MidiTargetCommand, capacity> commands {};
    std::uint8_t count = 0;
    bool overflowed = false;
    [[nodiscard]] bool push(MidiTargetCommand) noexcept;
};

struct MidiMappingResolution
{
    MidiTargetCommandBuffer targetCommands;
    bool consumed = false;
    bool ambiguous = false;
    std::uint16_t mappingsInspected = 0;
};

[[nodiscard]] MidiMappingResolution resolveMidiControlEvent(
    const MidiControlEvent&,
    const MidiMappingContext&,
    const RuntimeMidiMappingTable&,
    const MidiPageBindingSnapshot&) noexcept;

// Version 1/source-compatible overload. Control-slot routes remain unbound.
[[nodiscard]] MidiMappingResolution resolveMidiControlEvent(
    const MidiControlEvent&,
    const MidiMappingContext&,
    const RuntimeMidiMappingTable&) noexcept;

class MidiTargetMailbox
{
public:
    MidiTargetMailbox() noexcept;
    void accumulate(MidiTargetHandle, int movement) noexcept;
    [[nodiscard]] int take(MidiTargetHandle) noexcept;
    void clear() noexcept;

    template <typename Visitor>
    void drainDirty(Visitor&& visitor) noexcept
    {
        for (std::size_t wordIndex = 0; wordIndex < dirty_.size(); ++wordIndex)
        {
            auto word = dirty_[wordIndex].exchange(0, std::memory_order_acq_rel);
            while (word != 0)
            {
                const auto bit = trailingZeroCount(word);
                word &= word - 1u;
                const auto targetIndex = wordIndex * bitsPerDirtyWord + bit;
                if (targetIndex >= pending_.size())
                    continue;
                const auto movement = pending_[targetIndex].exchange(
                    0, std::memory_order_acq_rel);
                if (movement != 0)
                    std::forward<Visitor>(visitor)(
                        MidiTargetHandle {
                            static_cast<std::uint16_t>(targetIndex)},
                        movement);
            }
        }
    }

private:
    static constexpr std::size_t bitsPerDirtyWord = 64;
    static constexpr std::size_t dirtyWordCount =
        (maximumMidiTargetCount + bitsPerDirtyWord - 1) / bitsPerDirtyWord;
    [[nodiscard]] static std::size_t trailingZeroCount(
        std::uint64_t) noexcept;

    std::array<std::atomic<int>, maximumMidiTargetCount> pending_;
    std::array<std::atomic<std::uint64_t>, dirtyWordCount> dirty_;
};

static_assert(std::is_trivially_copyable_v<RuntimeMidiMappingTable>);
static_assert(std::is_trivially_copyable_v<MidiPageBindingSnapshot>);

} // namespace lps
