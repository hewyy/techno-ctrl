#pragma once

#include "core/IPlayer.h"
#include "core/IPlayerEditorModels.h"
#include "core/ModulationLane.h"

#include <cstdint>
#include <limits>

namespace lps
{

class PulsePlayer final : public IPlayer, public IModulationEditorModel
{
public:
    explicit PulsePlayer(double periodPpq = 0.5, double gateRatio = 0.5) noexcept;

    void publishModulationState(const ModulationLaneState& state) noexcept;

    void prepare(const PrepareSpec& spec) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] PlayerProcessResult process(
        const TimelineBlock& block,
        const PlayerDirectives& directives,
        SequencerEventBuffer& output) noexcept override;
    [[nodiscard]] PlayerSyncCapabilities syncCapabilities() const noexcept override;
    [[nodiscard]] ModulationPlaybackSnapshot modulationPlaybackSnapshot()
        const noexcept override;

private:
    double periodPpq_ = 0.5;
    double gateRatio_ = 0.5;
    double originPpq_ = 0.0;
    double pendingEndPpq_ = std::numeric_limits<double>::infinity();
    std::int64_t lastPulse_ = std::numeric_limits<std::int64_t>::min();
    std::uint64_t nextTriggerId_ = 1;
    TriggerId activeTriggerId_;
    ModulationLaneRuntime intensityLane_ { makeIntensityLaneDefinition() };
    ModulationPlaybackSnapshot modulationSnapshot_;
    bool triggerActive_ = false;
};

} // namespace lps
