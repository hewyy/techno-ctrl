#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
struct DrumVoiceDefinition
{
    const char* name;
    std::uint8_t midiNote;
    bool isMaster;
    std::uint8_t midiChannel;
    bool hasCvOutput;
};

// Default drum-machine map. Mark exactly one voice as the master. Adding or
// removing definitions here does not require routing, matrix, or editor changes.
constexpr std::array defaultDrumVoices {
    DrumVoiceDefinition { "BD1", 36, true, 2, true },
    DrumVoiceDefinition { "BD2", 37, false, 2, true },
    DrumVoiceDefinition { "Machine", 38, false, 2, true },
    DrumVoiceDefinition { "Snare", 39, false, 2, true },
    DrumVoiceDefinition { "Clap", 40, false, 2, true },
    DrumVoiceDefinition { "Rimshot", 41, false, 2, true },
    DrumVoiceDefinition { "OH", 42, false, 2, true },
    DrumVoiceDefinition { "CH", 43, false, 2, true },
    DrumVoiceDefinition { "Crash", 44, false, 2, true },
    DrumVoiceDefinition { "Ride", 45, false, 2, true },
    DrumVoiceDefinition { "Voice 11", 46, false, 1, false }
};

constexpr std::size_t configuredMasterCount() noexcept
{
    std::size_t count = 0;
    for (const auto& voice : defaultDrumVoices)
        if (voice.isMaster)
            ++count;
    return count;
}

constexpr std::size_t configuredMasterIndex() noexcept
{
    for (std::size_t index = 0; index < defaultDrumVoices.size(); ++index)
        if (defaultDrumVoices[index].isMaster)
            return index;
    return defaultDrumVoices.size();
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
          existingVoiceMidiChannel)),
      channelOneRenderer_(std::make_unique<lps::MidiBufferRenderer>(
          newVoiceMidiChannel)),
      cvRenderer_(std::make_unique<lps::CvBufferRenderer>()),
      runtimeGraph_(std::make_unique<lps::RuntimeGraph>())
{
    // Catalog replacement is startup-only and must finish before PatternPlayer
    // instances retain references to the library.
    (void) patternLibraryFileStore_.loadOrCreate(patternLibrary_);
    (void) modulationLibraryFileStore_.loadOrCreate(
        modulationLibrary_);

    const auto totalPlayerCount = defaultDrumVoices.size();
    players_.reserve(totalPlayerCount);
    currentSteps_.reserve(totalPlayerCount);
    currentModulationSteps_.reserve(
        totalPlayerCount * modulationLaneCount);

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
    lps::RuntimeGraphConfig graphConfig;
    const bool outputsRegistered = runtimeGraph_->registerOutputEndpoint(
            channelTwoMidiOutputEndpoint, *drumRenderer_)
        && runtimeGraph_->registerOutputEndpoint(
            cvOutputEndpoint, *cvRenderer_)
        && runtimeGraph_->registerOutputEndpoint(
            channelOneMidiOutputEndpoint, *channelOneRenderer_);
    jassert(outputsRegistered);
    (void) outputsRegistered;

    for (std::size_t index = 0; index < defaultDrumVoices.size(); ++index)
    {
        const auto& voice = defaultDrumVoices[index];
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
        const auto voiceId = lps::VoiceId {
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

        auto resolvedVoice = std::make_unique<lps::Voice>(voiceId);
        constexpr lps::VoiceParameterId pitchParameter {0};
        constexpr lps::VoiceParameterId intensityParameter {1};
        constexpr lps::VoiceParameterId gateParameter {2};
        const bool voiceConfigured = resolvedVoice->addParameter(
                lps::VoiceParameterDescriptor::pitch(pitchParameter))
            && resolvedVoice->addParameter(
                lps::VoiceParameterDescriptor::intensity(intensityParameter))
            && resolvedVoice->addParameter(
                lps::VoiceParameterDescriptor::gate(gateParameter));
        jassert(voiceConfigured);
        (void) voiceConfigured;

        const bool nodesRegistered = runtimeGraph_->registerPatternPlayer(*player)
            && runtimeGraph_->registerModulationPlayer(*pitchPlayer)
            && runtimeGraph_->registerModulationPlayer(*velocityPlayer)
            && runtimeGraph_->registerModulationPlayer(*gatePlayer)
            && runtimeGraph_->registerVoice(*resolvedVoice);
        jassert(nodesRegistered);
        (void) nodesRegistered;

        const auto transitionPolicy = waitForCycleBeforeSelection
            ? lps::PatternTransitionPolicy {}
            : lps::PatternTransitionPolicy {
                lps::PatternTransitionPolicyType::immediate, {}};
        const bool playerConfigsAdded = graphConfig.addPlayerConfig(
                {patternId, lps::ClockAdvance {0.25},
                    lps::PlayMode::continuous, transitionPolicy})
            && graphConfig.addPlayerConfig(
                {pitchModulationId, hitAdvance, lps::PlayMode::continuous,
                    waitForCycleBeforeSelection})
            && graphConfig.addPlayerConfig(
                {velocityModulationId, hitAdvance, lps::PlayMode::continuous,
                    waitForCycleBeforeSelection})
            && graphConfig.addPlayerConfig(
                {gateModulationId, hitAdvance, lps::PlayMode::continuous,
                    waitForCycleBeforeSelection});
        jassert(playerConfigsAdded);
        (void) playerConfigsAdded;

        const auto midiEndpoint = voice.midiChannel == newVoiceMidiChannel
            ? channelOneMidiOutputEndpoint
            : channelTwoMidiOutputEndpoint;
        bool bindingsAdded = graphConfig.add(lps::TriggerBinding {
                lps::TriggerBindingId {static_cast<std::uint32_t>(index)},
                patternId, voiceId})
            && graphConfig.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationBase},
                pitchModulationId, voiceId, pitchParameter})
            && graphConfig.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationBase + 1},
                velocityModulationId, voiceId, intensityParameter})
            && graphConfig.add(lps::ParameterBinding {
                lps::ParameterBindingId {modulationBase + 2},
                gateModulationId, voiceId, gateParameter})
            && graphConfig.add(lps::OutputBinding {
                lps::OutputBindingId {
                    static_cast<std::uint32_t>(index * 2) },
                voiceId, midiEndpoint,
                lps::RouteId {static_cast<std::uint32_t>(index)}, {},
                lps::OutputSignalType::triggers});
        if (voice.hasCvOutput)
        {
            bindingsAdded = bindingsAdded
                && graphConfig.add(lps::OutputBinding {
                lps::OutputBindingId {
                    static_cast<std::uint32_t>(index * 2 + 1) },
                voiceId, cvOutputEndpoint,
                lps::RouteId {static_cast<std::uint32_t>(index)}, {},
                lps::OutputSignalType::triggers});
        }
        jassert(bindingsAdded);
        (void) bindingsAdded;

        const auto routeId = lps::RouteId {static_cast<std::uint32_t>(index)};
        const auto cvRouteId = routeId;
        if (voice.hasCvOutput)
        {
            jassert(index < cvPlayerCapacity);
            const int cvChannel = static_cast<int>(index)
                * cvChannelsPerPlayer;
            const bool cvConfigured = cvRenderer_->configureRoute(
                    cvRouteId,
                    { cvChannel, cvChannel + 1, cvChannel + 2 });
            jassert(cvConfigured);
            (void) cvConfigured;
        }

        PlayerBundle bundle;
        bundle.descriptor = {
            voice.name,
            voice.midiNote,
            voice.midiChannel,
            voice.hasCvOutput
        };
        bundle.patternController = patternController;
        bundle.patternModel = patternController;
        bundle.pitchPlayer = std::move(pitchPlayer);
        bundle.velocityPlayer = std::move(velocityPlayer);
        bundle.gatePlayer = std::move(gatePlayer);
        bundle.voice = std::move(resolvedVoice);
        bundle.midiRouteId = routeId;
        bundle.cvRouteId = cvRouteId;
        bundle.realtime = std::move(player);
        players_.push_back(std::move(bundle));
        currentSteps_.push_back(std::make_unique<std::atomic<int>>(-1));
        for (std::size_t lane = 0; lane < modulationLaneCount; ++lane)
            currentModulationSteps_.push_back(
                std::make_unique<std::atomic<int>>(-1));
    }

    const bool graphActivated = runtimeGraph_->activate(graphConfig);
    jassert(graphActivated);
    (void) graphActivated;
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
    updateUiSnapshot();
}

void LivePatternSequencerProcessor::releaseResources()
{
    runtimeGraph_->reset();
    runtimeGraph_->resetOutputs();
    expectedNextPpq_.reset();
    wasPlaying_ = false;
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

    if (const auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            block.playing = position->getIsPlaying();

            if (const auto bpm = position->getBpm())
                block.tempoBpm = *bpm;

            if (const auto ppq = position->getPpqPosition())
                block.ppqStart = *ppq;
            else
                block.playing = false;
        }
    }

    const double ppqPerSample = block.tempoBpm > 0.0 && block.sampleRate > 0.0
        ? block.tempoBpm / (60.0 * block.sampleRate)
        : 0.0;
    block.ppqEnd = block.ppqStart
        + static_cast<double>(block.sampleCount) * ppqPerSample;

    if (block.playing)
    {
        const double tolerance = ppqPerSample * 4.0 + 1.0e-7;
        block.transportDiscontinuity = !wasPlaying_
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
    channelOneRenderer_->setMidiBuffer(midi);
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
    channelOneRenderer_->clearMidiBuffer();

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
    root->setProperty("schemaVersion", 1);

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
        const auto serializeModulation = [&object](
            const char* idProperty,
            const char* valuesProperty,
            const lps::ModulationPlayer& player)
        {
            object->setProperty(idProperty, static_cast<juce::int64>(
                player.selectedModulationId().value()));
            const auto modulation = player.modulationForSave();
            juce::Array<juce::var> values;
            for (std::size_t step = 0; step < modulation.length; ++step)
                values.add(static_cast<int>(modulation.values[step].raw));
            object->setProperty(valuesProperty, values);
        };
        serializeModulation(
            "modulationId", "modulationValues",
            *players_[index].velocityPlayer);
        serializeModulation(
            "pitchModulationId", "pitchModulationValues",
            *players_[index].pitchPlayer);
        serializeModulation(
            "gateModulationId", "gateModulationValues",
            *players_[index].gatePlayer);
        object->setProperty("muted", playerMutedForUi(index));
        serializedPlayers.add(juce::var(object.get()));
    }
    root->setProperty("players", serializedPlayers);

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
    if (root == nullptr
        || root->getProperty("format").toString()
            != "live-pattern-sequencer-graph-state"
        || static_cast<int>(root->getProperty("schemaVersion")) != 1)
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
            || !object->getProperty("muted").isBool())
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

        struct SavedModulation
        {
            ModulationLane lane;
            lps::ModulationId id;
            const juce::Array<juce::var>* values;
        };
        std::array<SavedModulation, modulationLaneCount> savedModulations {{
            {ModulationLane::velocity, velocityModulationId, velocityValues},
            {ModulationLane::pitch, {}, nullptr},
            {ModulationLane::gate, {}, nullptr}
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
            if (saved.values != nullptr
                && modulationLibrary_.find(saved.id) == nullptr)
                return;

        const auto playerIndex = static_cast<std::size_t>(index);
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
        }
        setPlayerMuted(playerIndex,
            static_cast<bool>(object->getProperty("muted")));
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
    return existingVoiceMidiChannel;
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

            player->selectSavedPattern(existing->id);
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

        player->selectSavedPattern(insertion.entry->id);
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
    auto* player = patternPlayerAt(playerIndex);
    const auto* pattern = patternLibrary_.recordAt(patternIndex);
    if (player != nullptr && pattern != nullptr)
        player->selectPattern(pattern->id);
}

std::size_t LivePatternSequencerProcessor::selectedPatternForPlayer(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr)
        return 0;

    return patternLibrary_.indexOf(player->selectedPatternId()).value_or(0);
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

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::savePlayerModulation(
    std::size_t playerIndex,
    ModulationLane lane,
    const juce::String& name)
{
    auto* player = modulationPlayerAt(playerIndex, lane);
    if (player == nullptr)
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
        || candidateModulation.length == 0
        || candidateModulation.length > lps::Modulation::maxLength)
    {
        return {};
    }

    try
    {
        if (const auto* existing = modulationLibrary_.findEquivalent(
                candidateModulation))
        {
            const auto index = modulationLibrary_.indexOf(existing->id);
            if (!index)
                return {};

            player->selectModulation(existing->id);
            return {
                SaveModulationStatus::selectedExisting,
                *index,
                candidateModulation
            };
        }

        const auto trimmedName = name.trim();
        if (trimmedName.isEmpty())
        {
            return {
                SaveModulationStatus::needsName,
                std::numeric_limits<std::size_t>::max(),
                candidateModulation
            };
        }

        const auto insertion = modulationLibrary_.addOrFind(
            trimmedName.toStdString(),
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

        player->selectModulation(insertion.entry->id);
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
    if (player != nullptr && modulation != nullptr)
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
    if (auto* player = modulationPlayerAt(playerIndex, lane))
        player->setUnipolar8Value(step, value);
}

void LivePatternSequencerProcessor::setPlayerModulationLength(
    std::size_t playerIndex,
    ModulationLane lane,
    std::size_t length) noexcept
{
    if (auto* player = modulationPlayerAt(playerIndex, lane))
        player->setLength(length);
}

void LivePatternSequencerProcessor::setPlayerMuted(
    std::size_t playerIndex,
    bool muted) noexcept
{
    if (playerIndex < players_.size())
        runtimeGraph_->setVoiceMuted(
            lps::VoiceId {static_cast<std::uint32_t>(playerIndex)}, muted);
}

bool LivePatternSequencerProcessor::playerMutedForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && runtimeGraph_->voiceMuted(
            lps::VoiceId {static_cast<std::uint32_t>(playerIndex)});
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

void LivePatternSequencerProcessor::updateUiSnapshot() noexcept
{
    bool anyPlaying = false;

    for (std::size_t index = 0; index < currentSteps_.size(); ++index)
    {
        const auto* pattern = players_[index].patternModel;
        const auto patternSnapshot = pattern != nullptr
            ? pattern->patternPlaybackSnapshot()
            : lps::PatternPlaybackSnapshot {};
        currentSteps_[index]->store(
            patternSnapshot.currentStep, std::memory_order_relaxed);
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

    playing_.store(anyPlaying, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LivePatternSequencerProcessor();
}
