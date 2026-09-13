#pragma once

#include <cstdint>
#include <type_traits>

namespace lps
{

// Exact, protocol-neutral storage for reusable unipolar modulation values.
struct NormalizedValue
{
    static constexpr std::uint16_t maximum = 65'535;
    std::uint16_t raw = 0;

    [[nodiscard]] static constexpr NormalizedValue fromUnipolar8(
        std::uint8_t value) noexcept
    {
        return { static_cast<std::uint16_t>(value * 257u) };
    }

    // Non-finite input is handled deterministically: NaN and negative
    // infinity become zero, while positive infinity becomes one.
    [[nodiscard]] static NormalizedValue fromFloat(float value) noexcept;

    [[nodiscard]] constexpr float toFloat() const noexcept
    {
        return static_cast<float>(raw) / static_cast<float>(maximum);
    }

    friend constexpr bool operator==(
        NormalizedValue left, NormalizedValue right) noexcept
    {
        return left.raw == right.raw;
    }

    friend constexpr bool operator!=(
        NormalizedValue left, NormalizedValue right) noexcept
    {
        return !(left == right);
    }
};

static_assert(std::is_trivially_copyable_v<NormalizedValue>);

} // namespace lps
