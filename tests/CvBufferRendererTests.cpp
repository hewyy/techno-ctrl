#include "plugin/CvBufferRenderer.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

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

lps::RoutedEvent trigger(
    lps::SemanticEventType type,
    std::uint64_t id,
    std::uint32_t frame,
    float pitch = 60.0f)
{
    lps::RoutedEvent event;
    event.event = type == lps::SemanticEventType::triggerStart
        ? lps::SequencerEvent::triggerStart(0.0, { id }, 1.0f)
        : lps::SequencerEvent::triggerEnd(0.0, { id });
    event.sourcePlayerId = { 0 };
    event.routeId = { 1 };
    event.frameOffset = frame;
    event.mappedPitchSemitones = pitch;
    event.hasMappedPitch = true;
    return event;
}

void checkChannel(
    const juce::AudioBuffer<float>& buffer,
    int channel,
    std::initializer_list<float> expected)
{
    CHECK(buffer.getNumSamples() == static_cast<int>(expected.size()));
    int sample = 0;
    for (const auto value : expected)
        CHECK(std::abs(buffer.getSample(channel, sample++) - value) < 1.0e-6f);
}

void testGateHoldsAcrossEmptyBlocksAndReleasesAtExactFrame()
{
    lps::CvBufferRenderer renderer;
    CHECK(renderer.configureRoute({ 1 }, { 0, 1, 2 }));
    renderer.prepare({ 48'000.0, 8 });
    juce::AudioBuffer<float> buffer { 3, 8 };
    renderer.setAudioBuffer(buffer);

    const auto start = trigger(lps::SemanticEventType::triggerStart, 7, 2, 72.0f);
    CHECK(renderer.renderBlock({}, { &start, 1 }));
    checkChannel(buffer, 0, { 0, 0, 1, 1, 1, 1, 1, 1 });
    checkChannel(buffer, 1, { 0, 0, 1, 1, 1, 1, 1, 1 });

    buffer.clear();
    CHECK(renderer.renderBlock({}, {}));
    checkChannel(buffer, 0, { 1, 1, 1, 1, 1, 1, 1, 1 });

    auto end = trigger(lps::SemanticEventType::triggerEnd, 7, 3);
    end.hasMappedPitch = false;
    CHECK(renderer.renderBlock({}, { &end, 1 }));
    checkChannel(buffer, 0, { 1, 1, 1, 0, 0, 0, 0, 0 });
}

void testUnmatchedEndCannotLowerAnotherSourcesGate()
{
    lps::CvBufferRenderer renderer;
    CHECK(renderer.configureRoute({ 1 }, { 0, 1, 2 }));
    juce::AudioBuffer<float> buffer { 3, 8 };
    renderer.setAudioBuffer(buffer);

    const auto start = trigger(lps::SemanticEventType::triggerStart, 1, 0);
    CHECK(renderer.renderBlock({}, { &start, 1 }));
    auto unrelatedEnd = trigger(lps::SemanticEventType::triggerEnd, 99, 4);
    unrelatedEnd.sourcePlayerId = { 9 };
    CHECK(renderer.renderBlock({}, { &unrelatedEnd, 1 }));
    checkChannel(buffer, 0, { 1, 1, 1, 1, 1, 1, 1, 1 });
}

void testControlRampsAndResetForcesEveryOutputLow()
{
    lps::CvBufferRenderer renderer;
    CHECK(renderer.configureRoute({ 1 }, { 0, 1, 2 }));
    juce::AudioBuffer<float> buffer { 3, 5 };
    renderer.setAudioBuffer(buffer);

    auto control = lps::RoutedEvent {};
    control.event = lps::SequencerEvent::controlPoint(
        0.0, 4, 1.0f, lps::InterpolationPolicy::linear);
    control.routeId = { 1 };
    control.frameOffset = 4;
    CHECK(renderer.renderBlock({}, { &control, 1 }));
    checkChannel(buffer, 2, { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f });

    const auto start = trigger(lps::SemanticEventType::triggerStart, 2, 0, 72.0f);
    CHECK(renderer.renderBlock({}, { &start, 1 }));
    renderer.resetOutputs();
    checkChannel(buffer, 0, { 0, 0, 0, 0, 0 });
    checkChannel(buffer, 1, { 0, 0, 0, 0, 0 });
    checkChannel(buffer, 2, { 0, 0, 0, 0, 0 });
}
} // namespace

int main()
{
    testGateHoldsAcrossEmptyBlocksAndReleasesAtExactFrame();
    testUnmatchedEndCannotLowerAnotherSourcesGate();
    testControlRampsAndResetForcesEveryOutputLow();
    std::cout << "All CV buffer renderer tests passed.\n";
    return EXIT_SUCCESS;
}
