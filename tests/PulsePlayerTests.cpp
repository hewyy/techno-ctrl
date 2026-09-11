#include "core/PulsePlayer.h"
#include "core/SequencerEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: "
              << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

lps::ModulationLaneState alternatingIntensity()
{
    lps::ModulationLaneState state;
    state.length = 2;
    state.values[0] = lps::NormalizedValue::fromUnipolar8(255);
    state.values[1] = lps::NormalizedValue::fromUnipolar8(100);
    return state;
}

lps::ModulationLaneDefinition laneDefinition(
    std::uint16_t id,
    lps::ModulationTarget target,
    lps::ModulationAdvancePoint advance,
    float minimum,
    float maximum,
    lps::ModulationCombineMode combine = lps::ModulationCombineMode::replace)
{
    lps::ModulationLaneDefinition definition;
    definition.id = { id };
    definition.target = target;
    definition.advanceOn = advance;
    definition.mapping = { minimum, maximum, combine };
    return definition;
}

class CollectingRenderer final : public lps::IOutputRenderer
{
public:
    void prepare(const lps::RenderSpec&) noexcept override {}
    [[nodiscard]] bool renderBlock(
        const lps::TimelineBlock&,
        lps::RoutedEventView events) noexcept override
    {
        for (const auto& event : events)
            rendered.push_back(event);
        return true;
    }
    void resetOutputs() noexcept override {}

    std::vector<lps::RoutedEvent> rendered;
};

void testPulsePlayerReusesLaneForTriggerIntensity()
{
    lps::PulsePlayer player { 0.5, 0.5 };
    player.publishModulationState(alternatingIntensity());
    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 1.1;
    block.playing = true;
    block.transportDiscontinuity = true;
    (void) player.process(block, {}, events);

    std::vector<float> intensities;
    lps::TriggerId lastStart;
    for (const auto& event : events)
    {
        if (event.type == lps::SemanticEventType::triggerStart)
        {
            intensities.push_back(event.normalizedValue);
            lastStart = event.triggerId;
        }
        else
        {
            CHECK(event.triggerId == lastStart);
        }
    }

    CHECK(intensities.size() == 3);
    CHECK(intensities[0] == 1.0f);
    CHECK(std::abs(intensities[1] - 100.0f / 255.0f) < 1.0e-7f);
    CHECK(intensities[2] == 1.0f);
    CHECK(player.modulationPlaybackSnapshot().currentStep == 0);
}

void testPulsePlayerUsesTheNormalEngineRoute()
{
    lps::PulsePlayer player;
    player.publishModulationState(alternatingIntensity());
    CollectingRenderer renderer;
    lps::SequencerEngine engine;
    const auto playerId = engine.registerPlayer(player);
    CHECK(playerId.has_value());
    CHECK(engine.connect(
        *playerId,
        renderer,
        lps::RouteMapping::fixedPitch(48.0f)).has_value());
    engine.prepare({ 48'000.0, 512 });

    lps::TimelineBlock block;
    block.ppqEnd = 0.01;
    block.tempoBpm = 120.0;
    block.sampleRate = 48'000.0;
    block.sampleCount = 512;
    block.playing = true;
    block.transportDiscontinuity = true;
    engine.run(block);

    CHECK(renderer.rendered.size() == 1);
    CHECK(renderer.rendered[0].sourcePlayerId == *playerId);
    CHECK(renderer.rendered[0].mappedPitchSemitones == 48.0f);
    CHECK(renderer.rendered[0].event.type
        == lps::SemanticEventType::triggerStart);
}

void testPitchGateProbabilityAndControlTargetsCompose()
{
    lps::PulsePlayer player { 0.5, 0.5 };
    player.setBasePitch(60.0f);

    lps::ModulationLaneState pitch;
    pitch.length = 2;
    pitch.values[0] = lps::NormalizedValue::fromUnipolar8(0);
    pitch.values[1] = lps::NormalizedValue::fromUnipolar8(255);
    CHECK(player.addModulationLane(
        laneDefinition(2, lps::ModulationTarget::pitch,
            lps::ModulationAdvancePoint::candidateTrigger,
            -12.0f, 12.0f, lps::ModulationCombineMode::add),
        pitch));

    lps::ModulationLaneState gate;
    gate.length = 1;
    gate.values[0] = lps::NormalizedValue::fromUnipolar8(0);
    CHECK(player.addModulationLane(
        laneDefinition(3, lps::ModulationTarget::gateLength,
            lps::ModulationAdvancePoint::sourceStep, 0.25f, 0.25f),
        gate));

    lps::ModulationLaneState control;
    control.length = 1;
    control.values[0] = lps::NormalizedValue::fromUnipolar8(128);
    auto controlDefinition = laneDefinition(
        4, lps::ModulationTarget::control,
        lps::ModulationAdvancePoint::time, 0.0f, 1.0f);
    controlDefinition.logicalControl = 74;
    CHECK(player.addModulationLane(controlDefinition, control));

    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 0.6;
    block.playing = true;
    block.transportDiscontinuity = true;
    (void) player.process(block, {}, events);

    std::vector<lps::SequencerEvent> starts;
    std::vector<lps::SequencerEvent> ends;
    std::vector<lps::SequencerEvent> controls;
    for (const auto& event : events)
    {
        if (event.type == lps::SemanticEventType::triggerStart)
            starts.push_back(event);
        else if (event.type == lps::SemanticEventType::triggerEnd)
            ends.push_back(event);
        else
            controls.push_back(event);
    }
    CHECK(starts.size() == 2);
    CHECK(starts[0].hasMusicalPitch && starts[0].musicalPitchSemitones == 48.0f);
    CHECK(starts[1].hasMusicalPitch && starts[1].musicalPitchSemitones == 72.0f);
    CHECK(ends.size() == 1);
    CHECK(std::abs(ends[0].ppqPosition - 0.125) < 1.0e-9);
    CHECK(controls.size() == 2);
    CHECK(controls[0].logicalControl == 74);
}

void testRejectedCandidatesDoNotAdvanceEmittedTriggerLanes()
{
    lps::PulsePlayer player { 0.5, 0.5 };

    lps::ModulationLaneState probability;
    probability.length = 2;
    probability.values[0] = lps::NormalizedValue::fromUnipolar8(0);
    probability.values[1] = lps::NormalizedValue::fromUnipolar8(255);
    CHECK(player.addModulationLane(
        laneDefinition(2, lps::ModulationTarget::probability,
            lps::ModulationAdvancePoint::candidateTrigger, 0.0f, 1.0f),
        probability));

    lps::ModulationLaneState emittedIntensity;
    emittedIntensity.length = 2;
    emittedIntensity.values[0] = lps::NormalizedValue::fromUnipolar8(100);
    emittedIntensity.values[1] = lps::NormalizedValue::fromUnipolar8(200);
    CHECK(player.addModulationLane(
        laneDefinition(3, lps::ModulationTarget::intensity,
            lps::ModulationAdvancePoint::emittedTrigger, 0.0f, 1.0f),
        emittedIntensity));

    lps::SequencerEventBuffer events;
    lps::TimelineBlock block;
    block.ppqEnd = 1.1;
    block.playing = true;
    block.transportDiscontinuity = true;
    (void) player.process(block, {}, events);

    std::vector<lps::SequencerEvent> starts;
    for (const auto& event : events)
        if (event.type == lps::SemanticEventType::triggerStart)
            starts.push_back(event);
    CHECK(starts.size() == 1);
    CHECK(std::abs(starts[0].ppqPosition - 0.5) < 1.0e-9);
    CHECK(std::abs(starts[0].normalizedValue - 100.0f / 255.0f) < 1.0e-7f);
}
} // namespace

int main()
{
    testPulsePlayerReusesLaneForTriggerIntensity();
    testPulsePlayerUsesTheNormalEngineRoute();
    testPitchGateProbabilityAndControlTargetsCompose();
    testRejectedCandidatesDoNotAdvanceEmittedTriggerLanes();
    std::cout << "All pulse player tests passed.\n";
    return EXIT_SUCCESS;
}
