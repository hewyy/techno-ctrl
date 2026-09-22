#include "core/RuntimeGraph.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

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

class CapturingRenderer final : public lps::IOutputRenderer
{
public:
    void prepare(const lps::RenderSpec&) noexcept override {}
    bool renderBlock(
        const lps::TimelineBlock&,
        lps::RoutedEventView events) noexcept override
    {
        eventCount = events.size();
        for (std::size_t index = 0; index < eventCount; ++index)
            captured[index] = events[index];
        return true;
    }
    void resetOutputs() noexcept override { eventCount = 0; }

    std::array<lps::RoutedEvent, 16> captured {};
    std::size_t eventCount = 0;
};

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
    CapturingRenderer renderer;
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
    CHECK(graph.registerOutputEndpoint({0}, renderer));

    lps::RuntimeGraphConfig config;
    CHECK(config.add(lps::TriggerBinding {{0}, {1}, {1}}));
    CHECK(config.add({{0}, {1}, {1}, pitchId}));
    CHECK(config.add({{1}, {2}, {1}, intensityId}));
    CHECK(config.add({{2}, {3}, {1}, gateId}));
    CHECK(config.add(lps::OutputBinding {
        {0}, {1}, {0}, {7}, {}, lps::OutputSignalType::triggers}));
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
    CHECK(graph.render(
        {0.0, 0.25, 120.0, 48'000.0, 12'000, true, true}, output));
    CHECK(renderer.eventCount == 2);
    CHECK(renderer.captured[0].routeId == lps::RouteId {7});

    graph.setVoiceMuted({1}, true);
    CHECK(graph.process(
        {1.0, 1.25, 120.0, 48'000.0, 12'000, true, true}, output));
    CHECK(output.size() == 2);
    CHECK(graph.render(
        {1.0, 1.25, 120.0, 48'000.0, 12'000, true, true}, output));
    CHECK(renderer.eventCount == 0);
}

void testRenderClampsFinalHalfSampleEventToLastFrame()
{
    lps::Voice voice(lps::VoiceId {0});
    CapturingRenderer renderer;
    lps::RuntimeGraph graph;
    CHECK(graph.registerVoice(voice));
    CHECK(graph.registerOutputEndpoint({0}, renderer));

    lps::RuntimeGraphConfig config;
    CHECK(config.add(lps::OutputBinding {
        {0}, {0}, {0}, {0}, {}, lps::OutputSignalType::triggers}));
    CHECK(graph.activate(config));

    constexpr double sampleRate = 44'100.0;
    constexpr double tempoBpm = 126.0;
    constexpr std::uint32_t sampleCount = 512;
    constexpr double ppqStart = 189.209821615;
    const auto ppqPerSample = tempoBpm / (60.0 * sampleRate);
    const auto ppqEnd = ppqStart
        + static_cast<double>(sampleCount) * ppqPerSample;
    const auto eventPpq = ppqStart
        + (static_cast<double>(sampleCount) - 0.25) * ppqPerSample;
    const lps::TimelineBlock block {
        ppqStart, ppqEnd, tempoBpm, sampleRate, sampleCount, true, false};

    graph.prepare({sampleRate, sampleCount});
    lps::ResolvedVoiceEventBuffer events;
    CHECK(events.push({
        lps::SequencerEvent::triggerStart(eventPpq, {1}, 1.0f, 60.0f),
        {0}, {}, 0, true}));

    CHECK(graph.render(block, events));
    CHECK(renderer.eventCount == 1);
    CHECK(renderer.captured[0].frameOffset == sampleCount - 1);
}

void testValidationRejectsCollidingUnscopedParameterSources()
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

void testMultiplePatternPlayersShareVoiceWithScopedPitchAndSuppression()
{
    lps::PatternLibrary patterns;
    lps::ModulationLibrary modulations;
    lps::PatternPlayer firstPattern(patterns);
    lps::PatternPlayer secondPattern(patterns);
    firstPattern.setRuntimeId(1);
    secondPattern.setRuntimeId(2);

    std::array<std::unique_ptr<lps::ModulationPlayer>, 6> modPlayers;
    for (std::size_t index = 0; index < modPlayers.size(); ++index)
    {
        modPlayers[index] = std::make_unique<lps::ModulationPlayer>(
            modulations,
            lps::ModulationPlayerId {static_cast<std::uint32_t>(index + 1)});
        const auto source = index < 3
            ? lps::PatternPlayerId {1} : lps::PatternPlayerId {2};
        modPlayers[index]->setAdvanceSource(lps::PatternHitAdvance {source});
    }
    modPlayers[0]->setValue(
        0, lps::NormalizedValue::fromFloat(36.0f / 127.0f));
    modPlayers[3]->setValue(
        0, lps::NormalizedValue::fromFloat(72.0f / 127.0f));
    for (const auto index : {1u, 4u})
        modPlayers[index]->setValue(0, lps::NormalizedValue::fromFloat(1.0f));
    for (const auto index : {2u, 5u})
        modPlayers[index]->setValue(0, lps::NormalizedValue::fromFloat(0.5f));

    lps::Voice voice(lps::VoiceId {1});
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::pitch(pitchId)));
    CHECK(voice.addParameter(
        lps::VoiceParameterDescriptor::intensity(intensityId)));
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::gate(gateId)));
    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(firstPattern));
    CHECK(graph.registerPatternPlayer(secondPattern));
    for (auto& player : modPlayers)
        CHECK(graph.registerModulationPlayer(*player));
    CHECK(graph.registerVoice(voice));

    lps::RuntimeGraphConfig config;
    CHECK(config.add(lps::TriggerBinding {{0}, {1}, {1}}));
    CHECK(config.add(lps::TriggerBinding {{1}, {2}, {1}}));
    constexpr std::array parameters {
        pitchId, intensityId, gateId, pitchId, intensityId, gateId
    };
    for (std::size_t index = 0; index < modPlayers.size(); ++index)
    {
        const auto scope = index < 3
            ? lps::PatternPlayerId {1} : lps::PatternPlayerId {2};
        CHECK(config.add(lps::ParameterBinding {
            lps::ParameterBindingId {static_cast<std::uint32_t>(index)},
            lps::ModulationPlayerId {static_cast<std::uint32_t>(index + 1)},
            {1},
            parameters[index],
            scope}));
    }
    CHECK(graph.validate(config) == lps::GraphValidationError::none);
    CHECK(graph.activate(config));
    graph.setSuppression({1}, {2}, true);
    graph.prepare({48'000.0, 12'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.25, 120.0, 48'000.0, 12'000, true, true}, output));
    bool foundFirst = false;
    bool foundSecond = false;
    for (const auto& resolved : output)
    {
        if (resolved.event.type != lps::SemanticEventType::triggerStart)
            continue;
        if (resolved.triggerSource == lps::PatternPlayerId {1})
        {
            foundFirst = true;
            CHECK(resolved.eligible);
            CHECK(std::abs(
                resolved.event.musicalPitchSemitones - 36.0f) < 0.01f);
        }
        if (resolved.triggerSource == lps::PatternPlayerId {2})
        {
            foundSecond = true;
            CHECK(!resolved.eligible);
            CHECK(std::abs(
                resolved.event.musicalPitchSemitones - 72.0f) < 0.01f);
        }
    }
    CHECK(foundFirst);
    CHECK(foundSecond);

    graph.setSuppression({1}, {2}, false);
    graph.setPatternPlayerMuted({1}, true);
    CHECK(graph.patternPlayerMuted({1}));
    CHECK(!graph.patternPlayerMuted({2}));
    CHECK(!graph.voiceMuted({1}));
    CHECK(graph.process(
        {1.0, 1.25, 120.0, 48'000.0, 12'000, true, true}, output));
    foundFirst = false;
    foundSecond = false;
    for (const auto& resolved : output)
    {
        if (resolved.event.type != lps::SemanticEventType::triggerStart)
            continue;
        if (resolved.triggerSource == lps::PatternPlayerId {1})
        {
            foundFirst = true;
            CHECK(!resolved.eligible);
        }
        if (resolved.triggerSource == lps::PatternPlayerId {2})
        {
            foundSecond = true;
            CHECK(resolved.eligible);
        }
    }
    CHECK(foundFirst);
    CHECK(foundSecond);
}

void testPatternPlayerMuteSchedulerKeepsSharedVoiceIndependent()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer first(patterns);
    lps::PatternPlayer second(patterns);
    first.setRuntimeId(0);
    second.setRuntimeId(1);
    lps::Voice sharedVoice(lps::VoiceId {0});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(first));
    CHECK(graph.registerPatternPlayer(second));
    CHECK(graph.registerVoice(sharedVoice));
    CHECK(graph.activate({}));
    CHECK(graph.configureBarClock({0}));
    graph.prepare({48'000.0, 24'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.5, 120.0, 48'000.0, 24'000, true, true}, output));
    CHECK(!graph.schedulePatternPlayerMute({0}, true, 0));
    CHECK(graph.schedulePatternPlayerMute({0}, true, 1));
    CHECK(graph.scheduledPatternPlayerMute({0})
        == std::optional<bool> {true});
    CHECK(!graph.scheduledPatternPlayerMute({1}).has_value());
    CHECK(graph.scheduledPatternPlayerMuteBarsRemaining({0}) == 1);
    CHECK(graph.patternPlayerMuteScheduledAtBarOffset({0}, true, 1));
    CHECK(!graph.patternPlayerMuteScheduledAtBarOffset({1}, true, 1));

    CHECK(graph.process(
        {0.5, 1.1, 120.0, 48'000.0, 28'800, true, false}, output));
    CHECK(graph.patternPlayerMuted({0}));
    CHECK(!graph.patternPlayerMuted({1}));
    CHECK(!graph.voiceMuted({0}));
    CHECK(!graph.scheduledPatternPlayerMute({0}).has_value());
}

void testArmedResetRestartsPatternAndModulationAtMasterRestBoundary()
{
    lps::PatternLibrary patterns;
    lps::ModulationLibrary modulations;
    lps::PatternPlayer master(patterns);
    lps::PatternPlayer follower(patterns);
    master.setRuntimeId(0);
    follower.setRuntimeId(1);
    master.toggleStep(0); // The cycle boundary must not depend on a hit.
    follower.selectPattern(lps::PatternId {2});

    lps::ModulationPlayer modulation(
        modulations, lps::ModulationPlayerId {3});
    modulation.selectModulation(lps::ModulationId {2});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(master));
    CHECK(graph.registerPatternPlayer(follower));
    CHECK(graph.registerModulationPlayer(modulation));

    lps::RuntimeGraphConfig config;
    CHECK(config.addPlayerConfig(lps::ModulationPlayerRuntimeConfig {
        {3}, lps::PatternHitAdvance {{1}},
        lps::PlayMode::continuous, true}));
    CHECK(graph.activate(config));
    CHECK(graph.configureArmedCycleCommand(
        1, {0}, lps::PlayerRef::pattern({1}),
        lps::PlayerCommand::resetAndPlay));
    CHECK(graph.configureArmedCycleCommand(
        1, {0}, lps::PlayerRef::modulation({3}),
        lps::PlayerCommand::resetAndPlay));
    graph.prepare({48'000.0, 36'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.75, 120.0, 48'000.0, 36'000, true, true}, output));
    CHECK(modulation.status().currentStep == 2);
    CHECK(graph.armCycleCommand(1));
    CHECK(graph.cycleCommandPending(1));

    CHECK(graph.process(
        {0.75, 1.25, 120.0, 48'000.0, 24'000, true, false}, output));
    CHECK(!graph.cycleCommandPending(1));
    CHECK(follower.patternPlaybackSnapshot().currentStep == 0);
    CHECK(modulation.status().currentStep == 0);
}

void testArmedHitResetUsesFirstValueOnTheTriggeringHit()
{
    lps::PatternLibrary patterns;
    lps::ModulationLibrary modulations;
    lps::PatternPlayer source(patterns);
    source.setRuntimeId(1);
    source.selectPattern(lps::PatternId {2});

    lps::ModulationPlayer modulation(
        modulations, lps::ModulationPlayerId {3});
    modulation.selectModulation(lps::ModulationId {2});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(source));
    CHECK(graph.registerModulationPlayer(modulation));

    lps::RuntimeGraphConfig config;
    CHECK(config.addPlayerConfig(lps::ModulationPlayerRuntimeConfig {
        {3}, lps::PatternHitAdvance {{1}},
        lps::PlayMode::continuous, true}));
    CHECK(graph.activate(config));
    CHECK(graph.configureArmedCommand(
        20,
        {{1}, lps::ControlSourcePort::hit},
        lps::PlayerRef::modulation({3}),
        lps::PlayerCommand::resetAndPlay));
    graph.prepare({48'000.0, 36'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.75, 120.0, 48'000.0, 36'000, true, true}, output));
    CHECK(modulation.status().currentStep == 2);
    CHECK(graph.armCommand(20));
    CHECK(graph.commandPending(20));

    CHECK(graph.process(
        {0.75, 1.0, 120.0, 48'000.0, 12'000, true, false}, output));
    CHECK(!graph.commandPending(20));
    CHECK(modulation.status().currentStep == 0);
}

void testArmedClockResetUsesMasterCycleBoundaryWithoutAHit()
{
    lps::PatternLibrary patterns;
    lps::ModulationLibrary modulations;
    lps::PatternPlayer master(patterns);
    master.setRuntimeId(0);
    master.toggleStep(0); // The master loop now contains no hits.
    master.setPlaybackWindow(0, 2);

    lps::ModulationPlayer modulation(
        modulations, lps::ModulationPlayerId {3});
    modulation.selectModulation(lps::ModulationId {2});
    constexpr lps::VoiceParameterId controlId {9};
    lps::Voice voice(lps::VoiceId {4});
    lps::VoiceParameterDescriptor control;
    control.id = controlId;
    control.role = lps::VoiceParameterRole::continuousControl;
    control.behavior = lps::VoiceParameterBehavior::continuous;
    CHECK(voice.addParameter(control));

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(master));
    CHECK(graph.registerModulationPlayer(modulation));
    CHECK(graph.registerVoice(voice));

    lps::RuntimeGraphConfig config;
    CHECK(config.addPlayerConfig(lps::ModulationPlayerRuntimeConfig {
        {3}, lps::ClockAdvance {0.25},
        lps::PlayMode::continuous, false}));
    CHECK(config.add(lps::ParameterBinding {
        {9}, {3}, {4}, controlId, {}}));
    CHECK(graph.activate(config));
    CHECK(graph.configureArmedCommand(
        21,
        {{0}, lps::ControlSourcePort::cycleBoundary},
        lps::PlayerRef::modulation({3}),
        lps::PlayerCommand::resetAndPlay));
    graph.prepare({48'000.0, 36'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.75, 120.0, 48'000.0, 36'000, true, true}, output));
    CHECK(modulation.status().currentStep == 2);
    CHECK(graph.armCommand(21));

    CHECK(graph.process(
        {0.75, 1.0, 120.0, 48'000.0, 12'000, true, false}, output));
    CHECK(!graph.commandPending(21));
    CHECK(modulation.status().currentStep == 0);
    CHECK(output.size() == 1);
    CHECK(std::abs(output[0].event.ppqPosition - 0.75) < 1.0e-12);
    CHECK(std::abs(output[0].event.normalizedValue - 1.0f) < 0.001f);
}

void testBarSchedulerDefersMuteToMasterCycleAndBoundsTheHorizon()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer master(patterns);
    master.setRuntimeId(0);
    lps::Voice voice(lps::VoiceId {0});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(master));
    CHECK(graph.registerVoice(voice));
    CHECK(graph.activate({}));
    CHECK(graph.configureBarClock({0}));
    graph.prepare({48'000.0, 24'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.5, 120.0, 48'000.0, 24'000, true, true}, output));
    CHECK(graph.currentBar() == 1);
    CHECK(!graph.voiceMuted({0}));

    CHECK(!graph.scheduleVoiceMute({0}, true, 0));
    CHECK(!graph.scheduleVoiceMute(
        {0}, true, lps::RuntimeGraph::maximumScheduledBars + 1));
    CHECK(graph.scheduleVoiceMute({0}, true, 1));
    CHECK(graph.scheduleVoiceMute({0}, false, 1));
    CHECK(graph.scheduledChangeCountAtBarOffset(1) == 1);
    CHECK(graph.scheduledVoiceMute({0}) == std::optional<bool> {false});
    CHECK(graph.voiceMuteScheduledAtBarOffset({0}, false, 1));
    CHECK(!graph.voiceMuteScheduledAtBarOffset({0}, true, 1));
    CHECK(graph.scheduleVoiceMute({0}, true, 1));
    CHECK(graph.scheduleVoiceMute(
        {0}, false, lps::RuntimeGraph::maximumScheduledBars));
    CHECK(graph.scheduledVoiceMute({0}) == std::optional<bool> {true});
    CHECK(graph.scheduledVoiceMuteBarsRemaining({0}) == 1);
    CHECK(graph.voiceMuteScheduledAtBarOffset({0}, true, 1));
    CHECK(graph.voiceMuteScheduledAtBarOffset(
        {0}, false, lps::RuntimeGraph::maximumScheduledBars));
    CHECK(graph.scheduledChangeCountAtBarOffset(1) == 1);
    CHECK(graph.scheduledChangeCountAtBarOffset(
        lps::RuntimeGraph::maximumScheduledBars) == 1);

    CHECK(graph.process(
        {0.5, 0.9, 120.0, 48'000.0, 19'200, true, false}, output));
    CHECK(!graph.voiceMuted({0}));
    CHECK(graph.currentBar() == 1);

    CHECK(graph.process(
        {0.9, 1.1, 120.0, 48'000.0, 9'600, true, false}, output));
    CHECK(graph.voiceMuted({0}));
    CHECK(graph.scheduledVoiceMute({0}) == std::optional<bool> {false});
    CHECK(graph.scheduledVoiceMuteBarsRemaining({0}) == 7);
    CHECK(graph.currentBar() == 2);

    CHECK(graph.process(
        {1.1, 7.1, 120.0, 48'000.0, 288'000, true, false}, output));
    CHECK(graph.currentBar() == 8);
    CHECK(graph.voiceMuted({0}));
    CHECK(graph.scheduledVoiceMuteBarsRemaining({0}) == 1);
    CHECK(graph.process(
        {7.1, 8.1, 120.0, 48'000.0, 48'000, true, false}, output));
    CHECK(graph.currentBar() == 1);
    CHECK(!graph.voiceMuted({0}));
    CHECK(!graph.scheduledVoiceMute({0}).has_value());
}

void testBarSchedulerDoesNotApplyAtStartOfFirstBar()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer master(patterns);
    master.setRuntimeId(0);
    lps::Voice voice(lps::VoiceId {0});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(master));
    CHECK(graph.registerVoice(voice));
    CHECK(graph.activate({}));
    CHECK(graph.configureBarClock({0}));
    graph.prepare({48'000.0, 24'000});

    CHECK(graph.scheduleVoiceMute({0}, true, 1));

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.5, 120.0, 48'000.0, 24'000, true, true}, output));
    CHECK(graph.currentBar() == 1);
    CHECK(!graph.voiceMuted({0}));
    CHECK(graph.scheduledVoiceMuteBarsRemaining({0}) == 1);

    CHECK(graph.process(
        {0.5, 1.1, 120.0, 48'000.0, 28'800, true, false}, output));
    CHECK(graph.currentBar() == 2);
    CHECK(graph.voiceMuted({0}));
    CHECK(!graph.scheduledVoiceMute({0}).has_value());
}

void testBarSchedulerSupportsIndependentFutureChanges()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer master(patterns);
    lps::PatternPlayer follower(patterns);
    master.setRuntimeId(0);
    follower.setRuntimeId(1);
    master.setTransitionPolicy({
        lps::PatternTransitionPolicyType::explicitBoundary, {}});
    follower.setTransitionPolicy({
        lps::PatternTransitionPolicyType::explicitBoundary, {}});
    lps::Voice voice(lps::VoiceId {1});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(master));
    CHECK(graph.registerPatternPlayer(follower));
    CHECK(graph.registerVoice(voice));
    CHECK(graph.activate({}));
    CHECK(graph.configureBarClock({0}));
    graph.prepare({48'000.0, 48'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.5, 120.0, 48'000.0, 24'000, true, true}, output));
    CHECK(graph.schedulePatternSelection({1}, lps::PatternId {2}, 2));
    CHECK(graph.scheduleVoiceMute({1}, true, 2));
    CHECK(graph.scheduleVoiceMute({1}, false, 4));
    CHECK(graph.patternSelectionScheduledAtBarOffset({1}, 2)
        == std::optional<lps::PatternId> {lps::PatternId {2}});
    CHECK(!graph.patternSelectionScheduledAtBarOffset({1}, 1).has_value());
    CHECK(graph.scheduledChangeCountAtBarOffset(2) == 2);
    CHECK(graph.scheduledChangeCountAtBarOffset(4) == 1);

    CHECK(graph.process(
        {0.5, 1.1, 120.0, 48'000.0, 28'800, true, false}, output));
    CHECK(follower.activePatternId() == lps::PatternId {1});
    CHECK(!graph.voiceMuted({1}));
    CHECK(graph.scheduledPatternBarsRemaining({1}) == 1);

    CHECK(graph.process(
        {1.1, 2.1, 120.0, 48'000.0, 48'000, true, false}, output));
    CHECK(follower.activePatternId() == lps::PatternId {2});
    CHECK(!graph.patternSelectionScheduledAtBarOffset({1}, 1).has_value());
    CHECK(graph.voiceMuted({1}));
    CHECK(graph.scheduledVoiceMuteBarsRemaining({1}) == 2);

    CHECK(graph.process(
        {2.1, 4.1, 120.0, 48'000.0, 96'000, true, false}, output));
    CHECK(!graph.voiceMuted({1}));
    CHECK(graph.currentBar() == 5);
}

void testScheduledMasterPatternChangeCountsOneBoundary()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer master(patterns);
    master.setRuntimeId(0);
    master.setTransitionPolicy({
        lps::PatternTransitionPolicyType::explicitBoundary, {}});

    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(master));
    CHECK(graph.activate({}));
    CHECK(graph.configureBarClock({0}));
    graph.prepare({48'000.0, 48'000});

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.5, 120.0, 48'000.0, 24'000, true, true}, output));
    CHECK(graph.schedulePatternSelection({0}, lps::PatternId {2}, 1));
    CHECK(graph.process(
        {0.5, 1.1, 120.0, 48'000.0, 28'800, true, false}, output));
    CHECK(master.activePatternId() == lps::PatternId {2});
    CHECK(graph.currentBar() == 2);
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

void testPublishedGraphIsAdoptedAtBlockBoundary()
{
    lps::PatternLibrary patterns;
    lps::PatternPlayer pattern(patterns);
    pattern.setRuntimeId(1);
    lps::Voice voice(lps::VoiceId {1});
    lps::RuntimeGraph graph;
    CHECK(graph.registerPatternPlayer(pattern));
    CHECK(graph.registerVoice(voice));

    lps::RuntimeGraphConfig initial;
    CHECK(initial.add(lps::TriggerBinding {{0}, {1}, {1}}));
    CHECK(initial.addPlayerConfig(lps::PatternPlayerRuntimeConfig {
        {1}, lps::ClockAdvance {0.25}, lps::PlayMode::continuous, {}}));
    CHECK(graph.activate(initial));
    graph.prepare({48'000.0, 256});
    const auto initialGeneration = graph.activeGeneration();

    lps::RuntimeGraphConfig replacement;
    CHECK(replacement.addPlayerConfig(lps::PatternPlayerRuntimeConfig {
        {1}, lps::ClockAdvance {0.25}, lps::PlayMode::oneShot, {}}));
    CHECK(graph.publish(replacement));
    CHECK(graph.publishedGeneration() > initialGeneration);
    CHECK(graph.activeGeneration() == initialGeneration);

    lps::ResolvedVoiceEventBuffer output;
    CHECK(graph.process(
        {0.0, 0.01, 120.0, 48'000.0, 256, false, false}, output));
    CHECK(graph.activeGeneration() == graph.publishedGeneration());
    CHECK(pattern.playMode() == lps::PlayMode::oneShot);
}
} // namespace

int main()
{
    testNewModulationValuesAreSampledBeforeHit();
    testRenderClampsFinalHalfSampleEventToLastFrame();
    testArmedResetRestartsPatternAndModulationAtMasterRestBoundary();
    testArmedHitResetUsesFirstValueOnTheTriggeringHit();
    testArmedClockResetUsesMasterCycleBoundaryWithoutAHit();
    testBarSchedulerDefersMuteToMasterCycleAndBoundsTheHorizon();
    testBarSchedulerDoesNotApplyAtStartOfFirstBar();
    testBarSchedulerSupportsIndependentFutureChanges();
    testScheduledMasterPatternChangeCountsOneBoundary();
    testPatternPlayerMuteSchedulerKeepsSharedVoiceIndependent();
    testValidationRejectsCollidingUnscopedParameterSources();
    testMultiplePatternPlayersShareVoiceWithScopedPitchAndSuppression();
    testValidationRejectsControlCycles();
    testActivationKeepsOldGraphOnRejection();
    testPublishedGraphIsAdoptedAtBlockBoundary();
    std::cout << "RuntimeGraph tests passed\n";
    return EXIT_SUCCESS;
}
