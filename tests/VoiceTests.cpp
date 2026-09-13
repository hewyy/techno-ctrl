#include "core/Voice.h"

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
constexpr lps::VoiceParameterId controlId {3};

lps::PlayerSignal value(double ppq, float normalized, std::uint8_t step = 0)
{
    return lps::PlayerSignal::modulationValue(
        ppq,
        lps::ModulationPlayerId {4},
        lps::NormalizedValue::fromFloat(normalized),
        step);
}

lps::PlayerSignal hit(double ppq, double nominalStep = 0.25)
{
    return lps::PlayerSignal::patternHit(
        ppq,
        lps::PatternPlayerId {1},
        lps::TriggerId {9},
        nominalStep);
}

void configureVoice(lps::Voice& voice)
{
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::pitch(pitchId)));
    CHECK(voice.addParameter(
        lps::VoiceParameterDescriptor::intensity(intensityId)));
    CHECK(voice.addParameter(lps::VoiceParameterDescriptor::gate(gateId)));
}

void setRequiredValues(
    lps::Voice& voice,
    lps::SequencerEventBuffer& output,
    float pitch,
    float intensity,
    float gate)
{
    CHECK(voice.applyParameterValue(pitchId, value(0.0, pitch), output));
    CHECK(voice.applyParameterValue(intensityId, value(0.0, intensity), output));
    CHECK(voice.applyParameterValue(gateId, value(0.0, gate), output));
}

void testMissingRequiredValueDropsTrigger()
{
    lps::Voice voice(lps::VoiceId {2});
    configureVoice(voice);
    lps::SequencerEventBuffer output;
    CHECK(!voice.trigger(hit(1.0), 0.001, output));
    CHECK(output.empty());
    CHECK(voice.diagnostics().droppedMissingRequired == 1);
}

void testSampledValuesAndGateLifetime()
{
    lps::Voice voice(lps::VoiceId {2});
    configureVoice(voice);
    lps::SequencerEventBuffer output;
    setRequiredValues(voice, output, 36.0f / 127.0f, 201.0f / 255.0f, 0.5f);

    CHECK(voice.trigger(hit(1.0), 0.001, output));
    CHECK(output.size() == 1);
    CHECK(output[0].type == lps::SemanticEventType::triggerStart);
    CHECK(output[0].hasMusicalPitch);
    CHECK(std::abs(output[0].musicalPitchSemitones - 36.0f) < 0.01f);
    CHECK(std::abs(output[0].normalizedValue - 201.0f / 255.0f) < 0.001f);

    voice.emitEndsBefore(1.125, false, output);
    CHECK(output.size() == 1);
    voice.emitEndsBefore(1.126, false, output);
    CHECK(output.size() == 2);
    CHECK(output[1].type == lps::SemanticEventType::triggerEnd);
    CHECK(output[1].triggerId == output[0].triggerId);
    CHECK(std::abs(output[1].ppqPosition - 1.125) < 0.0001);
}

void testRetriggerEndsBeforeReplacementStart()
{
    lps::Voice voice(lps::VoiceId {2});
    configureVoice(voice);
    lps::SequencerEventBuffer output;
    setRequiredValues(voice, output, 0.5f, 1.0f, 1.0f);
    CHECK(voice.trigger(hit(2.0), 0.001, output));
    CHECK(voice.trigger(hit(2.1), 0.001, output));
    CHECK(output.size() == 3);
    CHECK(output[1].type == lps::SemanticEventType::triggerEnd);
    CHECK(output[2].type == lps::SemanticEventType::triggerStart);
    CHECK(output[1].ppqPosition == output[2].ppqPosition);
    CHECK(output[0].triggerId != output[2].triggerId);
}

void testZeroGateWithholdsStart()
{
    lps::Voice voice(lps::VoiceId {2});
    configureVoice(voice);
    lps::SequencerEventBuffer output;
    setRequiredValues(voice, output, 0.5f, 1.0f, 0.0f);
    CHECK(voice.trigger(hit(3.0), 0.001, output));
    CHECK(output.empty());
    CHECK(!voice.active());
}

void testContinuousParameterEmitsWithoutTrigger()
{
    lps::Voice voice(lps::VoiceId {2});
    configureVoice(voice);
    lps::VoiceParameterDescriptor descriptor;
    descriptor.id = controlId;
    descriptor.role = lps::VoiceParameterRole::continuousControl;
    descriptor.behavior = lps::VoiceParameterBehavior::continuous;
    descriptor.minimum = -1.0f;
    descriptor.maximum = 1.0f;
    descriptor.interpolation = lps::InterpolationPolicy::linear;
    CHECK(voice.addParameter(descriptor));

    lps::SequencerEventBuffer output;
    CHECK(voice.applyParameterValue(controlId, value(4.0, 0.75f), output));
    CHECK(output.size() == 1);
    CHECK(output[0].type == lps::SemanticEventType::controlPoint);
    CHECK(output[0].voiceParameterId == controlId);
    CHECK(output[0].interpolation == lps::InterpolationPolicy::linear);
    CHECK(std::abs(output[0].normalizedValue - 0.5f) < 0.001f);
}
} // namespace

int main()
{
    testMissingRequiredValueDropsTrigger();
    testSampledValuesAndGateLifetime();
    testRetriggerEndsBeforeReplacementStart();
    testZeroGateWithholdsStart();
    testContinuousParameterEmitsWithoutTrigger();
    std::cout << "Voice tests passed\n";
    return EXIT_SUCCESS;
}
