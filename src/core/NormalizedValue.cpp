#include "core/NormalizedValue.h"

#include <algorithm>
#include <cmath>

namespace lps
{

NormalizedValue NormalizedValue::fromFloat(float value) noexcept
{
    if (std::isnan(value) || value <= 0.0f)
        return {};
    if (!std::isfinite(value) || value >= 1.0f)
        return { maximum };

    return { static_cast<std::uint16_t>(std::lround(
        value * static_cast<float>(maximum))) };
}

} // namespace lps
