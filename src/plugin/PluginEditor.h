#pragma once

#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
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

    class MatrixComponent final : public juce::PopupMenu::CustomComponent
    {
    public:
        explicit MatrixComponent(LivePatternSequencerEditor&) noexcept;

        void getIdealSize(int& idealWidth, int& idealHeight) override;
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class ControlPaneComponent final : public juce::Component
    {
    public:
        explicit ControlPaneComponent(LivePatternSequencerEditor&) noexcept;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class BarSchedulerComponent final : public juce::Component
    {
    public:
        explicit BarSchedulerComponent(
            LivePatternSequencerEditor&) noexcept;

        void paint(juce::Graphics&) override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class ModulationEditorComponent final : public juce::Component
    {
    public:
        explicit ModulationEditorComponent(
            LivePatternSequencerEditor&) noexcept;

        void paint(juce::Graphics&) override;
        void resized() override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class ModulationBlockLayer final : public juce::Component
    {
    public:
        explicit ModulationBlockLayer(
            LivePatternSequencerEditor&) noexcept;
        void mouseDown(const juce::MouseEvent&) override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class MenuDismissLayer final : public juce::Component
    {
    public:
        explicit MenuDismissLayer(LivePatternSequencerEditor&) noexcept;
        void mouseDown(const juce::MouseEvent&) override;

    private:
        LivePatternSequencerEditor& editor_;
    };

    class PagedViewport final : public juce::Viewport
    {
    public:
        void mouseWheelMove(
            const juce::MouseEvent&,
            const juce::MouseWheelDetails&) override;
    };

    class SynthPageComponent;

    class UtilityButton final : public juce::TextButton
    {
    public:
        enum class Kind { previousPage, nextPage, play, pause, matrix };

        explicit UtilityButton(Kind kind) noexcept;
        void setKind(Kind kind) noexcept;
        void paintButton(juce::Graphics&, bool isMouseOverButton,
            bool isButtonDown) override;

    private:
        Kind kind_;
    };

    class SpeakerButton final : public juce::TextButton
    {
    public:
        enum class Kind { mute, unmute };

        explicit SpeakerButton(Kind kind = Kind::mute) noexcept;
        void setKind(Kind kind) noexcept;
        void setScheduledTarget(std::size_t targetBar);
        void paintButton(juce::Graphics&, bool isMouseOverButton,
            bool isButtonDown) override;

    private:
        Kind kind_;
        std::size_t scheduledTargetBar_ = 0;
    };

    class GroupAssignmentButton final : public juce::Button
    {
    public:
        GroupAssignmentButton();

        void setMembershipMask(std::uint8_t mask);
        void paintButton(juce::Graphics&, bool isMouseOverButton,
            bool isButtonDown) override;
        void clicked(const juce::ModifierKeys&) override;

        std::function<void(std::size_t)> onGroupClicked;

    private:
        std::uint8_t membershipMask_ = 0;
    };

    class ModulationCell final : public juce::Slider
    {
    public:
        ModulationCell();

        void setActive(bool);
        void setPitchDisplay(bool shouldDisplayPitch) noexcept
        {
            pitchDisplay_ = shouldDisplayPitch;
            repaint();
        }
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseDrag(const juce::MouseEvent&) override;
        void mouseUp(const juce::MouseEvent&) override;
        [[nodiscard]] int takeEditDirection() noexcept;
        void setValueFromModel(double value);

    private:
        std::unique_ptr<juce::Component> activePopup_;
        lps::QuantizedValueEditTracker editTracker_;
        bool active_ = false;
        bool pitchDisplay_ = false;
    };

    using ModulationLane = LivePatternSequencerProcessor::ModulationLane;

    void timerCallback() override;
    void paintContent(juce::Graphics&);
    void paintModulationPreview(
        juce::Graphics&,
        std::size_t playerIndex,
        juce::Rectangle<int> bounds);
    void paintControlPane(juce::Graphics&);
    void paintBarScheduler(juce::Graphics&);
    void paintModulationEditor(juce::Graphics&);
    void paintMatrix(juce::Graphics&, const juce::Component&);
    void resizedContent();
    void resizedControlPane();
    void resizedModulationEditor();
    void toggleModulationEditor(
        std::size_t playerIndex,
        juce::Component* anchorComponent = nullptr);
    void positionModulationEditor();
    void modulationBlockLayerMouseDown();
    [[nodiscard]] std::size_t scheduledBarsFromNow() const noexcept;
    [[nodiscard]] bool playerMutedAtBarOffset(
        std::size_t playerIndex,
        std::size_t barsFromNow) const noexcept;
    [[nodiscard]] std::optional<std::size_t> heldScheduleBar() const noexcept;
    enum class PopupKind { none, pattern, modulation, matrix };
    [[nodiscard]] std::uint64_t beginPopupSession(
        PopupKind kind,
        juce::Component* target);
    void finishPopupSession(std::uint64_t generation);
    void menuDismissLayerMouseDown(const juce::MouseEvent&);
    void showSuppressionMatrix();
    void matrixMouseDown(const juce::MouseEvent&, juce::Component&);
    void showPatternMenu(
        std::size_t playerIndex,
        juce::Component* targetComponent);
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
    void handleModulationSaveResult(
        std::size_t playerIndex,
        ModulationLane lane,
        LivePatternSequencerProcessor::SaveModulationResult result);
    void refreshPatternSelectors();
    void refreshModulationSelectors();
    void selectVoice(std::size_t playerIndex, bool additiveSelection);
    void refreshSelectedControls(bool force = false);
    void applyToSelectedPatternPlayers(
        const std::function<void(std::size_t)>& action);
    void applyToSelectedPlayers(
        const std::function<void(std::size_t)>& action);
    [[nodiscard]] std::size_t selectedVoiceCount() const noexcept;
    [[nodiscard]] std::size_t pageSize() const noexcept;
    void pageVoices(int direction);
    void updatePageButtons();
    enum class MainPage { allVoices, synthTwo, sample };
    void showPage(MainPage page);
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
    PagedViewport viewport_;
    juce::Viewport synthViewport_;
    std::unique_ptr<SynthPageComponent> synthPage_;
    std::unique_ptr<SynthPageComponent> samplePage_;
    ControlPaneComponent controlPane_;
    BarSchedulerComponent barScheduler_;
    ModulationBlockLayer modulationBlockLayer_;
    ModulationEditorComponent modulationEditor_;
    MenuDismissLayer menuDismissLayer_;
    UtilityButton suppressionButton_;
    UtilityButton previousPageButton_;
    UtilityButton nextPageButton_;
    UtilityButton globalPlayButton_;
    juce::TextButton voicesPageButton_ {"ALL VOICES"};
    juce::TextButton synthTwoPageButton_ {"SYNTH 2"};
    juce::TextButton samplePageButton_ {"SAMPLE"};
    juce::Label selectionLabel_;
    std::vector<std::unique_ptr<juce::Label>> controlVoiceLabels_;
    std::vector<std::unique_ptr<SpeakerButton>>
        controlVoiceMuteButtons_;
    std::array<juce::Label,
        LivePatternSequencerProcessor::groupCount> groupLabels_;
    std::array<SpeakerButton,
        LivePatternSequencerProcessor::groupCount> groupMuteButtons_;
    std::array<SpeakerButton,
        LivePatternSequencerProcessor::groupCount> groupUnmuteButtons_;
    std::array<juce::TextButton,
        LivePatternSequencerProcessor::groupCount> groupResetButtons_;
    bool suppressionMenuOpen_ = false;
    MainPage visiblePage_ = MainPage::synthTwo;
    enum class SuppressionMatrixScope { patternPlayers, voices };
    SuppressionMatrixScope suppressionMatrixScope_ =
        SuppressionMatrixScope::patternPlayers;
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
    std::vector<std::unique_ptr<SpeakerButton>> muteButtons_;
    std::vector<std::unique_ptr<GroupAssignmentButton>> groupAssignmentButtons_;
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
    std::vector<bool> selectedVoices_;
    std::size_t primaryPlayerIndex_ = 0;
    std::size_t firstVisiblePlayer_ = 0;
    std::optional<std::size_t> openModulationPlayer_;
    juce::Component* modulationEditorAnchor_ = nullptr;
    std::optional<std::size_t> selectedScheduleBar_;
    PopupKind activePopupKind_ = PopupKind::none;
    juce::Component* activePopupTarget_ = nullptr;
    std::uint64_t popupGeneration_ = 0;

    enum class DraggedRangeHandle { none, start, end };
    DraggedRangeHandle draggedRangeHandle_ = DraggedRangeHandle::none;
    std::size_t draggedPlayerIndex_ = 0;
    int draggedRangeHandleOffsetX_ = 0;
    juce::Point<int> rangeHandleDragStart_;
    bool rangeHandleWasDragged_ = false;

    [[nodiscard]] juce::Rectangle<int> cellsAreaForPlayer(std::size_t playerIndex) const;
    [[nodiscard]] juce::Rectangle<int> modulationPreviewAreaForPlayer(
        std::size_t playerIndex) const;
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
