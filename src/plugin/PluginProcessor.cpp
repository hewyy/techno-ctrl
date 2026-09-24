#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace
{
constexpr int drumMidiChannel = 11;
constexpr int synthOneMidiChannel = 12;
constexpr int synthTwoMidiChannel = 13;
constexpr std::size_t synthTwoVoiceIndex = 11;
constexpr std::size_t firstSampleVoiceIndex = 12;
constexpr std::size_t firstSamplePlayerIndex =
    synthTwoVoiceIndex + LivePatternSequencerProcessor::synthTwoPatternCount;

struct SynthParameterDefinition
{
    const char* section;
    const char* name;
    std::uint8_t midiCc;
    std::uint8_t initialValue;
    int secondaryMidiCc = -1;
    int maximumValue = 127;
};

// Korg volca keys MIDI implementation from volca keys.csv. These remain
// data, rather than new parameter types: every row is a 7-bit continuous
// Voice parameter whose renderer route determines the outgoing MIDI CC.
constexpr std::array synthTwoParameters {
    SynthParameterDefinition {"VCO", "Portamento", 5, 0},
    SynthParameterDefinition {"General", "Expression", 11, 127},
    SynthParameterDefinition {"General", "Voice", 40, 0},
    SynthParameterDefinition {"General", "Octave", 41, 44},
    SynthParameterDefinition {"VCO", "Detune", 42, 64},
    SynthParameterDefinition {"VCO", "VCO EG depth", 43, 64},
    SynthParameterDefinition {"VCF", "Cutoff", 44, 127},
    SynthParameterDefinition {"VCF", "VCF EG intensity", 45, 64},
    SynthParameterDefinition {"LFO", "Rate", 46, 64},
    SynthParameterDefinition {"LFO", "Pitch intensity", 47, 0},
    SynthParameterDefinition {"LFO", "Cutoff intensity", 48, 0},
    SynthParameterDefinition {"EG", "Attack", 49, 0},
    SynthParameterDefinition {"EG", "Decay/release", 50, 64},
    SynthParameterDefinition {"EG", "Sustain", 51, 127},
    SynthParameterDefinition {"Delay", "Delay time", 52, 0},
    SynthParameterDefinition {"Delay", "Delay feedback", 53, 0}
};

// Korg volca sample MIDI implementation. Each of the ten parts listens on
// its matching MIDI channel. Sample selection is one logical 0...199
// parameter rendered as CC 3 (hundreds) followed by CC 35 (remainder).
constexpr std::array sampleParameters {
    SynthParameterDefinition {"Sample", "Current sample", 3, 0, 35, 199},
    SynthParameterDefinition {"Sample", "Level", 7, 127},
    SynthParameterDefinition {"Sample", "Pan", 10, 64},
    SynthParameterDefinition {"Sample", "Start point", 40, 0},
    SynthParameterDefinition {"Sample", "Length", 41, 127},
    SynthParameterDefinition {"Sample", "Hi cut", 42, 127},
    SynthParameterDefinition {"Sample", "Speed", 43, 64},
    SynthParameterDefinition {"Sample", "Pitch EG intensity", 44, 64},
    SynthParameterDefinition {"Sample", "Pitch EG attack", 45, 0},
    SynthParameterDefinition {"Sample", "Pitch EG decay", 46, 64},
    SynthParameterDefinition {"Sample", "Amp EG attack", 47, 0},
    SynthParameterDefinition {"Sample", "Amp EG decay", 48, 64}
};

struct VoiceDefinition
{
    const char* name;
    std::uint8_t midiNote;
    bool isMaster;
    std::uint8_t midiChannel;
    bool hasCvOutput;
};

// Default voice map. Mark exactly one voice as the master. Adding or
// removing definitions here does not require routing, matrix, or editor changes.
constexpr std::array defaultVoices {
    VoiceDefinition { "BD1", 36, true, drumMidiChannel, true },
    VoiceDefinition { "BD2", 37, false, drumMidiChannel, true },
    VoiceDefinition { "Machine", 38, false, drumMidiChannel, true },
    VoiceDefinition { "Snare", 39, false, drumMidiChannel, true },
    VoiceDefinition { "Clap", 40, false, drumMidiChannel, true },
    VoiceDefinition { "Rimshot", 41, false, drumMidiChannel, true },
    VoiceDefinition { "OH", 42, false, drumMidiChannel, true },
    VoiceDefinition { "CH", 43, false, drumMidiChannel, true },
    VoiceDefinition { "Crash", 44, false, drumMidiChannel, true },
    VoiceDefinition { "Ride", 45, false, drumMidiChannel, true },
    VoiceDefinition { "Synth 1", 46, false, synthOneMidiChannel, false },
    VoiceDefinition { "Synth 2", 47, false, synthTwoMidiChannel, false },
    VoiceDefinition { "Sample 1", 60, false, 1, false },
    VoiceDefinition { "Sample 2", 60, false, 2, false },
    VoiceDefinition { "Sample 3", 60, false, 3, false },
    VoiceDefinition { "Sample 4", 60, false, 4, false },
    VoiceDefinition { "Sample 5", 60, false, 5, false },
    VoiceDefinition { "Sample 6", 60, false, 6, false },
    VoiceDefinition { "Sample 7", 60, false, 7, false },
    VoiceDefinition { "Sample 8", 60, false, 8, false },
    VoiceDefinition { "Sample 9", 60, false, 9, false },
    VoiceDefinition { "Sample 10", 60, false, 10, false }
};

constexpr std::size_t voiceIndexForPlayer(std::size_t playerIndex) noexcept
{
    if (playerIndex <= synthTwoVoiceIndex)
        return playerIndex;
    if (playerIndex < firstSamplePlayerIndex)
        return synthTwoVoiceIndex;
    return playerIndex - (LivePatternSequencerProcessor::synthTwoPatternCount - 1);
}

constexpr std::size_t configuredMasterCount() noexcept
{
    std::size_t count = 0;
    for (const auto& voice : defaultVoices)
        if (voice.isMaster)
            ++count;
    return count;
}

constexpr std::size_t configuredMasterIndex() noexcept
{
    for (std::size_t index = 0; index < defaultVoices.size(); ++index)
        if (defaultVoices[index].isMaster)
            return index;
    return defaultVoices.size();
}

static_assert(configuredMasterCount() == 1,
    "Exactly one statically configured drum voice must be the master");

lps::Modulation constantModulation(float value) noexcept
{
    lps::Modulation modulation;
    modulation.length = 1;
    modulation.values[0] = lps::NormalizedValue::fromFloat(value);
    return modulation;
}
}

LivePatternSequencerProcessor::LivePatternSequencerProcessor()
    : LivePatternSequencerProcessor(
        PatternLibraryFileStore::defaultCatalogFile())
{
}

LivePatternSequencerProcessor::LivePatternSequencerProcessor(
    const juce::File& patternCatalogFile)
    : juce::AudioProcessor(
          juce::AudioProcessor::BusesProperties().withOutput(
              "CV Output",
              juce::AudioChannelSet::discreteChannels(cvOutputChannelCount),
              true)),
      patternLibraryFileStore_(patternCatalogFile),
      modulationLibraryFileStore_(
          patternCatalogFile.getSiblingFile("modulations.json")),
      drumRenderer_(std::make_unique<lps::MidiBufferRenderer>(
          drumMidiChannel)),
      synthOneRenderer_(std::make_unique<lps::MidiBufferRenderer>(
          synthOneMidiChannel)),
      synthTwoRenderer_(std::make_unique<lps::MidiBufferRenderer>(
          synthTwoMidiChannel)),
      sampleRenderer_(std::make_unique<lps::MidiBufferRenderer>(1)),
      cvRenderer_(std::make_unique<lps::CvBufferRenderer>()),
      runtimeGraph_(std::make_unique<lps::RuntimeGraph>())
{
    const auto logDirectory = patternCatalogFile.getParentDirectory();
    (void) logDirectory.createDirectory();
    const auto logFile = logDirectory.getChildFile("sequencer.log");
    const auto* configuredLevel = std::getenv("LPS_LOG_LEVEL");
    const auto* configuredStderr = std::getenv("LPS_LOG_STDERR");
    const lps::LoggerConfig loggerConfig {
        logFile.getFullPathName().toStdString(),
        configuredLevel != nullptr
            ? lps::Logger::parseLevel(configuredLevel)
            : lps::LogLevel::info,
        configuredStderr != nullptr
            && std::string_view {configuredStderr} == "1"
    };
    if (lps::Logger::configure(loggerConfig))
    {
        lps::Logger::logf(lps::LogLevel::info, "plugin", "instance_starting",
            "version=%s log_file=\"%s\"",
            JucePlugin_VersionString,
            logFile.getFullPathName().toRawUTF8());
    }

    // Catalog replacement is startup-only and must finish before PatternPlayer
    // instances retain references to the library.
    const bool patternsLoaded = patternLibraryFileStore_.loadOrCreate(patternLibrary_);
    const bool modulationsLoaded = modulationLibraryFileStore_.loadOrCreate(
        modulationLibrary_);
    if (!patternsLoaded)
        lps::Logger::logf(lps::LogLevel::error, "plugin", "pattern_catalog_load_failed",
            "reason=\"%s\"", patternLibraryFileStore_.lastError().toRawUTF8());
    if (!modulationsLoaded)
        lps::Logger::logf(lps::LogLevel::error, "plugin", "modulation_catalog_load_failed",
            "reason=\"%s\"", modulationLibraryFileStore_.lastError().toRawUTF8());

    const auto totalPlayerCount = defaultVoices.size()
        + synthTwoPatternCount - 1;
    players_.reserve(totalPlayerCount);
    pitchEditContexts_.reserve(totalPlayerCount);
    voices_.reserve(defaultVoices.size());
    synthParameterLanes_.reserve(synthTwoParameters.size());
    sampleParameterLanes_.reserve(
        samplePartCount * sampleParameters.size());
    currentSteps_.reserve(totalPlayerCount);
    currentModulationSteps_.reserve(
        totalPlayerCount * modulationLaneCount);
    modulationLocks_.reserve(totalPlayerCount * modulationLaneCount);
    playerGroupMasks_.reserve(totalPlayerCount);
    synthParameterCurrentSteps_.reserve(synthTwoParameters.size());
    sampleParameterCurrentSteps_.reserve(
        samplePartCount * sampleParameters.size());
    synthLaneAdvanceModes_.reserve(
        synthTwoPatternCount + synthTwoParameters.size());
    synthLanePatternSlots_.reserve(
        synthTwoPatternCount + synthTwoParameters.size());
    sampleLaneAdvanceModes_.reserve(
        samplePartCount * (sampleParameters.size() + 1));
    sampleLanePatternSlots_.reserve(
        samplePartCount * (sampleParameters.size() + 1));

    const auto addDefaultModulation = [this](
        std::string name,
        const lps::Modulation& modulation)
    {
        auto insertion = modulationLibrary_.addOrFind(
            name,
            modulation,
            [this](lps::ModulationLibraryEntry& stagedEntry)
            {
                return modulationLibraryFileStore_.persistNewEntry(
                    modulationLibrary_, stagedEntry);
            });
        if (insertion.entry == nullptr)
            insertion = modulationLibrary_.addOrFind(
                std::move(name), modulation);
        return insertion;
    };

    const auto gateRecord = addDefaultModulation(
        "Constant Gate 1/2", constantModulation(0.5f));
    jassert(gateRecord.entry != nullptr);
    const bool outputsRegistered = runtimeGraph_->registerOutputEndpoint(
            drumMidiOutputEndpoint, *drumRenderer_)
        && runtimeGraph_->registerOutputEndpoint(
            cvOutputEndpoint, *cvRenderer_)
        && runtimeGraph_->registerOutputEndpoint(
            synthOneMidiOutputEndpoint, *synthOneRenderer_)
        && runtimeGraph_->registerOutputEndpoint(
            synthTwoMidiOutputEndpoint, *synthTwoRenderer_)
        && runtimeGraph_->registerOutputEndpoint(
            sampleMidiOutputEndpoint, *sampleRenderer_);
    jassert(outputsRegistered);
    (void) outputsRegistered;

    constexpr lps::VoiceParameterId pitchParameter {0};
    constexpr lps::VoiceParameterId intensityParameter {1};
    constexpr lps::VoiceParameterId gateParameter {2};
    for (std::size_t voiceIndex = 0;
         voiceIndex < defaultVoices.size();
         ++voiceIndex)
    {
        const auto voiceId = lps::VoiceId {
            static_cast<std::uint32_t>(voiceIndex) };
        auto resolvedVoice = std::make_unique<lps::Voice>(voiceId);
        bool voiceConfigured = resolvedVoice->addParameter(
                lps::VoiceParameterDescriptor::pitch(pitchParameter))
            && resolvedVoice->addParameter(
                lps::VoiceParameterDescriptor::intensity(intensityParameter))
            && resolvedVoice->addParameter(
                lps::VoiceParameterDescriptor::gate(gateParameter));
        if (voiceIndex == synthTwoVoiceIndex)
        {
            for (std::size_t parameterIndex = 0;
                 parameterIndex < synthTwoParameters.size();
                 ++parameterIndex)
            {
                lps::VoiceParameterDescriptor descriptor;
                descriptor.id = lps::VoiceParameterId {
                    static_cast<std::uint16_t>(parameterIndex + 3) };
                descriptor.role = lps::VoiceParameterRole::continuousControl;
                descriptor.behavior = lps::VoiceParameterBehavior::continuous;
                descriptor.minimum = 0.0f;
                descriptor.maximum = 127.0f;
                descriptor.interpolation = lps::InterpolationPolicy::step;
                voiceConfigured = resolvedVoice->addParameter(descriptor)
                    && voiceConfigured;
            }
        }
        if (voiceIndex >= firstSampleVoiceIndex)
        {
            for (std::size_t parameterIndex = 0;
                 parameterIndex < sampleParameters.size();
                 ++parameterIndex)
            {
                lps::VoiceParameterDescriptor descriptor;
                descriptor.id = lps::VoiceParameterId {
                    static_cast<std::uint16_t>(parameterIndex + 3) };
                descriptor.role = lps::VoiceParameterRole::continuousControl;
                descriptor.behavior = lps::VoiceParameterBehavior::continuous;
                descriptor.minimum = 0.0f;
                descriptor.maximum = static_cast<float>(
                    sampleParameters[parameterIndex].maximumValue);
                descriptor.interpolation = lps::InterpolationPolicy::step;
                voiceConfigured = resolvedVoice->addParameter(descriptor)
                    && voiceConfigured;
            }
        }
        const bool registered = runtimeGraph_->registerVoice(*resolvedVoice);
        jassert(voiceConfigured && registered);
        (void) voiceConfigured;
        (void) registered;
        voices_.push_back(std::move(resolvedVoice));
    }

    for (std::size_t index = 0; index < totalPlayerCount; ++index)
    {
        const auto voiceIndex = voiceIndexForPlayer(index);
        const auto& voice = defaultVoices[voiceIndex];
        const auto voiceId = lps::VoiceId {
            static_cast<std::uint32_t>(voiceIndex) };
        auto player = std::make_unique<lps::PatternPlayer>(patternLibrary_);
        player->setRuntimeId(static_cast<std::uint32_t>(index));
        if (patternLibrary_.size() != 0)
        {
            if (const auto* pattern = patternLibrary_.recordAt(index % patternLibrary_.size()))
                player->selectPattern(pattern->id);
        }
        auto* patternController = player.get();
        const auto patternId = lps::PatternPlayerId {
            static_cast<std::uint32_t>(index) };
        const auto modulationBase = static_cast<std::uint32_t>(index * 3);
        const auto pitchModulationId = lps::ModulationPlayerId {modulationBase};
        const auto velocityModulationId = lps::ModulationPlayerId {
            modulationBase + 1 };
        const auto gateModulationId = lps::ModulationPlayerId {
            modulationBase + 2 };

        const auto pitchRecord = addDefaultModulation(
            "Constant Pitch " + std::to_string(voice.midiNote),
            constantModulation(static_cast<float>(voice.midiNote) / 127.0f));
        jassert(pitchRecord.entry != nullptr);
        auto pitchPlayer = std::make_unique<lps::ModulationPlayer>(
            modulationLibrary_, pitchModulationId);
        auto velocityPlayer = std::make_unique<lps::ModulationPlayer>(
            modulationLibrary_, velocityModulationId);
        auto gatePlayer = std::make_unique<lps::ModulationPlayer>(
            modulationLibrary_, gateModulationId);
        pitchPlayer->selectModulation(pitchRecord.entry->id);
        gatePlayer->selectModulation(gateRecord.entry->id);
        const auto hitAdvance = lps::PatternHitAdvance {patternId};

        const bool nodesRegistered = runtimeGraph_->registerPatternPlayer(*player)
            && runtimeGraph_->registerModulationPlayer(*pitchPlayer)
            && runtimeGraph_->registerModulationPlayer(*velocityPlayer)
            && runtimeGraph_->registerModulationPlayer(*gatePlayer);
        jassert(nodesRegistered);
        (void) nodesRegistered;

        const auto transitionPolicy = waitForCycleBeforeSelection
            ? lps::PatternTransitionPolicy {
                lps::PatternTransitionPolicyType::explicitBoundary, {}}
            : lps::PatternTransitionPolicy {
                lps::PatternTransitionPolicyType::immediate, {}};
        const bool playerConfigsAdded = runtimeConfig_.addPlayerConfig(
                {patternId, lps::ClockAdvance {0.25},
                    lps::PlayMode::continuous, transitionPolicy})
            && runtimeConfig_.addPlayerConfig(
                {pitchModulationId, hitAdvance, lps::PlayMode::continuous,
                    waitForCycleBeforeSelection})
            && runtimeConfig_.addPlayerConfig(
                {velocityModulationId, hitAdvance, lps::PlayMode::continuous,
                    waitForCycleBeforeSelection})
            && runtimeConfig_.addPlayerConfig(
                {gateModulationId, hitAdvance, lps::PlayMode::continuous,
                    waitForCycleBeforeSelection});
        jassert(playerConfigsAdded);
        (void) playerConfigsAdded;

        const bool bindingsAdded = runtimeConfig_.add(lps::TriggerBinding {
                lps::TriggerBindingId {static_cast<std::uint32_t>(index)},
                patternId, voiceId})
            && runtimeConfig_.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationBase},
                pitchModulationId, voiceId, pitchParameter, patternId})
            && runtimeConfig_.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationBase + 1},
                velocityModulationId, voiceId, intensityParameter, patternId})
            && runtimeConfig_.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationBase + 2},
                gateModulationId, voiceId, gateParameter, patternId});
        jassert(bindingsAdded);
        (void) bindingsAdded;

        const auto routeId = lps::RouteId {
            static_cast<std::uint32_t>(voiceIndex)};
        const auto cvRouteId = routeId;

        PlayerBundle bundle;
        const auto synthSlot = index >= synthTwoVoiceIndex
                && index < firstSamplePlayerIndex
            ? index - synthTwoVoiceIndex : 0;
        const auto playerName = voiceIndex == synthTwoVoiceIndex
            ? juce::String("Synth 2 / Pattern ")
                + juce::String(static_cast<int>(synthSlot + 1))
            : juce::String(voice.name);
        bundle.descriptor = {
            playerName,
            voice.midiNote,
            voice.midiChannel,
            voice.hasCvOutput,
            voiceIndex,
            voiceId
        };
        bundle.patternController = patternController;
        bundle.patternModel = patternController;
        bundle.pitchPlayer = std::move(pitchPlayer);
        bundle.velocityPlayer = std::move(velocityPlayer);
        bundle.gatePlayer = std::move(gatePlayer);
        bundle.midiRouteId = routeId;
        bundle.cvRouteId = cvRouteId;
        bundle.realtime = std::move(player);
        players_.push_back(std::move(bundle));
        pitchEditContexts_.push_back({});
        currentSteps_.push_back(std::make_unique<std::atomic<int>>(-1));
        playerGroupMasks_.push_back(
            std::make_unique<std::atomic<std::uint8_t>>(0));
        for (std::size_t lane = 0; lane < modulationLaneCount; ++lane)
        {
            currentModulationSteps_.push_back(
                std::make_unique<std::atomic<int>>(-1));
            modulationLocks_.push_back(
                std::make_unique<std::atomic<bool>>(false));
        }
    }

    for (std::size_t slot = 0; slot < synthTwoPatternCount; ++slot)
    {
        synthLaneAdvanceModes_.push_back(
            std::make_unique<std::atomic<std::uint8_t>>(
                static_cast<std::uint8_t>(ModulationAdvanceMode::onHit)));
        synthLanePatternSlots_.push_back(
            std::make_unique<std::atomic<std::size_t>>(slot));
    }
    for (std::size_t slot = 0; slot < samplePartCount; ++slot)
    {
        sampleLaneAdvanceModes_.push_back(
            std::make_unique<std::atomic<std::uint8_t>>(
                static_cast<std::uint8_t>(ModulationAdvanceMode::onHit)));
        sampleLanePatternSlots_.push_back(
            std::make_unique<std::atomic<std::size_t>>(slot));
    }

    const auto firstSynthParameterModulationId = static_cast<std::uint32_t>(
        totalPlayerCount * modulationLaneCount);
    const auto synthVoiceId = lps::VoiceId {
        static_cast<std::uint32_t>(synthTwoVoiceIndex) };
    const auto firstSynthPatternId = lps::PatternPlayerId {
        static_cast<std::uint32_t>(synthTwoVoiceIndex) };
    for (std::size_t parameterIndex = 0;
         parameterIndex < synthTwoParameters.size();
         ++parameterIndex)
    {
        const auto& definition = synthTwoParameters[parameterIndex];
        const auto modulationId = lps::ModulationPlayerId {
            firstSynthParameterModulationId
                + static_cast<std::uint32_t>(parameterIndex) };
        const auto parameterId = lps::VoiceParameterId {
            static_cast<std::uint16_t>(parameterIndex + 3) };
        const auto record = addDefaultModulation(
            std::string("Volca Keys ") + definition.name,
            constantModulation(
                static_cast<float>(definition.initialValue) / 127.0f));
        jassert(record.entry != nullptr);
        auto player = std::make_unique<lps::ModulationPlayer>(
            modulationLibrary_, modulationId);
        player->selectModulation(record.entry->id);
        const bool registered = runtimeGraph_->registerModulationPlayer(*player);
        const bool configured = runtimeConfig_.addPlayerConfig({
            modulationId,
            lps::PatternHitAdvance {firstSynthPatternId},
            lps::PlayMode::continuous,
            waitForCycleBeforeSelection});
        const bool bound = runtimeConfig_.add(lps::ParameterBinding {
            lps::ParameterBindingId {modulationId.value},
            modulationId,
            synthVoiceId,
            parameterId,
            {}});
        jassert(registered && configured && bound);
        (void) registered;
        (void) configured;
        (void) bound;

        synthParameterLanes_.push_back({
            std::move(player),
            parameterId,
            definition.section,
            definition.name,
            definition.midiCc
        });
        synthParameterCurrentSteps_.push_back(
            std::make_unique<std::atomic<int>>(-1));
        synthLaneAdvanceModes_.push_back(
            std::make_unique<std::atomic<std::uint8_t>>(
                static_cast<std::uint8_t>(ModulationAdvanceMode::onHit)));
        synthLanePatternSlots_.push_back(
            std::make_unique<std::atomic<std::size_t>>(0));
    }

    const auto firstSampleParameterModulationId =
        firstSynthParameterModulationId
        + static_cast<std::uint32_t>(synthTwoParameters.size());
    for (std::size_t partSlot = 0; partSlot < samplePartCount; ++partSlot)
    {
        const auto voiceIndex = firstSampleVoiceIndex + partSlot;
        const auto voiceId = lps::VoiceId {
            static_cast<std::uint32_t>(voiceIndex) };
        const auto patternId = lps::PatternPlayerId {
            static_cast<std::uint32_t>(samplePlayerIndexForUi(partSlot)) };
        for (std::size_t parameterIndex = 0;
             parameterIndex < sampleParameters.size();
             ++parameterIndex)
        {
            const auto& definition = sampleParameters[parameterIndex];
            const auto flatIndex = partSlot * sampleParameters.size()
                + parameterIndex;
            const auto modulationId = lps::ModulationPlayerId {
                firstSampleParameterModulationId
                    + static_cast<std::uint32_t>(flatIndex) };
            const auto parameterId = lps::VoiceParameterId {
                static_cast<std::uint16_t>(parameterIndex + 3) };
            const auto record = addDefaultModulation(
                "Volca Sample " + std::to_string(partSlot + 1) + " "
                    + definition.name,
                constantModulation(
                    static_cast<float>(definition.initialValue)
                        / static_cast<float>(definition.maximumValue)));
            jassert(record.entry != nullptr);
            auto player = std::make_unique<lps::ModulationPlayer>(
                modulationLibrary_, modulationId);
            player->selectModulation(record.entry->id);
            const bool registered = runtimeGraph_->registerModulationPlayer(
                *player);
            const bool configured = runtimeConfig_.addPlayerConfig({
                modulationId,
                lps::PatternHitAdvance {patternId},
                lps::PlayMode::continuous,
                waitForCycleBeforeSelection});
            const bool bound = runtimeConfig_.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationId.value},
                modulationId,
                voiceId,
                parameterId,
                {}});
            jassert(registered && configured && bound);
            (void) registered;
            (void) configured;
            (void) bound;

            sampleParameterLanes_.push_back({
                std::move(player),
                parameterId,
                "Part " + juce::String(static_cast<int>(partSlot + 1))
                    + " · MIDI Ch "
                    + juce::String(static_cast<int>(partSlot + 1)),
                definition.name,
                definition.midiCc,
                definition.secondaryMidiCc,
                definition.maximumValue,
                partSlot
            });
            sampleParameterCurrentSteps_.push_back(
                std::make_unique<std::atomic<int>>(-1));
            sampleLaneAdvanceModes_.push_back(
                std::make_unique<std::atomic<std::uint8_t>>(
                    static_cast<std::uint8_t>(ModulationAdvanceMode::onHit)));
            sampleLanePatternSlots_.push_back(
                std::make_unique<std::atomic<std::size_t>>(partSlot));
        }
    }

    std::uint32_t outputBindingId = 0;
    for (std::size_t voiceIndex = 0;
         voiceIndex < defaultVoices.size();
         ++voiceIndex)
    {
        const auto& voice = defaultVoices[voiceIndex];
        const auto voiceId = lps::VoiceId {
            static_cast<std::uint32_t>(voiceIndex) };
        const auto route = lps::RouteId {
            static_cast<std::uint32_t>(voiceIndex) };
        const auto midiEndpoint = voiceIndex >= firstSampleVoiceIndex
            ? sampleMidiOutputEndpoint
            : voice.midiChannel == synthOneMidiChannel
            ? synthOneMidiOutputEndpoint
            : voice.midiChannel == synthTwoMidiChannel
                ? synthTwoMidiOutputEndpoint
                : drumMidiOutputEndpoint;
        if (voiceIndex >= firstSampleVoiceIndex)
        {
            const bool channelConfigured = sampleRenderer_->configureMidiChannel(
                route, voice.midiChannel);
            jassert(channelConfigured);
            (void) channelConfigured;
        }
        bool added = runtimeConfig_.add(lps::OutputBinding {
            lps::OutputBindingId {outputBindingId++},
            voiceId,
            midiEndpoint,
            route,
            {},
            lps::OutputSignalType::triggers});
        if (voice.hasCvOutput)
        {
            added = runtimeConfig_.add(lps::OutputBinding {
                lps::OutputBindingId {outputBindingId++},
                voiceId,
                cvOutputEndpoint,
                route,
                {},
                lps::OutputSignalType::triggers}) && added;
            jassert(voiceIndex < cvPlayerCapacity);
            const int cvChannel = static_cast<int>(voiceIndex)
                * cvChannelsPerPlayer;
            const bool cvConfigured = cvRenderer_->configureRoute(
                route, {cvChannel, cvChannel + 1, cvChannel + 2});
            jassert(cvConfigured);
            (void) cvConfigured;
        }
        jassert(added);
        (void) added;
    }

    for (std::size_t parameterIndex = 0;
         parameterIndex < synthParameterLanes_.size();
         ++parameterIndex)
    {
        const auto route = lps::RouteId {
            static_cast<std::uint32_t>(100 + parameterIndex) };
        const auto& lane = synthParameterLanes_[parameterIndex];
        const bool routeConfigured = synthTwoRenderer_->configureControlRoute(
            route, lane.midiCc);
        const bool outputAdded = runtimeConfig_.add(lps::OutputBinding {
            lps::OutputBindingId {outputBindingId++},
            synthVoiceId,
            synthTwoMidiOutputEndpoint,
            route,
            lane.parameter,
            lps::OutputSignalType::continuousParameter});
        jassert(routeConfigured && outputAdded);
        (void) routeConfigured;
        (void) outputAdded;
    }

    for (std::size_t parameterIndex = 0;
         parameterIndex < sampleParameterLanes_.size();
         ++parameterIndex)
    {
        const auto route = lps::RouteId {
            static_cast<std::uint32_t>(1000 + parameterIndex) };
        const auto& lane = sampleParameterLanes_[parameterIndex];
        const auto voiceId = lps::VoiceId {static_cast<std::uint32_t>(
            firstSampleVoiceIndex + lane.patternSlot)};
        const bool controlConfigured = lane.secondaryMidiCc >= 0
            ? sampleRenderer_->configureVolcaSampleSelectRoute(
                route, lane.midiCc, lane.secondaryMidiCc)
            : sampleRenderer_->configureControlRoute(route, lane.midiCc);
        const bool routeConfigured = controlConfigured
            && sampleRenderer_->configureMidiChannel(
                route, static_cast<int>(lane.patternSlot + 1));
        const bool outputAdded = runtimeConfig_.add(lps::OutputBinding {
            lps::OutputBindingId {outputBindingId++},
            voiceId,
            sampleMidiOutputEndpoint,
            route,
            lane.parameter,
            lps::OutputSignalType::continuousParameter});
        jassert(routeConfigured && outputAdded);
        (void) routeConfigured;
        (void) outputAdded;
    }

    const bool graphActivated = runtimeGraph_->activate(runtimeConfig_);
    jassert(graphActivated);
    (void) graphActivated;
    const bool barClockConfigured = runtimeGraph_->configureBarClock(
        lps::PatternPlayerId {
            static_cast<std::uint32_t>(configuredMasterIndex()) });
    jassert(barClockConfigured);
    (void) barClockConfigured;
    for (std::size_t index = 0; index < players_.size(); ++index)
    {
        if (index == configuredMasterIndex())
            continue;
        const auto master = lps::PatternPlayerId {
            static_cast<std::uint32_t>(configuredMasterIndex()) };
        const auto modulationBase = static_cast<std::uint32_t>(index * 3);
        const bool configured = runtimeGraph_->configureArmedCycleCommand(
            index,
            master,
            lps::PlayerRef::pattern(lps::PatternPlayerId {
                static_cast<std::uint32_t>(index) }),
            lps::PlayerCommand::resetAndPlay)
            && runtimeGraph_->configureArmedCycleCommand(
                index,
                master,
                lps::PlayerRef::modulation(
                    lps::ModulationPlayerId {modulationBase}),
                lps::PlayerCommand::resetAndPlay)
            && runtimeGraph_->configureArmedCycleCommand(
                index,
                master,
                lps::PlayerRef::modulation(
                    lps::ModulationPlayerId {modulationBase + 1}),
                lps::PlayerCommand::resetAndPlay)
            && runtimeGraph_->configureArmedCycleCommand(
                index,
                master,
                lps::PlayerRef::modulation(
                    lps::ModulationPlayerId {modulationBase + 2}),
                lps::PlayerCommand::resetAndPlay);
        jassert(configured);
        (void) configured;
    }

    const auto master = lps::PatternPlayerId {
        static_cast<std::uint32_t>(configuredMasterIndex()) };
    for (std::size_t slot = 0; slot < synthTwoPatternCount; ++slot)
    {
        const auto playerIndex = synthTwoPlayerIndexForUi(slot);
        const bool configured = runtimeGraph_->configureArmedCommand(
            synthPatternResetGroup(slot),
            {master, lps::ControlSourcePort::cycleBoundary},
            lps::PlayerRef::pattern(lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex) }),
            lps::PlayerCommand::resetAndPlay);
        jassert(configured);
        (void) configured;
    }

    for (std::size_t laneIndex = 0;
         laneIndex < synthLaneCountForUi();
         ++laneIndex)
    {
        auto* lanePlayer = synthLanePlayerAt(laneIndex);
        jassert(lanePlayer != nullptr);
        if (lanePlayer == nullptr)
            continue;

        bool configured = runtimeGraph_->configureArmedCommand(
            synthLaneResetGroup(laneIndex, 0),
            {master, lps::ControlSourcePort::cycleBoundary},
            lanePlayer->playerRef(),
            lps::PlayerCommand::resetAndPlay);
        for (std::size_t slot = 0; slot < synthTwoPatternCount; ++slot)
        {
            const auto sourceIndex = synthTwoPlayerIndexForUi(slot);
            configured = runtimeGraph_->configureArmedCommand(
                synthLaneResetGroup(laneIndex, slot + 1),
                {lps::PatternPlayerId {
                     static_cast<std::uint32_t>(sourceIndex)},
                    lps::ControlSourcePort::hit},
                lanePlayer->playerRef(),
                lps::PlayerCommand::resetAndPlay) && configured;
        }
        jassert(configured);
        (void) configured;
    }

    for (std::size_t laneIndex = 0;
         laneIndex < sampleLaneCountForUi();
         ++laneIndex)
    {
        auto* lanePlayer = sampleLanePlayerAt(laneIndex);
        jassert(lanePlayer != nullptr);
        if (lanePlayer == nullptr)
            continue;

        bool configured = runtimeGraph_->configureArmedCommand(
            sampleLaneResetGroup(laneIndex, 0),
            {master, lps::ControlSourcePort::cycleBoundary},
            lanePlayer->playerRef(),
            lps::PlayerCommand::resetAndPlay);
        for (std::size_t slot = 0; slot < samplePartCount; ++slot)
        {
            const auto sourceIndex = samplePlayerIndexForUi(slot);
            configured = runtimeGraph_->configureArmedCommand(
                sampleLaneResetGroup(laneIndex, slot + 1),
                {lps::PatternPlayerId {
                     static_cast<std::uint32_t>(sourceIndex)},
                    lps::ControlSourcePort::hit},
                lanePlayer->playerRef(),
                lps::PlayerCommand::resetAndPlay) && configured;
        }
        jassert(configured);
        (void) configured;
    }
}

const juce::String LivePatternSequencerProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LivePatternSequencerProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet()
            == juce::AudioChannelSet::discreteChannels(cvOutputChannelCount);
}

void LivePatternSequencerProcessor::prepareToPlay(
    double sampleRate,
    int maximumExpectedSamplesPerBlock)
{
    const lps::PrepareSpec spec {
        sampleRate,
        static_cast<std::uint32_t>(std::max(maximumExpectedSamplesPerBlock, 0))
    };
    runtimeGraph_->prepare(spec);
    expectedNextPpq_.reset();
    wasPlaying_ = false;
    internalTransportWasPlaying_ = false;
    internalPpqPosition_ = 0.0;
    internalTransportRequested_.store(false, std::memory_order_relaxed);
    internalTransportPlaying_.store(false, std::memory_order_relaxed);
    hostTransportPlaying_.store(false, std::memory_order_relaxed);
    updateUiSnapshot();
}

void LivePatternSequencerProcessor::releaseResources()
{
    runtimeGraph_->reset();
    runtimeGraph_->resetOutputs();
    expectedNextPpq_.reset();
    wasPlaying_ = false;
    internalTransportWasPlaying_ = false;
    internalPpqPosition_ = 0.0;
    internalTransportRequested_.store(false, std::memory_order_relaxed);
    internalTransportPlaying_.store(false, std::memory_order_relaxed);
    hostTransportPlaying_.store(false, std::memory_order_relaxed);
    updateUiSnapshot();
}

void LivePatternSequencerProcessor::processBlock(
    juce::AudioBuffer<float>& audio,
    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    audio.clear();

    lps::TimelineBlock block;
    block.sampleRate = getSampleRate();
    block.sampleCount = static_cast<std::uint32_t>(audio.getNumSamples());
    block.tempoBpm = lastKnownTempoBpm_;

    bool hostPlaying = false;
    bool hostPpqAvailable = false;
    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            hostPlaying = position->getIsPlaying();

            if (const auto bpm = position->getBpm())
            {
                if (*bpm > 0.0)
                {
                    block.tempoBpm = *bpm;
                    lastKnownTempoBpm_ = *bpm;
                }
            }

            if (const auto ppq = position->getPpqPosition())
            {
                block.ppqStart = *ppq;
                hostPpqAvailable = true;
            }
            else
                hostPlaying = false;
        }
    }

    hostTransportPlaying_.store(hostPlaying, std::memory_order_relaxed);
    const bool wasInternalTransport = internalTransportWasPlaying_;
    bool usingInternalTransport = false;
    if (hostPlaying)
    {
        block.playing = true;
        internalTransportRequested_.store(false, std::memory_order_release);
        internalTransportPlaying_.store(false, std::memory_order_relaxed);
        internalTransportWasPlaying_ = false;
    }
    else if (internalTransportRequested_.load(std::memory_order_acquire))
    {
        if (!internalTransportWasPlaying_)
        {
            internalPpqPosition_ = hostPpqAvailable
                ? block.ppqStart
                : 0.0;
        }
        block.playing = true;
        block.tempoBpm = lastKnownTempoBpm_;
        block.ppqStart = internalPpqPosition_;
        usingInternalTransport = true;
        internalTransportWasPlaying_ = true;
        internalTransportPlaying_.store(true, std::memory_order_relaxed);
    }
    else
    {
        block.playing = false;
        internalTransportWasPlaying_ = false;
        internalPpqPosition_ = 0.0;
        internalTransportPlaying_.store(false, std::memory_order_relaxed);
    }

    const double ppqPerSample = block.tempoBpm > 0.0 && block.sampleRate > 0.0
        ? block.tempoBpm / (60.0 * block.sampleRate)
        : 0.0;
    block.ppqEnd = block.ppqStart
        + static_cast<double>(block.sampleCount) * ppqPerSample;
    if (usingInternalTransport)
        internalPpqPosition_ = block.ppqEnd;

    if (block.playing)
    {
        const double tolerance = ppqPerSample * 4.0 + 1.0e-7;
        block.transportDiscontinuity = !wasPlaying_
            || wasInternalTransport != usingInternalTransport
            || (expectedNextPpq_.has_value()
                && std::abs(block.ppqStart - *expectedNextPpq_) > tolerance);

        expectedNextPpq_ = block.ppqEnd;
    }
    else
    {
        block.transportDiscontinuity = wasPlaying_;
        expectedNextPpq_.reset();
    }

    wasPlaying_ = block.playing;
    drumRenderer_->setMidiBuffer(midi);
    synthOneRenderer_->setMidiBuffer(midi);
    synthTwoRenderer_->setMidiBuffer(midi);
    sampleRenderer_->setMidiBuffer(midi);
    cvRenderer_->setAudioBuffer(audio);
    lps::ResolvedVoiceEventBuffer resolvedEvents;
    if (!runtimeGraph_->process(block, resolvedEvents)
        || !runtimeGraph_->render(block, resolvedEvents))
    {
        runtimeGraph_->reset();
        runtimeGraph_->resetOutputs();
    }
    cvRenderer_->clearAudioBuffer();
    drumRenderer_->clearMidiBuffer();
    synthOneRenderer_->clearMidiBuffer();
    synthTwoRenderer_->clearMidiBuffer();
    sampleRenderer_->clearMidiBuffer();

    updateUiSnapshot();
}

juce::AudioProcessorEditor* LivePatternSequencerProcessor::createEditor()
{
    return new LivePatternSequencerEditor(*this);
}

void LivePatternSequencerProcessor::getStateInformation(
    juce::MemoryBlock& destination)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("format", "live-pattern-sequencer-graph-state");
    root->setProperty("schemaVersion", 3);

    juce::Array<juce::var> serializedPlayers;
    for (std::size_t index = 0; index < players_.size(); ++index)
    {
        const auto pattern = players_[index].patternController
            ->capturePersistentState();
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("patternId",
            static_cast<juce::int64>(pattern.patternId.value()));
        object->setProperty("hitMask",
            static_cast<juce::int64>(pattern.hitMask));
        object->setProperty("offset", pattern.patternOffset);
        object->setProperty("playbackStart", pattern.playbackStart);
        object->setProperty("playbackEnd", pattern.playbackEnd);
        object->setProperty("playbackSpeed", pattern.playbackSpeed);
        const auto serializeModulation = [this, &object, index](
            const char* idProperty,
            const char* valuesProperty,
            const char* lockedProperty,
            ModulationLane lane,
            const lps::ModulationPlayer& player)
        {
            object->setProperty(idProperty, static_cast<juce::int64>(
                player.selectedModulationId().value()));
            const auto modulation = player.modulationForSave();
            juce::Array<juce::var> values;
            for (std::size_t step = 0; step < modulation.length; ++step)
                values.add(static_cast<int>(modulation.values[step].raw));
            object->setProperty(valuesProperty, values);
            object->setProperty(
                lockedProperty,
                playerModulationLockedForUi(index, lane));
        };
        serializeModulation(
            "modulationId", "modulationValues", "modulationLocked",
            ModulationLane::velocity,
            *players_[index].velocityPlayer);
        serializeModulation(
            "pitchModulationId", "pitchModulationValues",
            "pitchModulationLocked", ModulationLane::pitch,
            *players_[index].pitchPlayer);
        serializeModulation(
            "gateModulationId", "gateModulationValues",
            "gateModulationLocked", ModulationLane::gate,
            *players_[index].gatePlayer);
        const auto pitchContext = pitchEditContextForPlayer(index);
        object->setProperty(
            "pitchEditMode", static_cast<int>(pitchContext.mode));
        object->setProperty(
            "pitchRootPitchClass",
            static_cast<int>(pitchContext.rootPitchClass));
        object->setProperty(
            "pitchScaleId", static_cast<int>(pitchContext.scaleId));
        object->setProperty("muted", playerMutedForUi(index));
        object->setProperty("groupMask", static_cast<int>(
            playerGroupMasks_[index]->load(std::memory_order_relaxed)));
        if (index >= synthTwoVoiceIndex
            && index < synthTwoVoiceIndex + synthTwoPatternCount)
        {
            const auto laneIndex = index - synthTwoVoiceIndex;
            object->setProperty(
                "pitchAdvanceMode",
                static_cast<int>(synthLaneAdvanceModeForUi(laneIndex)));
        }
        if (index >= firstSamplePlayerIndex
            && index < firstSamplePlayerIndex + samplePartCount)
        {
            const auto laneIndex = index - firstSamplePlayerIndex;
            object->setProperty(
                "samplePitchAdvanceMode",
                static_cast<int>(sampleLaneAdvanceModeForUi(laneIndex)));
            object->setProperty(
                "samplePitchPatternSlot",
                static_cast<int>(sampleLanePatternSlotForUi(laneIndex)));
        }
        serializedPlayers.add(juce::var(object.get()));
    }
    root->setProperty("players", serializedPlayers);

    juce::Array<juce::var> serializedSynthLanes;
    for (std::size_t index = 0; index < synthParameterLanes_.size(); ++index)
    {
        const auto laneIndex = synthTwoPatternCount + index;
        const auto& lane = synthParameterLanes_[index];
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("cc", lane.midiCc);
        object->setProperty("modulationId", static_cast<juce::int64>(
            lane.player->selectedModulationId().value()));
        juce::Array<juce::var> values;
        const auto modulation = lane.player->modulationForSave();
        for (std::size_t step = 0; step < modulation.length; ++step)
            values.add(static_cast<int>(modulation.values[step].raw));
        object->setProperty("values", values);
        object->setProperty(
            "advanceMode",
            static_cast<int>(synthLaneAdvanceModeForUi(laneIndex)));
        object->setProperty(
            "patternSlot",
            static_cast<int>(synthLanePatternSlotForUi(laneIndex)));
        serializedSynthLanes.add(juce::var(object.get()));
    }
    root->setProperty("synthTwoParameterLanes", serializedSynthLanes);

    juce::Array<juce::var> serializedSampleLanes;
    for (std::size_t index = 0; index < sampleParameterLanes_.size(); ++index)
    {
        const auto laneIndex = samplePartCount + index;
        const auto& lane = sampleParameterLanes_[index];
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("part", static_cast<int>(lane.patternSlot));
        object->setProperty("cc", lane.midiCc);
        object->setProperty("modulationId", static_cast<juce::int64>(
            lane.player->selectedModulationId().value()));
        juce::Array<juce::var> values;
        const auto modulation = lane.player->modulationForSave();
        for (std::size_t step = 0; step < modulation.length; ++step)
            values.add(static_cast<int>(modulation.values[step].raw));
        object->setProperty("values", values);
        object->setProperty(
            "advanceMode",
            static_cast<int>(sampleLaneAdvanceModeForUi(laneIndex)));
        object->setProperty(
            "patternSlot",
            static_cast<int>(sampleLanePatternSlotForUi(laneIndex)));
        serializedSampleLanes.add(juce::var(object.get()));
    }
    root->setProperty("sampleParameterLanes", serializedSampleLanes);

    juce::Array<juce::var> suppressions;
    for (std::size_t from = 0; from < players_.size(); ++from)
        for (std::size_t to = 0; to < players_.size(); ++to)
            if (suppression(from, to))
            {
                auto edge = juce::DynamicObject::Ptr(new juce::DynamicObject());
                edge->setProperty("from", static_cast<juce::int64>(from));
                edge->setProperty("to", static_cast<juce::int64>(to));
                suppressions.add(juce::var(edge.get()));
            }
    root->setProperty("suppressions", suppressions);

    const auto json = juce::JSON::toString(juce::var(root.get()), true);
    destination.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void LivePatternSequencerProcessor::setStateInformation(
    const void* data,
    int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;
    const auto parsed = juce::JSON::parse(juce::String::fromUTF8(
        static_cast<const char*>(data), sizeInBytes));
    const auto* root = parsed.getDynamicObject();
    const auto schemaVersion = root != nullptr
        ? static_cast<int>(root->getProperty("schemaVersion")) : 0;
    if (root == nullptr
        || root->getProperty("format").toString()
            != "live-pattern-sequencer-graph-state"
        || schemaVersion < 1 || schemaVersion > 3)
        return;

    const auto* serializedPlayers = root->getProperty("players").getArray();
    if (serializedPlayers == nullptr
        || serializedPlayers->isEmpty()
        || serializedPlayers->size() > static_cast<int>(players_.size()))
        return;

    for (int index = 0; index < serializedPlayers->size(); ++index)
    {
        const auto* object = serializedPlayers->getReference(index)
            .getDynamicObject();
        const auto* velocityValues = object != nullptr
            ? object->getProperty("modulationValues").getArray()
            : nullptr;
        if (object == nullptr || velocityValues == nullptr
            || velocityValues->isEmpty()
            || velocityValues->size()
                > static_cast<int>(lps::Modulation::maxLength))
            return;

        lps::PatternPlayerPersistentState pattern;
        pattern.patternId = lps::PatternId {static_cast<std::uint64_t>(
            static_cast<juce::int64>(object->getProperty("patternId")))};
        pattern.hitMask = static_cast<std::uint32_t>(
            static_cast<juce::int64>(object->getProperty("hitMask")));
        pattern.patternOffset = static_cast<int>(object->getProperty("offset"));
        pattern.playbackStart = static_cast<std::uint16_t>(
            static_cast<int>(object->getProperty("playbackStart")));
        pattern.playbackEnd = static_cast<std::uint16_t>(
            static_cast<int>(object->getProperty("playbackEnd")));
        pattern.playbackSpeed = static_cast<std::uint8_t>(
            static_cast<int>(object->getProperty("playbackSpeed")));
        const auto velocityModulationId = lps::ModulationId {
            static_cast<std::uint64_t>(static_cast<juce::int64>(
                object->getProperty("modulationId")))};
        if (patternLibrary_.find(pattern.patternId) == nullptr
            || modulationLibrary_.find(velocityModulationId) == nullptr
            || !object->getProperty("muted").isBool()
            || (object->hasProperty("groupMask")
                && (!(object->getProperty("groupMask").isInt()
                        || object->getProperty("groupMask").isInt64())
                    || static_cast<int>(object->getProperty("groupMask")) < 0
                    || static_cast<int>(object->getProperty("groupMask"))
                        >= (1 << groupCount))))
            return;

        const auto valuesAreValid = [](const juce::Array<juce::var>& values)
        {
            if (values.isEmpty()
                || values.size()
                    > static_cast<int>(lps::Modulation::maxLength))
                return false;
            return std::all_of(values.begin(), values.end(), [](const auto& value)
            {
                return (value.isInt() || value.isInt64())
                    && static_cast<juce::int64>(value) >= 0
                    && static_cast<juce::int64>(value)
                        <= lps::NormalizedValue::maximum;
            });
        };
        if (!valuesAreValid(*velocityValues))
            return;

        const bool hasPitchContext = object->hasProperty("pitchEditMode")
            || object->hasProperty("pitchRootPitchClass")
            || object->hasProperty("pitchScaleId");
        lps::PitchEditContext pitchContext;
        if (hasPitchContext)
        {
            const auto& modeValue = object->getProperty("pitchEditMode");
            const auto& rootValue = object->getProperty(
                "pitchRootPitchClass");
            const auto& scaleValue = object->getProperty("pitchScaleId");
            if (!(modeValue.isInt() || modeValue.isInt64())
                || !(rootValue.isInt() || rootValue.isInt64())
                || !(scaleValue.isInt() || scaleValue.isInt64()))
            {
                return;
            }
            const int mode = static_cast<int>(modeValue);
            const int rootPitchClass = static_cast<int>(rootValue);
            const auto scaleId = static_cast<lps::ScaleId>(
                static_cast<int>(scaleValue));
            if (mode < static_cast<int>(lps::PitchEditMode::chromatic)
                || mode > static_cast<int>(lps::PitchEditMode::scaleAware)
                || rootPitchClass < 0
                || rootPitchClass >= static_cast<int>(lps::pitchClassCount())
                || lps::scaleDefinition(scaleId) == nullptr)
            {
                return;
            }
            pitchContext.mode = static_cast<lps::PitchEditMode>(mode);
            pitchContext.rootPitchClass = static_cast<std::uint8_t>(
                rootPitchClass);
            pitchContext.scaleId = scaleId;
        }

        struct SavedModulation
        {
            ModulationLane lane;
            lps::ModulationId id;
            const juce::Array<juce::var>* values;
            const char* lockedProperty;
        };
        std::array<SavedModulation, modulationLaneCount> savedModulations {{
            {ModulationLane::velocity, velocityModulationId, velocityValues,
                "modulationLocked"},
            {ModulationLane::pitch, {}, nullptr, "pitchModulationLocked"},
            {ModulationLane::gate, {}, nullptr, "gateModulationLocked"}
        }};
        const auto readOptionalModulation = [&object, &valuesAreValid](
            const char* idProperty,
            const char* valuesProperty,
            SavedModulation& saved)
        {
            if (!object->hasProperty(valuesProperty))
                return true;
            saved.values = object->getProperty(valuesProperty).getArray();
            saved.id = lps::ModulationId {static_cast<std::uint64_t>(
                static_cast<juce::int64>(object->getProperty(idProperty)))};
            return saved.values != nullptr && valuesAreValid(*saved.values);
        };
        if (!readOptionalModulation(
                "pitchModulationId", "pitchModulationValues",
                savedModulations[1])
            || !readOptionalModulation(
                "gateModulationId", "gateModulationValues",
                savedModulations[2]))
            return;
        for (const auto& saved : savedModulations)
        {
            if (saved.values != nullptr
                && modulationLibrary_.find(saved.id) == nullptr)
                return;
            if (object->hasProperty(saved.lockedProperty)
                && !object->getProperty(saved.lockedProperty).isBool())
            {
                return;
            }
        }

        const auto playerIndex = static_cast<std::size_t>(index);
        pitchEditContexts_[playerIndex] = pitchContext;
        if (!players_[playerIndex].patternController
                ->restorePersistentState(pattern))
            return;
        for (const auto& saved : savedModulations)
        {
            if (saved.values == nullptr)
                continue;
            auto* modulation = modulationPlayerAt(playerIndex, saved.lane);
            if (modulation == nullptr)
                return;
            modulation->selectModulation(saved.id);
            modulation->prepare({});
            modulation->setLength(
                static_cast<std::size_t>(saved.values->size()));
            for (int step = 0; step < saved.values->size(); ++step)
                modulation->setValue(static_cast<std::size_t>(step), {
                    static_cast<std::uint16_t>(static_cast<juce::int64>(
                        saved.values->getReference(step))) });
            setPlayerModulationLocked(
                playerIndex,
                saved.lane,
                object->hasProperty(saved.lockedProperty)
                    && static_cast<bool>(
                        object->getProperty(saved.lockedProperty)));
        }
        runtimeGraph_->setPatternPlayerMuted(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)},
            static_cast<bool>(object->getProperty("muted")));
        playerGroupMasks_[playerIndex]->store(
            object->hasProperty("groupMask")
                ? static_cast<std::uint8_t>(
                    static_cast<int>(object->getProperty("groupMask")))
                : 0,
            std::memory_order_relaxed);

        if (playerIndex >= synthTwoVoiceIndex
            && playerIndex < synthTwoVoiceIndex + synthTwoPatternCount
            && object->hasProperty("pitchAdvanceMode"))
        {
            const auto mode = static_cast<int>(
                object->getProperty("pitchAdvanceMode"));
            if (mode < static_cast<int>(ModulationAdvanceMode::onHit)
                || mode > static_cast<int>(ModulationAdvanceMode::onClock))
            {
                return;
            }
            setSynthLaneAdvanceMode(
                playerIndex - synthTwoVoiceIndex,
                static_cast<ModulationAdvanceMode>(mode));
        }
        if (playerIndex >= firstSamplePlayerIndex
            && playerIndex < firstSamplePlayerIndex + samplePartCount
            && object->hasProperty("samplePitchAdvanceMode"))
        {
            const auto mode = static_cast<int>(
                object->getProperty("samplePitchAdvanceMode"));
            if (mode < static_cast<int>(ModulationAdvanceMode::onHit)
                || mode > static_cast<int>(ModulationAdvanceMode::onClock))
            {
                return;
            }
            const auto patternSlot = object->hasProperty(
                    "samplePitchPatternSlot")
                ? static_cast<int>(
                    object->getProperty("samplePitchPatternSlot"))
                : static_cast<int>(playerIndex - firstSamplePlayerIndex);
            if (patternSlot < 0
                || patternSlot >= static_cast<int>(samplePartCount))
            {
                return;
            }
            setSampleLanePatternSlot(
                playerIndex - firstSamplePlayerIndex,
                static_cast<std::size_t>(patternSlot));
            setSampleLaneAdvanceMode(
                playerIndex - firstSamplePlayerIndex,
                static_cast<ModulationAdvanceMode>(mode));
        }
    }

    if (schemaVersion >= 2)
    {
        const auto* serializedSynthLanes = root->getProperty(
            "synthTwoParameterLanes").getArray();
        if (serializedSynthLanes == nullptr
            || serializedSynthLanes->size()
                != static_cast<int>(synthParameterLanes_.size()))
        {
            return;
        }
        for (const auto& serialized : *serializedSynthLanes)
        {
            const auto* object = serialized.getDynamicObject();
            const auto* values = object != nullptr
                ? object->getProperty("values").getArray() : nullptr;
            if (object == nullptr || values == nullptr
                || values->isEmpty()
                || values->size()
                    > static_cast<int>(lps::Modulation::maxLength))
            {
                return;
            }
            const int cc = static_cast<int>(object->getProperty("cc"));
            const auto lane = std::find_if(
                synthParameterLanes_.begin(),
                synthParameterLanes_.end(),
                [cc](const auto& candidate) { return candidate.midiCc == cc; });
            if (lane == synthParameterLanes_.end())
                return;
            const auto parameterIndex = static_cast<std::size_t>(
                std::distance(synthParameterLanes_.begin(), lane));
            const auto laneIndex = synthTwoPatternCount + parameterIndex;
            const auto modulationId = lps::ModulationId {
                static_cast<std::uint64_t>(static_cast<juce::int64>(
                    object->getProperty("modulationId"))) };
            const int mode = static_cast<int>(
                object->getProperty("advanceMode"));
            const int patternSlot = static_cast<int>(
                object->getProperty("patternSlot"));
            if (modulationLibrary_.find(modulationId) == nullptr
                || mode < static_cast<int>(ModulationAdvanceMode::onHit)
                || mode > static_cast<int>(ModulationAdvanceMode::onClock)
                || patternSlot < 0
                || patternSlot >= static_cast<int>(synthTwoPatternCount))
            {
                return;
            }
            for (const auto& value : *values)
            {
                if (!(value.isInt() || value.isInt64())
                    || static_cast<juce::int64>(value) < 0
                    || static_cast<juce::int64>(value)
                        > lps::NormalizedValue::maximum)
                {
                    return;
                }
            }

            lane->player->selectModulation(modulationId);
            lane->player->prepare({});
            lane->player->setLength(static_cast<std::size_t>(values->size()));
            for (int step = 0; step < values->size(); ++step)
            {
                lane->player->setValue(
                    static_cast<std::size_t>(step),
                    {static_cast<std::uint16_t>(static_cast<juce::int64>(
                        values->getReference(step)))});
            }
            setSynthLanePatternSlot(
                laneIndex, static_cast<std::size_t>(patternSlot));
            setSynthLaneAdvanceMode(
                laneIndex, static_cast<ModulationAdvanceMode>(mode));
        }
    }

    if (schemaVersion >= 3)
    {
        const auto* serializedSampleLanes = root->getProperty(
            "sampleParameterLanes").getArray();
        if (serializedSampleLanes == nullptr
            || serializedSampleLanes->isEmpty()
            || serializedSampleLanes->size()
                > static_cast<int>(sampleParameterLanes_.size()))
        {
            return;
        }
        for (const auto& serialized : *serializedSampleLanes)
        {
            const auto* object = serialized.getDynamicObject();
            const auto* values = object != nullptr
                ? object->getProperty("values").getArray() : nullptr;
            if (object == nullptr || values == nullptr || values->isEmpty()
                || values->size()
                    > static_cast<int>(lps::Modulation::maxLength))
            {
                return;
            }
            const int part = static_cast<int>(object->getProperty("part"));
            const int cc = static_cast<int>(object->getProperty("cc"));
            const auto lane = std::find_if(
                sampleParameterLanes_.begin(),
                sampleParameterLanes_.end(),
                [part, cc](const auto& candidate)
                {
                    return candidate.patternSlot == static_cast<std::size_t>(part)
                        && candidate.midiCc == cc;
                });
            if (part < 0 || part >= static_cast<int>(samplePartCount)
                || lane == sampleParameterLanes_.end())
            {
                return;
            }
            const auto parameterIndex = static_cast<std::size_t>(
                std::distance(sampleParameterLanes_.begin(), lane));
            const auto laneIndex = samplePartCount + parameterIndex;
            const auto modulationId = lps::ModulationId {
                static_cast<std::uint64_t>(static_cast<juce::int64>(
                    object->getProperty("modulationId"))) };
            const int mode = static_cast<int>(
                object->getProperty("advanceMode"));
            const int patternSlot = static_cast<int>(
                object->getProperty("patternSlot"));
            if (modulationLibrary_.find(modulationId) == nullptr
                || mode < static_cast<int>(ModulationAdvanceMode::onHit)
                || mode > static_cast<int>(ModulationAdvanceMode::onClock)
                || patternSlot < 0
                || patternSlot >= static_cast<int>(samplePartCount))
            {
                return;
            }
            for (const auto& value : *values)
            {
                if (!(value.isInt() || value.isInt64())
                    || static_cast<juce::int64>(value) < 0
                    || static_cast<juce::int64>(value)
                        > lps::NormalizedValue::maximum)
                {
                    return;
                }
            }

            lane->player->selectModulation(modulationId);
            lane->player->prepare({});
            lane->player->setLength(static_cast<std::size_t>(values->size()));
            for (int step = 0; step < values->size(); ++step)
            {
                lane->player->setValue(
                    static_cast<std::size_t>(step),
                    {static_cast<std::uint16_t>(static_cast<juce::int64>(
                        values->getReference(step)))});
            }
            setSampleLanePatternSlot(
                laneIndex, static_cast<std::size_t>(patternSlot));
            setSampleLaneAdvanceMode(
                laneIndex, static_cast<ModulationAdvanceMode>(mode));
        }
    }

    for (std::size_t from = 0; from < players_.size(); ++from)
        for (std::size_t to = 0; to < players_.size(); ++to)
            if (from != to)
                setSuppression(from, to, false);
    if (const auto* suppressions = root->getProperty("suppressions").getArray())
        for (const auto& value : *suppressions)
            if (const auto* edge = value.getDynamicObject())
            {
                const auto from = static_cast<juce::int64>(
                    edge->getProperty("from"));
                const auto to = static_cast<juce::int64>(
                    edge->getProperty("to"));
                if (from >= 0 && to >= 0
                    && from < static_cast<juce::int64>(players_.size())
                    && to < static_cast<juce::int64>(players_.size())
                    && from != to)
                    setSuppression(
                        static_cast<std::size_t>(from),
                        static_cast<std::size_t>(to), true);
            }
}

std::size_t LivePatternSequencerProcessor::playerCountForUi() const noexcept
{
    return players_.size();
}

std::optional<std::size_t>
LivePatternSequencerProcessor::masterPlayerIndexForUi() const noexcept
{
    return configuredMasterIndex() < playerCountForUi()
        ? std::optional<std::size_t> {configuredMasterIndex()}
        : std::nullopt;
}

bool LivePatternSequencerProcessor::playerIsMasterForUi(
    std::size_t playerIndex) const noexcept
{
    const auto masterIndex = masterPlayerIndexForUi();
    return masterIndex && *masterIndex == playerIndex;
}

bool LivePatternSequencerProcessor::resetPlayerToMaster(
    std::size_t playerIndex) noexcept
{
    if (playerIndex >= playerCountForUi() || playerIsMasterForUi(playerIndex))
        return false;

    return runtimeGraph_->armCycleCommand(playerIndex);
}

bool LivePatternSequencerProcessor::playerResetToMasterPendingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < playerCountForUi()
        && !playerIsMasterForUi(playerIndex)
        && runtimeGraph_->cycleCommandPending(playerIndex);
}

int LivePatternSequencerProcessor::currentStepForUi(std::size_t playerIndex) const noexcept
{
    return playerIndex < currentSteps_.size()
        ? currentSteps_[playerIndex]->load(std::memory_order_relaxed)
        : -1;
}

int LivePatternSequencerProcessor::currentModulationStepForUi(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    const auto index = playerIndex * modulationLaneCount
        + modulationLaneIndex(lane);
    return index < currentModulationSteps_.size()
        ? currentModulationSteps_[index]->load(
            std::memory_order_relaxed)
        : -1;
}

bool LivePatternSequencerProcessor::playingForUi() const noexcept
{
    return playing_.load(std::memory_order_relaxed);
}

void LivePatternSequencerProcessor::setInternalTransportPlayingForUi(
    bool playing) noexcept
{
    internalTransportRequested_.store(playing, std::memory_order_release);
    internalTransportPlaying_.store(playing, std::memory_order_relaxed);
}

bool LivePatternSequencerProcessor::internalTransportPlayingForUi() const noexcept
{
    return internalTransportPlaying_.load(std::memory_order_relaxed);
}

bool LivePatternSequencerProcessor::hostTransportPlayingForUi() const noexcept
{
    return hostTransportPlaying_.load(std::memory_order_relaxed);
}

lps::PatternView LivePatternSequencerProcessor::patternForUi(std::size_t playerIndex) const noexcept
{
    const auto* model = playerIndex < players_.size()
        ? players_[playerIndex].patternModel
        : nullptr;
    return model != nullptr ? model->patternView() : lps::PatternView {};
}

juce::String LivePatternSequencerProcessor::playerNameForUi(std::size_t playerIndex) const
{
    return playerIndex < players_.size()
        ? players_[playerIndex].descriptor.name
        : juce::String {};
}

std::size_t LivePatternSequencerProcessor::voiceCountForUi() const noexcept
{
    return voices_.size();
}

juce::String LivePatternSequencerProcessor::voiceNameForUi(
    std::size_t voiceIndex) const
{
    return voiceIndex < defaultVoices.size()
        ? juce::String(defaultVoices[voiceIndex].name)
        : juce::String {};
}

std::size_t LivePatternSequencerProcessor::playerVoiceIndexForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        ? players_[playerIndex].descriptor.voiceIndex
        : voices_.size();
}

bool LivePatternSequencerProcessor::playerSupportsPatternEditingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && players_[playerIndex].patternController != nullptr
        && players_[playerIndex].patternModel != nullptr;
}

bool LivePatternSequencerProcessor::playerSupportsModulationEditingForUi(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    return modulationPlayerAt(playerIndex, lane) != nullptr;
}

bool LivePatternSequencerProcessor::playerCanResetToMasterForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && players_[playerIndex].realtime != nullptr
        && players_[playerIndex].realtime->syncCapabilities().acceptsExternalRestart;
}

int LivePatternSequencerProcessor::playerMidiNoteForUi(std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        ? players_[playerIndex].descriptor.midiNote
        : -1;
}

int LivePatternSequencerProcessor::drumMidiChannelForUi() const noexcept
{
    return drumMidiChannel;
}

std::size_t LivePatternSequencerProcessor::patternCountForUi() const noexcept
{
    return patternLibrary_.size();
}

lps::Pattern LivePatternSequencerProcessor::patternAtForUi(
    std::size_t patternIndex) const noexcept
{
    const auto* entry = patternLibrary_.recordAt(patternIndex);
    return entry != nullptr ? entry->pattern : lps::Pattern {};
}

juce::String LivePatternSequencerProcessor::patternNameForUi(
    std::size_t patternIndex) const
{
    const auto* pattern = patternLibrary_.recordAt(patternIndex);
    if (pattern == nullptr)
        return {};
    return pattern->name.empty()
        ? juce::String(static_cast<juce::int64>(pattern->id.value()))
        : juce::String(pattern->name);
}

juce::String LivePatternSequencerProcessor::patternCatalogErrorForUi() const
{
    return patternLibraryFileStore_.lastError();
}

bool LivePatternSequencerProcessor::playerPatternModifiedForUi(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr && player->hasUnsavedPatternChanges();
}

LivePatternSequencerProcessor::SavePatternResult
LivePatternSequencerProcessor::savePlayerPattern(std::size_t playerIndex)
{
    auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr)
        return {};

    return savePlayerPattern(playerIndex, player->patternForSave());
}

LivePatternSequencerProcessor::SavePatternResult
LivePatternSequencerProcessor::savePlayerPattern(
    std::size_t playerIndex,
    const lps::Pattern& candidatePattern)
{
    auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr
        || candidatePattern.length == 0
        || candidatePattern.length > lps::Pattern::maxLength)
    {
        return {};
    }

    try
    {
        if (const auto* existing = patternLibrary_.findEquivalent(candidatePattern))
        {
            const auto index = patternLibrary_.indexOf(existing->id);
            if (!index)
                return {};

            (void) runtimeGraph_->schedulePatternSelection(
                lps::PatternPlayerId {
                    static_cast<std::uint32_t>(playerIndex) },
                existing->id,
                1);
            return {
                SavePatternStatus::selectedExisting,
                *index,
                candidatePattern
            };
        }

        const auto insertion = patternLibrary_.addOrFind(
            candidatePattern,
            [this](lps::PatternLibraryEntry& stagedEntry)
            {
                return patternLibraryFileStore_.persistNewEntry(
                    patternLibrary_, stagedEntry);
            });
        if (insertion.entry == nullptr)
            return {};

        const auto index = patternLibrary_.indexOf(insertion.entry->id);
        if (!index)
            return {};

        (void) runtimeGraph_->schedulePatternSelection(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex) },
            insertion.entry->id,
            1);
        return {
            insertion.inserted
                ? SavePatternStatus::savedNew
                : SavePatternStatus::selectedExisting,
            *index,
            candidatePattern
        };
    }
    catch (...)
    {
        return {};
    }
}

lps::PatternPlayer* LivePatternSequencerProcessor::patternPlayerAt(std::size_t playerIndex) noexcept
{
    return playerIndex < players_.size()
        ? players_[playerIndex].patternController
        : nullptr;
}

const lps::PatternPlayer* LivePatternSequencerProcessor::patternPlayerAt(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        ? players_[playerIndex].patternController
        : nullptr;
}

lps::ModulationPlayer* LivePatternSequencerProcessor::modulationPlayerAt(
    std::size_t playerIndex,
    ModulationLane lane) noexcept
{
    if (playerIndex >= players_.size())
        return nullptr;

    auto& player = players_[playerIndex];
    switch (lane)
    {
        case ModulationLane::velocity: return player.velocityPlayer.get();
        case ModulationLane::pitch: return player.pitchPlayer.get();
        case ModulationLane::gate: return player.gatePlayer.get();
    }
    return nullptr;
}

const lps::ModulationPlayer* LivePatternSequencerProcessor::modulationPlayerAt(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    if (playerIndex >= players_.size())
        return nullptr;

    const auto& player = players_[playerIndex];
    switch (lane)
    {
        case ModulationLane::velocity: return player.velocityPlayer.get();
        case ModulationLane::pitch: return player.pitchPlayer.get();
        case ModulationLane::gate: return player.gatePlayer.get();
    }
    return nullptr;
}

void LivePatternSequencerProcessor::selectPatternForPlayer(
    std::size_t playerIndex,
    std::size_t patternIndex) noexcept
{
    (void) schedulePatternForPlayer(playerIndex, patternIndex, 1);
}

bool LivePatternSequencerProcessor::schedulePatternForPlayer(
    std::size_t playerIndex,
    std::size_t patternIndex,
    std::size_t barsFromNow) noexcept
{
    const auto* pattern = patternLibrary_.recordAt(patternIndex);
    return playerIndex < players_.size() && pattern != nullptr
        && runtimeGraph_->schedulePatternSelection(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex) },
            pattern->id,
            barsFromNow);
}

std::size_t LivePatternSequencerProcessor::selectedPatternForPlayer(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr)
        return 0;

    const auto pending = runtimeGraph_->scheduledPatternSelection(
        lps::PatternPlayerId {static_cast<std::uint32_t>(playerIndex)});
    return patternLibrary_.indexOf(
        pending.value_or(player->selectedPatternId())).value_or(0);
}

bool LivePatternSequencerProcessor::playerPatternChangePendingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && runtimeGraph_->scheduledPatternBarsRemaining(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex) }) != 0;
}

void LivePatternSequencerProcessor::offsetPlayerPatternLeft(std::size_t playerIndex) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex)) player->offsetPatternLeft();
}

void LivePatternSequencerProcessor::offsetPlayerPatternRight(std::size_t playerIndex) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex)) player->offsetPatternRight();
}

int LivePatternSequencerProcessor::playerPatternOffset(std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr ? player->patternOffset() : 0;
}

void LivePatternSequencerProcessor::setPlayerPlaybackSpeed(
    std::size_t playerIndex, std::size_t speedIndex) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex)) player->setPlaybackSpeed(speedIndex);
}

std::size_t LivePatternSequencerProcessor::playerPlaybackSpeed(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr ? player->playbackSpeed() : 1;
}

void LivePatternSequencerProcessor::setPlayerPlaybackWindow(
    std::size_t playerIndex,
    std::size_t startStep,
    std::size_t endStep) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex)) player->setPlaybackWindow(startStep, endStep);
}

std::size_t LivePatternSequencerProcessor::playerPlaybackStart(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr ? player->requestedPlaybackStart() : 0;
}

std::size_t LivePatternSequencerProcessor::playerPlaybackEnd(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr ? player->requestedPlaybackEnd() : 0;
}

void LivePatternSequencerProcessor::togglePlayerStep(
    std::size_t playerIndex,
    std::size_t step) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex))
        player->toggleStep(step);
}

std::size_t LivePatternSequencerProcessor::modulationCountForUi() const noexcept
{
    return modulationLibrary_.size();
}

lps::Modulation LivePatternSequencerProcessor::modulationAtForUi(
    std::size_t modulationIndex) const noexcept
{
    const auto* entry = modulationLibrary_.recordAt(modulationIndex);
    return entry != nullptr ? entry->modulation : lps::Modulation {};
}

juce::String LivePatternSequencerProcessor::modulationNameForUi(
    std::size_t modulationIndex) const
{
    const auto* modulation = modulationLibrary_.recordAt(modulationIndex);
    return modulation != nullptr
        ? juce::String(modulation->name)
        : juce::String {};
}

juce::String
LivePatternSequencerProcessor::modulationCatalogErrorForUi() const
{
    return modulationLibraryFileStore_.lastError();
}

lps::Modulation LivePatternSequencerProcessor::modulationForUi(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    const auto* player = modulationPlayerAt(playerIndex, lane);
    return player != nullptr
        ? player->modulationForUi()
        : lps::Modulation {};
}

bool LivePatternSequencerProcessor::playerModulationModifiedForUi(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    const auto* player = modulationPlayerAt(playerIndex, lane);
    return player != nullptr
        && player->hasUnsavedChanges();
}

bool LivePatternSequencerProcessor::playerModulationLockedForUi(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    const auto index = playerIndex * modulationLaneCount
        + modulationLaneIndex(lane);
    return index < modulationLocks_.size()
        && modulationLocks_[index]->load(std::memory_order_relaxed);
}

void LivePatternSequencerProcessor::setPlayerModulationLocked(
    std::size_t playerIndex,
    ModulationLane lane,
    bool locked) noexcept
{
    const auto index = playerIndex * modulationLaneCount
        + modulationLaneIndex(lane);
    if (index < modulationLocks_.size())
        modulationLocks_[index]->store(locked, std::memory_order_relaxed);
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::savePlayerModulation(
    std::size_t playerIndex,
    ModulationLane lane,
    const juce::String& name)
{
    auto* player = modulationPlayerAt(playerIndex, lane);
    if (player == nullptr
        || playerModulationLockedForUi(playerIndex, lane))
        return {};

    return savePlayerModulation(
        playerIndex, lane, player->modulationForSave(), name);
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::savePlayerModulation(
    std::size_t playerIndex,
    ModulationLane lane,
    const lps::Modulation& candidateModulation,
    const juce::String& name)
{
    auto* player = modulationPlayerAt(playerIndex, lane);
    if (player == nullptr
        || playerModulationLockedForUi(playerIndex, lane)
        || candidateModulation.length == 0
        || candidateModulation.length > lps::Modulation::maxLength)
    {
        return {};
    }

    return saveModulationPlayer(*player, candidateModulation, name);
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::saveModulationPlayer(
    lps::ModulationPlayer& player,
    const lps::Modulation& candidateModulation,
    const juce::String& name)
{
    try
    {
        if (const auto* existing = modulationLibrary_.findEquivalent(
                candidateModulation))
        {
            const auto index = modulationLibrary_.indexOf(existing->id);
            if (!index)
                return {};

            player.selectModulation(existing->id);
            return {
                SaveModulationStatus::selectedExisting,
                *index,
                candidateModulation
            };
        }

        const auto insertion = modulationLibrary_.addOrFind(
            name.trim().toStdString(),
            candidateModulation,
            [this](lps::ModulationLibraryEntry& stagedEntry)
            {
                return modulationLibraryFileStore_.persistNewEntry(
                    modulationLibrary_, stagedEntry);
            });
        if (insertion.entry == nullptr)
            return {};

        const auto index = modulationLibrary_.indexOf(insertion.entry->id);
        if (!index)
            return {};

        player.selectModulation(insertion.entry->id);
        return {
            insertion.inserted
                ? SaveModulationStatus::savedNew
                : SaveModulationStatus::selectedExisting,
            *index,
            candidateModulation
        };
    }
    catch (...)
    {
        return {};
    }
}

void LivePatternSequencerProcessor::selectModulationForPlayer(
    std::size_t playerIndex,
    ModulationLane lane,
    std::size_t modulationIndex) noexcept
{
    auto* player = modulationPlayerAt(playerIndex, lane);
    const auto* modulation = modulationLibrary_.recordAt(modulationIndex);
    if (player != nullptr && modulation != nullptr
        && !playerModulationLockedForUi(playerIndex, lane))
        player->selectModulation(modulation->id);
}

std::size_t LivePatternSequencerProcessor::selectedModulationForPlayer(
    std::size_t playerIndex,
    ModulationLane lane) const noexcept
{
    const auto* player = modulationPlayerAt(playerIndex, lane);
    if (player == nullptr)
        return 0;

    return modulationLibrary_.indexOf(
        player->selectedModulationId()).value_or(0);
}

void LivePatternSequencerProcessor::setPlayerModulationValue(
    std::size_t playerIndex,
    ModulationLane lane,
    std::size_t step,
    std::uint8_t value) noexcept
{
    if (!playerModulationLockedForUi(playerIndex, lane))
    {
        if (auto* player = modulationPlayerAt(playerIndex, lane))
        {
            if (lane == ModulationLane::pitch)
            {
                auto midiNote = lps::midiNoteFromModulation(value);
                const auto context = pitchEditContextForPlayer(playerIndex);
                if (context.mode == lps::PitchEditMode::scaleAware)
                {
                    midiNote = lps::nearestScaleMidiNote(
                        midiNote,
                        context.rootPitchClass,
                        context.scaleId);
                }
                value = lps::canonicalModulationValue(midiNote);
            }
            player->setUnipolar8Value(step, value);
        }
    }
}

lps::PitchEditContext
LivePatternSequencerProcessor::pitchEditContextForPlayer(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < pitchEditContexts_.size()
        ? pitchEditContexts_[playerIndex] : lps::PitchEditContext {};
}

void LivePatternSequencerProcessor::setPitchEditModeForPlayer(
    std::size_t playerIndex,
    lps::PitchEditMode mode) noexcept
{
    if (playerIndex < pitchEditContexts_.size())
        pitchEditContexts_[playerIndex].mode = mode;
}

void LivePatternSequencerProcessor::setPitchRootForPlayer(
    std::size_t playerIndex,
    std::uint8_t rootPitchClass) noexcept
{
    if (playerIndex < pitchEditContexts_.size())
        pitchEditContexts_[playerIndex].rootPitchClass = rootPitchClass % 12u;
}

void LivePatternSequencerProcessor::setPitchScaleForPlayer(
    std::size_t playerIndex,
    lps::ScaleId scaleId) noexcept
{
    if (playerIndex < pitchEditContexts_.size()
        && lps::scaleDefinition(scaleId) != nullptr)
    {
        pitchEditContexts_[playerIndex].scaleId = scaleId;
    }
}

void LivePatternSequencerProcessor::editPlayerPitch(
    std::size_t playerIndex,
    std::size_t step,
    int direction) noexcept
{
    if (playerModulationLockedForUi(playerIndex, ModulationLane::pitch))
        return;
    auto* player = modulationPlayerAt(playerIndex, ModulationLane::pitch);
    if (player == nullptr)
        return;
    const auto modulation = player->modulationForUi();
    if (step >= modulation.length)
        return;
    const auto raw = static_cast<std::uint8_t>(
        modulation.values[step].raw / 257u);
    player->setUnipolar8Value(
        step,
        lps::editedPitchValue(
            raw, direction, pitchEditContextForPlayer(playerIndex)));
}

bool LivePatternSequencerProcessor::adjustPlayerPitchToScale(
    std::size_t playerIndex) noexcept
{
    if (playerModulationLockedForUi(playerIndex, ModulationLane::pitch))
        return false;
    auto* player = modulationPlayerAt(playerIndex, ModulationLane::pitch);
    const auto context = pitchEditContextForPlayer(playerIndex);
    if (player == nullptr || context.mode != lps::PitchEditMode::scaleAware)
        return false;
    player->replaceDraft(lps::adjustedToScale(
        player->modulationForUi(),
        context.rootPitchClass,
        context.scaleId));
    return true;
}

juce::String LivePatternSequencerProcessor::pitchValueDisplayForUi(
    std::uint8_t value) const
{
    return juce::String::fromUTF8(lps::pitchValueDisplay(value).c_str());
}

void LivePatternSequencerProcessor::setPlayerModulationLength(
    std::size_t playerIndex,
    ModulationLane lane,
    std::size_t length) noexcept
{
    if (!playerModulationLockedForUi(playerIndex, lane))
    {
        if (auto* player = modulationPlayerAt(playerIndex, lane))
        {
            const auto oldLength = player->modulationForUi().length;
            player->setLength(length);
            if (lane == ModulationLane::pitch && length > oldLength)
            {
                const auto modulation = player->modulationForUi();
                const auto context = pitchEditContextForPlayer(playerIndex);
                for (std::size_t step = oldLength;
                     step < modulation.length;
                     ++step)
                {
                    const auto raw = static_cast<std::uint8_t>(
                        modulation.values[step].raw / 257u);
                    auto midiNote = lps::midiNoteFromModulation(raw);
                    if (context.mode == lps::PitchEditMode::scaleAware)
                    {
                        midiNote = lps::nearestScaleMidiNote(
                            midiNote,
                            context.rootPitchClass,
                            context.scaleId);
                    }
                    player->setUnipolar8Value(
                        step, lps::canonicalModulationValue(midiNote));
                }
            }
        }
    }
}

void LivePatternSequencerProcessor::setPlayerMuted(
    std::size_t playerIndex,
    bool muted) noexcept
{
    (void) schedulePlayerMute(playerIndex, muted, 1);
}

bool LivePatternSequencerProcessor::schedulePlayerMute(
    std::size_t playerIndex,
    bool muted,
    std::size_t barsFromNow) noexcept
{
    return playerIndex < players_.size()
        && runtimeGraph_->schedulePatternPlayerMute(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)},
            muted,
            barsFromNow);
}

bool LivePatternSequencerProcessor::playerMutedForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && runtimeGraph_->patternPlayerMuted(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)});
}

bool LivePatternSequencerProcessor::playerTargetMutedForUi(
    std::size_t playerIndex) const noexcept
{
    if (playerIndex >= players_.size())
        return false;
    const auto pending = runtimeGraph_->scheduledPatternPlayerMute(
        lps::PatternPlayerId {
            static_cast<std::uint32_t>(playerIndex)});
    return pending.value_or(playerMutedForUi(playerIndex));
}

bool LivePatternSequencerProcessor::playerMuteChangePendingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && runtimeGraph_->scheduledPatternPlayerMuteBarsRemaining(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)}) != 0;
}

std::size_t LivePatternSequencerProcessor::currentBarForUi() const noexcept
{
    return runtimeGraph_->currentBar();
}

float LivePatternSequencerProcessor::currentBarProgressForUi() const noexcept
{
    return currentBarProgress_.load(std::memory_order_relaxed);
}

std::size_t
LivePatternSequencerProcessor::playerMuteChangeBarsRemainingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        ? runtimeGraph_->scheduledPatternPlayerMuteBarsRemaining(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)})
        : 0;
}

bool LivePatternSequencerProcessor::playerMuteScheduledAtBarOffsetForUi(
    std::size_t playerIndex,
    bool muted,
    std::size_t barsFromNow) const noexcept
{
    return playerIndex < players_.size()
        && runtimeGraph_->patternPlayerMuteScheduledAtBarOffset(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)},
            muted,
            barsFromNow);
}

std::size_t
LivePatternSequencerProcessor::playerPatternChangeBarsRemainingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        ? runtimeGraph_->scheduledPatternBarsRemaining(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(playerIndex)})
        : 0;
}

std::optional<std::size_t>
LivePatternSequencerProcessor::playerPatternScheduledAtBarOffsetForUi(
    std::size_t playerIndex,
    std::size_t barsFromNow) const noexcept
{
    if (playerIndex >= players_.size())
        return std::nullopt;
    const auto scheduled = runtimeGraph_->patternSelectionScheduledAtBarOffset(
        lps::PatternPlayerId {static_cast<std::uint32_t>(playerIndex)},
        barsFromNow);
    return scheduled ? patternLibrary_.indexOf(*scheduled) : std::nullopt;
}

std::size_t
LivePatternSequencerProcessor::scheduledChangeCountAtBarOffsetForUi(
    std::size_t barsFromNow) const noexcept
{
    return runtimeGraph_->scheduledChangeCountAtBarOffset(barsFromNow);
}

bool LivePatternSequencerProcessor::playerInGroupForUi(
    std::size_t playerIndex,
    std::size_t groupIndex) const noexcept
{
    if (playerIndex >= playerGroupMasks_.size() || groupIndex >= groupCount)
        return false;
    const auto bit = static_cast<std::uint8_t>(1u << groupIndex);
    return (playerGroupMasks_[playerIndex]->load(std::memory_order_relaxed)
        & bit) != 0;
}

void LivePatternSequencerProcessor::setPlayerInGroup(
    std::size_t playerIndex,
    std::size_t groupIndex,
    bool enabled) noexcept
{
    if (playerIndex >= playerGroupMasks_.size() || groupIndex >= groupCount)
        return;
    const auto bit = static_cast<std::uint8_t>(1u << groupIndex);
    if (enabled)
        playerGroupMasks_[playerIndex]->fetch_or(bit, std::memory_order_relaxed);
    else
        playerGroupMasks_[playerIndex]->fetch_and(
            static_cast<std::uint8_t>(~bit), std::memory_order_relaxed);
}

std::size_t LivePatternSequencerProcessor::groupPlayerCountForUi(
    std::size_t groupIndex) const noexcept
{
    if (groupIndex >= groupCount)
        return 0;
    std::size_t count = 0;
    for (std::size_t playerIndex = 0;
         playerIndex < playerGroupMasks_.size();
         ++playerIndex)
    {
        if (playerInGroupForUi(playerIndex, groupIndex))
            ++count;
    }
    return count;
}

bool LivePatternSequencerProcessor::scheduleGroupMute(
    std::size_t groupIndex,
    bool muted,
    std::size_t barsFromNow) noexcept
{
    if (groupIndex >= groupCount)
        return false;
    bool foundMember = false;
    bool allScheduled = true;
    for (std::size_t playerIndex = 0; playerIndex < players_.size(); ++playerIndex)
    {
        if (!playerInGroupForUi(playerIndex, groupIndex))
            continue;
        foundMember = true;
        allScheduled = schedulePlayerMute(playerIndex, muted, barsFromNow)
            && allScheduled;
    }
    return foundMember && allScheduled;
}

bool LivePatternSequencerProcessor::resetGroupToMaster(
    std::size_t groupIndex) noexcept
{
    if (groupIndex >= groupCount)
        return false;
    bool foundEligibleMember = false;
    bool allReset = true;
    for (std::size_t playerIndex = 0; playerIndex < players_.size(); ++playerIndex)
    {
        if (!playerInGroupForUi(playerIndex, groupIndex)
            || !playerCanResetToMasterForUi(playerIndex)
            || playerIsMasterForUi(playerIndex))
        {
            continue;
        }
        foundEligibleMember = true;
        allReset = resetPlayerToMaster(playerIndex) && allReset;
    }
    return foundEligibleMember && allReset;
}

bool LivePatternSequencerProcessor::groupMuteScheduledAtBarOffsetForUi(
    std::size_t groupIndex,
    bool muted,
    std::size_t barsFromNow) const noexcept
{
    if (groupIndex >= groupCount)
        return false;
    for (std::size_t playerIndex = 0; playerIndex < players_.size(); ++playerIndex)
    {
        if (playerInGroupForUi(playerIndex, groupIndex)
            && runtimeGraph_->patternPlayerMuteScheduledAtBarOffset(
                lps::PatternPlayerId {
                    static_cast<std::uint32_t>(playerIndex)},
                muted,
                barsFromNow))
        {
            return true;
        }
    }
    return false;
}

void LivePatternSequencerProcessor::setSuppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex,
    bool enabled) noexcept
{
    if (suppressorIndex < players_.size()
        && suppressedIndex < players_.size()
        && suppressorIndex != suppressedIndex)
    {
        runtimeGraph_->setSuppression(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(suppressorIndex) },
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(suppressedIndex) },
            enabled);
    }
}

bool LivePatternSequencerProcessor::suppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex) const noexcept
{
    return suppressorIndex < players_.size()
        && suppressedIndex < players_.size()
        && suppressorIndex != suppressedIndex
        && runtimeGraph_->suppression(
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(suppressorIndex) },
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(suppressedIndex) });
}

void LivePatternSequencerProcessor::setVoiceSuppression(
    std::size_t suppressorVoiceIndex,
    std::size_t suppressedVoiceIndex,
    bool enabled) noexcept
{
    if (suppressorVoiceIndex >= voices_.size()
        || suppressedVoiceIndex >= voices_.size()
        || suppressorVoiceIndex == suppressedVoiceIndex)
    {
        return;
    }

    for (std::size_t from = 0; from < players_.size(); ++from)
        for (std::size_t to = 0; to < players_.size(); ++to)
            if (players_[from].descriptor.voiceIndex == suppressorVoiceIndex
                && players_[to].descriptor.voiceIndex == suppressedVoiceIndex)
                setSuppression(from, to, enabled);
}

bool LivePatternSequencerProcessor::voiceSuppression(
    std::size_t suppressorVoiceIndex,
    std::size_t suppressedVoiceIndex) const noexcept
{
    if (suppressorVoiceIndex >= voices_.size()
        || suppressedVoiceIndex >= voices_.size()
        || suppressorVoiceIndex == suppressedVoiceIndex)
    {
        return false;
    }

    bool found = false;
    for (std::size_t from = 0; from < players_.size(); ++from)
    {
        for (std::size_t to = 0; to < players_.size(); ++to)
        {
            if (players_[from].descriptor.voiceIndex != suppressorVoiceIndex
                || players_[to].descriptor.voiceIndex != suppressedVoiceIndex)
            {
                continue;
            }
            found = true;
            if (!suppression(from, to))
                return false;
        }
    }
    return found;
}

std::size_t LivePatternSequencerProcessor::synthTwoPlayerIndexForUi(
    std::size_t patternSlot) const noexcept
{
    const auto index = synthTwoVoiceIndex + patternSlot;
    return patternSlot < synthTwoPatternCount && index < players_.size()
        ? index : players_.size();
}

std::size_t LivePatternSequencerProcessor::synthPatternResetGroup(
    std::size_t patternSlot) const noexcept
{
    return players_.size() + patternSlot;
}

std::size_t LivePatternSequencerProcessor::synthLaneResetGroup(
    std::size_t laneIndex,
    std::size_t sourceIndex) const noexcept
{
    constexpr std::size_t sourceCount = synthTwoPatternCount + 1;
    return players_.size() + synthTwoPatternCount
        + laneIndex * sourceCount + sourceIndex;
}

bool LivePatternSequencerProcessor::resetSynthPatternToMaster(
    std::size_t patternSlot) noexcept
{
    if (synthTwoPlayerIndexForUi(patternSlot) >= players_.size())
        return false;
    return runtimeGraph_->armCommand(synthPatternResetGroup(patternSlot));
}

bool LivePatternSequencerProcessor::synthPatternResetPendingForUi(
    std::size_t patternSlot) const noexcept
{
    return patternSlot < synthTwoPatternCount
        && runtimeGraph_->commandPending(
            synthPatternResetGroup(patternSlot));
}

std::size_t LivePatternSequencerProcessor::synthLaneCountForUi() const noexcept
{
    return synthTwoPatternCount + synthParameterLanes_.size();
}

LivePatternSequencerProcessor::SynthLaneInfo
LivePatternSequencerProcessor::synthLaneInfoForUi(
    std::size_t laneIndex) const
{
    if (laneIndex < synthTwoPatternCount)
    {
        return {
            "Notes",
            "Pitch " + juce::String(static_cast<int>(laneIndex + 1)),
            -1,
            -1,
            255,
            true,
            laneIndex
        };
    }
    const auto parameterIndex = laneIndex - synthTwoPatternCount;
    if (parameterIndex >= synthParameterLanes_.size())
        return {};
    const auto& lane = synthParameterLanes_[parameterIndex];
    return {
        lane.section,
        lane.name,
        lane.midiCc,
        lane.secondaryMidiCc,
        lane.maximumValue,
        false,
        synthLanePatternSlotForUi(laneIndex)
    };
}

lps::ModulationPlayer* LivePatternSequencerProcessor::synthLanePlayerAt(
    std::size_t laneIndex) noexcept
{
    if (laneIndex < synthTwoPatternCount)
    {
        const auto playerIndex = synthTwoPlayerIndexForUi(laneIndex);
        return modulationPlayerAt(playerIndex, ModulationLane::pitch);
    }
    const auto parameterIndex = laneIndex - synthTwoPatternCount;
    return parameterIndex < synthParameterLanes_.size()
        ? synthParameterLanes_[parameterIndex].player.get() : nullptr;
}

const lps::ModulationPlayer* LivePatternSequencerProcessor::synthLanePlayerAt(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex < synthTwoPatternCount)
    {
        const auto playerIndex = synthTwoPlayerIndexForUi(laneIndex);
        return modulationPlayerAt(playerIndex, ModulationLane::pitch);
    }
    const auto parameterIndex = laneIndex - synthTwoPatternCount;
    return parameterIndex < synthParameterLanes_.size()
        ? synthParameterLanes_[parameterIndex].player.get() : nullptr;
}

lps::Modulation LivePatternSequencerProcessor::synthLaneModulationForUi(
    std::size_t laneIndex) const noexcept
{
    const auto* player = synthLanePlayerAt(laneIndex);
    return player != nullptr ? player->modulationForUi() : lps::Modulation {};
}

bool LivePatternSequencerProcessor::synthLaneModulationModifiedForUi(
    std::size_t laneIndex) const noexcept
{
    const auto* player = synthLanePlayerAt(laneIndex);
    return player != nullptr && player->hasUnsavedChanges();
}

std::size_t LivePatternSequencerProcessor::selectedSynthLaneModulationForUi(
    std::size_t laneIndex) const noexcept
{
    const auto* player = synthLanePlayerAt(laneIndex);
    if (player == nullptr)
        return 0;

    return modulationLibrary_.indexOf(
        player->selectedModulationId()).value_or(0);
}

void LivePatternSequencerProcessor::selectModulationForSynthLane(
    std::size_t laneIndex,
    std::size_t modulationIndex) noexcept
{
    auto* player = synthLanePlayerAt(laneIndex);
    const auto* modulation = modulationLibrary_.recordAt(modulationIndex);
    if (player != nullptr && modulation != nullptr)
        player->selectModulation(modulation->id);
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::saveSynthLaneModulation(
    std::size_t laneIndex,
    const juce::String& name)
{
    auto* player = synthLanePlayerAt(laneIndex);
    if (player == nullptr)
        return {};

    const auto candidate = player->modulationForSave();
    if (candidate.length == 0
        || candidate.length > lps::Modulation::maxLength)
    {
        return {};
    }

    return saveModulationPlayer(*player, candidate, name);
}

int LivePatternSequencerProcessor::synthLaneCurrentStepForUi(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex < synthTwoPatternCount)
    {
        return currentModulationStepForUi(
            synthTwoPlayerIndexForUi(laneIndex), ModulationLane::pitch);
    }
    const auto parameterIndex = laneIndex - synthTwoPatternCount;
    return parameterIndex < synthParameterCurrentSteps_.size()
        ? synthParameterCurrentSteps_[parameterIndex]->load(
            std::memory_order_relaxed)
        : -1;
}

LivePatternSequencerProcessor::ModulationAdvanceMode
LivePatternSequencerProcessor::synthLaneAdvanceModeForUi(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex >= synthLaneAdvanceModes_.size())
        return ModulationAdvanceMode::onHit;
    return static_cast<ModulationAdvanceMode>(
        synthLaneAdvanceModes_[laneIndex]->load(std::memory_order_relaxed));
}

std::size_t LivePatternSequencerProcessor::synthLanePatternSlotForUi(
    std::size_t laneIndex) const noexcept
{
    return laneIndex < synthLanePatternSlots_.size()
        ? synthLanePatternSlots_[laneIndex]->load(std::memory_order_relaxed)
        : 0;
}

bool LivePatternSequencerProcessor::resetSynthLane(
    std::size_t laneIndex) noexcept
{
    if (laneIndex >= synthLaneCountForUi())
        return false;
    const auto mode = synthLaneAdvanceModeForUi(laneIndex);
    const auto sourceIndex = mode == ModulationAdvanceMode::onClock
        ? 0
        : synthLanePatternSlotForUi(laneIndex) + 1;
    return runtimeGraph_->armCommand(
        synthLaneResetGroup(laneIndex, sourceIndex));
}

bool LivePatternSequencerProcessor::synthLaneResetPendingForUi(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex >= synthLaneCountForUi())
        return false;
    for (std::size_t sourceIndex = 0;
         sourceIndex <= synthTwoPatternCount;
         ++sourceIndex)
    {
        if (runtimeGraph_->commandPending(
                synthLaneResetGroup(laneIndex, sourceIndex)))
        {
            return true;
        }
    }
    return false;
}

bool LivePatternSequencerProcessor::publishSynthLaneAdvanceSource(
    std::size_t laneIndex,
    ModulationAdvanceMode mode,
    std::size_t patternSlot) noexcept
{
    auto* player = synthLanePlayerAt(laneIndex);
    const auto sourceIndex = synthTwoPlayerIndexForUi(patternSlot);
    if (player == nullptr || sourceIndex >= players_.size()
        || laneIndex >= synthLaneAdvanceModes_.size()
        || laneIndex >= synthLanePatternSlots_.size())
    {
        return false;
    }

    auto candidate = runtimeConfig_;
    bool found = false;
    for (std::size_t index = 0;
         index < candidate.modulationPlayerConfigCount;
         ++index)
    {
        auto& config = candidate.modulationPlayerConfigs[index];
        if (config.player.value != player->playerRef().value)
            continue;
        config.advanceSource = mode == ModulationAdvanceMode::onClock
            ? lps::AdvanceSource {lps::ClockAdvance {0.25}}
            : lps::AdvanceSource {lps::PatternHitAdvance {
                lps::PatternPlayerId {
                    static_cast<std::uint32_t>(sourceIndex) }}};
        config.quantizeSelectionToPatternCycle =
            mode == ModulationAdvanceMode::onHit
                && waitForCycleBeforeSelection;
        found = true;
        break;
    }
    if (!found || !runtimeGraph_->publish(candidate))
        return false;

    runtimeConfig_ = candidate;
    synthLaneAdvanceModes_[laneIndex]->store(
        static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
    synthLanePatternSlots_[laneIndex]->store(
        patternSlot, std::memory_order_relaxed);
    return true;
}

void LivePatternSequencerProcessor::setSynthLaneAdvanceMode(
    std::size_t laneIndex,
    ModulationAdvanceMode mode) noexcept
{
    (void) publishSynthLaneAdvanceSource(
        laneIndex, mode, synthLanePatternSlotForUi(laneIndex));
}

void LivePatternSequencerProcessor::setSynthLanePatternSlot(
    std::size_t laneIndex,
    std::size_t patternSlot) noexcept
{
    if (patternSlot < synthTwoPatternCount)
        (void) publishSynthLaneAdvanceSource(
            laneIndex, synthLaneAdvanceModeForUi(laneIndex), patternSlot);
}

void LivePatternSequencerProcessor::setSynthLaneValue(
    std::size_t laneIndex,
    std::size_t step,
    std::uint8_t value) noexcept
{
    if (laneIndex < synthTwoPatternCount)
    {
        setPlayerModulationValue(
            synthTwoPlayerIndexForUi(laneIndex),
            ModulationLane::pitch,
            step,
            value);
        return;
    }
    if (auto* player = synthLanePlayerAt(laneIndex))
        player->setUnipolar8Value(step, value);
}

void LivePatternSequencerProcessor::setSynthLaneLength(
    std::size_t laneIndex,
    std::size_t length) noexcept
{
    if (laneIndex < synthTwoPatternCount)
    {
        setPlayerModulationLength(
            synthTwoPlayerIndexForUi(laneIndex),
            ModulationLane::pitch,
            length);
        return;
    }
    if (auto* player = synthLanePlayerAt(laneIndex))
        player->setLength(length);
}

std::size_t LivePatternSequencerProcessor::samplePlayerIndexForUi(
    std::size_t partSlot) const noexcept
{
    const auto index = firstSamplePlayerIndex + partSlot;
    return partSlot < samplePartCount && index < players_.size()
        ? index : players_.size();
}

std::size_t LivePatternSequencerProcessor::sampleLaneCountForUi() const noexcept
{
    return samplePartCount + sampleParameterLanes_.size();
}

LivePatternSequencerProcessor::SynthLaneInfo
LivePatternSequencerProcessor::sampleLaneInfoForUi(
    std::size_t laneIndex) const
{
    if (laneIndex < samplePartCount)
    {
        const auto part = static_cast<int>(laneIndex + 1);
        return {
            "Part " + juce::String(part) + " · MIDI Ch " + juce::String(part),
            "Pitch",
            -1,
            -1,
            255,
            true,
            laneIndex
        };
    }
    const auto parameterIndex = laneIndex - samplePartCount;
    if (parameterIndex >= sampleParameterLanes_.size())
        return {};
    const auto& lane = sampleParameterLanes_[parameterIndex];
    return {
        lane.section,
        lane.name,
        lane.midiCc,
        lane.secondaryMidiCc,
        lane.maximumValue,
        false,
        sampleLanePatternSlotForUi(laneIndex)
    };
}

lps::ModulationPlayer* LivePatternSequencerProcessor::sampleLanePlayerAt(
    std::size_t laneIndex) noexcept
{
    if (laneIndex < samplePartCount)
        return modulationPlayerAt(
            samplePlayerIndexForUi(laneIndex), ModulationLane::pitch);
    const auto parameterIndex = laneIndex - samplePartCount;
    return parameterIndex < sampleParameterLanes_.size()
        ? sampleParameterLanes_[parameterIndex].player.get() : nullptr;
}

const lps::ModulationPlayer* LivePatternSequencerProcessor::sampleLanePlayerAt(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex < samplePartCount)
        return modulationPlayerAt(
            samplePlayerIndexForUi(laneIndex), ModulationLane::pitch);
    const auto parameterIndex = laneIndex - samplePartCount;
    return parameterIndex < sampleParameterLanes_.size()
        ? sampleParameterLanes_[parameterIndex].player.get() : nullptr;
}

lps::Modulation LivePatternSequencerProcessor::sampleLaneModulationForUi(
    std::size_t laneIndex) const noexcept
{
    const auto* player = sampleLanePlayerAt(laneIndex);
    return player != nullptr ? player->modulationForUi() : lps::Modulation {};
}

bool LivePatternSequencerProcessor::sampleLaneModulationModifiedForUi(
    std::size_t laneIndex) const noexcept
{
    const auto* player = sampleLanePlayerAt(laneIndex);
    return player != nullptr && player->hasUnsavedChanges();
}

std::size_t
LivePatternSequencerProcessor::selectedSampleLaneModulationForUi(
    std::size_t laneIndex) const noexcept
{
    const auto* player = sampleLanePlayerAt(laneIndex);
    return player != nullptr
        ? modulationLibrary_.indexOf(
            player->selectedModulationId()).value_or(0)
        : 0;
}

void LivePatternSequencerProcessor::selectModulationForSampleLane(
    std::size_t laneIndex,
    std::size_t modulationIndex) noexcept
{
    auto* player = sampleLanePlayerAt(laneIndex);
    const auto* modulation = modulationLibrary_.recordAt(modulationIndex);
    if (player != nullptr && modulation != nullptr)
        player->selectModulation(modulation->id);
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::saveSampleLaneModulation(
    std::size_t laneIndex,
    const juce::String& name)
{
    auto* player = sampleLanePlayerAt(laneIndex);
    if (player == nullptr)
        return {};
    const auto candidate = player->modulationForSave();
    return candidate.length > 0
            && candidate.length <= lps::Modulation::maxLength
        ? saveModulationPlayer(*player, candidate, name)
        : SaveModulationResult {};
}

int LivePatternSequencerProcessor::sampleLaneCurrentStepForUi(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex < samplePartCount)
    {
        return currentModulationStepForUi(
            samplePlayerIndexForUi(laneIndex), ModulationLane::pitch);
    }
    const auto parameterIndex = laneIndex - samplePartCount;
    return parameterIndex < sampleParameterCurrentSteps_.size()
        ? sampleParameterCurrentSteps_[parameterIndex]->load(
            std::memory_order_relaxed)
        : -1;
}

LivePatternSequencerProcessor::ModulationAdvanceMode
LivePatternSequencerProcessor::sampleLaneAdvanceModeForUi(
    std::size_t laneIndex) const noexcept
{
    return laneIndex < sampleLaneAdvanceModes_.size()
        ? static_cast<ModulationAdvanceMode>(
            sampleLaneAdvanceModes_[laneIndex]->load(
                std::memory_order_relaxed))
        : ModulationAdvanceMode::onHit;
}

std::size_t LivePatternSequencerProcessor::sampleLanePatternSlotForUi(
    std::size_t laneIndex) const noexcept
{
    return laneIndex < sampleLanePatternSlots_.size()
        ? sampleLanePatternSlots_[laneIndex]->load(std::memory_order_relaxed)
        : 0;
}

std::size_t LivePatternSequencerProcessor::sampleLaneResetGroup(
    std::size_t laneIndex,
    std::size_t sourceIndex) const noexcept
{
    constexpr std::size_t synthSourceCount = synthTwoPatternCount + 1;
    constexpr std::size_t sampleSourceCount = samplePartCount + 1;
    return players_.size() + synthTwoPatternCount
        + synthLaneCountForUi() * synthSourceCount
        + laneIndex * sampleSourceCount + sourceIndex;
}

bool LivePatternSequencerProcessor::resetSampleLane(
    std::size_t laneIndex) noexcept
{
    if (laneIndex >= sampleLaneCountForUi())
        return false;
    const auto sourceIndex = sampleLaneAdvanceModeForUi(laneIndex)
            == ModulationAdvanceMode::onClock
        ? 0 : sampleLanePatternSlotForUi(laneIndex) + 1;
    return runtimeGraph_->armCommand(
        sampleLaneResetGroup(laneIndex, sourceIndex));
}

bool LivePatternSequencerProcessor::sampleLaneResetPendingForUi(
    std::size_t laneIndex) const noexcept
{
    if (laneIndex >= sampleLaneCountForUi())
        return false;
    for (std::size_t sourceIndex = 0;
         sourceIndex <= samplePartCount;
         ++sourceIndex)
    {
        if (runtimeGraph_->commandPending(
                sampleLaneResetGroup(laneIndex, sourceIndex)))
        {
            return true;
        }
    }
    return false;
}

bool LivePatternSequencerProcessor::publishSampleLaneAdvanceSource(
    std::size_t laneIndex,
    ModulationAdvanceMode mode,
    std::size_t patternSlot) noexcept
{
    auto* player = sampleLanePlayerAt(laneIndex);
    const auto sourceIndex = samplePlayerIndexForUi(patternSlot);
    if (player == nullptr || sourceIndex >= players_.size()
        || laneIndex >= sampleLaneAdvanceModes_.size()
        || laneIndex >= sampleLanePatternSlots_.size())
    {
        return false;
    }

    auto candidate = runtimeConfig_;
    bool found = false;
    for (std::size_t index = 0;
         index < candidate.modulationPlayerConfigCount;
         ++index)
    {
        auto& config = candidate.modulationPlayerConfigs[index];
        if (config.player.value != player->playerRef().value)
            continue;
        config.advanceSource = mode == ModulationAdvanceMode::onClock
            ? lps::AdvanceSource {lps::ClockAdvance {0.25}}
            : lps::AdvanceSource {lps::PatternHitAdvance {
                lps::PatternPlayerId {
                    static_cast<std::uint32_t>(sourceIndex) }}};
        config.quantizeSelectionToPatternCycle =
            mode == ModulationAdvanceMode::onHit
                && waitForCycleBeforeSelection;
        found = true;
        break;
    }
    if (!found || !runtimeGraph_->publish(candidate))
        return false;

    runtimeConfig_ = candidate;
    sampleLaneAdvanceModes_[laneIndex]->store(
        static_cast<std::uint8_t>(mode), std::memory_order_relaxed);
    sampleLanePatternSlots_[laneIndex]->store(
        patternSlot, std::memory_order_relaxed);
    return true;
}

void LivePatternSequencerProcessor::setSampleLaneAdvanceMode(
    std::size_t laneIndex,
    ModulationAdvanceMode mode) noexcept
{
    (void) publishSampleLaneAdvanceSource(
        laneIndex, mode, sampleLanePatternSlotForUi(laneIndex));
}

void LivePatternSequencerProcessor::setSampleLanePatternSlot(
    std::size_t laneIndex,
    std::size_t partSlot) noexcept
{
    if (partSlot < samplePartCount)
        (void) publishSampleLaneAdvanceSource(
            laneIndex, sampleLaneAdvanceModeForUi(laneIndex), partSlot);
}

void LivePatternSequencerProcessor::setSampleLaneValue(
    std::size_t laneIndex,
    std::size_t step,
    std::uint8_t value) noexcept
{
    if (laneIndex < samplePartCount)
    {
        setPlayerModulationValue(
            samplePlayerIndexForUi(laneIndex),
            ModulationLane::pitch,
            step,
            value);
        return;
    }
    if (auto* player = sampleLanePlayerAt(laneIndex))
        player->setUnipolar8Value(step, value);
}

void LivePatternSequencerProcessor::setSampleLaneLength(
    std::size_t laneIndex,
    std::size_t length) noexcept
{
    if (laneIndex < samplePartCount)
    {
        setPlayerModulationLength(
            samplePlayerIndexForUi(laneIndex),
            ModulationLane::pitch,
            length);
        return;
    }
    if (auto* player = sampleLanePlayerAt(laneIndex))
        player->setLength(length);
}

void LivePatternSequencerProcessor::updateUiSnapshot() noexcept
{
    bool anyPlaying = false;
    float masterCycleProgress = 0.0f;

    for (std::size_t index = 0; index < currentSteps_.size(); ++index)
    {
        const auto* pattern = players_[index].patternModel;
        const auto patternSnapshot = pattern != nullptr
            ? pattern->patternPlaybackSnapshot()
            : lps::PatternPlaybackSnapshot {};
        currentSteps_[index]->store(
            patternSnapshot.currentStep, std::memory_order_relaxed);
        if (index == configuredMasterIndex())
            masterCycleProgress = patternSnapshot.cycleProgress;
        for (std::size_t laneIndex = 0;
             laneIndex < modulationLaneCount;
             ++laneIndex)
        {
            const auto lane = static_cast<ModulationLane>(laneIndex);
            const auto* modulation = modulationPlayerAt(index, lane);
            const auto snapshot = modulation != nullptr
                ? modulation->modulationPlaybackSnapshot()
                : lps::ModulationPlaybackSnapshot {};
            currentModulationSteps_[index * modulationLaneCount + laneIndex]
                ->store(snapshot.currentStep, std::memory_order_relaxed);
        }
        anyPlaying = anyPlaying || patternSnapshot.playing;
    }

    for (std::size_t index = 0;
         index < synthParameterLanes_.size();
         ++index)
    {
        const auto snapshot = synthParameterLanes_[index].player
            ->modulationPlaybackSnapshot();
        synthParameterCurrentSteps_[index]->store(
            snapshot.currentStep, std::memory_order_relaxed);
    }

    for (std::size_t index = 0;
         index < sampleParameterLanes_.size();
         ++index)
    {
        const auto snapshot = sampleParameterLanes_[index].player
            ->modulationPlaybackSnapshot();
        sampleParameterCurrentSteps_[index]->store(
            snapshot.currentStep, std::memory_order_relaxed);
    }

    playing_.store(anyPlaying, std::memory_order_relaxed);
    currentBarProgress_.store(
        anyPlaying ? masterCycleProgress : 0.0f,
        std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LivePatternSequencerProcessor();
}
