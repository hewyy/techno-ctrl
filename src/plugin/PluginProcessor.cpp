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
};

// Default drum-machine map. Mark exactly one voice as the master. Adding or
// removing definitions here does not require routing, matrix, or editor changes.
constexpr std::array defaultDrumVoices {
    DrumVoiceDefinition { "BD1", 36, true },
    DrumVoiceDefinition { "BD2", 37, false },
    DrumVoiceDefinition { "Machine", 38, false },
    DrumVoiceDefinition { "Snare", 39, false },
    DrumVoiceDefinition { "Clap", 40, false },
    DrumVoiceDefinition { "Rimshot", 41, false },
    DrumVoiceDefinition { "OH", 42, false },
    DrumVoiceDefinition { "CH", 43, false },
    DrumVoiceDefinition { "Crash", 44, false },
    DrumVoiceDefinition { "Ride", 45, false }
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
      drumRenderer_(std::make_unique<lps::MidiBufferRenderer>(drumMidiChannel)),
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
    currentModulationSteps_.reserve(totalPlayerCount);

    const auto gateRecord = fixedModulationLibrary_.addOrFind(
        "Constant Gate 1/2", constantModulation(0.5f));
    jassert(gateRecord.entry != nullptr);
    lps::RuntimeGraphConfig graphConfig;

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

        const auto pitchRecord = fixedModulationLibrary_.addOrFind(
            "Constant Pitch " + std::to_string(voice.midiNote),
            constantModulation(static_cast<float>(voice.midiNote) / 127.0f));
        jassert(pitchRecord.entry != nullptr);
        auto pitchPlayer = std::make_unique<lps::ModulationPlayer>(
            fixedModulationLibrary_, pitchModulationId);
        auto velocityPlayer = std::make_unique<lps::ModulationPlayer>(
            modulationLibrary_, velocityModulationId);
        auto gatePlayer = std::make_unique<lps::ModulationPlayer>(
            fixedModulationLibrary_, gateModulationId);
        pitchPlayer->selectModulation(pitchRecord.entry->id);
        gatePlayer->selectModulation(gateRecord.entry->id);
        const auto hitAdvance = lps::PatternHitAdvance {patternId};
        pitchPlayer->setAdvanceSource(hitAdvance);
        velocityPlayer->setAdvanceSource(hitAdvance);
        gatePlayer->setAdvanceSource(hitAdvance);

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

        const bool bindingsAdded = graphConfig.add(lps::TriggerBinding {
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
                gateModulationId, voiceId, gateParameter});
        jassert(bindingsAdded);
        (void) bindingsAdded;

        const auto routeId = lps::RouteId {static_cast<std::uint32_t>(index)};
        const auto cvRouteId = routeId;
        const int cvChannel = static_cast<int>(index) * cvChannelsPerPlayer;
        const bool cvConfigured = cvRenderer_->configureRoute(
                cvRouteId,
                { cvChannel, cvChannel + 1, cvChannel + 2 });
        jassert(cvConfigured);
        (void) cvConfigured;

        PlayerBundle bundle;
        bundle.descriptor = {
            voice.name,
            voice.midiNote,
            true
        };
        bundle.patternController = patternController;
        bundle.patternModel = patternController;
        bundle.modulationModel = velocityPlayer.get();
        bundle.pitchPlayer = std::move(pitchPlayer);
        bundle.velocityPlayer = std::move(velocityPlayer);
        bundle.gatePlayer = std::move(gatePlayer);
        bundle.voice = std::move(resolvedVoice);
        bundle.midiRouteId = routeId;
        bundle.cvRouteId = cvRouteId;
        bundle.realtime = std::move(player);
        players_.push_back(std::move(bundle));
        currentSteps_.push_back(std::make_unique<std::atomic<int>>(-1));
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
        const bool configured = runtimeGraph_->configureArmedCycleCommand(
            index,
            lps::PatternPlayerId {
                static_cast<std::uint32_t>(configuredMasterIndex()) },
            lps::PlayerRef::pattern(lps::PatternPlayerId {
                static_cast<std::uint32_t>(index) }),
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
    drumRenderer_->prepare({spec.sampleRate, spec.maximumBlockSize});
    cvRenderer_->prepare({spec.sampleRate, spec.maximumBlockSize});
    for (auto& trigger : audibleTriggers_)
        trigger = {};
    expectedNextPpq_.reset();
    wasPlaying_ = false;
    updateUiSnapshot();
}

void LivePatternSequencerProcessor::releaseResources()
{
    runtimeGraph_->reset();
    resetGraphOutputs();
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
    cvRenderer_->setAudioBuffer(audio);
    if (block.transportDiscontinuity)
        resetGraphOutputs();

    lps::ResolvedVoiceEventBuffer resolvedEvents;
    if (!runtimeGraph_->process(block, resolvedEvents)
        || !routeGraphOutput(block, resolvedEvents))
    {
        runtimeGraph_->reset();
        resetGraphOutputs();
    }
    cvRenderer_->clearAudioBuffer();
    drumRenderer_->clearMidiBuffer();

    updateUiSnapshot();
}

bool LivePatternSequencerProcessor::routeGraphOutput(
    const lps::TimelineBlock& block,
    const lps::ResolvedVoiceEventBuffer& events) noexcept
{
    std::size_t midiCount = 0;
    std::size_t cvCount = 0;

    for (const auto& resolved : events)
    {
        const auto voiceIndex = static_cast<std::size_t>(
            resolved.sourceVoiceId.value);
        if (voiceIndex >= players_.size())
            return false;

        bool eligible = true;
        if (resolved.event.type == lps::SemanticEventType::triggerStart)
        {
            eligible = !muted_[voiceIndex].load(std::memory_order_relaxed);
            for (std::size_t suppressor = 0;
                 eligible && suppressor < players_.size();
                 ++suppressor)
            {
                if (!suppression_[suppressor][voiceIndex].load(
                        std::memory_order_relaxed))
                    continue;
                if (runtimeGraph_->patternHitOccurred(
                        lps::PatternPlayerId {
                            static_cast<std::uint32_t>(suppressor) },
                        resolved.event.ppqPosition))
                {
                    eligible = false;
                }
            }
            audibleTriggers_[voiceIndex] = {
                resolved.event.triggerId, eligible, true };
        }
        else if (resolved.event.type == lps::SemanticEventType::triggerEnd)
        {
            auto& trigger = audibleTriggers_[voiceIndex];
            eligible = trigger.active
                && trigger.id == resolved.event.triggerId
                && trigger.eligible;
            if (trigger.active && trigger.id == resolved.event.triggerId)
                trigger = {};
        }

        if (!eligible
            && resolved.event.type != lps::SemanticEventType::controlPoint)
            continue;
        if (!std::isfinite(resolved.event.ppqPosition))
            return false;

        std::uint32_t frameOffset = 0;
        if (block.sampleCount != 0)
        {
            if (!std::isfinite(block.ppqStart)
                || !std::isfinite(block.tempoBpm)
                || !std::isfinite(block.sampleRate)
                || block.tempoBpm <= 0.0 || block.sampleRate <= 0.0)
                return false;
            const auto ppqPerSample = block.tempoBpm
                / (60.0 * block.sampleRate);
            const auto rawOffset = (resolved.event.ppqPosition - block.ppqStart)
                / ppqPerSample;
            if (!std::isfinite(rawOffset) || rawOffset < -0.5
                || rawOffset > static_cast<double>(block.sampleCount) - 0.5)
                return false;
            frameOffset = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                std::llround(rawOffset), 0,
                static_cast<std::int64_t>(block.sampleCount - 1)));
        }

        lps::RoutedEvent routed;
        routed.event = resolved.event;
        routed.sourceVoiceId = resolved.sourceVoiceId;
        routed.frameOffset = frameOffset;
        routed.stableOrder = resolved.stableOrder;
        routed.routeId = players_[voiceIndex].midiRouteId;
        if (midiCount == midiRoutedEvents_.size())
            return false;
        midiRoutedEvents_[midiCount++] = routed;

        routed.routeId = players_[voiceIndex].cvRouteId;
        if (cvCount == cvRoutedEvents_.size())
            return false;
        cvRoutedEvents_[cvCount++] = routed;
    }

    const auto midiOk = drumRenderer_->renderBlock(
        block, {midiRoutedEvents_.data(), midiCount});
    const auto cvOk = cvRenderer_->renderBlock(
        block, {cvRoutedEvents_.data(), cvCount});
    return midiOk && cvOk;
}

void LivePatternSequencerProcessor::resetGraphOutputs() noexcept
{
    for (auto& trigger : audibleTriggers_)
        trigger = {};
    drumRenderer_->resetOutputs();
    cvRenderer_->resetOutputs();
}

juce::AudioProcessorEditor* LivePatternSequencerProcessor::createEditor()
{
    return new LivePatternSequencerEditor(*this);
}

#if 0 // Removed legacy player/lane state schema.
void LivePatternSequencerProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("format", "live-pattern-sequencer-state");
    root->setProperty("schemaVersion", 2);
    root->setProperty(
        "masterPlayerId",
        static_cast<juce::int64>(engine_->masterPlayerId().value_or(
            lps::PlayerId {}).value));

    juce::Array<juce::var> serializedPlayers;
    for (std::size_t index = 0; index < players_.size(); ++index)
    {
        const auto& bundle = players_[index];
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("id", static_cast<juce::int64>(index));
        object->setProperty(
            "type",
            bundle.descriptor.type == PlayerDescriptor::Type::pattern
                ? "pattern" : "pulse");
        object->setProperty("name", bundle.descriptor.name);
        object->setProperty("midiNote", bundle.descriptor.midiNote);
        object->setProperty("muted", playerMutedForUi(index));

        juce::Array<juce::var> routes;
        auto midiRoute = juce::DynamicObject::Ptr(new juce::DynamicObject());
        midiRoute->setProperty("renderer", "midi");
        midiRoute->setProperty(
            "routeId", static_cast<juce::int64>(bundle.midiRouteId.value));
        midiRoute->setProperty("fixedPitch", bundle.descriptor.midiNote);
        routes.add(juce::var(midiRoute.get()));
        auto cvRoute = juce::DynamicObject::Ptr(new juce::DynamicObject());
        cvRoute->setProperty("renderer", "cv");
        cvRoute->setProperty(
            "routeId", static_cast<juce::int64>(bundle.cvRouteId.value));
        const auto channel = static_cast<int>(index) * cvChannelsPerPlayer;
        cvRoute->setProperty("gateChannel", channel);
        cvRoute->setProperty("pitchChannel", channel + 1);
        cvRoute->setProperty("controlChannel", channel + 2);
        routes.add(juce::var(cvRoute.get()));
        object->setProperty("routes", routes);

        juce::Array<juce::var> lanes;
        const auto serializeLane = [&lanes](
            const lps::ModulationLaneDefinition& definition,
            const lps::ModulationLaneState& state,
            std::optional<lps::ModulationId> legacyPreset)
        {
            auto lane = juce::DynamicObject::Ptr(new juce::DynamicObject());
            lane->setProperty("id", static_cast<int>(definition.id.value));
            lane->setProperty("target", static_cast<int>(definition.target));
            lane->setProperty("advanceOn", static_cast<int>(definition.advanceOn));
            lane->setProperty("resetOn", static_cast<int>(definition.resetOn));
            lane->setProperty("minimum", definition.mapping.minimum);
            lane->setProperty("maximum", definition.mapping.maximum);
            lane->setProperty("combine", static_cast<int>(definition.mapping.combine));
            lane->setProperty("logicalControl", definition.logicalControl);
            lane->setProperty("length", static_cast<int>(state.length));
            if (legacyPreset.has_value())
            {
                lane->setProperty(
                    "legacyVelocityPresetId",
                    static_cast<juce::int64>(legacyPreset->value()));
            }
            juce::Array<juce::var> values;
            for (std::size_t step = 0; step < state.length; ++step)
                values.add(static_cast<int>(state.values[step].raw));
            lane->setProperty("values", values);
            lanes.add(juce::var(lane.get()));
        };

        if (bundle.patternController != nullptr)
        {
            const auto state = bundle.patternController->capturePersistentState();
            auto pattern = juce::DynamicObject::Ptr(new juce::DynamicObject());
            pattern->setProperty(
                "patternId", static_cast<juce::int64>(state.patternId.value()));
            pattern->setProperty("hitMask", static_cast<juce::int64>(state.hitMask));
            pattern->setProperty("offset", state.patternOffset);
            pattern->setProperty("playbackStart", state.playbackStart);
            pattern->setProperty("playbackEnd", state.playbackEnd);
            pattern->setProperty("playbackSpeed", state.playbackSpeed);
            object->setProperty("pattern", juce::var(pattern.get()));

            lps::ModulationLaneState laneState;
            laneState.length = static_cast<std::uint8_t>(state.velocityModulation.length);
            for (std::size_t step = 0; step < laneState.length; ++step)
            {
                laneState.values[step] = state.velocityModulation.values[step];
            }
            serializeLane(
                lps::makeIntensityLaneDefinition(),
                laneState,
                state.velocityModulationId);
        }
        else if (bundle.pulseController != nullptr)
        {
            const auto state = bundle.pulseController->capturePersistentState();
            object->setProperty("periodPpq", state.periodPpq);
            object->setProperty("gateRatio", state.gateRatio);
            if (state.hasBasePitch)
                object->setProperty("basePitch", state.basePitchSemitones);
            for (std::size_t lane = 0; lane < state.laneCount; ++lane)
                serializeLane(
                    state.laneDefinitions[lane], state.laneStates[lane], std::nullopt);
        }
        object->setProperty("lanes", lanes);
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
    destination.replaceWith(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void LivePatternSequencerProcessor::setStateInformation(
    const void* data,
    int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const auto parsed = juce::JSON::parse(
        juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes));
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr
        || root->getProperty("format").toString()
            != "live-pattern-sequencer-state")
    {
        return;
    }

    const auto integer = [](const juce::DynamicObject* object,
                            const juce::Identifier& property,
                            juce::int64 minimum,
                            juce::int64 maximum,
                            juce::int64& result)
    {
        if (object == nullptr || !object->hasProperty(property))
            return false;
        const auto value = object->getProperty(property);
        if (!value.isInt() && !value.isInt64())
            return false;
        result = static_cast<juce::int64>(value);
        return result >= minimum && result <= maximum;
    };
    const auto number = [](const juce::DynamicObject* object,
                           const juce::Identifier& property,
                           double& result)
    {
        if (object == nullptr || !object->hasProperty(property))
            return false;
        const auto value = object->getProperty(property);
        if (!value.isInt() && !value.isInt64() && !value.isDouble())
            return false;
        result = static_cast<double>(value);
        return std::isfinite(result);
    };

    juce::int64 schemaVersion = 0;
    if (!integer(root, "schemaVersion", 1, 2, schemaVersion))
        return;
    const auto* serializedPlayers = root->getProperty("players").getArray();
    if (serializedPlayers == nullptr)
        return;

    if (schemaVersion == 1)
    {
        if (serializedPlayers->size() > static_cast<int>(players_.size()))
            return;
        struct LegacySelection
        {
            std::size_t index = 0;
            lps::PatternPlayerPersistentState state;
        };
        std::vector<LegacySelection> selections;
        selections.reserve(static_cast<std::size_t>(serializedPlayers->size()));
        for (int index = 0; index < serializedPlayers->size(); ++index)
        {
            auto* player = patternPlayerAt(static_cast<std::size_t>(index));
            const auto* object = serializedPlayers->getReference(index).getDynamicObject();
            juce::int64 patternId = 0;
            juce::int64 modulationId = 0;
            if (player == nullptr
                || !integer(object, "patternId", 1,
                    static_cast<juce::int64>(lps::PatternLibrary::maxEntryCount),
                    patternId)
                || !integer(object, "velocityModulationId", 1,
                    static_cast<juce::int64>(
                        lps::ModulationLibrary::maxEntryCount),
                    modulationId))
            {
                return;
            }

            const auto* pattern = patternLibrary_.find(
                lps::PatternId { static_cast<std::uint64_t>(patternId) });
            const auto* modulation = modulationLibrary_.find(
                lps::ModulationId {
                    static_cast<std::uint64_t>(modulationId) });
            if (pattern == nullptr || modulation == nullptr)
                return;

            auto state = player->capturePersistentState();
            state.patternId = pattern->id;
            state.hitMask = 0;
            const auto patternLength = std::min(
                pattern->pattern.length, lps::Pattern::maxLength);
            if (patternLength == 0)
                return;
            for (std::size_t step = 0; step < patternLength; ++step)
                if (pattern->pattern.hits[step])
                    state.hitMask |= std::uint32_t { 1 } << step;
            state.patternOffset = 0;
            state.playbackStart = 0;
            state.playbackEnd = static_cast<std::uint16_t>(patternLength - 1);
            state.velocityModulationId = modulation->id;
            state.velocityModulation = modulation->modulation;
            selections.push_back({ static_cast<std::size_t>(index), state });
        }
        for (const auto& selection : selections)
            (void) patternPlayerAt(selection.index)->restorePersistentState(
                selection.state);
        return;
    }

    if (serializedPlayers->size() != static_cast<int>(players_.size()))
        return;

    struct RestoredPlayer
    {
        bool muted = false;
        std::optional<lps::PatternPlayerPersistentState> pattern;
        std::optional<lps::PulsePlayerPersistentState> pulse;
    };
    std::vector<RestoredPlayer> restored(players_.size());

    const auto parseLane = [&](const juce::var& value,
                               lps::ModulationLaneDefinition& definition,
                               lps::ModulationLaneState& state,
                               std::optional<lps::ModulationId>& legacyId)
    {
        const auto* lane = value.getDynamicObject();
        juce::int64 id = 0;
        juce::int64 target = 0;
        juce::int64 advance = 0;
        juce::int64 reset = 0;
        juce::int64 combine = 0;
        juce::int64 control = 0;
        juce::int64 length = 0;
        double minimum = 0.0;
        double maximum = 0.0;
        if (!integer(lane, "id", 1, std::numeric_limits<std::uint16_t>::max(), id)
            || !integer(lane, "target", 0,
                static_cast<int>(lps::ModulationTarget::control), target)
            || !integer(lane, "advanceOn", 0,
                static_cast<int>(lps::ModulationAdvancePoint::time), advance)
            || !integer(lane, "resetOn", 0, 255, reset)
            || !integer(lane, "combine", 0,
                static_cast<int>(lps::ModulationCombineMode::multiply), combine)
            || !integer(lane, "logicalControl", 0,
                std::numeric_limits<std::uint16_t>::max(), control)
            || !integer(lane, "length", 1,
                lps::ModulationLaneState::maximumStepCount, length)
            || !number(lane, "minimum", minimum)
            || !number(lane, "maximum", maximum))
        {
            return false;
        }
        const auto* values = lane->getProperty("values").getArray();
        if (values == nullptr || values->size() != static_cast<int>(length))
            return false;

        definition.id = { static_cast<std::uint16_t>(id) };
        definition.target = static_cast<lps::ModulationTarget>(target);
        definition.advanceOn = static_cast<lps::ModulationAdvancePoint>(advance);
        definition.resetOn = static_cast<lps::ModulationResetMask>(reset);
        definition.mapping = {
            static_cast<float>(minimum),
            static_cast<float>(maximum),
            static_cast<lps::ModulationCombineMode>(combine)
        };
        definition.logicalControl = static_cast<std::uint16_t>(control);
        state.length = static_cast<std::uint8_t>(length);
        for (int step = 0; step < values->size(); ++step)
        {
            const auto raw = values->getReference(step);
            if ((!raw.isInt() && !raw.isInt64())
                || static_cast<juce::int64>(raw) < 0
                || static_cast<juce::int64>(raw)
                    > lps::NormalizedValue::maximum)
            {
                return false;
            }
            state.values[static_cast<std::size_t>(step)].raw =
                static_cast<std::uint16_t>(static_cast<juce::int64>(raw));
        }
        if (lane->hasProperty("legacyVelocityPresetId"))
        {
            juce::int64 preset = 0;
            if (!integer(lane, "legacyVelocityPresetId", 1,
                lps::ModulationLibrary::maxEntryCount, preset))
            {
                return false;
            }
            legacyId = lps::ModulationId {
                static_cast<std::uint64_t>(preset) };
        }
        return true;
    };

    for (int index = 0; index < serializedPlayers->size(); ++index)
    {
        const auto playerIndex = static_cast<std::size_t>(index);
        const auto& bundle = players_[playerIndex];
        const auto* object = serializedPlayers->getReference(index).getDynamicObject();
        juce::int64 id = 0;
        juce::int64 midiNote = 0;
        if (!integer(object, "id", index, index, id)
            || !integer(object, "midiNote", 0, 127, midiNote)
            || midiNote != bundle.descriptor.midiNote
            || object->getProperty("type").toString()
                != (bundle.descriptor.type == PlayerDescriptor::Type::pattern
                    ? "pattern" : "pulse")
            || !object->getProperty("muted").isBool())
        {
            return;
        }
        restored[playerIndex].muted = static_cast<bool>(
            object->getProperty("muted"));

        const auto* routes = object->getProperty("routes").getArray();
        if (routes == nullptr || routes->size() != 2)
            return;
        bool foundMidi = false;
        bool foundCv = false;
        for (const auto& routeValue : *routes)
        {
            const auto* route = routeValue.getDynamicObject();
            const auto renderer = route != nullptr
                ? route->getProperty("renderer").toString()
                : juce::String {};
            juce::int64 routeId = 0;
            if (!integer(route, "routeId", 0,
                std::numeric_limits<std::uint32_t>::max(), routeId))
            {
                return;
            }
            if (renderer == "midi")
            {
                juce::int64 fixedPitch = 0;
                if (foundMidi
                    || routeId != bundle.midiRouteId.value
                    || !integer(route, "fixedPitch", 0, 127, fixedPitch)
                    || fixedPitch != bundle.descriptor.midiNote)
                {
                    return;
                }
                foundMidi = true;
            }
            else if (renderer == "cv")
            {
                juce::int64 gate = 0;
                juce::int64 pitch = 0;
                juce::int64 control = 0;
                const auto channel = static_cast<juce::int64>(playerIndex)
                    * cvChannelsPerPlayer;
                if (foundCv
                    || routeId != bundle.cvRouteId.value
                    || !integer(route, "gateChannel", channel, channel, gate)
                    || !integer(route, "pitchChannel", channel + 1,
                        channel + 1, pitch)
                    || !integer(route, "controlChannel", channel + 2,
                        channel + 2, control))
                {
                    return;
                }
                foundCv = true;
            }
            else
            {
                return;
            }
        }
        if (!foundMidi || !foundCv)
            return;

        const auto* lanes = object->getProperty("lanes").getArray();
        if (lanes == nullptr || lanes->isEmpty()
            || lanes->size() > static_cast<int>(lps::ModulationBank::maximumLaneCount))
        {
            return;
        }

        if (bundle.descriptor.type == PlayerDescriptor::Type::pattern)
        {
            if (lanes->size() != 1)
                return;
            const auto* pattern = object->getProperty("pattern").getDynamicObject();
            juce::int64 patternId = 0;
            juce::int64 hitMask = 0;
            juce::int64 offset = 0;
            juce::int64 playbackStart = 0;
            juce::int64 playbackEnd = 0;
            juce::int64 playbackSpeed = 0;
            if (!integer(pattern, "patternId", 1,
                    lps::PatternLibrary::maxEntryCount, patternId)
                || !integer(pattern, "hitMask", 0,
                    std::numeric_limits<std::uint32_t>::max(), hitMask)
                || !integer(pattern, "offset",
                    std::numeric_limits<int>::min(),
                    std::numeric_limits<int>::max(), offset)
                || !integer(pattern, "playbackStart", 0,
                    lps::Pattern::maxLength - 1, playbackStart)
                || !integer(pattern, "playbackEnd", playbackStart,
                    lps::Pattern::maxLength - 1, playbackEnd)
                || !integer(pattern, "playbackSpeed", 0,
                    lps::PatternPlayer::playbackSpeedCount - 1, playbackSpeed))
            {
                return;
            }

            lps::ModulationLaneDefinition definition;
            lps::ModulationLaneState laneState;
            std::optional<lps::ModulationId> legacyId;
            if (!parseLane(lanes->getReference(0), definition, laneState, legacyId)
                || definition.target != lps::ModulationTarget::intensity
                || !legacyId.has_value()
                || modulationLibrary_.find(*legacyId) == nullptr
                || laneState.length > lps::Modulation::maxLength
                || patternLibrary_.find(lps::PatternId {
                    static_cast<std::uint64_t>(patternId) }) == nullptr)
            {
                return;
            }

            lps::PatternPlayerPersistentState state;
            state.patternId = lps::PatternId {
                static_cast<std::uint64_t>(patternId) };
            state.hitMask = static_cast<std::uint32_t>(hitMask);
            state.patternOffset = static_cast<int>(offset);
            state.playbackStart = static_cast<std::uint16_t>(playbackStart);
            state.playbackEnd = static_cast<std::uint16_t>(playbackEnd);
            state.playbackSpeed = static_cast<std::uint8_t>(playbackSpeed);
            state.velocityModulationId = *legacyId;
            state.velocityModulation.length = laneState.length;
            for (std::size_t step = 0; step < laneState.length; ++step)
            {
                state.velocityModulation.values[step] = laneState.values[step];
            }
            restored[playerIndex].pattern = state;
        }
        else
        {
            lps::PulsePlayerPersistentState state;
            if (!number(object, "periodPpq", state.periodPpq)
                || !number(object, "gateRatio", state.gateRatio))
            {
                return;
            }
            if (object->hasProperty("basePitch"))
            {
                double pitch = 0.0;
                if (!number(object, "basePitch", pitch))
                    return;
                state.hasBasePitch = true;
                state.basePitchSemitones = static_cast<float>(pitch);
            }
            state.laneCount = static_cast<std::uint8_t>(lanes->size());
            for (int laneIndex = 0; laneIndex < lanes->size(); ++laneIndex)
            {
                std::optional<lps::ModulationId> legacyId;
                if (!parseLane(
                    lanes->getReference(laneIndex),
                    state.laneDefinitions[static_cast<std::size_t>(laneIndex)],
                    state.laneStates[static_cast<std::size_t>(laneIndex)],
                    legacyId)
                    || legacyId.has_value())
                {
                    return;
                }
            }
            restored[playerIndex].pulse = state;
        }
    }

    std::vector<std::pair<std::size_t, std::size_t>> suppressionEdges;
    const auto* suppressions = root->getProperty("suppressions").getArray();
    if (suppressions == nullptr)
        return;
    suppressionEdges.reserve(static_cast<std::size_t>(suppressions->size()));
    for (const auto& edgeValue : *suppressions)
    {
        const auto* edge = edgeValue.getDynamicObject();
        juce::int64 from = 0;
        juce::int64 to = 0;
        if (!integer(edge, "from", 0, players_.size() - 1, from)
            || !integer(edge, "to", 0, players_.size() - 1, to)
            || from == to)
        {
            return;
        }
        suppressionEdges.emplace_back(
            static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    }

    juce::int64 master = 0;
    if (!integer(root, "masterPlayerId", 0, players_.size() - 1, master))
        return;

    for (std::size_t index = 0; index < restored.size(); ++index)
    {
        bool restoredSuccessfully = false;
        if (restored[index].pattern.has_value())
        {
            restoredSuccessfully = players_[index].patternController
                ->restorePersistentState(*restored[index].pattern);
        }
        else if (restored[index].pulse.has_value())
        {
            restoredSuccessfully = players_[index].pulseController
                ->restorePersistentState(*restored[index].pulse);
        }
        if (!restoredSuccessfully)
            return;
        setPlayerMuted(index, restored[index].muted);
    }
    for (std::size_t from = 0; from < players_.size(); ++from)
        for (std::size_t to = 0; to < players_.size(); ++to)
            if (from != to)
                setSuppression(from, to, false);
    for (const auto& edge : suppressionEdges)
        setSuppression(edge.first, edge.second, true);
    (void) engine_->setMasterPlayer(lps::PlayerId {
        static_cast<std::uint32_t>(master) });
}

#endif

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
        const auto modulation = players_[index].velocityPlayer
            ->modulationForSave();
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("patternId",
            static_cast<juce::int64>(pattern.patternId.value()));
        object->setProperty("hitMask",
            static_cast<juce::int64>(pattern.hitMask));
        object->setProperty("offset", pattern.patternOffset);
        object->setProperty("playbackStart", pattern.playbackStart);
        object->setProperty("playbackEnd", pattern.playbackEnd);
        object->setProperty("playbackSpeed", pattern.playbackSpeed);
        object->setProperty("modulationId", static_cast<juce::int64>(
            players_[index].velocityPlayer->selectedModulationId().value()));
        juce::Array<juce::var> values;
        for (std::size_t step = 0; step < modulation.length; ++step)
            values.add(static_cast<int>(modulation.values[step].raw));
        object->setProperty("modulationValues", values);
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
        || serializedPlayers->size() != static_cast<int>(players_.size()))
        return;

    for (int index = 0; index < serializedPlayers->size(); ++index)
    {
        const auto* object = serializedPlayers->getReference(index)
            .getDynamicObject();
        const auto* values = object != nullptr
            ? object->getProperty("modulationValues").getArray()
            : nullptr;
        if (object == nullptr || values == nullptr || values->isEmpty()
            || values->size() > static_cast<int>(lps::Modulation::maxLength))
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
        const auto modulationId = lps::ModulationId {
            static_cast<std::uint64_t>(static_cast<juce::int64>(
                object->getProperty("modulationId")))};
        if (patternLibrary_.find(pattern.patternId) == nullptr
            || modulationLibrary_.find(modulationId) == nullptr
            || !object->getProperty("muted").isBool())
            return;
        for (const auto& value : *values)
            if ((!value.isInt() && !value.isInt64())
                || static_cast<juce::int64>(value) < 0
                || static_cast<juce::int64>(value)
                    > lps::NormalizedValue::maximum)
                return;

        const auto playerIndex = static_cast<std::size_t>(index);
        if (!players_[playerIndex].patternController
                ->restorePersistentState(pattern))
            return;
        auto& modulation = *players_[playerIndex].velocityPlayer;
        modulation.selectModulation(modulationId);
        modulation.prepare({});
        modulation.setLength(static_cast<std::size_t>(values->size()));
        for (int step = 0; step < values->size(); ++step)
            modulation.setValue(static_cast<std::size_t>(step), {
                static_cast<std::uint16_t>(static_cast<juce::int64>(
                    values->getReference(step))) });
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
    std::size_t playerIndex) const noexcept
{
    return playerIndex < currentModulationSteps_.size()
        ? currentModulationSteps_[playerIndex]->load(
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

bool LivePatternSequencerProcessor::playerSupportsVelocityEditingForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && players_[playerIndex].descriptor.supportsVelocityEditing;
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
    return pattern != nullptr ? juce::String(pattern->name) : juce::String {};
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
LivePatternSequencerProcessor::savePlayerPattern(
    std::size_t playerIndex,
    const juce::String& name)
{
    auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr)
        return {};

    return savePlayerPattern(playerIndex, player->patternForSave(), name);
}

LivePatternSequencerProcessor::SavePatternResult
LivePatternSequencerProcessor::savePlayerPattern(
    std::size_t playerIndex,
    const lps::Pattern& candidatePattern,
    const juce::String& name)
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

        const auto trimmedName = name.trim();
        if (trimmedName.isEmpty())
            return {
                SavePatternStatus::needsName,
                std::numeric_limits<std::size_t>::max(),
                candidatePattern
            };

        const auto insertion = patternLibrary_.addOrFind(
            trimmedName.toStdString(),
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
    std::size_t playerIndex) noexcept
{
    return playerIndex < players_.size()
        ? players_[playerIndex].velocityPlayer.get()
        : nullptr;
}

const lps::ModulationPlayer* LivePatternSequencerProcessor::modulationPlayerAt(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        ? players_[playerIndex].velocityPlayer.get()
        : nullptr;
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

std::size_t LivePatternSequencerProcessor::velocityModulationCountForUi() const noexcept
{
    return modulationLibrary_.size();
}

juce::String LivePatternSequencerProcessor::velocityModulationNameForUi(
    std::size_t modulationIndex) const
{
    const auto* modulation = modulationLibrary_.recordAt(modulationIndex);
    return modulation != nullptr
        ? juce::String(modulation->name)
        : juce::String {};
}

juce::String
LivePatternSequencerProcessor::velocityModulationCatalogErrorForUi() const
{
    return modulationLibraryFileStore_.lastError();
}

lps::Modulation LivePatternSequencerProcessor::velocityModulationForUi(
    std::size_t playerIndex) const noexcept
{
    const auto* player = modulationPlayerAt(playerIndex);
    return player != nullptr
        ? player->modulationForUi()
        : lps::Modulation {};
}

bool LivePatternSequencerProcessor::playerModulationModifiedForUi(
    std::size_t playerIndex) const noexcept
{
    const auto* player = modulationPlayerAt(playerIndex);
    return player != nullptr
        && player->hasUnsavedChanges();
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::savePlayerModulation(
    std::size_t playerIndex,
    const juce::String& name)
{
    auto* player = modulationPlayerAt(playerIndex);
    if (player == nullptr)
        return {};

    return savePlayerModulation(
        playerIndex, player->modulationForSave(), name);
}

LivePatternSequencerProcessor::SaveModulationResult
LivePatternSequencerProcessor::savePlayerModulation(
    std::size_t playerIndex,
    const lps::Modulation& candidateModulation,
    const juce::String& name)
{
    auto* player = modulationPlayerAt(playerIndex);
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
    std::size_t modulationIndex) noexcept
{
    auto* player = modulationPlayerAt(playerIndex);
    const auto* modulation = modulationLibrary_.recordAt(modulationIndex);
    if (player != nullptr && modulation != nullptr)
        player->selectModulation(modulation->id);
}

std::size_t LivePatternSequencerProcessor::selectedModulationForPlayer(
    std::size_t playerIndex) const noexcept
{
    const auto* player = modulationPlayerAt(playerIndex);
    if (player == nullptr)
        return 0;

    return modulationLibrary_.indexOf(
        player->selectedModulationId()).value_or(0);
}

void LivePatternSequencerProcessor::setPlayerModulationValue(
    std::size_t playerIndex,
    std::size_t step,
    std::uint8_t value) noexcept
{
    if (auto* player = modulationPlayerAt(playerIndex))
        player->setUnipolar8Value(step, value);
}

void LivePatternSequencerProcessor::setPlayerModulationLength(
    std::size_t playerIndex,
    std::size_t length) noexcept
{
    if (auto* player = modulationPlayerAt(playerIndex))
        player->setLength(length);
}

void LivePatternSequencerProcessor::setPlayerMuted(
    std::size_t playerIndex,
    bool muted) noexcept
{
    if (playerIndex < players_.size())
        muted_[playerIndex].store(muted, std::memory_order_relaxed);
}

bool LivePatternSequencerProcessor::playerMutedForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < players_.size()
        && muted_[playerIndex].load(std::memory_order_relaxed);
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
        suppression_[suppressorIndex][suppressedIndex].store(
            enabled, std::memory_order_relaxed);
    }
}

bool LivePatternSequencerProcessor::suppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex) const noexcept
{
    return suppressorIndex < players_.size()
        && suppressedIndex < players_.size()
        && suppressorIndex != suppressedIndex
        && suppression_[suppressorIndex][suppressedIndex].load(
            std::memory_order_relaxed);
}

void LivePatternSequencerProcessor::updateUiSnapshot() noexcept
{
    bool anyPlaying = false;

    for (std::size_t index = 0; index < currentSteps_.size(); ++index)
    {
        const auto* pattern = players_[index].patternModel;
        const auto* modulation = players_[index].modulationModel;
        const auto patternSnapshot = pattern != nullptr
            ? pattern->patternPlaybackSnapshot()
            : lps::PatternPlaybackSnapshot {};
        const auto modulationSnapshot = modulation != nullptr
            ? modulation->modulationPlaybackSnapshot()
            : lps::ModulationPlaybackSnapshot {};
        currentSteps_[index]->store(
            patternSnapshot.currentStep, std::memory_order_relaxed);
        currentModulationSteps_[index]->store(
            modulationSnapshot.currentStep,
            std::memory_order_relaxed);
        anyPlaying = anyPlaying || patternSnapshot.playing;
    }

    playing_.store(anyPlaying, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LivePatternSequencerProcessor();
}
