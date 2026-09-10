
#pragma once

#include "core/SequencerTypes.h"

namespace lps
{

class ITransport
{
public:
    virtual ~ITransport() = default;
    virtual void send(
        const SequencerEvent& event,
        const ClockBlock& block) noexcept = 0;
};

} // namespace lps
