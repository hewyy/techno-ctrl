#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lps
{

struct LaneId
{
    std::uint16_t value = 0;

    friend constexpr bool operator==(LaneId left, LaneId right) noexcept
    {
        return left.value == right.value;
    }
};

struct NormalizedValue
{
    static constexpr std::uint16_t maximum = 65'535;
    std::uint16_t raw = 0;

    [[nodiscard]] static constexpr NormalizedValue fromUnipolar8(
        std::uint8_t value) noexcept
    {
        return { static_cast<std::uint16_t>(value * 257u) };
    }

    [[nodiscard]] static NormalizedValue fromFloat(float value) noexcept;
    [[nodiscard]] constexpr float toFloat() const noexcept
    {
        return static_cast<float>(raw) / static_cast<float>(maximum);
    }
};

enum class ModulationTarget : std::uint8_t
{
    intensity,
    pitch,
    gateLength,
    probability,
    control
};

enum class ModulationAdvancePoint : std::uint8_t
{
    sourceStep,
    candidateTrigger,
    emittedTrigger,
    time
};

enum class ModulationResetReason : std::uint8_t
{
    sourceSelection = 1u << 0u,
    transportDiscontinuity = 1u << 1u,
    explicitRestart = 1u << 2u,
    patternChange = 1u << 3u
};

using ModulationResetMask = std::uint8_t;

[[nodiscard]] constexpr ModulationResetMask resetMask(
    ModulationResetReason reason) noexcept
{
    return static_cast<ModulationResetMask>(reason);
}

[[nodiscard]] constexpr ModulationResetMask operator|(
    ModulationResetReason left,
    ModulationResetReason right) noexcept
{
    return resetMask(left) | resetMask(right);
}

enum class ModulationCombineMode : std::uint8_t
{
    replace,
    add,
    multiply
};

struct ValueMapping
{
    float minimum = 0.0f;
    float maximum = 1.0f;
    ModulationCombineMode combine = ModulationCombineMode::replace;

    [[nodiscard]] float map(NormalizedValue value) const noexcept;
};

struct ModulationLaneDefinition
{
    LaneId id;
    ModulationTarget target = ModulationTarget::intensity;
    ModulationAdvancePoint advanceOn = ModulationAdvancePoint::candidateTrigger;
    ModulationResetMask resetOn = 0;
    ValueMapping mapping;
    std::uint16_t logicalControl = 0;
};

[[nodiscard]] ModulationLaneDefinition makeIntensityLaneDefinition(
    LaneId id = { 1 }) noexcept;

struct ModulationLaneState
{
    static constexpr std::size_t maximumStepCount = 32;
    std::array<NormalizedValue, maximumStepCount> values {};
    std::uint8_t length = 0;
};

struct ModulationSample
{
    LaneId laneId;
    ModulationTarget target = ModulationTarget::intensity;
    std::uint16_t logicalControl = 0;
    NormalizedValue normalized;
    float mappedValue = 0.0f;
    std::uint8_t sourceStep = 0;
    ModulationCombineMode combine = ModulationCombineMode::replace;
};

class ModulationLaneRuntime
{
public:
    ModulationLaneRuntime() noexcept = default;
    explicit ModulationLaneRuntime(ModulationLaneDefinition definition) noexcept;

    void configure(ModulationLaneDefinition definition) noexcept;
    void publishState(const ModulationLaneState& state) noexcept;
    void reset(ModulationResetReason reason) noexcept;
    [[nodiscard]] std::optional<ModulationSample> advance(
        ModulationAdvancePoint point) noexcept;

    [[nodiscard]] const ModulationLaneDefinition& definition() const noexcept;
    [[nodiscard]] int currentStep() const noexcept;

private:
    ModulationLaneDefinition definition_;
    ModulationLaneState state_;
    std::uint8_t nextStep_ = 0;
    int currentStep_ = -1;
};

class ModulationBank
{
public:
    static constexpr std::size_t maximumLaneCount = 8;

    [[nodiscard]] ModulationLaneRuntime* addLane(
        ModulationLaneDefinition definition) noexcept;
    [[nodiscard]] ModulationLaneRuntime* lane(LaneId id) noexcept;
    [[nodiscard]] const ModulationLaneRuntime* lane(LaneId id) const noexcept;
    [[nodiscard]] std::size_t advance(
        ModulationAdvancePoint point,
        std::array<ModulationSample, maximumLaneCount>& output) noexcept;
    void reset(ModulationResetReason reason) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::array<ModulationLaneRuntime, maximumLaneCount> lanes_ {};
    std::size_t size_ = 0;
};

} // namespace lps
