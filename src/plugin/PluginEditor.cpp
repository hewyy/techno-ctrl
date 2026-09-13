#include "plugin/PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
constexpr auto uiYellow = 0xffe3c51a;
constexpr auto uiBlack = 0xff000000;
constexpr auto uiRed = 0xffc9574f;
constexpr auto uiBlue = 0xff78a5b4;
constexpr auto warmIvory = 0xffd5d2b8;
constexpr auto lightGray = 0xffb8b7ac;
constexpr auto darkGray = 0xff505255;

constexpr auto background = darkGray;
constexpr auto panel = 0xff444648;
constexpr auto border = uiBlack;
constexpr auto amber = uiBlack;
constexpr auto pastelHit = warmIvory;
constexpr auto pastelPlayhead = uiBlue;
constexpr auto inactive = 0xff46484a;
constexpr auto primaryText = warmIvory;
constexpr auto secondaryText = lightGray;
constexpr auto muteRed = uiRed;
constexpr auto muteRedDark = darkGray;
constexpr auto patternStepOff = lightGray;
constexpr auto patternStepOffAlternate = 0xff92928a;
constexpr auto patternStepOn = 0xff747672;
constexpr auto patternStepTextOff = darkGray;
constexpr auto patternStepTextOn = uiYellow;
constexpr auto patternPlayhead = uiBlue;

constexpr int outerPadding = 2;
constexpr int headerHeight = 0;
constexpr int sectionGap = 2;
constexpr int contentHorizontalPadding = 2;
constexpr int playerPanelHorizontalPadding = 4;
constexpr int playerControlsWidth = 279;
constexpr int controlsToGridGap = 30;
constexpr int clickTargetSize = 41;
constexpr int modulationPanelHeight = clickTargetSize + 2;
constexpr int playerBottomPadding = 2;
constexpr int velocityControlGap = 4;
constexpr int matrixGridLeft = 82;
constexpr int matrixGridTop = 112;
constexpr int matrixCellWidth = clickTargetSize + 4;
constexpr int matrixCellHeight = clickTargetSize + 4;
constexpr int rangeHandleExtension = 0;
constexpr int rangeHandleCapLength = 18;
constexpr int rangeHandleCapGrabRadius = clickTargetSize / 2 + 6;
constexpr int rangeHandleCapVerticalGrabRadius = 10;
constexpr int rangeHandleStrokeGrabRadius = clickTargetSize / 2 + 6;
constexpr int rangeHandleDragThreshold = 5;
constexpr float rangeHandleStrokeWidth = 5.0f;
constexpr int patternCellGap = 1;
constexpr int minimumPatternCellSize = 8;
constexpr int patternGridLeft = contentHorizontalPadding
    + playerPanelHorizontalPadding + playerControlsWidth + controlsToGridGap;
constexpr int velocityKnobWidth = clickTargetSize;
constexpr int velocityKnobHeight = clickTargetSize;
constexpr int velocityKnobGap = 2;
constexpr int velocityGridLeft = patternGridLeft;

[[nodiscard]] int patternCellSizeForContentWidth(int contentWidth) noexcept
{
    constexpr int stepCount = static_cast<int>(lps::Pattern::maxLength);
    constexpr int rightPadding = contentHorizontalPadding
        + playerPanelHorizontalPadding;
    const int availableGridWidth = contentWidth - patternGridLeft - rightPadding;
    return std::max(
        minimumPatternCellSize,
        (availableGridWidth - (stepCount - 1) * patternCellGap) / stepCount);
}

[[nodiscard]] juce::String patternMenuLabel(
    std::size_t playerIndex, bool modified)
{
    return juce::String(static_cast<int>(playerIndex + 1))
        + (modified ? " *" : "");
}

[[nodiscard]] juce::String velocityMenuLabel(
    std::size_t playerIndex, bool modified)
{
    return juce::String(static_cast<int>(playerIndex + 1))
        + (modified ? " *" : "");
}

class LaneMenuButton final : public juce::TextButton
{
public:
    enum class Kind { pattern, velocity };

    explicit LaneMenuButton(Kind kind) : kind_(kind) {}

    void paintButton(
        juce::Graphics& graphics,
        bool isMouseOverButton,
        bool isButtonDown) override
    {
        auto face = juce::Colour(panel);
        if (isButtonDown)
            face = face.darker(0.12f);
        else if (isMouseOverButton)
            face = face.brighter(0.08f);

        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        graphics.setColour(face);
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(juce::Colour(border));
        graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

        if (kind_ == Kind::pattern)
        {
            constexpr int previewSteps = 6;
            constexpr float cellSize = 5.0f;
            constexpr float gap = 2.0f;
            const float startX = 8.0f;
            const float centreY = static_cast<float>(getHeight()) * 0.5f;
            for (int step = 0; step < previewSteps; ++step)
            {
                const bool hit = step == 0 || step == 3;
                graphics.setColour(juce::Colour(
                    hit ? uiYellow : lightGray));
                graphics.fillRect(
                    startX + static_cast<float>(step) * (cellSize + gap),
                    centreY - cellSize * 0.5f,
                    cellSize,
                    cellSize);
            }
        }
        else
        {
            const float left = 8.0f;
            const float right = 45.0f;
            const float centreY = static_cast<float>(getHeight()) * 0.62f;
            juce::Path wave;
            wave.startNewSubPath(left, centreY);
            wave.cubicTo(
                left + 9.0f, centreY - 18.0f,
                right - 9.0f, centreY - 18.0f,
                right, centreY);
            graphics.setColour(juce::Colour(warmIvory));
            graphics.strokePath(wave, juce::PathStrokeType(2.2f));
            graphics.fillEllipse(left - 3.0f, centreY - 3.0f, 6.0f, 6.0f);
            graphics.fillEllipse(right - 3.0f, centreY - 3.0f, 6.0f, 6.0f);
        }

        graphics.setColour(juce::Colour(primaryText));
        graphics.setFont(juce::Font(
            juce::FontOptions(13.0f, juce::Font::bold)));
        graphics.drawText(
            getButtonText(),
            53,
            0,
            getWidth() - 58,
            getHeight(),
            juce::Justification::centred);
    }

private:
    Kind kind_;
};

class PatternPreviewMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    PatternPreviewMenuItem(lps::Pattern pattern, bool selected)
        : juce::PopupMenu::CustomComponent(true),
          pattern_(pattern),
          selected_(selected)
    {
    }

    void getIdealSize(int& idealWidth, int& idealHeight) override
    {
        idealWidth = 310;
        idealHeight = 38;
    }

    void paint(juce::Graphics& graphics) override
    {
        if (isItemHighlighted())
            graphics.fillAll(juce::Colour(uiBlue).withAlpha(0.3f));

        const int stepCount = juce::jlimit(
            1, static_cast<int>(lps::Pattern::maxLength),
            static_cast<int>(pattern_.length));
        constexpr int gap = 2;
        const int availableWidth = getWidth() - 20;
        const int cellSize = juce::jlimit(
            6, 20, (availableWidth - gap * (stepCount - 1)) / stepCount);
        const int previewWidth = stepCount * cellSize + (stepCount - 1) * gap;
        int x = 10;
        const int y = (getHeight() - cellSize) / 2;

        if (selected_)
        {
            graphics.setColour(juce::Colour(uiBlue));
            graphics.fillRect(2, 4, 4, getHeight() - 8);
        }

        for (int step = 0; step < stepCount; ++step)
        {
            const bool hit = pattern_.hits[static_cast<std::size_t>(step)];
            const auto restColour = (step / 4) % 2 == 0
                ? juce::Colour(lightGray)
                : juce::Colour(patternStepOffAlternate);
            graphics.setColour(hit ? juce::Colour(uiYellow) : restColour);
            graphics.fillRect(x, y, cellSize, cellSize);
            graphics.setColour(juce::Colour(uiBlack));
            graphics.drawRect(x, y, cellSize, cellSize, 1);
            x += cellSize + gap;
        }

        if (previewWidth < availableWidth)
        {
            graphics.setColour(juce::Colour(uiBlack));
            graphics.drawVerticalLine(
                10 + previewWidth,
                static_cast<float>(y),
                static_cast<float>(y + cellSize));
        }
    }

private:
    lps::Pattern pattern_;
    bool selected_ = false;
};

class MuteButton final : public juce::TextButton
{
public:
    void paintButton(
        juce::Graphics& graphics,
        bool isMouseOverButton,
        bool isButtonDown) override
    {
        const bool engaged = getToggleState() || isButtonDown;
        auto face = juce::Colour(engaged ? muteRed : inactive);
        if (isMouseOverButton && !engaged)
            face = face.brighter(0.08f);

        const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        graphics.setColour(face);
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(juce::Colour(
            engaged ? muteRedDark : muteRed));
        graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

        graphics.setFont(juce::Font(
            juce::FontOptions(20.0f, juce::Font::bold)));
        graphics.drawText(
            "M",
            getLocalBounds(),
            juce::Justification::centred,
            false);
    }
};

class VelocitySliderPopup final : public juce::Component
{
public:
    explicit VelocitySliderPopup(juce::Slider& target)
    {
        setInterceptsMouseClicks(false, false);
        slider_.setSliderStyle(juce::Slider::LinearVertical);
        slider_.setInterceptsMouseClicks(false, false);
        slider_.setRange(
            target.getMinimum(), target.getMaximum(), target.getInterval());
        slider_.setNumDecimalPlacesToDisplay(0);
        slider_.setTextBoxStyle(
            juce::Slider::TextBoxBelow, false, 56, 20);
        slider_.setScrollWheelEnabled(false);
        slider_.setColour(
            juce::Slider::trackColourId, juce::Colour(pastelHit));
        slider_.setColour(
            juce::Slider::thumbColourId, juce::Colour(primaryText));
        slider_.setColour(
            juce::Slider::backgroundColourId, juce::Colour(inactive));
        slider_.setColour(
            juce::Slider::textBoxTextColourId, juce::Colour(primaryText));
        slider_.setColour(
            juce::Slider::textBoxBackgroundColourId,
            juce::Colours::transparentBlack);
        slider_.setColour(
            juce::Slider::textBoxOutlineColourId,
            juce::Colours::transparentBlack);
        slider_.getValueObject().referTo(target.getValueObject());
        addAndMakeVisible(slider_);
        setSize(76, 220);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(panel));
        graphics.setColour(juce::Colour(border));
        graphics.drawRect(getLocalBounds(), 1);
    }

    void resized() override
    {
        slider_.setBounds(getLocalBounds().reduced(8));
    }

private:
    juce::Slider slider_;
};
}

LivePatternSequencerEditor::ContentComponent::ContentComponent(
    LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setOpaque(true);
}

void LivePatternSequencerEditor::ContentComponent::paint(juce::Graphics& graphics)
{
    editor_.paintContent(graphics);
}

void LivePatternSequencerEditor::ContentComponent::resized()
{
    editor_.resizedContent();
}

void LivePatternSequencerEditor::ContentComponent::mouseDown(const juce::MouseEvent& event)
{
    editor_.contentMouseDown(event);
}

void LivePatternSequencerEditor::ContentComponent::mouseDrag(const juce::MouseEvent& event)
{
    editor_.contentMouseDrag(event);
}

void LivePatternSequencerEditor::ContentComponent::mouseUp(const juce::MouseEvent& event)
{
    editor_.contentMouseUp(event);
}

LivePatternSequencerEditor::MatrixComponent::MatrixComponent(
    LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setOpaque(true);
}

void LivePatternSequencerEditor::MatrixComponent::paint(juce::Graphics& graphics)
{
    editor_.paintMatrix(graphics);
}

void LivePatternSequencerEditor::MatrixComponent::resized()
{
    editor_.resizedMatrix();
}

void LivePatternSequencerEditor::MatrixComponent::mouseDown(
    const juce::MouseEvent& event)
{
    editor_.matrixMouseDown(event);
}

LivePatternSequencerEditor::VelocityCell::VelocityCell()
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setRange(0.0, 255.0, 1.0);
    setNumDecimalPlacesToDisplay(0);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setScrollWheelEnabled(false);
    setMouseDragSensitivity(180);
    setWantsKeyboardFocus(true);
}

void LivePatternSequencerEditor::VelocityCell::setActive(bool shouldBeActive)
{
    if (active_ == shouldBeActive)
        return;
    active_ = shouldBeActive;
    repaint();
}

void LivePatternSequencerEditor::VelocityCell::paint(
    juce::Graphics& graphics)
{
    auto square = getLocalBounds().reduced(1);
    graphics.setColour(juce::Colour(inactive));
    graphics.fillRect(square);

    const auto valueRange = getMaximum() - getMinimum();
    const auto proportion = valueRange > 0.0
        ? juce::jlimit(0.0, 1.0, (getValue() - getMinimum()) / valueRange)
        : 0.0;
    const int fillHeight = juce::roundToInt(
        static_cast<double>(square.getHeight()) * proportion);
    graphics.setColour(juce::Colour(active_ ? pastelPlayhead : pastelHit));
    graphics.fillRect(square.removeFromBottom(fillHeight));

    graphics.setColour(active_
        ? juce::Colour(pastelPlayhead).brighter(0.1f)
        : juce::Colour(border).brighter(0.22f));
    graphics.drawRect(getLocalBounds().reduced(1), 1);
    graphics.setColour(juce::Colour(
        proportion >= 0.5 ? background : primaryText));
    graphics.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    graphics.drawText(
        juce::String(juce::roundToInt(getValue())),
        getLocalBounds(),
        juce::Justification::centred);
}

void LivePatternSequencerEditor::VelocityCell::mouseDown(
    const juce::MouseEvent& event)
{
    auto* editor = findParentComponentOfClass<LivePatternSequencerEditor>();
    if (editor == nullptr)
        return;

    activePopup_.reset();

    juce::Slider::mouseDown(event);
    auto popup = std::make_unique<VelocitySliderPopup>(*this);
    const auto anchor = editor->getLocalArea(this, getLocalBounds());
    const int popupWidth = popup->getWidth();
    const int popupHeight = popup->getHeight();
    int popupY = anchor.getY() - popupHeight - 8;
    if (popupY < 0)
        popupY = anchor.getBottom() + 8;
    const int popupX = juce::jlimit(
        0,
        std::max(0, editor->getWidth() - popupWidth),
        anchor.getCentreX() - popupWidth / 2);
    popupY = juce::jlimit(
        0,
        std::max(0, editor->getHeight() - popupHeight),
        popupY);
    editor->addAndMakeVisible(*popup);
    popup->setTopLeftPosition(popupX, popupY);
    popup->toFront(false);
    activePopup_ = std::move(popup);
}

void LivePatternSequencerEditor::VelocityCell::mouseDrag(
    const juce::MouseEvent& event)
{
    juce::Slider::mouseDrag(event);
}

void LivePatternSequencerEditor::VelocityCell::mouseUp(
    const juce::MouseEvent& event)
{
    juce::Slider::mouseUp(event);
    activePopup_.reset();
}

LivePatternSequencerEditor::LivePatternSequencerEditor(
    LivePatternSequencerProcessor& processorToEdit)
    : juce::AudioProcessorEditor(processorToEdit),
      processor_(processorToEdit),
      playerCount_(processorToEdit.playerCountForUi()),
      content_(*this),
      matrix_(*this)
{
    lookAndFeel_.setColour(
        juce::TextButton::buttonColourId, juce::Colour(panel));
    lookAndFeel_.setColour(
        juce::TextButton::buttonOnColourId, juce::Colour(uiYellow));
    lookAndFeel_.setColour(
        juce::TextButton::textColourOffId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::TextButton::textColourOnId, juce::Colour(background));
    lookAndFeel_.setColour(
        juce::ComboBox::backgroundColourId, juce::Colour(panel));
    lookAndFeel_.setColour(
        juce::ComboBox::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::ComboBox::outlineColourId, juce::Colour(border));
    lookAndFeel_.setColour(
        juce::ComboBox::arrowColourId, juce::Colour(uiYellow));
    lookAndFeel_.setColour(
        juce::PopupMenu::backgroundColourId, juce::Colour(panel));
    lookAndFeel_.setColour(
        juce::PopupMenu::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::PopupMenu::highlightedBackgroundColourId,
        juce::Colour(uiBlue));
    lookAndFeel_.setColour(
        juce::PopupMenu::highlightedTextColourId, juce::Colour(background));
    lookAndFeel_.setColour(
        juce::PopupMenu::headerTextColourId, juce::Colour(warmIvory));
    lookAndFeel_.setColour(
        juce::Slider::thumbColourId, juce::Colour(uiYellow));
    lookAndFeel_.setColour(
        juce::Slider::trackColourId, juce::Colour(warmIvory));
    lookAndFeel_.setColour(
        juce::Slider::backgroundColourId, juce::Colour(inactive));
    lookAndFeel_.setColour(
        juce::ToggleButton::tickColourId, juce::Colour(uiYellow));
    lookAndFeel_.setColour(
        juce::ToggleButton::tickDisabledColourId, juce::Colour(border));
    lookAndFeel_.setColour(
        juce::ToggleButton::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::ScrollBar::thumbColourId, juce::Colour(uiBlue));
    lookAndFeel_.setColour(
        juce::ScrollBar::trackColourId, juce::Colour(background));
    lookAndFeel_.setColour(
        juce::AlertWindow::backgroundColourId, juce::Colour(panel));
    lookAndFeel_.setColour(
        juce::AlertWindow::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::AlertWindow::outlineColourId, juce::Colour(uiYellow));
    lookAndFeel_.setColour(
        juce::Label::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::TextEditor::backgroundColourId, juce::Colour(background));
    lookAndFeel_.setColour(
        juce::TextEditor::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::TextEditor::outlineColourId, juce::Colour(border));
    lookAndFeel_.setColour(
        juce::TextEditor::focusedOutlineColourId, juce::Colour(uiBlue));
    lookAndFeel_.setColour(
        juce::TextEditor::highlightColourId, juce::Colour(uiBlue));
    lookAndFeel_.setColour(
        juce::TextEditor::highlightedTextColourId, juce::Colour(background));
    lookAndFeel_.setColour(
        juce::TooltipWindow::backgroundColourId, juce::Colour(panel));
    lookAndFeel_.setColour(
        juce::TooltipWindow::textColourId, juce::Colour(primaryText));
    lookAndFeel_.setColour(
        juce::TooltipWindow::outlineColourId, juce::Colour(uiYellow));
    setLookAndFeel(&lookAndFeel_);

    patternMenuButtons_.reserve(playerCount_);
    velocityMenuButtons_.reserve(playerCount_);
    patternSelectors_.reserve(playerCount_);
    savePatternButtons_.reserve(playerCount_);
    resetToMasterButtons_.reserve(playerCount_);
    speedSelectors_.reserve(playerCount_);
    offsetLeftButtons_.reserve(playerCount_);
    offsetRightButtons_.reserve(playerCount_);
    muteButtons_.reserve(playerCount_);
    velocityModulationSelectors_.reserve(playerCount_);
    saveVelocityModulationButtons_.reserve(playerCount_);
    decreaseVelocityModulationLengthButtons_.reserve(playerCount_);
    increaseVelocityModulationLengthButtons_.reserve(playerCount_);
    velocityModulationSliders_.reserve(
        playerCount_ * lps::Modulation::maxLength);

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        const bool supportsPattern =
            processor_.playerSupportsPatternEditingForUi(playerIndex);
        const bool supportsVelocity =
            processor_.playerSupportsVelocityEditingForUi(playerIndex);

        patternMenuButtons_.push_back(std::make_unique<LaneMenuButton>(
            LaneMenuButton::Kind::pattern));
        auto& patternMenuButton = *patternMenuButtons_.back();
        patternMenuButton.setButtonText(patternMenuLabel(playerIndex, false));
        patternMenuButton.setTooltip("Choose a pattern, playback speed, or save");
        patternMenuButton.onClick = [this, playerIndex]
        {
            showPatternMenu(playerIndex);
        };
        content_.addAndMakeVisible(patternMenuButton);

        velocityMenuButtons_.push_back(std::make_unique<LaneMenuButton>(
            LaneMenuButton::Kind::velocity));
        auto& velocityMenuButton = *velocityMenuButtons_.back();
        velocityMenuButton.setButtonText(velocityMenuLabel(playerIndex, false));
        velocityMenuButton.setTooltip("Choose or save a velocity modulation");
        velocityMenuButton.onClick = [this, playerIndex]
        {
            showVelocityMenu(playerIndex);
        };
        content_.addAndMakeVisible(velocityMenuButton);

        patternSelectors_.push_back(std::make_unique<juce::ComboBox>());
        auto& selector = *patternSelectors_.back();
        for (std::size_t patternIndex = 0;
             patternIndex < processor_.patternCountForUi();
             ++patternIndex)
        {
            selector.addItem(
                processor_.patternNameForUi(patternIndex),
                static_cast<int>(patternIndex + 1));
        }

        selector.setSelectedItemIndex(
            static_cast<int>(processor_.selectedPatternForPlayer(playerIndex)),
            juce::dontSendNotification);
        selector.onChange = [this, playerIndex]
        {
            const auto selected = patternSelectors_[playerIndex]->getSelectedItemIndex();
            if (selected >= 0)
                processor_.selectPatternForPlayer(playerIndex, static_cast<std::size_t>(selected));
        };

        savePatternButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& saveButton = *savePatternButtons_.back();
        saveButton.setButtonText("Save");
        saveButton.setTooltip(
            "Save the active loop to the persistent pattern library");
        saveButton.onClick = [this, playerIndex]
        {
            beginSavePlayerPattern(playerIndex);
        };

        resetToMasterButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& resetButton = *resetToMasterButtons_.back();
        const bool isMaster = processor_.playerIsMasterForUi(playerIndex);
        resetButton.setButtonText(isMaster ? "MASTER" : "Reset");
        resetButton.setEnabled(!isMaster
            && processor_.playerCanResetToMasterForUi(playerIndex));
        if (isMaster)
        {
            resetButton.setTooltip(
                processor_.playerNameForUi(playerIndex)
                + " is the master voice");
        }
        else
        {
            const auto masterIndex = processor_.masterPlayerIndexForUi();
            const auto masterName = masterIndex
                ? processor_.playerNameForUi(*masterIndex)
                : juce::String("the master voice");
            resetButton.setTooltip(
                "At " + masterName + "'s next loop start, restart this sequence "
                "at its active loop start");
            resetButton.onClick = [this, playerIndex]
            {
                (void) processor_.resetPlayerToMaster(playerIndex);
                updateResetToMasterIndicators(true);
            };
        }
        content_.addAndMakeVisible(resetButton);

        speedSelectors_.push_back(std::make_unique<juce::ComboBox>());
        auto& speedSelector = *speedSelectors_.back();
        speedSelector.addItem("0.5x", 1);
        speedSelector.addItem("1x", 2);
        speedSelector.addItem("2x", 3);
        speedSelector.setSelectedItemIndex(
            static_cast<int>(processor_.playerPlaybackSpeed(playerIndex)),
            juce::dontSendNotification);
        speedSelector.onChange = [this, playerIndex]
        {
            const auto selected = speedSelectors_[playerIndex]->getSelectedItemIndex();
            if (selected >= 0)
                processor_.setPlayerPlaybackSpeed(playerIndex, static_cast<std::size_t>(selected));
        };

        offsetLeftButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& offsetLeftButton = *offsetLeftButtons_.back();
        offsetLeftButton.setButtonText("<");
        offsetLeftButton.setTooltip("Rotate the pattern left by one step");
        offsetLeftButton.onClick = [this, playerIndex]
        {
            processor_.offsetPlayerPatternLeft(playerIndex);
        };
        content_.addAndMakeVisible(offsetLeftButton);

        offsetRightButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& offsetRightButton = *offsetRightButtons_.back();
        offsetRightButton.setButtonText(">");
        offsetRightButton.setTooltip("Rotate the pattern right by one step");
        offsetRightButton.onClick = [this, playerIndex]
        {
            processor_.offsetPlayerPatternRight(playerIndex);
        };
        content_.addAndMakeVisible(offsetRightButton);

        muteButtons_.push_back(std::make_unique<MuteButton>());
        auto& muteButton = *muteButtons_.back();
        muteButton.setButtonText("M");
        muteButton.setClickingTogglesState(true);
        muteButton.setToggleState(
            processor_.playerMutedForUi(playerIndex),
            juce::dontSendNotification);
        muteButton.setTooltip(
            "Mute " + processor_.playerNameForUi(playerIndex)
            + " note output while its sequence keeps running");
        muteButton.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour(muteRed));
        muteButton.setColour(
            juce::TextButton::textColourOnId,
            juce::Colour(muteRedDark));
        muteButton.onClick = [this, playerIndex]
        {
            processor_.setPlayerMuted(
                playerIndex,
                muteButtons_[playerIndex]->getToggleState());
        };
        content_.addAndMakeVisible(muteButton);

        velocityModulationSelectors_.push_back(
            std::make_unique<juce::ComboBox>());
        auto& velocitySelector = *velocityModulationSelectors_.back();
        for (std::size_t modulationIndex = 0;
             modulationIndex < processor_.velocityModulationCountForUi();
             ++modulationIndex)
        {
            velocitySelector.addItem(
                processor_.velocityModulationNameForUi(modulationIndex),
                static_cast<int>(modulationIndex + 1));
        }
        velocitySelector.setSelectedItemIndex(
            static_cast<int>(
                processor_.selectedModulationForPlayer(playerIndex)),
            juce::dontSendNotification);
        velocitySelector.setTooltip(
            "Choose a velocity modulation independently of the hit pattern");
        velocitySelector.onChange = [this, playerIndex]
        {
            const auto selected =
                velocityModulationSelectors_[playerIndex]
                    ->getSelectedItemIndex();
            if (selected >= 0)
            {
                processor_.selectModulationForPlayer(
                    playerIndex, static_cast<std::size_t>(selected));
            }
        };

        saveVelocityModulationButtons_.push_back(
            std::make_unique<juce::TextButton>());
        auto& velocitySaveButton = *saveVelocityModulationButtons_.back();
        velocitySaveButton.setButtonText("Save");
        velocitySaveButton.setTooltip(
            "Save these values to the persistent velocity modulation library");
        velocitySaveButton.onClick = [this, playerIndex]
        {
            beginSavePlayerVelocityModulation(playerIndex);
        };

        decreaseVelocityModulationLengthButtons_.push_back(
            std::make_unique<juce::TextButton>());
        auto& decreaseLengthButton =
            *decreaseVelocityModulationLengthButtons_.back();
        decreaseLengthButton.setButtonText("-");
        decreaseLengthButton.setTooltip(
            "Remove the last velocity modulation step");
        decreaseLengthButton.onClick = [this, playerIndex]
        {
            const auto modulation =
                processor_.velocityModulationForUi(playerIndex);
            if (modulation.length > 1)
            {
                processor_.setPlayerModulationLength(
                    playerIndex, modulation.length - 1);
                refreshVelocityModulationControls(playerIndex, true);
                updateVelocityModulationModifiedIndicators(true);
                content_.repaint();
            }
        };
        content_.addAndMakeVisible(decreaseLengthButton);

        increaseVelocityModulationLengthButtons_.push_back(
            std::make_unique<juce::TextButton>());
        auto& increaseLengthButton =
            *increaseVelocityModulationLengthButtons_.back();
        increaseLengthButton.setButtonText("+");
        increaseLengthButton.setTooltip(
            "Append a velocity modulation step");
        increaseLengthButton.onClick = [this, playerIndex]
        {
            const auto modulation =
                processor_.velocityModulationForUi(playerIndex);
            if (modulation.length < lps::Modulation::maxLength)
            {
                const auto newLength = std::max<std::size_t>(
                    1, modulation.length + 1);
                processor_.setPlayerModulationLength(
                    playerIndex, newLength);
                refreshVelocityModulationControls(playerIndex, true);
                updateVelocityModulationModifiedIndicators(true);
                content_.repaint();
            }
        };
        content_.addAndMakeVisible(increaseLengthButton);

        const auto modulation =
            processor_.velocityModulationForUi(playerIndex);
        for (std::size_t step = 0;
             step < lps::Modulation::maxLength;
             ++step)
        {
            velocityModulationSliders_.push_back(
                std::make_unique<VelocityCell>());
            const auto sliderIndex = velocityModulationSliders_.size() - 1;
            auto& slider = *velocityModulationSliders_.back();
            slider.setName(
                "Velocity modulation step "
                + juce::String(static_cast<int>(step + 1)));
            slider.setTooltip(
                "Velocity step " + juce::String(static_cast<int>(step + 1))
                + " (0-255)");
            slider.setDoubleClickReturnValue(true, 255.0);
            slider.setValue(
                static_cast<double>(modulation.values[step].raw / 257u),
                juce::dontSendNotification);
            slider.onValueChange =
                [this, playerIndex, step, sliderIndex]
                {
                    const auto value = juce::jlimit(
                        0,
                        255,
                        juce::roundToInt(
                            velocityModulationSliders_[sliderIndex]
                                ->getValue()));
                    processor_.setPlayerModulationValue(
                        playerIndex,
                        step,
                        static_cast<std::uint8_t>(value));
                    updateVelocityModulationModifiedIndicators(true);
                    content_.repaint();
                };
            content_.addAndMakeVisible(slider);
            slider.setVisible(supportsVelocity && step < modulation.length);
        }

        patternMenuButton.setVisible(supportsPattern);
        offsetLeftButton.setVisible(supportsPattern);
        offsetRightButton.setVisible(supportsPattern);
        velocityMenuButton.setVisible(supportsVelocity);
        decreaseLengthButton.setVisible(supportsVelocity);
        increaseLengthButton.setVisible(supportsVelocity);
    }

    displayedPatternModified_.assign(playerCount_, false);
    displayedVelocityModulationModified_.assign(playerCount_, false);
    displayedResetToMasterPending_.assign(playerCount_, false);
    updatePatternModifiedIndicators(true);
    updateVelocityModulationModifiedIndicators(true);
    updateResetToMasterIndicators(true);

    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, true);
    viewport_.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport_);

    suppressionButton_.setButtonText("SUPPRESSION...");
    suppressionButton_.setTooltip("Open or close the suppression matrix");
    suppressionButton_.onClick = [this] { showSuppressionMatrix(); };
    addAndMakeVisible(suppressionButton_);

    suppressionCloseButton_.setButtonText("CLOSE");
    suppressionCloseButton_.setTooltip("Close the suppression matrix");
    suppressionCloseButton_.setColour(
        juce::TextButton::buttonColourId, juce::Colour(uiRed));
    suppressionCloseButton_.setColour(
        juce::TextButton::textColourOffId, juce::Colour(darkGray));
    suppressionCloseButton_.onClick = [this] { showSuppressionMatrix(); };
    matrix_.addAndMakeVisible(suppressionCloseButton_);

    content_.setSize(requiredContentWidth(), 1);

    constexpr int initialEditorHeight = 720;
    constexpr int editorWidth = 1466;
    setResizable(true, false);
    setResizeLimits(800, 400, 4096, 2160);
    setSize(editorWidth, initialEditorHeight);
    startTimerHz(30);

    const auto catalogError = processor_.patternCatalogErrorForUi();
    if (catalogError.isNotEmpty())
        showPatternLibraryWarning(catalogError);

    const auto velocityCatalogError =
        processor_.velocityModulationCatalogErrorForUi();
    if (velocityCatalogError.isNotEmpty())
        showVelocityModulationLibraryWarning(velocityCatalogError);
}

LivePatternSequencerEditor::~LivePatternSequencerEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    viewport_.setViewedComponent(nullptr, false);
    if (suppressionWindow_ != nullptr)
    {
        suppressionWindow_->clearContentComponent();
        suppressionWindow_->exitModalState(0);
    }
}

void LivePatternSequencerEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
}

void LivePatternSequencerEditor::resized()
{
    auto bounds = getLocalBounds().reduced(outerPadding);
    bounds.removeFromTop(headerHeight + sectionGap);

    auto footer = bounds.removeFromBottom(clickTargetSize);
    bounds.removeFromBottom(sectionGap);
    viewport_.setBounds(bounds);
    suppressionButton_.setBounds(footer.removeFromRight(160));

    updateContentSize();
}

void LivePatternSequencerEditor::resizedContent()
{
    constexpr int controlsX = contentHorizontalPadding + playerPanelHorizontalPadding;
    constexpr int menuWidth = 86;
    constexpr int resetWidth = 52;
    constexpr int resetX = controlsX + menuWidth + velocityControlGap;
    constexpr int offsetLeftX = resetX + resetWidth + velocityControlGap;
    constexpr int offsetRightX = offsetLeftX + clickTargetSize
        + velocityControlGap;
    constexpr int muteX = offsetRightX + clickTargetSize
        + velocityControlGap;
    constexpr int decreaseX = controlsX + menuWidth + velocityControlGap;
    constexpr int increaseX = decreaseX + clickTargetSize
        + velocityControlGap;

    for (std::size_t index = 0; index < patternMenuButtons_.size(); ++index)
    {
        const int y = static_cast<int>(index) * playerStride();
        patternMenuButtons_[index]->setBounds(
            controlsX, y + 1, menuWidth, clickTargetSize);
        resetToMasterButtons_[index]->setBounds(
            resetX, y + 1, resetWidth, clickTargetSize);
        offsetLeftButtons_[index]->setBounds(
            offsetLeftX, y + 1, clickTargetSize, clickTargetSize);
        offsetRightButtons_[index]->setBounds(
            offsetRightX, y + 1, clickTargetSize, clickTargetSize);
        muteButtons_[index]->setBounds(
            muteX, y + 1, 46, clickTargetSize);

        const int modulationY = y + patternPanelHeight() + 1;
        velocityMenuButtons_[index]->setBounds(
            controlsX, modulationY, menuWidth, clickTargetSize);
        decreaseVelocityModulationLengthButtons_[index]->setBounds(
            decreaseX, modulationY, clickTargetSize, clickTargetSize);
        increaseVelocityModulationLengthButtons_[index]->setBounds(
            increaseX, modulationY, clickTargetSize, clickTargetSize);

        for (std::size_t step = 0;
             step < lps::Modulation::maxLength;
             ++step)
        {
            const auto sliderIndex =
                index * lps::Modulation::maxLength + step;
            velocityModulationSliders_[sliderIndex]->setBounds(
                velocityGridLeft
                    + static_cast<int>(step)
                        * (velocityKnobWidth + velocityKnobGap),
                modulationY,
                velocityKnobWidth,
                velocityKnobHeight);
        }
    }
}

void LivePatternSequencerEditor::resizedMatrix()
{
    suppressionCloseButton_.setBounds(matrix_.getWidth() - 92, 10, 78, 32);
}

void LivePatternSequencerEditor::matrixMouseDown(
    const juce::MouseEvent& event)
{
    const int relativeX = event.x - matrixGridLeft;
    const int relativeY = event.y - matrixGridTop;
    if (relativeX < 0 || relativeY < 0)
        return;

    const auto column = static_cast<std::size_t>(relativeX / matrixCellWidth);
    const auto row = static_cast<std::size_t>(relativeY / matrixCellHeight);
    if (row >= playerCount_ || column >= playerCount_ || row == column)
        return;

    processor_.setSuppression(
        row, column, !processor_.suppression(row, column));
    matrix_.repaint();
}

void LivePatternSequencerEditor::timerCallback()
{
    updateContentSize();
    updatePatternModifiedIndicators();
    for (std::size_t playerIndex = 0;
         playerIndex < playerCount_;
         ++playerIndex)
    {
        refreshVelocityModulationControls(playerIndex);
    }
    updateVelocityModulationModifiedIndicators();
    updateResetToMasterIndicators();
    content_.repaint();
}

void LivePatternSequencerEditor::beginSavePlayerPattern(std::size_t playerIndex)
{
    handleSaveResult(playerIndex, processor_.savePlayerPattern(playerIndex));
}

void LivePatternSequencerEditor::promptForPatternName(
    std::size_t playerIndex,
    lps::Pattern candidatePattern)
{
    constexpr auto nameEditorId = "patternName";
    auto* alert = new juce::AlertWindow(
        "Save Pattern",
        "This loop is not in the library yet. Enter a name for the new pattern.",
        juce::MessageBoxIconType::QuestionIcon,
        this);
    alert->addTextEditor(nameEditorId, {}, "Name:");
    alert->addButton(
        "Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(
        "Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const juce::Component::SafePointer<juce::Button> safeSaveButton(
        alert->getButton("Save"));
    if (safeSaveButton != nullptr)
        safeSaveButton->setEnabled(false);

    // Keep the editor contents independently so the callback does not depend
    // on the AlertWindow object's lifetime.
    auto enteredName = std::make_shared<juce::String>();
    if (auto* textEditor = alert->getTextEditor(nameEditorId))
    {
        const juce::Component::SafePointer<juce::TextEditor> safeTextEditor(textEditor);
        textEditor->onTextChange = [safeTextEditor, safeSaveButton, enteredName]
        {
            if (safeTextEditor != nullptr)
                *enteredName = safeTextEditor->getText();
            if (safeSaveButton != nullptr)
                safeSaveButton->setEnabled(!enteredName->trim().isEmpty());
        };
    }

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, enteredName, playerIndex, candidatePattern](int result)
            {
                if (result != 1 || safeThis == nullptr)
                    return;

                const auto name = enteredName->trim();
                if (name.isEmpty())
                {
                    safeThis->showSaveWarning(
                        "Enter a name before saving the new pattern.");
                    return;
                }

                const auto saveResult = safeThis->processor_.savePlayerPattern(
                    playerIndex, candidatePattern, name);
                if (saveResult.status
                    == LivePatternSequencerProcessor::SavePatternStatus::needsName)
                {
                    safeThis->showSaveWarning(
                        "Enter a name before saving the new pattern.");
                    return;
                }

                safeThis->handleSaveResult(playerIndex, saveResult);
            }),
        true);
}

void LivePatternSequencerEditor::handleSaveResult(
    std::size_t playerIndex,
    LivePatternSequencerProcessor::SavePatternResult result)
{
    using Status = LivePatternSequencerProcessor::SavePatternStatus;
    switch (result.status)
    {
        case Status::needsName:
            promptForPatternName(playerIndex, result.candidatePattern);
            return;

        case Status::selectedExisting:
        case Status::savedNew:
            refreshPatternSelectors();
            updatePatternModifiedIndicators(true);
            content_.repaint();
            return;

        case Status::failed:
        {
            const auto catalogError = processor_.patternCatalogErrorForUi();
            showSaveWarning(catalogError.isNotEmpty()
                ? "The pattern could not be saved.\n\n" + catalogError
                : juce::String("The pattern could not be saved."));
            return;
        }
    }
}

void LivePatternSequencerEditor::beginSavePlayerVelocityModulation(
    std::size_t playerIndex)
{
    handleVelocityModulationSaveResult(
        playerIndex,
        processor_.savePlayerModulation(playerIndex));
}

void LivePatternSequencerEditor::promptForVelocityModulationName(
    std::size_t playerIndex,
    lps::Modulation candidateModulation)
{
    constexpr auto nameEditorId = "velocityModulationName";
    auto* alert = new juce::AlertWindow(
        "Save Velocity Modulation",
        "These velocity values are not in the library yet. Enter a name for "
        "the new modulation.",
        juce::MessageBoxIconType::QuestionIcon,
        this);
    alert->addTextEditor(nameEditorId, {}, "Name:");
    alert->addButton(
        "Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton(
        "Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    const juce::Component::SafePointer<juce::Button> safeSaveButton(
        alert->getButton("Save"));
    if (safeSaveButton != nullptr)
        safeSaveButton->setEnabled(false);

    auto enteredName = std::make_shared<juce::String>();
    if (auto* textEditor = alert->getTextEditor(nameEditorId))
    {
        const juce::Component::SafePointer<juce::TextEditor> safeTextEditor(
            textEditor);
        textEditor->onTextChange =
            [safeTextEditor, safeSaveButton, enteredName]
            {
                if (safeTextEditor != nullptr)
                    *enteredName = safeTextEditor->getText();
                if (safeSaveButton != nullptr)
                    safeSaveButton->setEnabled(
                        !enteredName->trim().isEmpty());
            };
    }

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(
        this);
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, enteredName, playerIndex, candidateModulation](int result)
            {
                if (result != 1 || safeThis == nullptr)
                    return;

                const auto name = enteredName->trim();
                if (name.isEmpty())
                {
                    safeThis->showVelocityModulationSaveWarning(
                        "Enter a name before saving the new velocity "
                        "modulation.");
                    return;
                }

                const auto saveResult =
                    safeThis->processor_.savePlayerModulation(
                        playerIndex, candidateModulation, name);
                if (saveResult.status
                    == LivePatternSequencerProcessor::
                        SaveModulationStatus::needsName)
                {
                    safeThis->showVelocityModulationSaveWarning(
                        "Enter a name before saving the new velocity "
                        "modulation.");
                    return;
                }

                safeThis->handleVelocityModulationSaveResult(
                    playerIndex, saveResult);
            }),
        true);
}

void LivePatternSequencerEditor::handleVelocityModulationSaveResult(
    std::size_t playerIndex,
    LivePatternSequencerProcessor::SaveModulationResult result)
{
    using Status =
        LivePatternSequencerProcessor::SaveModulationStatus;
    switch (result.status)
    {
        case Status::needsName:
            promptForVelocityModulationName(
                playerIndex, result.candidateModulation);
            return;

        case Status::selectedExisting:
        case Status::savedNew:
            refreshVelocityModulationSelectors();
            refreshVelocityModulationControls(playerIndex, true);
            updateVelocityModulationModifiedIndicators(true);
            content_.repaint();
            return;

        case Status::failed:
        {
            const auto catalogError =
                processor_.velocityModulationCatalogErrorForUi();
            showVelocityModulationSaveWarning(catalogError.isNotEmpty()
                ? "The velocity modulation could not be saved.\n\n"
                    + catalogError
                : juce::String(
                    "The velocity modulation could not be saved."));
            return;
        }
    }
}

void LivePatternSequencerEditor::refreshPatternSelectors()
{
    for (std::size_t playerIndex = 0;
         playerIndex < patternSelectors_.size();
         ++playerIndex)
    {
        if (!processor_.playerSupportsPatternEditingForUi(playerIndex))
            continue;
        auto& selector = *patternSelectors_[playerIndex];
        selector.clear(juce::dontSendNotification);
        for (std::size_t patternIndex = 0;
             patternIndex < processor_.patternCountForUi();
             ++patternIndex)
        {
            selector.addItem(
                processor_.patternNameForUi(patternIndex),
                static_cast<int>(patternIndex + 1));
        }

        selector.setSelectedItemIndex(
            static_cast<int>(processor_.selectedPatternForPlayer(playerIndex)),
            juce::dontSendNotification);
    }
}

void LivePatternSequencerEditor::refreshVelocityModulationSelectors()
{
    for (std::size_t playerIndex = 0;
         playerIndex < velocityModulationSelectors_.size();
         ++playerIndex)
    {
        if (!processor_.playerSupportsVelocityEditingForUi(playerIndex))
            continue;
        auto& selector = *velocityModulationSelectors_[playerIndex];
        selector.clear(juce::dontSendNotification);
        for (std::size_t modulationIndex = 0;
             modulationIndex < processor_.velocityModulationCountForUi();
             ++modulationIndex)
        {
            selector.addItem(
                processor_.velocityModulationNameForUi(modulationIndex),
                static_cast<int>(modulationIndex + 1));
        }

        selector.setSelectedItemIndex(
            static_cast<int>(
                processor_.selectedModulationForPlayer(playerIndex)),
            juce::dontSendNotification);
    }
}

void LivePatternSequencerEditor::refreshVelocityModulationControls(
    std::size_t playerIndex,
    bool force)
{
    if (playerIndex >= playerCount_
        || !processor_.playerSupportsVelocityEditingForUi(playerIndex)
        || playerIndex >= decreaseVelocityModulationLengthButtons_.size()
        || playerIndex >= increaseVelocityModulationLengthButtons_.size())
    {
        return;
    }

    const auto modulation =
        processor_.velocityModulationForUi(playerIndex);
    const auto length = std::min<std::size_t>(
        modulation.length, lps::Modulation::maxLength);
    const bool playing = processor_.playingForUi();
    const int currentStep =
        processor_.currentModulationStepForUi(playerIndex);
    decreaseVelocityModulationLengthButtons_[playerIndex]->setEnabled(
        length > 1);
    increaseVelocityModulationLengthButtons_[playerIndex]->setEnabled(
        length < lps::Modulation::maxLength);

    for (std::size_t step = 0;
         step < lps::Modulation::maxLength;
         ++step)
    {
        const auto sliderIndex =
            playerIndex * lps::Modulation::maxLength + step;
        if (sliderIndex >= velocityModulationSliders_.size())
            break;

        auto& slider = *velocityModulationSliders_[sliderIndex];
        const bool shouldBeVisible = step < length;
        slider.setActive(
            playing && shouldBeVisible
            && static_cast<int>(step) == currentStep);
        if (slider.isVisible() != shouldBeVisible)
            slider.setVisible(shouldBeVisible);

        const auto value = static_cast<double>(
            modulation.values[step].raw / 257u);
        if (!slider.isMouseButtonDown()
            && (force || std::abs(slider.getValue() - value) > 0.5))
        {
            slider.setValue(value, juce::dontSendNotification);
        }
    }
}

void LivePatternSequencerEditor::updatePatternModifiedIndicators(bool force)
{
    for (std::size_t playerIndex = 0;
         playerIndex < savePatternButtons_.size();
         ++playerIndex)
    {
        if (!processor_.playerSupportsPatternEditingForUi(playerIndex))
            continue;
        const bool modified = processor_.playerPatternModifiedForUi(playerIndex);
        if (!force && displayedPatternModified_[playerIndex] == modified)
            continue;

        displayedPatternModified_[playerIndex] = modified;
        auto& selector = *patternSelectors_[playerIndex];
        auto& saveButton = *savePatternButtons_[playerIndex];
        auto& menuButton = *patternMenuButtons_[playerIndex];
        saveButton.setEnabled(modified);
        saveButton.setButtonText(modified ? "Save *" : "Save");
        menuButton.setButtonText(patternMenuLabel(playerIndex, modified));

        if (modified)
        {
            selector.setColour(juce::ComboBox::outlineColourId, juce::Colour(amber));
            selector.setColour(juce::ComboBox::textColourId, juce::Colour(amber));
            selector.setColour(juce::ComboBox::arrowColourId, juce::Colour(amber));
            saveButton.setColour(
                juce::TextButton::buttonColourId,
                juce::Colour(amber).darker(0.45f));
            menuButton.setColour(
                juce::TextButton::buttonColourId,
                juce::Colour(amber).darker(0.45f));
        }
        else
        {
            selector.removeColour(juce::ComboBox::outlineColourId);
            selector.removeColour(juce::ComboBox::textColourId);
            selector.removeColour(juce::ComboBox::arrowColourId);
            saveButton.removeColour(juce::TextButton::buttonColourId);
            menuButton.removeColour(juce::TextButton::buttonColourId);
        }

        selector.repaint();
        saveButton.repaint();
        menuButton.repaint();
    }
}

void LivePatternSequencerEditor::updateVelocityModulationModifiedIndicators(
    bool force)
{
    for (std::size_t playerIndex = 0;
         playerIndex < saveVelocityModulationButtons_.size();
         ++playerIndex)
    {
        if (!processor_.playerSupportsVelocityEditingForUi(playerIndex))
            continue;
        const bool modified =
            processor_.playerModulationModifiedForUi(playerIndex);
        if (!force
            && displayedVelocityModulationModified_[playerIndex] == modified)
        {
            continue;
        }

        displayedVelocityModulationModified_[playerIndex] = modified;
        auto& selector = *velocityModulationSelectors_[playerIndex];
        auto& saveButton = *saveVelocityModulationButtons_[playerIndex];
        auto& menuButton = *velocityMenuButtons_[playerIndex];
        saveButton.setEnabled(modified);
        saveButton.setButtonText(modified ? "Save *" : "Save");
        menuButton.setButtonText(velocityMenuLabel(playerIndex, modified));

        if (modified)
        {
            selector.setColour(
                juce::ComboBox::outlineColourId, juce::Colour(amber));
            selector.setColour(
                juce::ComboBox::textColourId, juce::Colour(amber));
            selector.setColour(
                juce::ComboBox::arrowColourId, juce::Colour(amber));
            saveButton.setColour(
                juce::TextButton::buttonColourId,
                juce::Colour(amber).darker(0.45f));
            menuButton.setColour(
                juce::TextButton::buttonColourId,
                juce::Colour(amber).darker(0.45f));
        }
        else
        {
            selector.removeColour(juce::ComboBox::outlineColourId);
            selector.removeColour(juce::ComboBox::textColourId);
            selector.removeColour(juce::ComboBox::arrowColourId);
            saveButton.removeColour(juce::TextButton::buttonColourId);
            menuButton.removeColour(juce::TextButton::buttonColourId);
        }

        selector.repaint();
        saveButton.repaint();
        menuButton.repaint();
    }
}

void LivePatternSequencerEditor::updateResetToMasterIndicators(bool force)
{
    const auto masterIndex = processor_.masterPlayerIndexForUi();
    const auto masterName = masterIndex
        ? processor_.playerNameForUi(*masterIndex)
        : juce::String("the master voice");

    for (std::size_t playerIndex = 0;
         playerIndex < resetToMasterButtons_.size();
         ++playerIndex)
    {
        const bool isMaster = processor_.playerIsMasterForUi(playerIndex);
        const bool pending = !isMaster
            && processor_.playerResetToMasterPendingForUi(playerIndex);
        if (!force && displayedResetToMasterPending_[playerIndex] == pending)
            continue;

        displayedResetToMasterPending_[playerIndex] = pending;
        auto& button = *resetToMasterButtons_[playerIndex];

        if (isMaster)
        {
            button.setButtonText("MASTER");
            button.setEnabled(false);
            button.setTooltip(
                processor_.playerNameForUi(playerIndex)
                + " is the master voice");
            button.setColour(
                juce::TextButton::buttonColourId,
                juce::Colour(amber).darker(0.55f));
        }
        else
        {
            button.setButtonText(pending ? "Queued" : "Reset");
            button.setEnabled(!pending);
            button.setTooltip(pending
                ? "Reset queued; this sequence will restart at "
                    + masterName + "'s next loop start"
                : "At " + masterName + "'s next loop start, restart this "
                    "sequence at its active loop start");

            if (pending)
            {
                button.setColour(
                    juce::TextButton::buttonColourId,
                    juce::Colour(amber).darker(0.45f));
            }
            else
            {
                button.removeColour(juce::TextButton::buttonColourId);
            }
        }

        button.repaint();
    }
}

void LivePatternSequencerEditor::showSaveWarning(const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Pattern Not Saved",
        message,
        "OK",
        this);
}

void LivePatternSequencerEditor::showVelocityModulationSaveWarning(
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Velocity Modulation Not Saved",
        message,
        "OK",
        this);
}

void LivePatternSequencerEditor::showPatternLibraryWarning(
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Pattern Library Unavailable",
        message,
        "OK",
        this);
}

void LivePatternSequencerEditor::showVelocityModulationLibraryWarning(
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Velocity Modulation Library Unavailable",
        message,
        "OK",
        this);
}

juce::Rectangle<int> LivePatternSequencerEditor::cellsAreaForPlayer(
    std::size_t playerIndex) const
{
    const int y = static_cast<int>(playerIndex) * playerStride();
    constexpr int stepCount = static_cast<int>(lps::Pattern::maxLength);
    const int size = cellWidth();
    const int width = stepCount * size + (stepCount - 1) * patternCellGap;
    return {
        patternGridLeft,
        y + (patternPanelHeight() - size) / 2,
        width,
        size
    };
}

int LivePatternSequencerEditor::cellWidth() const
{
    return patternCellSizeForContentWidth(content_.getWidth());
}

int LivePatternSequencerEditor::patternPanelHeight() const
{
    return std::max(clickTargetSize, cellWidth()) + 2;
}

int LivePatternSequencerEditor::playerStride() const
{
    return patternPanelHeight() + modulationPanelHeight;
}

int LivePatternSequencerEditor::requiredContentWidth() const
{
    constexpr int stepCount = static_cast<int>(lps::Pattern::maxLength);
    constexpr int gridWidth = stepCount * minimumPatternCellSize
        + (stepCount - 1) * patternCellGap;
    constexpr int rightPadding = contentHorizontalPadding + playerPanelHorizontalPadding;
    return patternGridLeft + gridWidth + rightPadding;
}

void LivePatternSequencerEditor::updateContentSize()
{
    if (viewport_.getWidth() <= 0 || viewport_.getHeight() <= 0)
        return;

    int width = std::max(requiredContentWidth(), viewport_.getWidth());
    const auto heightForWidth = [this](int candidateWidth)
    {
        constexpr int panelChrome = 2;
        const int size = patternCellSizeForContentWidth(candidateWidth);
        const int stride = std::max(clickTargetSize, size) + panelChrome
            + modulationPanelHeight;
        return std::max(
            1,
            static_cast<int>(playerCount_) * stride + playerBottomPadding);
    };

    int height = heightForWidth(width);
    if (height > viewport_.getHeight())
    {
        width = std::max(
            requiredContentWidth(),
            viewport_.getWidth() - viewport_.getScrollBarThickness());
        height = heightForWidth(width);
    }

    if (content_.getWidth() != width || content_.getHeight() != height)
        content_.setSize(width, height);
}

int LivePatternSequencerEditor::matrixPanelWidth() const
{
    constexpr int matrixRightPadding = 10;
    return std::max(
        400,
        matrixGridLeft + static_cast<int>(playerCount_) * matrixCellWidth
            + matrixRightPadding);
}

std::size_t LivePatternSequencerEditor::stepAtX(std::size_t playerIndex, int x) const
{
    const int relativeX = x - cellsAreaForPlayer(playerIndex).getX();
    const int step = juce::jlimit(
        0, static_cast<int>(lps::Pattern::maxLength) - 1,
        static_cast<int>(std::floor(
            static_cast<double>(relativeX)
            / static_cast<double>(cellWidth() + patternCellGap))));
    return static_cast<std::size_t>(step);
}

std::size_t LivePatternSequencerEditor::nearestStepAtX(
    std::size_t playerIndex, int x) const
{
    const int pitch = cellWidth() + patternCellGap;
    const int relativeX = x - cellsAreaForPlayer(playerIndex).getX();
    const int step = juce::jlimit(
        0, static_cast<int>(lps::Pattern::maxLength) - 1,
        static_cast<int>(std::floor(
            static_cast<double>(relativeX + pitch / 2)
            / static_cast<double>(pitch))));
    return static_cast<std::size_t>(step);
}

void LivePatternSequencerEditor::contentMouseDown(const juce::MouseEvent& event)
{
    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        if (!processor_.playerSupportsPatternEditingForUi(playerIndex))
            continue;
        const auto cells = cellsAreaForPlayer(playerIndex);
        const int pitch = cellWidth() + patternCellGap;
        const int startX = cells.getX()
            + static_cast<int>(processor_.playerPlaybackStart(playerIndex)) * pitch;
        const int endX = cells.getX()
            + static_cast<int>(processor_.playerPlaybackEnd(playerIndex)) * pitch + cellWidth();
        const int bracketTop = cells.getY() - rangeHandleExtension;
        const int bracketBottom = cells.getBottom() + rangeHandleExtension;

        const auto handleContains = [&](int handleX)
        {
            const juce::Rectangle<int> strokeTarget(
                handleX - rangeHandleStrokeGrabRadius,
                bracketTop,
                rangeHandleStrokeGrabRadius * 2 + 1,
                bracketBottom - bracketTop + 1);
            const juce::Rectangle<int> topCapTarget(
                handleX - rangeHandleCapGrabRadius,
                bracketTop - rangeHandleCapVerticalGrabRadius,
                rangeHandleCapGrabRadius * 2 + 1,
                rangeHandleCapVerticalGrabRadius * 2 + 1);
            const juce::Rectangle<int> bottomCapTarget(
                handleX - rangeHandleCapGrabRadius,
                bracketBottom - rangeHandleCapVerticalGrabRadius,
                rangeHandleCapGrabRadius * 2 + 1,
                rangeHandleCapVerticalGrabRadius * 2 + 1);
            return strokeTarget.contains(event.getPosition())
                || topCapTarget.contains(event.getPosition())
                || bottomCapTarget.contains(event.getPosition());
        };

        const bool startHit = handleContains(startX);
        const bool endHit = handleContains(endX);
        if (startHit || endHit)
        {
            const int startDistance = std::abs(event.x - startX);
            const int endDistance = std::abs(event.x - endX);
            draggedPlayerIndex_ = playerIndex;
            draggedRangeHandle_ = startHit && (!endHit || startDistance <= endDistance)
                ? DraggedRangeHandle::start : DraggedRangeHandle::end;
            const int selectedHandleX = draggedRangeHandle_ == DraggedRangeHandle::start
                ? startX : endX;
            draggedRangeHandleOffsetX_ = event.x - selectedHandleX;
            rangeHandleDragStart_ = event.getPosition();
            rangeHandleWasDragged_ = false;
            content_.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }

        if (cells.contains(event.getPosition()))
        {
            processor_.togglePlayerStep(playerIndex, stepAtX(playerIndex, event.x));
            content_.repaint();
            return;
        }
    }
}

void LivePatternSequencerEditor::contentMouseDrag(const juce::MouseEvent& event)
{
    if (draggedRangeHandle_ == DraggedRangeHandle::none)
        return;

    if (!rangeHandleWasDragged_)
    {
        const int distanceX = std::abs(event.x - rangeHandleDragStart_.x);
        const int distanceY = std::abs(event.y - rangeHandleDragStart_.y);
        if (std::max(distanceX, distanceY) < rangeHandleDragThreshold)
            return;
        rangeHandleWasDragged_ = true;
    }

    updateDraggedRange(event.x);
}

void LivePatternSequencerEditor::contentMouseUp(const juce::MouseEvent& event)
{
    if (draggedRangeHandle_ != DraggedRangeHandle::none
        && !rangeHandleWasDragged_
        && draggedPlayerIndex_ < playerCount_)
    {
        const auto cells = cellsAreaForPlayer(draggedPlayerIndex_);
        if (cells.contains(event.getPosition()))
        {
            processor_.togglePlayerStep(
                draggedPlayerIndex_, stepAtX(draggedPlayerIndex_, event.x));
            content_.repaint();
        }
    }

    draggedRangeHandle_ = DraggedRangeHandle::none;
    draggedRangeHandleOffsetX_ = 0;
    rangeHandleWasDragged_ = false;
    content_.setMouseCursor(juce::MouseCursor::NormalCursor);
}

void LivePatternSequencerEditor::updateDraggedRange(int mouseX)
{
    if (draggedRangeHandle_ == DraggedRangeHandle::none)
        return;

    const int handleX = mouseX - draggedRangeHandleOffsetX_;
    const int stepAnchorX = draggedRangeHandle_ == DraggedRangeHandle::end
        ? handleX - cellWidth() : handleX;
    const auto step = nearestStepAtX(draggedPlayerIndex_, stepAnchorX);
    auto start = processor_.playerPlaybackStart(draggedPlayerIndex_);
    auto end = processor_.playerPlaybackEnd(draggedPlayerIndex_);
    if (draggedRangeHandle_ == DraggedRangeHandle::start)
        start = std::min(step, end);
    else
        end = std::max(step, start);
    processor_.setPlayerPlaybackWindow(draggedPlayerIndex_, start, end);
    content_.repaint();
}

void LivePatternSequencerEditor::paintContent(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));

    const bool playing = processor_.playingForUi();
    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        const juce::Rectangle<int> patternPanel(
            contentHorizontalPadding,
            static_cast<int>(playerIndex) * playerStride(),
            content_.getWidth() - contentHorizontalPadding * 2,
            patternPanelHeight());
        const juce::Rectangle<int> modulationPanel(
            contentHorizontalPadding,
            patternPanel.getBottom(),
            content_.getWidth() - contentHorizontalPadding * 2,
            modulationPanelHeight);
        graphics.setColour(juce::Colour(panel));
        graphics.fillRect(patternPanel);
        graphics.setColour(juce::Colour(border).brighter(0.08f));
        graphics.fillRect(
            patternPanel.getX(), patternPanel.getBottom() - 2,
            patternPanel.getWidth(), 2);

        graphics.setColour(juce::Colour(panel));
        graphics.fillRect(modulationPanel);
        graphics.setColour(juce::Colour(border).brighter(0.08f));
        graphics.fillRect(
            modulationPanel.getX(), modulationPanel.getBottom() - 3,
            modulationPanel.getWidth(), 3);

        graphics.setColour(juce::Colour(border).withMultipliedAlpha(0.65f));
        graphics.drawVerticalLine(
            patternGridLeft - controlsToGridGap / 2,
            static_cast<float>(patternPanel.getY() + 1),
            static_cast<float>(patternPanel.getBottom() - 2));

        auto cellsArea = cellsAreaForPlayer(playerIndex);
        const auto pattern = processor_.patternForUi(playerIndex);
        const int currentStep = processor_.currentStepForUi(playerIndex);
        graphics.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));

        const bool supportsPattern =
            processor_.playerSupportsPatternEditingForUi(playerIndex);
        if (!supportsPattern)
        {
            graphics.setColour(juce::Colour(secondaryText));
            graphics.drawFittedText(
                "PERIODIC PULSE — NO PATTERN EDITOR",
                cellsArea,
                juce::Justification::centredLeft,
                1);
        }
        else
        {
            for (std::uint16_t step = 0;
                 step < static_cast<std::uint16_t>(lps::Pattern::maxLength);
                 ++step)
            {
                auto cell = cellsArea.removeFromLeft(cellWidth());
                const bool isCurrent = playing && static_cast<int>(step) == currentStep;
                const bool isHit = pattern.isHit(step);
                const bool isInsideWindow = pattern.isInsidePlaybackWindow(step);
                const auto restColour = (step / 4) % 2 == 0
                    ? patternStepOff
                    : patternStepOffAlternate;
                auto fillColour = juce::Colour(
                    isHit ? patternStepOn : restColour);
                if (!isInsideWindow)
                    fillColour = fillColour.withMultipliedAlpha(0.28f);
                graphics.setColour(fillColour);
                graphics.fillRoundedRectangle(cell.toFloat(), 2.0f);
                const auto cellBorder = isCurrent
                    ? juce::Colour(patternPlayhead)
                    : (isHit
                        ? juce::Colour(uiYellow)
                        : (step % 4 == 0
                            ? juce::Colour(darkGray).darker(0.18f)
                            : juce::Colour(darkGray)));
                graphics.setColour(cellBorder);
                graphics.drawRoundedRectangle(
                    cell.toFloat(), 2.0f,
                    (isCurrent || isHit) ? 2.5f : 1.0f);

                auto textColour = juce::Colour(
                    isCurrent
                        ? patternPlayhead
                        : (isHit ? patternStepTextOn : patternStepTextOff));
                if (!isInsideWindow)
                    textColour = textColour.withMultipliedAlpha(0.38f);
                graphics.setColour(textColour);
                graphics.drawText(juce::String(static_cast<int>(step + 1)), cell,
                    juce::Justification::centred);
                cellsArea.removeFromLeft(patternCellGap);
            }
        }

        if (!supportsPattern || pattern.stepCount == 0)
            continue;

        const auto bracketArea = cellsAreaForPlayer(playerIndex);
        const int pitch = cellWidth() + patternCellGap;
        const int startX = bracketArea.getX() + static_cast<int>(pattern.playbackStart) * pitch;
        const int endX = bracketArea.getX()
            + static_cast<int>(pattern.playbackEnd) * pitch + cellWidth();
        const int top = bracketArea.getY() - rangeHandleExtension;
        const int bottom = bracketArea.getBottom() + rangeHandleExtension;
        juce::Path brackets;
        brackets.startNewSubPath(
            static_cast<float>(startX + rangeHandleCapLength), static_cast<float>(top));
        brackets.lineTo(static_cast<float>(startX), static_cast<float>(top));
        brackets.lineTo(static_cast<float>(startX), static_cast<float>(bottom));
        brackets.lineTo(
            static_cast<float>(startX + rangeHandleCapLength), static_cast<float>(bottom));
        brackets.startNewSubPath(
            static_cast<float>(endX - rangeHandleCapLength), static_cast<float>(top));
        brackets.lineTo(static_cast<float>(endX), static_cast<float>(top));
        brackets.lineTo(static_cast<float>(endX), static_cast<float>(bottom));
        brackets.lineTo(
            static_cast<float>(endX - rangeHandleCapLength), static_cast<float>(bottom));
        graphics.setColour(juce::Colour(amber));
        graphics.strokePath(
            brackets, juce::PathStrokeType(rangeHandleStrokeWidth));

        constexpr float gripWidth = 12.0f;
        constexpr float gripHeight = 28.0f;
        const float gripY = static_cast<float>(top + bottom) * 0.5f
            - gripHeight * 0.5f;
        graphics.fillRoundedRectangle(
            static_cast<float>(startX) - gripWidth * 0.5f,
            gripY,
            gripWidth,
            gripHeight,
            3.0f);
        graphics.fillRoundedRectangle(
            static_cast<float>(endX) - gripWidth * 0.5f,
            gripY,
            gripWidth,
            gripHeight,
            3.0f);
    }
}

void LivePatternSequencerEditor::showSuppressionMatrix()
{
    if (suppressionWindow_ != nullptr)
    {
        suppressionWindow_->closeButtonPressed();
        return;
    }

    matrix_.setSize(
        matrixPanelWidth(),
        matrixGridTop + static_cast<int>(playerCount_) * matrixCellHeight + 10);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Suppression Matrix";
    options.dialogBackgroundColour = juce::Colour(background);
    options.content.setNonOwned(&matrix_);
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    suppressionWindow_ = options.launchAsync();
}

void LivePatternSequencerEditor::showPatternMenu(std::size_t playerIndex)
{
    if (playerIndex >= playerCount_ || playerIndex >= patternMenuButtons_.size())
        return;

    constexpr int speedItemBase = 10000;
    constexpr int saveItemId = 20000;
    juce::PopupMenu patternChoices;
    const auto selectedPattern =
        processor_.selectedPatternForPlayer(playerIndex);
    for (std::size_t patternIndex = 0;
         patternIndex < processor_.patternCountForUi();
         ++patternIndex)
    {
        patternChoices.addCustomItem(
            static_cast<int>(patternIndex + 1),
            std::make_unique<PatternPreviewMenuItem>(
                processor_.patternAtForUi(patternIndex),
                patternIndex == selectedPattern),
            nullptr,
            processor_.patternNameForUi(patternIndex));
    }

    juce::PopupMenu speedChoices;
    static constexpr const char* speedNames[] { "0.5x", "1x", "2x" };
    const auto selectedSpeed = processor_.playerPlaybackSpeed(playerIndex);
    for (int speedIndex = 0; speedIndex < 3; ++speedIndex)
    {
        speedChoices.addItem(
            speedItemBase + speedIndex,
            speedNames[speedIndex],
            true,
            static_cast<std::size_t>(speedIndex) == selectedSpeed);
    }

    juce::PopupMenu menu;
    menu.addSubMenu("SELECT PATTERN", patternChoices);
    menu.addSubMenu("PLAY SPEED", speedChoices);
    menu.addSeparator();
    menu.addItem(
        saveItemId,
        "SAVE PATTERN",
        processor_.playerPatternModifiedForUi(playerIndex));

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(patternMenuButtons_[playerIndex].get()),
        [safeThis, playerIndex](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == saveItemId)
            {
                safeThis->beginSavePlayerPattern(playerIndex);
                return;
            }

            if (result >= speedItemBase && result < speedItemBase + 3)
            {
                safeThis->processor_.setPlayerPlaybackSpeed(
                    playerIndex,
                    static_cast<std::size_t>(result - speedItemBase));
                return;
            }

            const auto patternIndex = static_cast<std::size_t>(result - 1);
            if (patternIndex < safeThis->processor_.patternCountForUi())
            {
                safeThis->processor_.selectPatternForPlayer(
                    playerIndex, patternIndex);
                safeThis->updatePatternModifiedIndicators(true);
                safeThis->content_.repaint();
            }
        });
}

void LivePatternSequencerEditor::showVelocityMenu(std::size_t playerIndex)
{
    if (playerIndex >= playerCount_ || playerIndex >= velocityMenuButtons_.size())
        return;

    constexpr int saveItemId = 20000;
    juce::PopupMenu menu;
    const auto selected =
        processor_.selectedModulationForPlayer(playerIndex);
    for (std::size_t modulationIndex = 0;
         modulationIndex < processor_.velocityModulationCountForUi();
         ++modulationIndex)
    {
        menu.addItem(
            static_cast<int>(modulationIndex + 1),
            processor_.velocityModulationNameForUi(modulationIndex),
            true,
            modulationIndex == selected);
    }
    menu.addSeparator();
    menu.addItem(
        saveItemId,
        "SAVE MODULATION",
        processor_.playerModulationModifiedForUi(playerIndex));

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(velocityMenuButtons_[playerIndex].get()),
        [safeThis, playerIndex](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == saveItemId)
            {
                safeThis->beginSavePlayerVelocityModulation(playerIndex);
                return;
            }

            const auto modulationIndex = static_cast<std::size_t>(result - 1);
            if (modulationIndex
                < safeThis->processor_.velocityModulationCountForUi())
            {
                safeThis->processor_.selectModulationForPlayer(
                    playerIndex, modulationIndex);
                safeThis->refreshVelocityModulationControls(playerIndex, true);
                safeThis->updateVelocityModulationModifiedIndicators(true);
                safeThis->content_.repaint();
            }
        });
}

void LivePatternSequencerEditor::paintMatrix(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    const auto panelBounds = matrix_.getLocalBounds();
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(panelBounds.toFloat(), 7.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(
        panelBounds.toFloat().reduced(0.5f), 7.0f, 1.0f);

    graphics.setColour(juce::Colour(primaryText));
    graphics.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    graphics.drawText("SUPPRESSION: ROW suppresses COLUMN", 16, 14,
        matrix_.getWidth() - 32, 24,
        juce::Justification::centredLeft);

    for (std::size_t row = 0; row < playerCount_; ++row)
    {
        for (std::size_t column = 0; column < playerCount_; ++column)
        {
            const juce::Rectangle<int> cell(
                matrixGridLeft + static_cast<int>(column) * matrixCellWidth,
                matrixGridTop + static_cast<int>(row) * matrixCellHeight,
                matrixCellWidth,
                matrixCellHeight);
            const bool isDiagonal = row == column;
            const bool isActive = !isDiagonal
                && processor_.suppression(row, column);

            graphics.setColour(juce::Colour(
                isActive
                    ? uiBlue
                    : (isDiagonal ? panel : darkGray)));
            graphics.fillRect(cell);
            graphics.setColour(juce::Colour(uiBlack).withMultipliedAlpha(
                isDiagonal ? 0.35f : 0.9f));
            graphics.drawRect(cell, isActive ? 2 : 1);
        }
    }

    graphics.setColour(juce::Colour(secondaryText));
    graphics.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
    for (std::size_t index = 0; index < playerCount_; ++index)
    {
        auto label = processor_.playerNameForUi(index);
        if (label.isEmpty())
            label = "P" + juce::String(static_cast<int>(index + 1));

        const float columnCentreX = static_cast<float>(
            matrixGridLeft + static_cast<int>(index) * matrixCellWidth
                + matrixCellWidth / 2);
        constexpr float columnCentreY = static_cast<float>(matrixGridTop - 38);
        {
            juce::Graphics::ScopedSaveState savedState(graphics);
            graphics.addTransform(juce::AffineTransform::rotation(
                -juce::MathConstants<float>::halfPi,
                columnCentreX,
                columnCentreY));
            graphics.drawFittedText(
                label,
                juce::roundToInt(columnCentreX - 31.0f),
                juce::roundToInt(columnCentreY - 12.0f),
                62,
                24,
                juce::Justification::centred,
                1,
                0.8f);
        }

        graphics.drawFittedText(
            label,
            8,
            matrixGridTop + static_cast<int>(index) * matrixCellHeight + 3,
            matrixGridLeft - 14,
            24,
            juce::Justification::centredRight,
            1,
            0.8f);
    }
}
