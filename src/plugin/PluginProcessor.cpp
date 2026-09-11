#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
}

LivePatternSequencerProcessor::LivePatternSequencerProcessor()
    : LivePatternSequencerProcessor(
        PatternLibraryFileStore::defaultCatalogFile())
{
}

LivePatternSequencerProcessor::LivePatternSequencerProcessor(
    const juce::File& patternCatalogFile)
    : juce::AudioProcessor(juce::AudioProcessor::BusesProperties()),
      patternLibraryFileStore_(patternCatalogFile),
      velocityModulationLibraryFileStore_(
          patternCatalogFile.getSiblingFile("velocity-modulations.json")),
      drumRenderer_(std::make_unique<lps::MidiBufferRenderer>(drumMidiChannel)),
      engine_(std::make_unique<lps::SequencerEngine>())
{
    // Catalog replacement is startup-only and must finish before PatternPlayer
    // instances retain references to the library.
    (void) patternLibraryFileStore_.loadOrCreate(patternLibrary_);
    (void) velocityModulationLibraryFileStore_.loadOrCreate(
        velocityModulationLibrary_);

    constexpr std::size_t additionalPulsePlayers = 1;
    const auto totalPlayerCount = defaultDrumVoices.size() + additionalPulsePlayers;
    players_.reserve(totalPlayerCount);
    currentSteps_.reserve(totalPlayerCount);
    currentVelocityModulationSteps_.reserve(totalPlayerCount);

    for (std::size_t index = 0; index < defaultDrumVoices.size(); ++index)
    {
        const auto& voice = defaultDrumVoices[index];
        auto player = std::make_unique<lps::PatternPlayer>(
            patternLibrary_, velocityModulationLibrary_);
        if (patternLibrary_.size() != 0)
        {
            if (const auto* pattern = patternLibrary_.recordAt(index % patternLibrary_.size()))
                player->selectPattern(pattern->id);
        }
        auto* patternController = player.get();
        const auto playerId = engine_->registerPlayer(*patternController);
        jassert(playerId.has_value());
        const auto routeId = playerId.has_value()
            ? engine_->connect(
                *playerId,
                *drumRenderer_,
                lps::RouteMapping::fixedPitch(
                    static_cast<float>(voice.midiNote)))
            : std::nullopt;
        jassert(routeId.has_value());
        (void) routeId;

        PlayerBundle bundle;
        bundle.descriptor = { voice.name, voice.midiNote, true };
        bundle.patternController = patternController;
        bundle.patternModel = patternController;
        bundle.modulationModel = patternController;
        bundle.realtime = std::move(player);
        players_.push_back(std::move(bundle));
        currentSteps_.push_back(std::make_unique<std::atomic<int>>(-1));
        currentVelocityModulationSteps_.push_back(
            std::make_unique<std::atomic<int>>(-1));
    }

    auto pulse = std::make_unique<lps::PulsePlayer>(0.5, 0.5);
    lps::ModulationLaneState pulseIntensity;
    if (const auto* modulation = velocityModulationLibrary_.recordAt(0))
    {
        pulseIntensity.length = static_cast<std::uint8_t>(std::min<std::size_t>(
            modulation->modulation.length, pulseIntensity.values.size()));
        for (std::size_t step = 0; step < pulseIntensity.length; ++step)
        {
            pulseIntensity.values[step] = lps::NormalizedValue::fromUnipolar8(
                modulation->modulation.values[step]);
        }
    }
    pulse->publishModulationState(pulseIntensity);
    auto* pulseController = pulse.get();
    const auto pulseId = engine_->registerPlayer(*pulseController);
    jassert(pulseId.has_value());
    constexpr int pulseMidiNote = 46;
    const auto pulseRoute = pulseId.has_value()
        ? engine_->connect(
            *pulseId,
            *drumRenderer_,
            lps::RouteMapping::fixedPitch(static_cast<float>(pulseMidiNote)))
        : std::nullopt;
    jassert(pulseRoute.has_value());
    (void) pulseRoute;

    PlayerBundle pulseBundle;
    pulseBundle.descriptor = { "Pulse", pulseMidiNote, false };
    pulseBundle.modulationModel = pulseController;
    pulseBundle.realtime = std::move(pulse);
    players_.push_back(std::move(pulseBundle));
    currentSteps_.push_back(std::make_unique<std::atomic<int>>(-1));
    currentVelocityModulationSteps_.push_back(
        std::make_unique<std::atomic<int>>(-1));

    const auto masterId = engine_->playerIdAt(configuredMasterIndex());
    const bool masterConfigured = masterId.has_value()
        && engine_->setMasterPlayer(*masterId);
    jassert(masterConfigured);
    (void) masterConfigured;
}

const juce::String LivePatternSequencerProcessor::getName() const
{
    return JucePlugin_Name;
}

void LivePatternSequencerProcessor::prepareToPlay(
    double sampleRate,
    int maximumExpectedSamplesPerBlock)
{
    engine_->prepare({
        sampleRate,
        static_cast<std::uint32_t>(std::max(maximumExpectedSamplesPerBlock, 0))
    });
    expectedNextPpq_.reset();
    wasPlaying_ = false;
    updateUiSnapshot();
}

void LivePatternSequencerProcessor::releaseResources()
{
    engine_->reset();
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
    engine_->run(block);
    drumRenderer_->clearMidiBuffer();

    updateUiSnapshot();
}

juce::AudioProcessorEditor* LivePatternSequencerProcessor::createEditor()
{
    return new LivePatternSequencerEditor(*this);
}

void LivePatternSequencerProcessor::getStateInformation(juce::MemoryBlock&)
{
    // Plugin state persistence is not implemented in this MVP.
}

void LivePatternSequencerProcessor::setStateInformation(const void*, int)
{
    // Plugin state persistence is not implemented in this MVP.
}

std::size_t LivePatternSequencerProcessor::playerCountForUi() const noexcept
{
    return engine_->playerCount();
}

std::optional<std::size_t>
LivePatternSequencerProcessor::masterPlayerIndexForUi() const noexcept
{
    const auto masterId = engine_->masterPlayerId();
    return masterId && masterId->value < playerCountForUi()
        ? std::optional<std::size_t> { masterId->value }
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

    const auto playerId = engine_->playerIdAt(playerIndex);
    return playerId.has_value() && engine_->requestResetToMaster(*playerId);
}

bool LivePatternSequencerProcessor::playerResetToMasterPendingForUi(
    std::size_t playerIndex) const noexcept
{
    const auto playerId = engine_->playerIdAt(playerIndex);
    return playerId.has_value()
        && !playerIsMasterForUi(playerIndex)
        && engine_->resetToMasterPending(*playerId);
}

int LivePatternSequencerProcessor::currentStepForUi(std::size_t playerIndex) const noexcept
{
    return playerIndex < currentSteps_.size()
        ? currentSteps_[playerIndex]->load(std::memory_order_relaxed)
        : -1;
}

int LivePatternSequencerProcessor::currentVelocityModulationStepForUi(
    std::size_t playerIndex) const noexcept
{
    return playerIndex < currentVelocityModulationSteps_.size()
        ? currentVelocityModulationSteps_[playerIndex]->load(
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
    return velocityModulationLibrary_.size();
}

juce::String LivePatternSequencerProcessor::velocityModulationNameForUi(
    std::size_t modulationIndex) const
{
    const auto* modulation = velocityModulationLibrary_.recordAt(modulationIndex);
    return modulation != nullptr
        ? juce::String(modulation->name)
        : juce::String {};
}

juce::String
LivePatternSequencerProcessor::velocityModulationCatalogErrorForUi() const
{
    return velocityModulationLibraryFileStore_.lastError();
}

lps::VelocityModulation LivePatternSequencerProcessor::velocityModulationForUi(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr
        ? player->velocityModulationForUi()
        : lps::VelocityModulation {};
}

bool LivePatternSequencerProcessor::playerVelocityModulationModifiedForUi(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    return player != nullptr
        && player->hasUnsavedVelocityModulationChanges();
}

LivePatternSequencerProcessor::SaveVelocityModulationResult
LivePatternSequencerProcessor::savePlayerVelocityModulation(
    std::size_t playerIndex,
    const juce::String& name)
{
    auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr)
        return {};

    return savePlayerVelocityModulation(
        playerIndex, player->velocityModulationForSave(), name);
}

LivePatternSequencerProcessor::SaveVelocityModulationResult
LivePatternSequencerProcessor::savePlayerVelocityModulation(
    std::size_t playerIndex,
    const lps::VelocityModulation& candidateModulation,
    const juce::String& name)
{
    auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr
        || candidateModulation.length == 0
        || candidateModulation.length > lps::VelocityModulation::maxLength)
    {
        return {};
    }

    try
    {
        if (const auto* existing = velocityModulationLibrary_.findEquivalent(
                candidateModulation))
        {
            const auto index = velocityModulationLibrary_.indexOf(existing->id);
            if (!index)
                return {};

            player->selectVelocityModulation(existing->id);
            return {
                SaveVelocityModulationStatus::selectedExisting,
                *index,
                candidateModulation
            };
        }

        const auto trimmedName = name.trim();
        if (trimmedName.isEmpty())
        {
            return {
                SaveVelocityModulationStatus::needsName,
                std::numeric_limits<std::size_t>::max(),
                candidateModulation
            };
        }

        const auto insertion = velocityModulationLibrary_.addOrFind(
            trimmedName.toStdString(),
            candidateModulation,
            [this](lps::VelocityModulationLibraryEntry& stagedEntry)
            {
                return velocityModulationLibraryFileStore_.persistNewEntry(
                    velocityModulationLibrary_, stagedEntry);
            });
        if (insertion.entry == nullptr)
            return {};

        const auto index = velocityModulationLibrary_.indexOf(insertion.entry->id);
        if (!index)
            return {};

        player->selectVelocityModulation(insertion.entry->id);
        return {
            insertion.inserted
                ? SaveVelocityModulationStatus::savedNew
                : SaveVelocityModulationStatus::selectedExisting,
            *index,
            candidateModulation
        };
    }
    catch (...)
    {
        return {};
    }
}

void LivePatternSequencerProcessor::selectVelocityModulationForPlayer(
    std::size_t playerIndex,
    std::size_t modulationIndex) noexcept
{
    auto* player = patternPlayerAt(playerIndex);
    const auto* modulation = velocityModulationLibrary_.recordAt(modulationIndex);
    if (player != nullptr && modulation != nullptr)
        player->selectVelocityModulation(modulation->id);
}

std::size_t LivePatternSequencerProcessor::selectedVelocityModulationForPlayer(
    std::size_t playerIndex) const noexcept
{
    const auto* player = patternPlayerAt(playerIndex);
    if (player == nullptr)
        return 0;

    return velocityModulationLibrary_.indexOf(
        player->selectedVelocityModulationId()).value_or(0);
}

void LivePatternSequencerProcessor::setPlayerVelocityModulationValue(
    std::size_t playerIndex,
    std::size_t step,
    std::uint8_t value) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex))
        player->setVelocityModulationValue(step, value);
}

void LivePatternSequencerProcessor::setPlayerVelocityModulationLength(
    std::size_t playerIndex,
    std::size_t length) noexcept
{
    if (auto* player = patternPlayerAt(playerIndex))
        player->setVelocityModulationLength(length);
}

void LivePatternSequencerProcessor::setPlayerMuted(
    std::size_t playerIndex,
    bool muted) noexcept
{
    if (const auto playerId = engine_->playerIdAt(playerIndex))
        engine_->setPlayerMuted(*playerId, muted);
}

bool LivePatternSequencerProcessor::playerMutedForUi(
    std::size_t playerIndex) const noexcept
{
    const auto playerId = engine_->playerIdAt(playerIndex);
    return playerId.has_value() && engine_->playerMuted(*playerId);
}

void LivePatternSequencerProcessor::setSuppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex,
    bool enabled) noexcept
{
    const auto suppressorId = engine_->playerIdAt(suppressorIndex);
    const auto suppressedId = engine_->playerIdAt(suppressedIndex);
    if (suppressorId && suppressedId)
        engine_->setSuppression(*suppressorId, *suppressedId, enabled);
}

bool LivePatternSequencerProcessor::suppression(
    std::size_t suppressorIndex,
    std::size_t suppressedIndex) const noexcept
{
    const auto suppressorId = engine_->playerIdAt(suppressorIndex);
    const auto suppressedId = engine_->playerIdAt(suppressedIndex);
    return suppressorId && suppressedId
        && engine_->suppression(*suppressorId, *suppressedId);
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
        currentVelocityModulationSteps_[index]->store(
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
