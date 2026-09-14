#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
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
        void mouseDown(const juce::MouseEvent&) override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class ModulationCell final : public juce::Slider
    {
    public:
        ModulationCell();

        void setActive(bool);
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;

    private:
        std::unique_ptr<juce::Component> activePopup_;
        bool active_ = false;
    };

    using ModulationLane = LivePatternSequencerProcessor::ModulationLane;

    void timerCallback() override;
    void paintContent(juce::Graphics&);
    void paintMatrix(juce::Graphics&);
    void resizedContent();
    void resizedMatrix();
    void showSuppressionMatrix();
    void matrixMouseDown(const juce::MouseEvent&);
    void showPatternMenu(std::size_t playerIndex);
    void showModulationMenu(std::size_t playerIndex, ModulationLane lane);
    void contentMouseDown(const juce::MouseEvent&);
    void contentMouseDrag(const juce::MouseEvent&);
    void contentMouseUp(const juce::MouseEvent&);
    void beginSavePlayerPattern(std::size_t playerIndex);
    void handleSaveResult(
        LivePatternSequencerProcessor::SavePatternResult result);
    void beginSavePlayerModulation(
        std::size_t playerIndex,
        ModulationLane lane);
    void promptForModulationName(
        std::size_t playerIndex,
        ModulationLane lane,
        lps::Modulation candidateModulation);
    void handleModulationSaveResult(
        std::size_t playerIndex,
        ModulationLane lane,
        LivePatternSequencerProcessor::SaveModulationResult result);
    void refreshPatternSelectors();
    void refreshModulationSelectors();
    void refreshModulationControls(
        std::size_t playerIndex,
        ModulationLane lane,
        bool force = false);
    void updatePatternModifiedIndicators(bool force = false);
    void updateModulationModifiedIndicators(bool force = false);
    void updateResetToMasterIndicators(bool force = false);
    void showSaveWarning(const juce::String& message);
    void showModulationSaveWarning(
        ModulationLane lane,
        const juce::String& message);
    void showPatternLibraryWarning(const juce::String& message);
    void showModulationLibraryWarning(const juce::String& message);

    LivePatternSequencerProcessor& processor_;
    const std::size_t playerCount_;
    juce::LookAndFeel_V4 lookAndFeel_;
    ContentComponent content_;
    juce::Viewport viewport_;
    MatrixComponent matrix_;
    juce::TextButton suppressionButton_;
    juce::TextButton suppressionCloseButton_;
    juce::Component::SafePointer<juce::DialogWindow> suppressionWindow_;
    std::vector<std::unique_ptr<juce::TextButton>> patternMenuButtons_;
    std::array<std::vector<std::unique_ptr<juce::TextButton>>,
        LivePatternSequencerProcessor::modulationLaneCount>
        modulationMenuButtons_;
    std::vector<std::unique_ptr<juce::ComboBox>> patternSelectors_;
    std::vector<std::unique_ptr<juce::TextButton>> savePatternButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> resetToMasterButtons_;
    std::vector<std::unique_ptr<juce::ComboBox>> speedSelectors_;
    std::vector<std::unique_ptr<juce::TextButton>> offsetLeftButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> offsetRightButtons_;
    std::vector<std::unique_ptr<juce::TextButton>> muteButtons_;
    std::array<std::vector<std::unique_ptr<juce::ComboBox>>,
        LivePatternSequencerProcessor::modulationLaneCount>
        modulationSelectors_;
    std::array<std::vector<std::unique_ptr<juce::TextButton>>,
        LivePatternSequencerProcessor::modulationLaneCount>
        saveModulationButtons_;
    std::array<std::vector<std::unique_ptr<juce::TextButton>>,
        LivePatternSequencerProcessor::modulationLaneCount>
        decreaseModulationLengthButtons_;
    std::array<std::vector<std::unique_ptr<juce::TextButton>>,
        LivePatternSequencerProcessor::modulationLaneCount>
        increaseModulationLengthButtons_;
    std::array<std::vector<std::unique_ptr<ModulationCell>>,
        LivePatternSequencerProcessor::modulationLaneCount>
        modulationSliders_;
    std::vector<bool> displayedPatternModified_;
    std::array<std::vector<bool>,
        LivePatternSequencerProcessor::modulationLaneCount>
        displayedModulationModified_;
    std::vector<bool> displayedResetToMasterPending_;

    enum class DraggedRangeHandle { none, start, end };
    DraggedRangeHandle draggedRangeHandle_ = DraggedRangeHandle::none;
    std::size_t draggedPlayerIndex_ = 0;
    int draggedRangeHandleOffsetX_ = 0;
    juce::Point<int> rangeHandleDragStart_;
    bool rangeHandleWasDragged_ = false;

    [[nodiscard]] juce::Rectangle<int> cellsAreaForPlayer(std::size_t playerIndex) const;
    [[nodiscard]] int cellWidth() const;
    [[nodiscard]] int patternPanelHeight() const;
    [[nodiscard]] int playerStride() const;
    [[nodiscard]] int requiredContentWidth() const;
    [[nodiscard]] static constexpr std::size_t laneIndex(
        ModulationLane lane) noexcept
    {
        return static_cast<std::size_t>(lane);
    }
    void updateContentSize();
    [[nodiscard]] int matrixPanelWidth() const;
    [[nodiscard]] std::size_t stepAtX(std::size_t playerIndex, int x) const;
    [[nodiscard]] std::size_t nearestStepAtX(std::size_t playerIndex, int x) const;
    void updateDraggedRange(int mouseX);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LivePatternSequencerEditor)
};
