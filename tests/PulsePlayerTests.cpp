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
} // namespace

int main()
{
    testPulsePlayerReusesLaneForTriggerIntensity();
    testPulsePlayerUsesTheNormalEngineRoute();
    std::cout << "All pulse player tests passed.\n";
    return EXIT_SUCCESS;
}
