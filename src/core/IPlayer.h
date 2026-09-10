#pragma once

#include "core/SequencerTypes.h"

namespace lps
{

class IPlayer
{
public:
    virtual ~IPlayer() = default;

    virtual void prepare(double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process(
        const ClockBlock& block,
        SequencerEventBuffer& output) noexcept = 0;

    [[nodiscard]] virtual PatternView get_pattern_view() const noexcept = 0;
    [[nodiscard]] virtual PlaybackSnapshot snapshot() const noexcept = 0;
    [[nodiscard]] virtual CycleBoundarySnapshot cycleBoundarySnapshot() const noexcept
    {
        return {};
    }
};

} // namespace lps
