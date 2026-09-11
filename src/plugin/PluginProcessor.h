#pragma once

#include "core/PatternLibrary.h"
#include "core/SequencerEngine.h"
#include "core/PatternPlayer.h"
#include "core/VelocityModulationLibrary.h"
#include "plugin/MidiBufferRenderer.h"
#include "plugin/PatternLibraryFileStore.h"
#include "plugin/VelocityModulationLibraryFileStore.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class LivePatternSequencerProcessor final : public juce::AudioProcessor
{
public:
    enum class SavePatternStatus
    {
        failed,
        needsName,
        selectedExisting,
        savedNew
    };

    struct SavePatternResult
    {
        SavePatternStatus status = SavePatternStatus::failed;
        std::size_t patternIndex = std::numeric_limits<std::size_t>::max();
        lps::Pattern candidatePattern;
    };

    enum class SaveVelocityModulationStatus
    {
        failed,
        needsName,
        selectedExisting,
        savedNew
    };

    struct SaveVelocityModulationResult
    {
        SaveVelocityModulationStatus status =
            SaveVelocityModulationStatus::failed;
        std::size_t modulationIndex = std::numeric_limits<std::size_t>::max();
        lps::VelocityModulation candidateModulation;
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
    [[nodiscard]] bool isMidiEffect() const override { return true; }
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
    [[nodiscard]] int currentVelocityModulationStepForUi(
        std::size_t playerIndex = 0) const noexcept;
    [[nodiscard]] bool playingForUi() const noexcept;
    [[nodiscard]] lps::PatternView patternForUi(std::size_t playerIndex = 0) const noexcept;
    [[nodiscard]] juce::String playerNameForUi(std::size_t playerIndex) const;
    [[nodiscard]] int playerMidiNoteForUi(std::size_t playerIndex) const noexcept;
    [[nodiscard]] int drumMidiChannelForUi() const noexcept;
    [[nodiscard]] std::size_t patternCountForUi() const noexcept;
    [[nodiscard]] juce::String patternNameForUi(std::size_t patternIndex) const;
    [[nodiscard]] juce::String patternCatalogErrorForUi() const;
    [[nodiscard]] bool playerPatternModifiedForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] SavePatternResult savePlayerPattern(
        std::size_t playerIndex,
        const juce::String& name = {});
    [[nodiscard]] SavePatternResult savePlayerPattern(
        std::size_t playerIndex,
        const lps::Pattern& candidatePattern,
        const juce::String& name);
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

    [[nodiscard]] std::size_t velocityModulationCountForUi() const noexcept;
    [[nodiscard]] juce::String velocityModulationNameForUi(
        std::size_t modulationIndex) const;
    [[nodiscard]] juce::String velocityModulationCatalogErrorForUi() const;
    [[nodiscard]] lps::VelocityModulation velocityModulationForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] bool playerVelocityModulationModifiedForUi(
        std::size_t playerIndex) const noexcept;
    [[nodiscard]] SaveVelocityModulationResult savePlayerVelocityModulation(
        std::size_t playerIndex,
        const juce::String& name = {});
    [[nodiscard]] SaveVelocityModulationResult savePlayerVelocityModulation(
        std::size_t playerIndex,
        const lps::VelocityModulation& candidateModulation,
        const juce::String& name);
    void selectVelocityModulationForPlayer(
        std::size_t playerIndex,
        std::size_t modulationIndex) noexcept;
    [[nodiscard]] std::size_t selectedVelocityModulationForPlayer(
        std::size_t playerIndex) const noexcept;
    void setPlayerVelocityModulationValue(
        std::size_t playerIndex,
        std::size_t step,
        std::uint8_t value) noexcept;
    void setPlayerVelocityModulationLength(
        std::size_t playerIndex,
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
    static constexpr int drumMidiChannel = 1;

    void updateUiSnapshot() noexcept;
    [[nodiscard]] lps::PatternPlayer* playerAt(std::size_t playerIndex) noexcept;
    [[nodiscard]] const lps::PatternPlayer* playerAt(std::size_t playerIndex) const noexcept;

    // The library must outlive every player because players keep a read-only
    // reference to its immutable, append-only entries.
    lps::PatternLibrary patternLibrary_;
    lps::VelocityModulationLibrary velocityModulationLibrary_;
    PatternLibraryFileStore patternLibraryFileStore_;
    VelocityModulationLibraryFileStore velocityModulationLibraryFileStore_;
    std::vector<juce::String> playerNames_;
    std::vector<std::unique_ptr<lps::PatternPlayer>> players_;
    std::unique_ptr<lps::MidiBufferRenderer> drumRenderer_;
    std::unique_ptr<lps::SequencerEngine> engine_;

    std::optional<double> expectedNextPpq_;
    bool wasPlaying_ = false;

    std::vector<std::unique_ptr<std::atomic<int>>> currentSteps_;
    std::vector<std::unique_ptr<std::atomic<int>>>
        currentVelocityModulationSteps_;
    std::atomic<bool> playing_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivePatternSequencerProcessor)
};
