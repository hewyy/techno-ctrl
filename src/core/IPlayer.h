#pragma once

#include "core/SequencerTypes.h"

namespace lps
{

class IPlayer
{
public:
    virtual ~IPlayer() = default;

    [[nodiscard]] virtual PlayerRef playerRef() const noexcept = 0;
    virtual void setRuntimeId(std::uint32_t id) noexcept = 0;
    virtual void prepare(const PrepareSpec& spec) noexcept = 0;
    virtual void reset() noexcept = 0;
    [[nodiscard]] virtual PlayerProcessResult process(
        const TimelineBlock& block,
        const PlayerDirectives& directives,
        PlayerSignalBuffer& output) noexcept = 0;
    virtual void command(
        PlayerCommand command,
        double ppqPosition,
        PlayerSignalBuffer& output) noexcept = 0;
    virtual void advanceFromPatternHit(
        const PlayerSignal& hit,
        PlayerSignalBuffer& output) noexcept = 0;
    [[nodiscard]] virtual PlayerSyncCapabilities syncCapabilities() const noexcept = 0;
};

} // namespace lps
