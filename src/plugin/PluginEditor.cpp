#include "plugin/PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
constexpr auto background = 0xff101417;
constexpr auto panel = 0xff171d21;
constexpr auto border = 0xff39434a;
constexpr auto cyan = 0xff20d8e5;
constexpr auto amber = 0xffffb21a;
constexpr auto inactive = 0xff242c31;
constexpr auto primaryText = 0xffedf5f6;
constexpr auto secondaryText = 0xff8c9aa1;

constexpr int outerPadding = 12;
constexpr int headerHeight = 42;
constexpr int sectionGap = 8;
constexpr int minimumPlayerContentWidth = 900;
constexpr int contentHorizontalPadding = 6;
constexpr int playerPanelHorizontalPadding = 6;
constexpr int playerControlsWidth = 294;
constexpr int controlsToGridGap = 6;
constexpr int playerPanelHeight = 48;
constexpr int playerLaneGap = 2;
constexpr int velocityPanelHeight = 76;
constexpr int playerPanelGap = 5;
constexpr int playerStride = playerPanelHeight + playerLaneGap
    + velocityPanelHeight + playerPanelGap;
constexpr int playerBottomPadding = 6;
constexpr int matrixGridLeft = 82;
constexpr int matrixGridTop = 112;
constexpr int matrixCellWidth = 34;
constexpr int matrixCellHeight = 30;
constexpr int rangeHandleExtension = 5;
constexpr int rangeHandleCapLength = 7;
constexpr int rangeHandleCapGrabRadius = 10;
constexpr int rangeHandleCapVerticalGrabRadius = 2;
constexpr int rangeHandleStrokeGrabRadius = 3;
constexpr int patternCellWidth = 17;
constexpr int patternCellHeight = 34;
constexpr int patternCellGap = 1;
constexpr int patternGridLeft = contentHorizontalPadding
    + playerPanelHorizontalPadding + playerControlsWidth + controlsToGridGap;
constexpr int velocityKnobWidth = 48;
constexpr int velocityKnobHeight = 66;
constexpr int velocityKnobGap = 7;
constexpr int velocityTextBoxHeight = 16;
constexpr int velocityGridLeft = patternGridLeft;
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

LivePatternSequencerEditor::LivePatternSequencerEditor(
    LivePatternSequencerProcessor& processorToEdit)
    : juce::AudioProcessorEditor(processorToEdit),
      processor_(processorToEdit),
      playerCount_(processorToEdit.playerCountForUi()),
      content_(*this),
      matrix_(*this)
{
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
        playerCount_ * lps::VelocityModulation::maxLength);
    suppressionButtons_.reserve(playerCount_ * playerCount_);

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
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
        content_.addAndMakeVisible(selector);

        savePatternButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& saveButton = *savePatternButtons_.back();
        saveButton.setButtonText("Save");
        saveButton.setTooltip(
            "Save the active loop to the persistent pattern library");
        saveButton.onClick = [this, playerIndex]
        {
            beginSavePlayerPattern(playerIndex);
        };
        content_.addAndMakeVisible(saveButton);

        resetToMasterButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& resetButton = *resetToMasterButtons_.back();
        const bool isMaster = processor_.playerIsMasterForUi(playerIndex);
        resetButton.setButtonText(isMaster ? "MASTER" : "Reset");
        resetButton.setEnabled(!isMaster);
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
        content_.addAndMakeVisible(speedSelector);

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

        muteButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& muteButton = *muteButtons_.back();
        muteButton.setButtonText("MUTE");
        muteButton.setClickingTogglesState(true);
        muteButton.setToggleState(
            processor_.playerMutedForUi(playerIndex),
            juce::dontSendNotification);
        muteButton.setTooltip(
            "Mute " + processor_.playerNameForUi(playerIndex)
            + " note output while its sequence keeps running");
        muteButton.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour(0xffb84646));
        muteButton.setColour(
            juce::TextButton::textColourOnId,
            juce::Colour(primaryText));
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
                processor_.selectedVelocityModulationForPlayer(playerIndex)),
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
                processor_.selectVelocityModulationForPlayer(
                    playerIndex, static_cast<std::size_t>(selected));
            }
        };
        content_.addAndMakeVisible(velocitySelector);

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
        content_.addAndMakeVisible(velocitySaveButton);

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
                processor_.setPlayerVelocityModulationLength(
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
            if (modulation.length < lps::VelocityModulation::maxLength)
            {
                const auto newLength = std::max<std::size_t>(
                    1, modulation.length + 1);
                processor_.setPlayerVelocityModulationLength(
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
             step < lps::VelocityModulation::maxLength;
             ++step)
        {
            velocityModulationSliders_.push_back(
                std::make_unique<juce::Slider>());
            const auto sliderIndex = velocityModulationSliders_.size() - 1;
            auto& slider = *velocityModulationSliders_.back();
            slider.setName(
                "Velocity modulation step "
                + juce::String(static_cast<int>(step + 1)));
            slider.setTooltip(
                "Velocity step " + juce::String(static_cast<int>(step + 1))
                + " (0-255)");
            slider.setSliderStyle(
                juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRange(0.0, 255.0, 1.0);
            slider.setNumDecimalPlacesToDisplay(0);
            slider.setTextBoxStyle(
                juce::Slider::TextBoxBelow,
                false,
                velocityKnobWidth,
                velocityTextBoxHeight);
            slider.setDoubleClickReturnValue(true, 255.0);
            slider.setScrollWheelEnabled(false);
            slider.setColour(
                juce::Slider::rotarySliderFillColourId,
                juce::Colour(cyan));
            slider.setColour(
                juce::Slider::rotarySliderOutlineColourId,
                juce::Colour(inactive));
            slider.setColour(
                juce::Slider::thumbColourId,
                juce::Colour(primaryText));
            slider.setColour(
                juce::Slider::textBoxTextColourId,
                juce::Colour(primaryText));
            slider.setColour(
                juce::Slider::textBoxBackgroundColourId,
                juce::Colours::transparentBlack);
            slider.setColour(
                juce::Slider::textBoxOutlineColourId,
                juce::Colours::transparentBlack);
            slider.setValue(
                static_cast<double>(modulation.values[step]),
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
                    processor_.setPlayerVelocityModulationValue(
                        playerIndex,
                        step,
                        static_cast<std::uint8_t>(value));
                    updateVelocityModulationModifiedIndicators(true);
                    content_.repaint();
                };
            content_.addAndMakeVisible(slider);
            slider.setVisible(step < modulation.length);
        }
    }

    for (std::size_t row = 0; row < playerCount_; ++row)
        for (std::size_t column = 0; column < playerCount_; ++column)
        {
            suppressionButtons_.push_back(std::make_unique<juce::ToggleButton>());
            auto& button = *suppressionButtons_.back();
            button.setButtonText(row == column ? "--" : "X");
            button.setEnabled(row != column);
            button.setToggleState(
                processor_.suppression(row, column), juce::dontSendNotification);
            const auto suppressorName = processor_.playerNameForUi(row);
            const auto suppressedName = processor_.playerNameForUi(column);
            button.setTooltip(
                suppressorName + " suppresses " + suppressedName);
            button.onClick = [this, row, column]
            {
                const auto index = row * playerCount_ + column;
                processor_.setSuppression(
                    row, column, suppressionButtons_[index]->getToggleState());
            };
            matrix_.addAndMakeVisible(button);
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
    addAndMakeVisible(matrix_);

    const int rowsHeight = playerCount_ == 0
        ? 0
        : static_cast<int>(playerCount_) * playerStride - playerPanelGap;
    const int playerContentHeight = std::max(1, rowsHeight + playerBottomPadding);
    content_.setSize(requiredContentWidth(), playerContentHeight);

    constexpr int initialEditorHeight = 650;
    const int editorWidth = outerPadding * 2
        + minimumPlayerContentWidth + viewport_.getScrollBarThickness()
        + sectionGap + matrixPanelWidth();
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
    viewport_.setViewedComponent(nullptr, false);
}

void LivePatternSequencerEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));

    auto bounds = getLocalBounds().reduced(outerPadding);
    auto header = bounds.removeFromTop(headerHeight);

    graphics.setColour(juce::Colour(primaryText));
    graphics.setFont(juce::Font(juce::FontOptions(22.0f, juce::Font::bold)));
    graphics.drawText(
        "LIVE PATTERN SEQUENCER",
        header.removeFromLeft(430),
        juce::Justification::centredLeft);

    const bool playing = processor_.playingForUi();
    graphics.setColour(juce::Colour(playing ? cyan : secondaryText));
    graphics.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    graphics.drawText(
        playing ? "REAPER CLOCK: PLAYING" : "REAPER CLOCK: STOPPED",
        header,
        juce::Justification::centredRight);
}

void LivePatternSequencerEditor::resized()
{
    auto bounds = getLocalBounds().reduced(outerPadding);
    bounds.removeFromTop(headerHeight + sectionGap);

    auto matrixBounds = bounds.removeFromRight(matrixPanelWidth());
    bounds.removeFromRight(sectionGap);
    viewport_.setBounds(bounds);
    matrix_.setBounds(matrixBounds);

    const bool needsVerticalScrollbar = content_.getHeight() > viewport_.getHeight();
    const int visiblePlayerWidth = viewport_.getWidth()
        - (needsVerticalScrollbar ? viewport_.getScrollBarThickness() : 0);
    const int contentWidth = std::max(requiredContentWidth(), visiblePlayerWidth);
    if (content_.getWidth() != contentWidth)
        content_.setSize(contentWidth, content_.getHeight());
}

void LivePatternSequencerEditor::resizedContent()
{
    constexpr int controlsX = contentHorizontalPadding + playerPanelHorizontalPadding;
    constexpr int patternSelectorX = controlsX + 70;
    constexpr int secondaryControlsX = patternSelectorX + 112;
    constexpr int lowerButtonWidth = 52;
    constexpr int lowerButtonGap = 4;
    constexpr int muteButtonX = secondaryControlsX + 66;
    constexpr int lengthButtonWidth = 24;

    for (std::size_t index = 0; index < patternSelectors_.size(); ++index)
    {
        const int y = static_cast<int>(index) * playerStride;
        patternSelectors_[index]->setBounds(patternSelectorX, y + 3, 108, 21);
        savePatternButtons_[index]->setBounds(
            patternSelectorX, y + 27, lowerButtonWidth, 17);
        resetToMasterButtons_[index]->setBounds(
            patternSelectorX + lowerButtonWidth + lowerButtonGap,
            y + 27,
            lowerButtonWidth,
            17);
        speedSelectors_[index]->setBounds(secondaryControlsX, y + 4, 62, 21);
        offsetLeftButtons_[index]->setBounds(secondaryControlsX, y + 27, 30, 17);
        offsetRightButtons_[index]->setBounds(secondaryControlsX + 32, y + 27, 30, 17);
        muteButtons_[index]->setBounds(muteButtonX, y + 3, 46, 41);

        const int velocityY = y + playerPanelHeight + playerLaneGap;
        velocityModulationSelectors_[index]->setBounds(
            patternSelectorX, velocityY + 4, 108, 21);
        saveVelocityModulationButtons_[index]->setBounds(
            patternSelectorX, velocityY + 29, lowerButtonWidth, 18);
        decreaseVelocityModulationLengthButtons_[index]->setBounds(
            patternSelectorX + lowerButtonWidth + lowerButtonGap,
            velocityY + 29,
            lengthButtonWidth,
            18);
        increaseVelocityModulationLengthButtons_[index]->setBounds(
            patternSelectorX + lowerButtonWidth + lowerButtonGap
                + lengthButtonWidth + lowerButtonGap,
            velocityY + 29,
            lengthButtonWidth,
            18);

        for (std::size_t step = 0;
             step < lps::VelocityModulation::maxLength;
             ++step)
        {
            const auto sliderIndex =
                index * lps::VelocityModulation::maxLength + step;
            velocityModulationSliders_[sliderIndex]->setBounds(
                velocityGridLeft
                    + static_cast<int>(step)
                        * (velocityKnobWidth + velocityKnobGap),
                velocityY + 3,
                velocityKnobWidth,
                velocityKnobHeight);
        }
    }
}

void LivePatternSequencerEditor::resizedMatrix()
{
    for (std::size_t row = 0; row < playerCount_; ++row)
        for (std::size_t column = 0; column < playerCount_; ++column)
            suppressionButtons_[row * playerCount_ + column]->setBounds(
                matrixGridLeft + static_cast<int>(column) * matrixCellWidth + 3,
                matrixGridTop + static_cast<int>(row) * matrixCellHeight + 3,
                28,
                24);
}

void LivePatternSequencerEditor::timerCallback()
{
    updateContentWidth();
    updatePatternModifiedIndicators();
    for (std::size_t playerIndex = 0;
         playerIndex < playerCount_;
         ++playerIndex)
    {
        refreshVelocityModulationControls(playerIndex);
    }
    updateVelocityModulationModifiedIndicators();
    updateResetToMasterIndicators();
    auto header = getLocalBounds().reduced(outerPadding).removeFromTop(headerHeight);
    repaint(header);
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
        processor_.savePlayerVelocityModulation(playerIndex));
}

void LivePatternSequencerEditor::promptForVelocityModulationName(
    std::size_t playerIndex,
    lps::VelocityModulation candidateModulation)
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
                    safeThis->processor_.savePlayerVelocityModulation(
                        playerIndex, candidateModulation, name);
                if (saveResult.status
                    == LivePatternSequencerProcessor::
                        SaveVelocityModulationStatus::needsName)
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
    LivePatternSequencerProcessor::SaveVelocityModulationResult result)
{
    using Status =
        LivePatternSequencerProcessor::SaveVelocityModulationStatus;
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
                processor_.selectedVelocityModulationForPlayer(playerIndex)),
            juce::dontSendNotification);
    }
}

void LivePatternSequencerEditor::refreshVelocityModulationControls(
    std::size_t playerIndex,
    bool force)
{
    if (playerIndex >= playerCount_
        || playerIndex >= decreaseVelocityModulationLengthButtons_.size()
        || playerIndex >= increaseVelocityModulationLengthButtons_.size())
    {
        return;
    }

    const auto modulation =
        processor_.velocityModulationForUi(playerIndex);
    const auto length = std::min(
        modulation.length, lps::VelocityModulation::maxLength);
    decreaseVelocityModulationLengthButtons_[playerIndex]->setEnabled(
        length > 1);
    increaseVelocityModulationLengthButtons_[playerIndex]->setEnabled(
        length < lps::VelocityModulation::maxLength);

    for (std::size_t step = 0;
         step < lps::VelocityModulation::maxLength;
         ++step)
    {
        const auto sliderIndex =
            playerIndex * lps::VelocityModulation::maxLength + step;
        if (sliderIndex >= velocityModulationSliders_.size())
            break;

        auto& slider = *velocityModulationSliders_[sliderIndex];
        const bool shouldBeVisible = step < length;
        if (slider.isVisible() != shouldBeVisible)
            slider.setVisible(shouldBeVisible);

        const auto value = static_cast<double>(modulation.values[step]);
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
        const bool modified = processor_.playerPatternModifiedForUi(playerIndex);
        if (!force && displayedPatternModified_[playerIndex] == modified)
            continue;

        displayedPatternModified_[playerIndex] = modified;
        auto& selector = *patternSelectors_[playerIndex];
        auto& saveButton = *savePatternButtons_[playerIndex];
        saveButton.setEnabled(modified);
        saveButton.setButtonText(modified ? "Save *" : "Save");

        if (modified)
        {
            selector.setColour(juce::ComboBox::outlineColourId, juce::Colour(amber));
            selector.setColour(juce::ComboBox::textColourId, juce::Colour(amber));
            selector.setColour(juce::ComboBox::arrowColourId, juce::Colour(amber));
            saveButton.setColour(
                juce::TextButton::buttonColourId,
                juce::Colour(amber).darker(0.45f));
        }
        else
        {
            selector.removeColour(juce::ComboBox::outlineColourId);
            selector.removeColour(juce::ComboBox::textColourId);
            selector.removeColour(juce::ComboBox::arrowColourId);
            saveButton.removeColour(juce::TextButton::buttonColourId);
        }

        selector.repaint();
        saveButton.repaint();
    }
}

void LivePatternSequencerEditor::updateVelocityModulationModifiedIndicators(
    bool force)
{
    for (std::size_t playerIndex = 0;
         playerIndex < saveVelocityModulationButtons_.size();
         ++playerIndex)
    {
        const bool modified =
            processor_.playerVelocityModulationModifiedForUi(playerIndex);
        if (!force
            && displayedVelocityModulationModified_[playerIndex] == modified)
        {
            continue;
        }

        displayedVelocityModulationModified_[playerIndex] = modified;
        auto& selector = *velocityModulationSelectors_[playerIndex];
        auto& saveButton = *saveVelocityModulationButtons_[playerIndex];
        saveButton.setEnabled(modified);
        saveButton.setButtonText(modified ? "Save *" : "Save");

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
        }
        else
        {
            selector.removeColour(juce::ComboBox::outlineColourId);
            selector.removeColour(juce::ComboBox::textColourId);
            selector.removeColour(juce::ComboBox::arrowColourId);
            saveButton.removeColour(juce::TextButton::buttonColourId);
        }

        selector.repaint();
        saveButton.repaint();
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
    const int y = static_cast<int>(playerIndex) * playerStride;
    constexpr int stepCount = static_cast<int>(lps::Pattern::maxLength);
    constexpr int width = stepCount * patternCellWidth
        + (stepCount - 1) * patternCellGap;
    return {
        patternGridLeft,
        y + (playerPanelHeight - patternCellHeight) / 2,
        width,
        patternCellHeight
    };
}

int LivePatternSequencerEditor::cellWidth() const
{
    return patternCellWidth;
}

int LivePatternSequencerEditor::requiredContentWidth() const
{
    constexpr int stepCount = static_cast<int>(lps::Pattern::maxLength);
    constexpr int gridWidth = stepCount * patternCellWidth
        + (stepCount - 1) * patternCellGap;
    constexpr int rightPadding = contentHorizontalPadding + playerPanelHorizontalPadding;
    return std::max(minimumPlayerContentWidth, patternGridLeft + gridWidth + rightPadding);
}

void LivePatternSequencerEditor::updateContentWidth()
{
    const bool needsVerticalScrollbar = content_.getHeight() > viewport_.getHeight();
    const int visiblePlayerWidth = viewport_.getWidth()
        - (needsVerticalScrollbar ? viewport_.getScrollBarThickness() : 0);
    const int width = std::max(requiredContentWidth(), visiblePlayerWidth);
    if (content_.getWidth() != width)
        content_.setSize(width, content_.getHeight());
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
            content_.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            updateDraggedRange(event.x);
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
    updateDraggedRange(event.x);
}

void LivePatternSequencerEditor::contentMouseUp(const juce::MouseEvent&)
{
    draggedRangeHandle_ = DraggedRangeHandle::none;
    draggedRangeHandleOffsetX_ = 0;
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
    auto bounds = content_.getLocalBounds().reduced(contentHorizontalPadding, 0);
    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        auto patternPanel = bounds.removeFromTop(playerPanelHeight);
        bounds.removeFromTop(playerLaneGap);
        auto velocityPanel = bounds.removeFromTop(velocityPanelHeight);
        bounds.removeFromTop(playerPanelGap);
        graphics.setColour(juce::Colour(panel));
        graphics.fillRoundedRectangle(patternPanel.toFloat(), 3.0f);
        graphics.setColour(juce::Colour(border));
        graphics.drawRoundedRectangle(patternPanel.toFloat(), 3.0f, 1.0f);

        const int infoX = patternPanel.getX() + playerPanelHorizontalPadding;
        graphics.setColour(juce::Colour(
            processor_.playerIsMasterForUi(playerIndex) ? amber : cyan));
        graphics.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        graphics.drawFittedText(
            processor_.playerNameForUi(playerIndex),
            infoX,
            patternPanel.getY() + 4,
            64,
            19,
            juce::Justification::centredLeft,
            1,
            0.75f);
        graphics.setColour(juce::Colour(secondaryText));
        graphics.setFont(juce::Font(juce::FontOptions(9.0f)));
        const auto midiLabel = "CH " + juce::String(processor_.drumMidiChannelForUi())
            + " / N " + juce::String(processor_.playerMidiNoteForUi(playerIndex));
        graphics.drawFittedText(
            midiLabel,
            infoX,
            patternPanel.getY() + 25,
            64,
            17,
            juce::Justification::centredLeft,
            1,
            0.75f);

        graphics.setColour(juce::Colour(border).withMultipliedAlpha(0.65f));
        graphics.drawVerticalLine(
            patternGridLeft - controlsToGridGap / 2,
            static_cast<float>(patternPanel.getY() + 5),
            static_cast<float>(patternPanel.getBottom() - 5));

        auto cellsArea = cellsAreaForPlayer(playerIndex);
        const auto pattern = processor_.patternForUi(playerIndex);
        const int currentStep = processor_.currentStepForUi(playerIndex);
        graphics.setFont(juce::Font(juce::FontOptions(8.5f, juce::Font::bold)));

        for (std::uint16_t step = 0;
             step < static_cast<std::uint16_t>(lps::Pattern::maxLength);
             ++step)
        {
            auto cell = cellsArea.removeFromLeft(patternCellWidth);
            const bool isCurrent = playing && static_cast<int>(step) == currentStep;
            const bool isHit = pattern.isHit(step);
            const bool isInsideWindow = pattern.isInsidePlaybackWindow(step);
            auto fillColour = juce::Colour(isHit ? cyan : inactive);
            if (!isInsideWindow)
                fillColour = fillColour.withMultipliedAlpha(0.28f);
            graphics.setColour(fillColour);
            graphics.fillRoundedRectangle(cell.toFloat(), 2.0f);
            const auto cellBorder = step % 4 == 0
                ? juce::Colour(border).brighter(0.22f)
                : juce::Colour(border);
            graphics.setColour(cellBorder);
            graphics.drawRoundedRectangle(cell.toFloat(), 2.0f, 1.0f);

            if (isCurrent)
            {
                const auto playheadBar = cell.reduced(3, 0).removeFromBottom(3);
                graphics.setColour(juce::Colour(amber));
                graphics.fillRoundedRectangle(playheadBar.toFloat(), 1.5f);
            }

            auto textColour = juce::Colour(isHit ? primaryText : secondaryText);
            if (!isInsideWindow)
                textColour = textColour.withMultipliedAlpha(0.38f);
            graphics.setColour(textColour);
            graphics.drawText(juce::String(static_cast<int>(step + 1)), cell,
                juce::Justification::centred);
            cellsArea.removeFromLeft(patternCellGap);
        }

        graphics.setColour(juce::Colour(panel));
        graphics.fillRoundedRectangle(velocityPanel.toFloat(), 3.0f);
        graphics.setColour(juce::Colour(border));
        graphics.drawRoundedRectangle(velocityPanel.toFloat(), 3.0f, 1.0f);

        const auto velocityModulation =
            processor_.velocityModulationForUi(playerIndex);
        graphics.setColour(juce::Colour(cyan));
        graphics.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        graphics.drawFittedText(
            "VELOCITY",
            velocityPanel.getX() + playerPanelHorizontalPadding,
            velocityPanel.getY() + 12,
            64,
            18,
            juce::Justification::centredLeft,
            1,
            0.75f);
        graphics.setColour(juce::Colour(secondaryText));
        graphics.setFont(juce::Font(juce::FontOptions(9.0f)));
        graphics.drawFittedText(
            juce::String(static_cast<int>(velocityModulation.length))
                + (velocityModulation.length == 1 ? " HIT" : " HITS"),
            velocityPanel.getX() + playerPanelHorizontalPadding,
            velocityPanel.getY() + 35,
            64,
            17,
            juce::Justification::centredLeft,
            1,
            0.75f);

        graphics.setColour(juce::Colour(border).withMultipliedAlpha(0.65f));
        graphics.drawVerticalLine(
            velocityGridLeft - controlsToGridGap / 2,
            static_cast<float>(velocityPanel.getY() + 6),
            static_cast<float>(velocityPanel.getBottom() - 6));

        const int currentVelocityStep =
            processor_.currentVelocityModulationStepForUi(playerIndex);
        if (playing
            && currentVelocityStep >= 0
            && static_cast<std::size_t>(currentVelocityStep)
                < velocityModulation.length)
        {
            const int knobX = velocityGridLeft
                + currentVelocityStep * (velocityKnobWidth + velocityKnobGap);
            const juce::Rectangle<int> playheadBar(
                knobX + 8,
                velocityPanel.getBottom() - 5,
                velocityKnobWidth - 16,
                3);
            graphics.setColour(juce::Colour(amber));
            graphics.fillRoundedRectangle(playheadBar.toFloat(), 1.5f);
        }

        if (pattern.stepCount == 0)
            continue;

        const auto bracketArea = cellsAreaForPlayer(playerIndex);
        const int pitch = patternCellWidth + patternCellGap;
        const int startX = bracketArea.getX() + static_cast<int>(pattern.playbackStart) * pitch;
        const int endX = bracketArea.getX()
            + static_cast<int>(pattern.playbackEnd) * pitch + patternCellWidth;
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
        graphics.strokePath(brackets, juce::PathStrokeType(2.0f));
    }
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
