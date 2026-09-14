#pragma once

#include "core/PatternLibrary.h"
#include "core/PatternPlayer.h"
#include "core/ModulationLibrary.h"
#include "core/ModulationPlayer.h"
#include "core/RuntimeGraph.h"
#include "core/Voice.h"
#include "plugin/CvBufferRenderer.h"
#include "plugin/MidiBufferRenderer.h"
#include "plugin/PatternLibraryFileStore.h"
#include "plugin/ModulationLibraryFileStore.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class LivePatternSequencerProcessor final : public juce::AudioProcessor
{
public:
    enum class ModulationLane : std::uint8_t
    {
        velocity,
        pitch,
        gate
    };

    static constexpr std::size_t modulationLaneCount = 3;

    enum class SavePatternStatus
    {
        failed,
        selectedExisting,
        savedNew
    };

    struct SavePatternResult
    {
        SavePatternStatus status = SavePatternStatus::failed;
        std::size_t patternIndex = std::numeric_limits<std::size_t>::max();
        lps::Pattern candidatePattern;
    };

    enum class SaveModulationStatus
    {
        failed,
        needsName,
        selectedExisting,
        savedNew
    };

    struct SaveModulationResult
    {
        SaveModulationStatus status =
            SaveModulationStatus::failed;
        std::size_t modulationIndex = std::numeric_limits<std::size_t>::max();
        lps::Modulation candidateModulation;
    };

    LivePatternSequencerProcessor();
    explicit LivePatternSequencerProcessor(const juce::File& patternCatalogFile);
    ~LivePatternSequencerProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    [[nodiscard]] bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    [[nodiscard]] const juce::String getName() const override;
    [[nodiscard]] bool acceptsMidi() const override { return false; }
    [[nodiscard]] bool producesMidi() const override { return true; }
    [[nodiscard]] bool isMidiEffect() const override { return false; }
    [[nodiscard]] bool isBusesLayoutSupported(
        const BusesLayout& layouts) const override;
    [[nodiscard]] double getTailLengthSeconds() const override { return 0.0; }

    [[nodiscard]] int getNumPrograms() override { return 1; }
    [[nodiscard]] int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    [[nodiscard]] const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    [[nodiscard]] std::size_t playerCountForUi() const noexcept;
    [[nodiscard]] std::optional<std::size_t> masterPlayerIndexForUi() const noexcept;
    [[nodiscard]] bool playerIsMasterForUi(std::size_t playerIndex) const noexcept;
    [[nodiscard]] bool resetPlayerToMaster(std::size_t playerIndex) noexcept;
    [[nodiscard]] bool playerResetToMasterPendingForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] int currentStepForUi(std::size_t playerIndex = 0) const noexcept;
    [[nodiscard]] int currentModulationStepForUi(
        std::size_t playerIndex,
        ModulationLane lane) const noexcept;
    [[nodiscard]] bool playingForUi() const noexcept;
    [[nodiscard]] lps::PatternView patternForUi(std::size_t playerIndex = 0) const noexcept;
    [[nodiscard]] juce::String playerNameForUi(std::size_t playerIndex) const;
    [[nodiscard]] bool playerSupportsPatternEditingForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] bool playerSupportsModulationEditingForUi(
        std::size_t playerIndex,
        ModulationLane lane) const noexcept;
    [[nodiscard]] bool playerCanResetToMasterForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] int playerMidiNoteForUi(std::size_t playerIndex) const noexcept;
    [[nodiscard]] int drumMidiChannelForUi() const noexcept;
    [[nodiscard]] std::size_t patternCountForUi() const noexcept;
    [[nodiscard]] lps::Pattern patternAtForUi(
        std::size_t patternIndex) const noexcept;
    [[nodiscard]] juce::String patternNameForUi(std::size_t patternIndex) const;
    [[nodiscard]] juce::String patternCatalogErrorForUi() const;
    [[nodiscard]] bool playerPatternModifiedForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] SavePatternResult savePlayerPattern(std::size_t playerIndex);
    [[nodiscard]] SavePatternResult savePlayerPattern(
        std::size_t playerIndex,
        const lps::Pattern& candidatePattern);
    void selectPatternForPlayer(std::size_t playerIndex, std::size_t patternIndex) noexcept;
    [[nodiscard]] std::size_t selectedPatternForPlayer(std::size_t playerIndex) const noexcept;
    void offsetPlayerPatternLeft(std::size_t playerIndex) noexcept;
    void offsetPlayerPatternRight(std::size_t playerIndex) noexcept;
    [[nodiscard]] int playerPatternOffset(std::size_t playerIndex) const noexcept;
    void setPlayerPlaybackSpeed(std::size_t playerIndex, std::size_t speedIndex) noexcept;
    [[nodiscard]] std::size_t playerPlaybackSpeed(std::size_t playerIndex) const noexcept;
    void setPlayerPlaybackWindow(
        std::size_t playerIndex,
        std::size_t startStep,
        std::size_t endStep) noexcept;
    [[nodiscard]] std::size_t playerPlaybackStart(std::size_t playerIndex) const noexcept;
    [[nodiscard]] std::size_t playerPlaybackEnd(std::size_t playerIndex) const noexcept;
    void togglePlayerStep(std::size_t playerIndex, std::size_t step) noexcept;

    [[nodiscard]] std::size_t modulationCountForUi() const noexcept;
    [[nodiscard]] lps::Modulation modulationAtForUi(
        std::size_t modulationIndex) const noexcept;
    [[nodiscard]] juce::String modulationNameForUi(
        std::size_t modulationIndex) const;
    [[nodiscard]] juce::String modulationCatalogErrorForUi() const;
    [[nodiscard]] lps::Modulation modulationForUi(
        std::size_t playerIndex,
        ModulationLane lane) const noexcept;
    [[nodiscard]] bool playerModulationModifiedForUi(
        std::size_t playerIndex,
        ModulationLane lane) const noexcept;
    [[nodiscard]] SaveModulationResult savePlayerModulation(
        std::size_t playerIndex,
        ModulationLane lane,
        const juce::String& name = {});
    [[nodiscard]] SaveModulationResult savePlayerModulation(
        std::size_t playerIndex,
        ModulationLane lane,
        const lps::Modulation& candidateModulation,
        const juce::String& name);
    void selectModulationForPlayer(
        std::size_t playerIndex,
        ModulationLane lane,
        std::size_t modulationIndex) noexcept;
    [[nodiscard]] std::size_t selectedModulationForPlayer(
        std::size_t playerIndex,
        ModulationLane lane) const noexcept;
    void setPlayerModulationValue(
        std::size_t playerIndex,
        ModulationLane lane,
        std::size_t step,
        std::uint8_t value) noexcept;
    void setPlayerModulationLength(
        std::size_t playerIndex,
        ModulationLane lane,
        std::size_t length) noexcept;
    void setPlayerMuted(std::size_t playerIndex, bool muted) noexcept;
    [[nodiscard]] bool playerMutedForUi(std::size_t playerIndex) const noexcept;
    void setSuppression(
        std::size_t suppressorIndex,
        std::size_t suppressedIndex,
        bool enabled) noexcept;
    [[nodiscard]] bool suppression(
        std::size_t suppressorIndex,
        std::size_t suppressedIndex) const noexcept;

private:
    static constexpr int existingVoiceMidiChannel = 2;
    static constexpr int newVoiceMidiChannel = 1;
    // This is intentionally a policy switch so a future configuration menu
    // can expose immediate selection without changing the graph topology.
    static constexpr bool waitForCycleBeforeSelection = true;
    static constexpr std::size_t cvPlayerCapacity = 10;
    static constexpr int cvChannelsPerPlayer = 3;
    static constexpr int cvOutputChannelCount =
        static_cast<int>(cvPlayerCapacity) * cvChannelsPerPlayer;
    static constexpr lps::OutputEndpointId channelTwoMidiOutputEndpoint {0};
    static constexpr lps::OutputEndpointId cvOutputEndpoint {1};
    static constexpr lps::OutputEndpointId channelOneMidiOutputEndpoint {2};

    struct PlayerDescriptor
    {
        juce::String name;
        int midiNote = -1;
        int midiChannel = 1;
        bool hasCvOutput = false;
    };

    struct PlayerBundle
    {
        std::unique_ptr<lps::IPlayer> realtime;
        std::unique_ptr<lps::ModulationPlayer> pitchPlayer;
        std::unique_ptr<lps::ModulationPlayer> velocityPlayer;
        std::unique_ptr<lps::ModulationPlayer> gatePlayer;
        std::unique_ptr<lps::Voice> voice;
        PlayerDescriptor descriptor;
        lps::PatternPlayer* patternController = nullptr;
        lps::IPatternEditorModel* patternModel = nullptr;
        lps::RouteId midiRouteId;
        lps::RouteId cvRouteId;
    };

    void updateUiSnapshot() noexcept;
    [[nodiscard]] lps::PatternPlayer* patternPlayerAt(
        std::size_t playerIndex) noexcept;
    [[nodiscard]] const lps::PatternPlayer* patternPlayerAt(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] lps::ModulationPlayer* modulationPlayerAt(
        std::size_t playerIndex,
        ModulationLane lane) noexcept;
    [[nodiscard]] const lps::ModulationPlayer* modulationPlayerAt(
        std::size_t playerIndex,
        ModulationLane lane) const noexcept;
    [[nodiscard]] static constexpr std::size_t modulationLaneIndex(
        ModulationLane lane) noexcept
    {
        return static_cast<std::size_t>(lane);
    }

    // The library must outlive every player because players keep a read-only
    // reference to its immutable, append-only entries.
    lps::PatternLibrary patternLibrary_;
    lps::ModulationLibrary modulationLibrary_;
    PatternLibraryFileStore patternLibraryFileStore_;
    ModulationLibraryFileStore modulationLibraryFileStore_;
    std::vector<PlayerBundle> players_;
    std::unique_ptr<lps::MidiBufferRenderer> drumRenderer_;
    std::unique_ptr<lps::MidiBufferRenderer> channelOneRenderer_;
    std::unique_ptr<lps::CvBufferRenderer> cvRenderer_;
    std::unique_ptr<lps::RuntimeGraph> runtimeGraph_;

    std::optional<double> expectedNextPpq_;
    bool wasPlaying_ = false;

    std::vector<std::unique_ptr<std::atomic<int>>> currentSteps_;
    std::vector<std::unique_ptr<std::atomic<int>>>
        currentModulationSteps_;
    std::atomic<bool> playing_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivePatternSequencerProcessor)
};
