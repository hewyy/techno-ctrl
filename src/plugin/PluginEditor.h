#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

class LivePatternSequencerEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit LivePatternSequencerEditor(LivePatternSequencerProcessor&);
    ~LivePatternSequencerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class ContentComponent final : public juce::Component
    {
    public:
        explicit ContentComponent(LivePatternSequencerEditor&) noexcept;

        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class MatrixComponent final : public juce::Component
    {
    public:
        explicit MatrixComponent(LivePatternSequencerEditor&) noexcept;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    void timerCallback() override;
    void paintContent(juce::Graphics&);
    void paintMatrix(juce::Graphics&);
    void resizedContent();
    void resizedMatrix();
    void contentMouseDown(const juce::MouseEvent&);
    void contentMouseDrag(const juce::MouseEvent&);
    void contentMouseUp(const juce::MouseEvent&);
    void beginSavePlayerPattern(std::size_t playerIndex);
    void promptForPatternName(
        std::size_t playerIndex,
        lps::Pattern candidatePattern);
    void handleSaveResult(
        std::size_t playerIndex,
        LivePatternSequencerProcessor::SavePatternResult result);
    void beginSavePlayerVelocityModulation(std::size_t playerIndex);
    void promptForVelocityModulationName(
        std::size_t playerIndex,
        lps::VelocityModulation candidateModulation);
    void handleVelocityModulationSaveResult(
        std::size_t playerIndex,
        LivePatternSequencerProcessor::SaveVelocityModulationResult result);
    void refreshPatternSelectors();
    void refreshVelocityModulationSelectors();
    void refreshVelocityModulationControls(
        std::size_t playerIndex,
        bool force = false);
    void updatePatternModifiedIndicators(bool force = false);
    void updateVelocityModulationModifiedIndicators(bool force = false);
    void updateResetToMasterIndicators(bool force = false);
    void showSaveWarning(const juce::String& message);
    void showVelocityModulationSaveWarning(const juce::String& message);
    void showPatternLibraryWarning(const juce::String& message);
    void showVelocityModulationLibraryWarning(const juce::String& message);

    LivePatternSequencerProcessor& processor_;
    const std::size_t playerCount_;
    ContentComponent content_;
    juce::Viewport viewport_;
    MatrixComponent matrix_;
    std::vector<std::unique_ptr<juce::ComboBox>> patternSelectors_;
    std::vector<std::unique_ptr<juce::TextButton>> savePatternButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> resetToMasterButtons_;
    std::vector<std::unique_ptr<juce::ComboBox>> speedSelectors_;
    std::vector<std::unique_ptr<juce::TextButton>> offsetLeftButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> offsetRightButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> muteButtons_;
    std::vector<std::unique_ptr<juce::ComboBox>> velocityModulationSelectors_;
    std::vector<std::unique_ptr<juce::TextButton>>
        saveVelocityModulationButtons_;
    std::vector<std::unique_ptr<juce::TextButton>>
        decreaseVelocityModulationLengthButtons_;
    std::vector<std::unique_ptr<juce::TextButton>>
        increaseVelocityModulationLengthButtons_;
    std::vector<std::unique_ptr<juce::Slider>> velocityModulationSliders_;
    std::vector<std::unique_ptr<juce::ToggleButton>> suppressionButtons_;
    std::vector<bool> displayedPatternModified_;
    std::vector<bool> displayedVelocityModulationModified_;
    std::vector<bool> displayedResetToMasterPending_;

    enum class DraggedRangeHandle { none, start, end };
    DraggedRangeHandle draggedRangeHandle_ = DraggedRangeHandle::none;
    std::size_t draggedPlayerIndex_ = 0;
    int draggedRangeHandleOffsetX_ = 0;

    [[nodiscard]] juce::Rectangle<int> cellsAreaForPlayer(std::size_t playerIndex) const;
    [[nodiscard]] int cellWidth() const;
    [[nodiscard]] int requiredContentWidth() const;
    void updateContentWidth();
    [[nodiscard]] int matrixPanelWidth() const;
    [[nodiscard]] std::size_t stepAtX(std::size_t playerIndex, int x) const;
    [[nodiscard]] std::size_t nearestStepAtX(std::size_t playerIndex, int x) const;
    void updateDraggedRange(int mouseX);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivePatternSequencerEditor)
};
