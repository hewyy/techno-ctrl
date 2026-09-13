#include "core/RuntimeGraph.h"

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

constexpr lps::VoiceParameterId pitchId {0};
constexpr lps::VoiceParameterId intensityId {1};
constexpr lps::VoiceParameterId gateId {2};

void testNewModulationValuesAreSampledBeforeHit()
{
    lps::PatternLibrary patterns;
    lps::ModulationLibrary modulations;
    lps::PatternPlayer pattern(patterns);
    pattern.setRuntimeId(1);

    lps::ModulationPlayer pitch(modulations, lps::ModulationPlayerId {1});
    lps::ModulationPlayer intensity(modulations, lps::ModulationPlayerId {2});
    lps::ModulationPlayer gate(modulations, lps::ModulationPlayerId {3});
    for (auto* player : {&pitch, &intensity, &gate})
        player->setAdvanceSource(
            lps::PatternHitAdvance {lps::PatternPlayerId {1}});
    pitch.setValue(0, lps::NormalizedValue::fromFloat(36.0f / 127.0f));
    intensity.setValue(0, lps::NormalizedValue::fromUnipolar8(201));
    gate.setValue(0, lps::NormalizedValue::fromFloat(0.5f));

    lps::Voice voice(lps::VoiceId {1});
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::pitch(pitchId)));
    CHECK(voice.addParameter(
        lps::VoiceParameterDescriptor::intensity(intensityId)));
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::gate(gateId)));

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(pattern));
    CHECK(graph.registerModulationPlayer(pitch));
    CHECK(graph.registerModulationPlayer(intensity));
    CHECK(graph.registerModulationPlayer(gate));
    CHECK(graph.registerVoice(voice));

    lps::RuntimeGraphConfig config;
    CHECK(config.add(lps::TriggerBinding {{0}, {1}, {1}}));
    CHECK(config.add({{0}, {1}, {1}, pitchId}));
    CHECK(config.add({{1}, {2}, {1}, intensityId}));
    CHECK(config.add({{2}, {3}, {1}, gateId}));
    CHECK(graph.activate(config));
    graph.prepare({48'000.0, 12'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.25, 120.0, 48'000.0, 12'000, true, true}, output));
    CHECK(output.size() == 2);
    CHECK(output[0].event.type == lps::SemanticEventType::triggerStart);
    CHECK(output[0].sourceVoiceId == lps::VoiceId {1});
    CHECK(std::abs(output[0].event.musicalPitchSemitones - 36.0f) < 0.01f);
    CHECK(output[1].event.type == lps::SemanticEventType::triggerEnd);
    CHECK(std::abs(output[1].event.ppqPosition - 0.125) < 0.0001);
}

void testValidationRejectsPhaseOneMultipleSources()
{
    lps::PatternLibrary patterns;
    lps::ModulationLibrary modulations;
    lps::PatternPlayer pattern(patterns);
    pattern.setRuntimeId(1);
    lps::ModulationPlayer first(modulations, lps::ModulationPlayerId {1});
    lps::ModulationPlayer second(modulations, lps::ModulationPlayerId {2});
    lps::Voice voice(lps::VoiceId {1});
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::pitch(pitchId)));
    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(pattern));
    CHECK(graph.registerModulationPlayer(first));
    CHECK(graph.registerModulationPlayer(second));
    CHECK(graph.registerVoice(voice));

    lps::RuntimeGraphConfig config;
    CHECK(config.add({{0}, {1}, {1}, pitchId}));
    CHECK(config.add({{1}, {2}, {1}, pitchId}));
    CHECK(graph.validate(config)
        == lps::GraphValidationError::multipleParameterSources);
}

void testValidationRejectsControlCycles()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer first(patterns);
    lps::PatternPlayer second(patterns);
    first.setRuntimeId(1);
    second.setRuntimeId(2);
    second.setAdvanceSource(
        lps::PatternHitAdvance {lps::PatternPlayerId {1}});
    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(first));
    CHECK(graph.registerPatternPlayer(second));

    lps::RuntimeGraphConfig config;
    CHECK(config.add({
        {0},
        {{2}, lps::ControlSourcePort::hit},
        lps::PlayerRef::pattern({1}),
        lps::PlayerCommand::resetAndPlay}));
    CHECK(graph.validate(config) == lps::GraphValidationError::controlCycle);
}

void testActivationKeepsOldGraphOnRejection()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer pattern(patterns);
    pattern.setRuntimeId(1);
    lps::Voice voice(lps::VoiceId {1});
    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(pattern));
    CHECK(graph.registerVoice(voice));
    lps::RuntimeGraphConfig valid;
    CHECK(valid.add(lps::TriggerBinding {{0}, {1}, {1}}));
    CHECK(graph.activate(valid));
    auto invalid = valid;
    CHECK(invalid.add(lps::TriggerBinding {{1}, {1}, {1}}));
    CHECK(!graph.activate(invalid));
    CHECK(graph.lastValidationError()
        == lps::GraphValidationError::duplicateBinding);
}
} // namespace

int main()
{
    testNewModulationValuesAreSampledBeforeHit();
    testValidationRejectsPhaseOneMultipleSources();
    testValidationRejectsControlCycles();
    testActivationKeepsOldGraphOnRejection();
    std::cout << "RuntimeGraph tests passed\n";
    return EXIT_SUCCESS;
}
