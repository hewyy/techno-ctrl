#pragma once

#include "core/SequencerTypes.h"

namespace lps
{

class ITransport
{
public:
    virtual ~ITransport() = default;
    virtual void prepare(const PrepareSpec& spec) noexcept = 0;
    [[nodiscard]] virtual bool send(
        const SequencerEvent& event,
        const TimelineBlock& block) noexcept = 0;
    virtual void resetOutputs(const TimelineBlock& block) noexcept = 0;
};

} // namespace lps
