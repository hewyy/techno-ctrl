#pragma once

#include "core/SequencerTypes.h"

namespace lps
{

class IPlayer
{
public:
    virtual ~IPlayer() = default;

    virtual void prepare(const PrepareSpec& spec) noexcept = 0;
    virtual void reset() noexcept = 0;
    [[nodiscard]] virtual PlayerProcessResult process(
        const TimelineBlock& block,
        const PlayerDirectives& directives,
        SequencerEventBuffer& output) noexcept = 0;
    [[nodiscard]] virtual PlayerSyncCapabilities syncCapabilities() const noexcept = 0;
};

} // namespace lps
