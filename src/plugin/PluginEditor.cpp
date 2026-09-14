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
constexpr int perVoiceControlGap = 4;
constexpr int modulationPreviewWidth = 64;
constexpr int controlsToGridGap = 12;
constexpr int clickTargetSize = 41;
constexpr int minimumPatternPanelHeight = 50;
constexpr int voiceNameHeight = 12;
constexpr int controlUtilityBarHeight = clickTargetSize;
constexpr int modulationPanelHeight = clickTargetSize + 2;
constexpr int playerBottomPadding = 2;
constexpr int controlPanePadding = 8;
constexpr int controlPaneTopRowHeight = 48;
constexpr int controlPaneHeight = controlPanePadding * 2
    + controlPaneTopRowHeight
    + 3 * modulationPanelHeight
    + sectionGap;
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
    + playerPanelHorizontalPadding + clickTargetSize
    + perVoiceControlGap + clickTargetSize
    + perVoiceControlGap + clickTargetSize
    + perVoiceControlGap + clickTargetSize
    + perVoiceControlGap + modulationPreviewWidth
    + controlsToGridGap;
constexpr int modulationCellHeight = clickTargetSize;
constexpr int modulationCellGap = 2;

[[nodiscard]] int patternCellSizeForContentWidth(int contentWidth) noexcept
{
    constexpr int stepCount = static_cast<int>(lps::Pattern::maxLength);
    constexpr int rightPadding = contentHorizontalPadding
        + playerPanelHorizontalPadding;
    const int availableGridWidth = contentWidth - patternGridLeft - rightPadding;
    return juce::jlimit(
        minimumPatternCellSize,
        clickTargetSize,
        (availableGridWidth - (stepCount - 1) * patternCellGap) / stepCount);
}

[[nodiscard]] juce::String patternMenuLabel(
    std::size_t playerIndex, bool modified)
{
    return juce::String(static_cast<int>(playerIndex + 1))
        + (modified ? " *" : "");
}

using ModulationLane = LivePatternSequencerProcessor::ModulationLane;

constexpr std::array modulationLanes {
    ModulationLane::velocity,
    ModulationLane::pitch,
    ModulationLane::gate
};

[[nodiscard]] juce::String modulationLaneName(ModulationLane lane)
{
    switch (lane)
    {
        case ModulationLane::velocity: return "Velocity";
        case ModulationLane::pitch: return "Pitch";
        case ModulationLane::gate: return "Gate";
    }
    return "Modulation";
}

[[nodiscard]] juce::String modulationMenuLabel(
    ModulationLane lane, bool modified)
{
    return modulationLaneName(lane).toUpperCase()
        + (modified ? " *" : "");
}

class LaneMenuButton final : public juce::TextButton
{
public:
    enum class Kind { pattern, modulation };

    explicit LaneMenuButton(
        Kind kind,
        ModulationLane modulationLane = ModulationLane::velocity)
        : kind_(kind), modulationLane_(modulationLane)
    {
    }

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

        const bool compact = getWidth() <= clickTargetSize + 2;
        if (kind_ == Kind::pattern)
        {
            const int previewSteps = compact ? 4 : 6;
            const float gap = compact ? 1.0f : 2.0f;
            const float availableWidth = compact
                ? std::max(4.0f, static_cast<float>(getWidth()) - 8.0f)
                : 40.0f;
            const float cellSize = std::max(
                1.0f,
                std::min(
                    5.0f,
                    (availableWidth
                        - static_cast<float>(previewSteps - 1) * gap)
                        / static_cast<float>(previewSteps)));
            const float previewWidth = static_cast<float>(previewSteps)
                    * cellSize
                + static_cast<float>(previewSteps - 1) * gap;
            const float startX = compact
                ? (static_cast<float>(getWidth()) - previewWidth) * 0.5f
                : 8.0f;
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
            if (compact && getButtonText().containsChar('*'))
            {
                graphics.setColour(juce::Colour(uiYellow));
                graphics.fillEllipse(
                    static_cast<float>(getWidth()) - 6.0f,
                    3.0f,
                    3.0f,
                    3.0f);
            }
        }
        else
        {
            const float left = compact ? 3.0f : 8.0f;
            const float right = compact
                ? std::max(left + 4.0f, static_cast<float>(getWidth()) - 3.0f)
                : 30.0f;
            const float top = 4.0f;
            const float bottom = std::max(
                top + 4.0f, static_cast<float>(getHeight()) - 5.0f);
            const float iconWidth = right - left;
            const float iconHeight = bottom - top;
            const float strokeWidth = std::max(
                1.0f, std::min(2.2f, iconWidth * 0.09f));
            graphics.setColour(juce::Colour(warmIvory));
            switch (modulationLane_)
            {
                case ModulationLane::velocity:
                {
                    constexpr std::array<float, 4> proportions {
                        0.35f, 0.74f, 0.52f, 1.0f
                    };
                    const float slotWidth = iconWidth
                        / static_cast<float>(proportions.size());
                    const float barWidth = std::max(1.0f, slotWidth * 0.58f);
                    for (std::size_t index = 0;
                         index < proportions.size();
                         ++index)
                    {
                        const float height = std::max(
                            1.0f, iconHeight * proportions[index]);
                        const float x = left
                            + static_cast<float>(index) * slotWidth
                            + (slotWidth - barWidth) * 0.5f;
                        graphics.setColour(juce::Colour(
                            index == proportions.size() - 1
                                ? uiYellow : warmIvory));
                        graphics.fillRoundedRectangle(
                            x,
                            bottom - height,
                            barWidth,
                            height,
                            std::min(1.2f, barWidth * 0.3f));
                    }
                    break;
                }

                case ModulationLane::pitch:
                {
                    juce::Path contour;
                    contour.startNewSubPath(left, bottom - iconHeight * 0.12f);
                    contour.lineTo(
                        left + iconWidth * 0.28f,
                        bottom - iconHeight * 0.43f);
                    contour.lineTo(
                        left + iconWidth * 0.60f,
                        top + iconHeight * 0.08f);
                    contour.lineTo(right, bottom - iconHeight * 0.48f);
                    graphics.strokePath(
                        contour,
                        juce::PathStrokeType(
                            strokeWidth,
                            juce::PathStrokeType::curved,
                            juce::PathStrokeType::rounded));
                    graphics.setColour(juce::Colour(uiYellow));
                    const float markerSize = std::max(
                        2.0f, std::min(6.0f, iconWidth * 0.22f));
                    graphics.fillEllipse(
                        left + iconWidth * 0.60f - markerSize * 0.5f,
                        top + iconHeight * 0.08f - markerSize * 0.5f,
                        markerSize,
                        markerSize);
                    break;
                }

                case ModulationLane::gate:
                {
                    juce::Path pulse;
                    pulse.startNewSubPath(left, bottom);
                    pulse.lineTo(left + iconWidth * 0.18f, bottom);
                    pulse.lineTo(left + iconWidth * 0.18f, top);
                    pulse.lineTo(left + iconWidth * 0.72f, top);
                    pulse.lineTo(left + iconWidth * 0.72f, bottom);
                    pulse.lineTo(right, bottom);
                    graphics.strokePath(
                        pulse,
                        juce::PathStrokeType(
                            strokeWidth,
                            juce::PathStrokeType::mitered,
                            juce::PathStrokeType::butt));
                    graphics.setColour(juce::Colour(uiYellow));
                    graphics.fillRect(
                        left + iconWidth * 0.36f,
                        top + iconHeight * 0.20f,
                        std::max(2.0f, iconWidth * 0.22f),
                        std::max(1.0f, iconHeight * 0.12f));
                    break;
                }
            }
        }

        if (!compact)
        {
            graphics.setColour(juce::Colour(primaryText));
            const bool isPattern = kind_ == Kind::pattern;
            graphics.setFont(juce::Font(
                juce::FontOptions(
                    isPattern ? 13.0f : 10.0f,
                    juce::Font::bold)));
            graphics.drawText(
                getButtonText(),
                isPattern ? 53 : 36,
                0,
                getWidth() - (isPattern ? 58 : 39),
                getHeight(),
                juce::Justification::centred);
        }
    }

private:
    Kind kind_;
    ModulationLane modulationLane_;
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
        const auto bounds = getLocalBounds();
        if (selected_)
        {
            graphics.setColour(juce::Colour(uiBlue).withAlpha(0.22f));
            graphics.fillRect(bounds);
            graphics.setColour(juce::Colour(uiBlue));
            graphics.drawRect(bounds.reduced(1), 2);
        }
        else if (isItemHighlighted())
        {
            graphics.fillAll(juce::Colour(uiBlue).withAlpha(0.3f));
        }

        graphics.setOpacity(selected_ ? 1.0f
            : (isItemHighlighted() ? 0.82f : 0.50f));

        const int stepCount = juce::jlimit(
            1, static_cast<int>(lps::Pattern::maxLength),
            static_cast<int>(pattern_.length));
        constexpr int gap = 2;
        constexpr int horizontalPadding = 10;
        const int availableWidth = getWidth() - horizontalPadding * 2;
        const int cellSize = juce::jlimit(
            6, 20, (availableWidth - gap * (stepCount - 1)) / stepCount);
        const int previewWidth = stepCount * cellSize + (stepCount - 1) * gap;
        int x = horizontalPadding;
        const int y = (getHeight() - cellSize) / 2;

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
                horizontalPadding + previewWidth,
                static_cast<float>(y),
                static_cast<float>(y + cellSize));
        }
    }

private:
    lps::Pattern pattern_;
    bool selected_ = false;
};

class ModulationPreviewMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    ModulationPreviewMenuItem(lps::Modulation modulation, bool selected)
        : juce::PopupMenu::CustomComponent(true),
          modulation_(modulation),
          selected_(selected)
    {
    }

    void getIdealSize(int& idealWidth, int& idealHeight) override
    {
        idealWidth = 310;
        idealHeight = 76;
    }

    void paint(juce::Graphics& graphics) override
    {
        if (isItemHighlighted())
            graphics.fillAll(juce::Colour(uiBlue).withAlpha(0.3f));

        if (selected_)
        {
            graphics.setColour(juce::Colour(uiBlue));
            graphics.fillRect(2, 4, 4, getHeight() - 8);
        }

        const juce::Rectangle<float> graphBounds {
            12.0f,
            7.0f,
            static_cast<float>(getWidth()) - 24.0f,
            36.0f
        };

        const auto valueCount = juce::jlimit(
            1,
            static_cast<int>(lps::Modulation::maxLength),
            static_cast<int>(modulation_.length));
        std::array<juce::Point<float>, lps::Modulation::maxLength> points;
        for (int index = 0; index < valueCount; ++index)
        {
            const float proportion = valueCount == 1
                ? 0.5f
                : static_cast<float>(index) / static_cast<float>(valueCount - 1);
            points[static_cast<std::size_t>(index)] = {
                graphBounds.getX() + graphBounds.getWidth() * proportion,
                graphBounds.getBottom()
                    - graphBounds.getHeight()
                        * modulation_.values[static_cast<std::size_t>(index)].toFloat()
            };
        }

        juce::Path curve;
        if (valueCount > 1)
        {
            curve.startNewSubPath(points[0]);
            for (int index = 1; index < valueCount; ++index)
            {
                const auto previous = points[static_cast<std::size_t>(index - 1)];
                const auto current = points[static_cast<std::size_t>(index)];
                const float controlDistance = (current.getX() - previous.getX()) / 3.0f;
                curve.cubicTo(
                    previous.getX() + controlDistance, previous.getY(),
                    current.getX() - controlDistance, current.getY(),
                    current.getX(), current.getY());
            }
        }

        if (valueCount > 1)
        {
            graphics.setColour(juce::Colour(warmIvory));
            graphics.strokePath(
                curve,
                juce::PathStrokeType(
                    2.2f,
                    juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
        }

        graphics.setColour(juce::Colour(uiYellow));
        for (int index = 0; index < valueCount; ++index)
        {
            const auto point = points[static_cast<std::size_t>(index)];
            graphics.fillEllipse(
                point.getX() - 2.5f, point.getY() - 2.5f, 5.0f, 5.0f);
        }

        graphics.setColour(juce::Colour(primaryText));
        graphics.setFont(juce::Font(juce::FontOptions(8.0f)));
        constexpr int labelWidth = 22;
        constexpr int labelHeight = 12;
        for (int index = 0; index < valueCount; ++index)
        {
            const auto point = points[static_cast<std::size_t>(index)];
            const auto value = modulation_.values[static_cast<std::size_t>(index)].raw
                / 257u;
            const int labelY = 46 + (index % 2) * labelHeight;
            graphics.drawFittedText(
                juce::String(static_cast<int>(value)),
                juce::roundToInt(point.getX()) - labelWidth / 2,
                labelY,
                labelWidth,
                labelHeight,
                juce::Justification::centred,
                1,
                0.7f);
        }
    }

private:
    lps::Modulation modulation_;
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

        const float labelSize = juce::jlimit(
            8.0f,
            20.0f,
            static_cast<float>(getHeight()) * 0.55f);
        graphics.setFont(juce::Font(
            juce::FontOptions(labelSize, juce::Font::bold)));
        graphics.drawText(
            "M",
            getLocalBounds(),
            juce::Justification::centred,
            false);
    }
};

class ModulationSliderPopup final : public juce::Component
{
public:
    explicit ModulationSliderPopup(juce::Slider& target)
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

LivePatternSequencerEditor::ControlPaneComponent::ControlPaneComponent(
    LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setOpaque(true);
}

void LivePatternSequencerEditor::ControlPaneComponent::paint(
    juce::Graphics& graphics)
{
    editor_.paintControlPane(graphics);
}

void LivePatternSequencerEditor::ControlPaneComponent::resized()
{
    editor_.resizedControlPane();
}

void LivePatternSequencerEditor::PagedViewport::mouseWheelMove(
    const juce::MouseEvent&,
    const juce::MouseWheelDetails&)
{
    // Voice navigation is deliberately page-based. The fixed navigation
    // buttons keep every transition aligned to a completely new set of rows.
}

void LivePatternSequencerEditor::SpeedSelector::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    const float centreX = bounds.getCentreX();
    const float centreY = bounds.getY() + bounds.getHeight() * 0.64f;
    const float radius = std::max(
        3.0f, std::min(bounds.getWidth(), bounds.getHeight()) * 0.30f);
    juce::Path gauge;
    constexpr int segmentCount = 12;
    for (int segment = 0; segment <= segmentCount; ++segment)
    {
        const float proportion = static_cast<float>(segment)
            / static_cast<float>(segmentCount);
        const float angle = juce::degreesToRadians(-65.0f + 130.0f * proportion);
        const juce::Point<float> point {
            centreX + std::sin(angle) * radius,
            centreY - std::cos(angle) * radius
        };
        if (segment == 0)
            gauge.startNewSubPath(point);
        else
            gauge.lineTo(point);
    }
    graphics.setColour(juce::Colour(warmIvory));
    graphics.strokePath(
        gauge,
        juce::PathStrokeType(
            std::max(1.0f, bounds.getWidth() * 0.055f),
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

    const int selectedSpeed = getSelectedItemIndex();
    graphics.setColour(juce::Colour(uiYellow));
    if (selectedSpeed >= 0 && selectedSpeed < 3)
    {
        const float needleAngle = juce::degreesToRadians(
            -55.0f + 55.0f * static_cast<float>(selectedSpeed));
        graphics.drawLine(
            centreX,
            centreY,
            centreX + std::sin(needleAngle) * radius * 0.82f,
            centreY - std::cos(needleAngle) * radius * 0.82f,
            std::max(1.0f, bounds.getWidth() * 0.06f));
        graphics.fillEllipse(
            centreX - 1.7f, centreY - 1.7f, 3.4f, 3.4f);
    }
    else
    {
        graphics.fillEllipse(
            centreX - 1.7f, centreY - 1.7f, 3.4f, 3.4f);
    }
}

LivePatternSequencerEditor::MatrixComponent::MatrixComponent(
    LivePatternSequencerEditor& editor) noexcept
    : juce::PopupMenu::CustomComponent(false),
      editor_(editor)
{
    setOpaque(true);
}

void LivePatternSequencerEditor::MatrixComponent::getIdealSize(
    int& idealWidth,
    int& idealHeight)
{
    idealWidth = editor_.matrixPanelWidth();
    idealHeight = matrixGridTop
        + static_cast<int>(editor_.playerCount_) * matrixCellHeight + 10;
}

void LivePatternSequencerEditor::MatrixComponent::paint(juce::Graphics& graphics)
{
    editor_.paintMatrix(graphics, *this);
}

void LivePatternSequencerEditor::MatrixComponent::mouseDown(
    const juce::MouseEvent& event)
{
    editor_.matrixMouseDown(event, *this);
}

LivePatternSequencerEditor::ModulationCell::ModulationCell()
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setRange(0.0, 255.0, 1.0);
    setNumDecimalPlacesToDisplay(0);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setScrollWheelEnabled(false);
    setMouseDragSensitivity(180);
    setWantsKeyboardFocus(true);
}

void LivePatternSequencerEditor::ModulationCell::setActive(bool shouldBeActive)
{
    if (active_ == shouldBeActive)
        return;
    active_ = shouldBeActive;
    repaint();
}

void LivePatternSequencerEditor::ModulationCell::paint(
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

void LivePatternSequencerEditor::ModulationCell::mouseDown(
    const juce::MouseEvent& event)
{
    auto* editor = findParentComponentOfClass<LivePatternSequencerEditor>();
    if (editor == nullptr)
        return;

    activePopup_.reset();

    juce::Slider::mouseDown(event);
    auto popup = std::make_unique<ModulationSliderPopup>(*this);
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

void LivePatternSequencerEditor::ModulationCell::mouseDrag(
    const juce::MouseEvent& event)
{
    juce::Slider::mouseDrag(event);
}

void LivePatternSequencerEditor::ModulationCell::mouseUp(
    const juce::MouseEvent& event)
{
    juce::Slider::mouseUp(event);
    activePopup_.reset();
}

LivePatternSequencerEditor::UtilityButton::UtilityButton(Kind kind) noexcept
    : kind_(kind)
{
}

void LivePatternSequencerEditor::UtilityButton::paintButton(
    juce::Graphics& graphics,
    bool isMouseOverButton,
    bool isButtonDown)
{
    const bool engaged = getToggleState() || isButtonDown;
    auto face = findColour(engaged
        ? juce::TextButton::buttonOnColourId
        : juce::TextButton::buttonColourId);
    if (isMouseOverButton && isEnabled())
        face = face.brighter(0.08f);

    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(face.withMultipliedAlpha(isEnabled() ? 1.0f : 0.45f));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(juce::Colour(border).withMultipliedAlpha(
        isEnabled() ? 1.0f : 0.45f));
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    const auto iconBounds = bounds.reduced(bounds.getWidth() * 0.27f);
    auto iconColour = findColour(engaged
        ? juce::TextButton::textColourOnId
        : juce::TextButton::textColourOffId);
    graphics.setColour(iconColour.withMultipliedAlpha(
        isEnabled() ? 1.0f : 0.45f));

    if (kind_ == Kind::play)
    {
        juce::Path play;
        play.addTriangle(iconBounds.getX(), iconBounds.getY(),
            iconBounds.getX(), iconBounds.getBottom(),
            iconBounds.getRight(), iconBounds.getCentreY());
        graphics.fillPath(play);
    }
    else if (kind_ == Kind::pause)
    {
        const float barWidth = iconBounds.getWidth() * 0.28f;
        graphics.fillRoundedRectangle(
            iconBounds.getX(), iconBounds.getY(), barWidth,
            iconBounds.getHeight(), 1.0f);
        graphics.fillRoundedRectangle(
            iconBounds.getRight() - barWidth, iconBounds.getY(), barWidth,
            iconBounds.getHeight(), 1.0f);
    }
    else if (kind_ == Kind::matrix)
    {
        constexpr float gap = 2.0f;
        const float cellWidth = (iconBounds.getWidth() - gap) * 0.5f;
        const float cellHeight = (iconBounds.getHeight() - gap) * 0.5f;
        for (int row = 0; row < 2; ++row)
        {
            for (int column = 0; column < 2; ++column)
            {
                graphics.drawRoundedRectangle(
                    iconBounds.getX() + column * (cellWidth + gap),
                    iconBounds.getY() + row * (cellHeight + gap),
                    cellWidth, cellHeight, 1.0f, 1.5f);
            }
        }
    }
    else
    {
        const bool pointsRight = kind_ == Kind::nextPage;
        juce::Path arrow;
        const float outerX = pointsRight
            ? iconBounds.getX() : iconBounds.getRight();
        const float pointX = pointsRight
            ? iconBounds.getRight() : iconBounds.getX();
        arrow.startNewSubPath(outerX, iconBounds.getY());
        arrow.lineTo(pointX, iconBounds.getCentreY());
        arrow.lineTo(outerX, iconBounds.getBottom());
        graphics.strokePath(arrow, juce::PathStrokeType(
            juce::jmax(1.5f, getWidth() * 0.065f),
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }
}

LivePatternSequencerEditor::LivePatternSequencerEditor(
    LivePatternSequencerProcessor& processorToEdit)
    : juce::AudioProcessorEditor(processorToEdit),
      processor_(processorToEdit),
      playerCount_(processorToEdit.playerCountForUi()),
      content_(*this),
      controlPane_(*this),
      suppressionButton_(UtilityButton::Kind::matrix),
      previousPageButton_(UtilityButton::Kind::previousPage),
      nextPageButton_(UtilityButton::Kind::nextPage),
      globalPlayButton_(UtilityButton::Kind::play),
      globalStopButton_(UtilityButton::Kind::pause)
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
    patternSelectors_.reserve(playerCount_);
    savePatternButtons_.reserve(playerCount_);
    resetToMasterButtons_.reserve(playerCount_);
    speedSelectors_.reserve(playerCount_);
    offsetLeftButtons_.reserve(playerCount_);
    offsetRightButtons_.reserve(playerCount_);
    muteButtons_.reserve(playerCount_);
    voiceSelectButtons_.reserve(playerCount_);
    for (const auto lane : modulationLanes)
    {
        const auto laneSlot = laneIndex(lane);
        modulationMenuButtons_[laneSlot].reserve(playerCount_);
        modulationSelectors_[laneSlot].reserve(playerCount_);
        saveModulationButtons_[laneSlot].reserve(playerCount_);
        decreaseModulationLengthButtons_[laneSlot].reserve(playerCount_);
        increaseModulationLengthButtons_[laneSlot].reserve(playerCount_);
        modulationSliders_[laneSlot].reserve(
            playerCount_ * lps::Modulation::maxLength);
    }

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        const bool supportsPattern =
            processor_.playerSupportsPatternEditingForUi(playerIndex);

        voiceSelectButtons_.push_back(std::make_unique<juce::TextButton>());
        auto& voiceButton = *voiceSelectButtons_.back();
        auto voiceName = processor_.playerNameForUi(playerIndex);
        if (voiceName.isEmpty())
            voiceName = "VOICE " + juce::String(static_cast<int>(playerIndex + 1));
        voiceButton.setButtonText(
            juce::String(static_cast<int>(playerIndex + 1)));
        voiceButton.setClickingTogglesState(true);
        voiceButton.setToggleState(playerIndex == 0, juce::dontSendNotification);
        voiceButton.setTooltip(
            "Select only " + voiceName
            + "; hold Shift or Command/Ctrl to add or remove voices");
        voiceButton.onClick = [this, playerIndex]
        {
            const auto modifiers =
                juce::ComponentPeer::getCurrentModifiersRealtime();
            selectVoice(
                playerIndex,
                modifiers.isShiftDown() || modifiers.isCommandDown());
        };
        content_.addAndMakeVisible(voiceButton);

        patternMenuButtons_.push_back(std::make_unique<LaneMenuButton>(
            LaneMenuButton::Kind::pattern));
        auto& patternMenuButton = *patternMenuButtons_.back();
        patternMenuButton.setButtonText(patternMenuLabel(playerIndex, false));
        patternMenuButton.setTooltip("Choose a pattern for this voice");
        patternMenuButton.onClick = [this, playerIndex]
        {
            selectVoice(playerIndex, false);
            showPatternMenu(
                playerIndex, patternMenuButtons_[playerIndex].get());
        };
        content_.addAndMakeVisible(patternMenuButton);

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
        resetButton.setButtonText(isMaster ? "—" : "R");
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
        muteButton.setClickingTogglesState(false);
        muteButton.setToggleState(
            processor_.playerMutedForUi(playerIndex),
            juce::dontSendNotification);
        muteButton.setTooltip(
            "Toggle mute for " + processor_.playerNameForUi(playerIndex)
            + " while its sequence keeps running");
        muteButton.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour(muteRed));
        muteButton.setColour(
            juce::TextButton::textColourOnId,
            juce::Colour(muteRedDark));
        muteButton.onClick = [this, playerIndex]
        {
            const bool shouldMute =
                !processor_.playerMutedForUi(playerIndex);
            processor_.setPlayerMuted(playerIndex, shouldMute);
            muteButtons_[playerIndex]->setToggleState(
                shouldMute, juce::dontSendNotification);
            content_.repaint();
        };
        content_.addAndMakeVisible(muteButton);

        for (const auto lane : modulationLanes)
        {
            const auto laneSlot = laneIndex(lane);
            const bool supportsModulation =
                processor_.playerSupportsModulationEditingForUi(
                    playerIndex, lane);
            const auto laneName = modulationLaneName(lane);

            modulationMenuButtons_[laneSlot].push_back(
                std::make_unique<LaneMenuButton>(
                    LaneMenuButton::Kind::modulation,
                    lane));
            auto& menuButton = *modulationMenuButtons_[laneSlot].back();
            menuButton.setButtonText(modulationMenuLabel(lane, false));
            menuButton.setTooltip("Choose or save a " + laneName
                + " modulation");
            menuButton.onClick = [this, playerIndex, lane]
            {
                showModulationMenu(playerIndex, lane);
            };
            content_.addAndMakeVisible(menuButton);

            modulationSelectors_[laneSlot].push_back(
                std::make_unique<juce::ComboBox>());
            auto& modulationSelector =
                *modulationSelectors_[laneSlot].back();
            for (std::size_t modulationIndex = 0;
                 modulationIndex < processor_.modulationCountForUi();
                 ++modulationIndex)
            {
                modulationSelector.addItem(
                    processor_.modulationNameForUi(modulationIndex),
                    static_cast<int>(modulationIndex + 1));
            }
            modulationSelector.setSelectedItemIndex(
                static_cast<int>(processor_.selectedModulationForPlayer(
                    playerIndex, lane)),
                juce::dontSendNotification);
            modulationSelector.setTooltip(
                "Choose a " + laneName
                + " modulation independently of the hit pattern");
            modulationSelector.onChange =
                [this, playerIndex, lane, laneSlot]
                {
                    const auto selected =
                        modulationSelectors_[laneSlot][playerIndex]
                            ->getSelectedItemIndex();
                    if (selected >= 0)
                        processor_.selectModulationForPlayer(
                            playerIndex,
                            lane,
                            static_cast<std::size_t>(selected));
                };

            saveModulationButtons_[laneSlot].push_back(
                std::make_unique<juce::TextButton>());
            auto& modulationSaveButton =
                *saveModulationButtons_[laneSlot].back();
            modulationSaveButton.setButtonText("Save");
            modulationSaveButton.setTooltip(
                "Save these values to the persistent modulation library");
            modulationSaveButton.onClick = [this, playerIndex, lane]
            {
                beginSavePlayerModulation(playerIndex, lane);
            };

            decreaseModulationLengthButtons_[laneSlot].push_back(
                std::make_unique<juce::TextButton>());
            auto& decreaseLengthButton =
                *decreaseModulationLengthButtons_[laneSlot].back();
            decreaseLengthButton.setButtonText("-");
            decreaseLengthButton.setTooltip(
                "Remove the last " + laneName + " modulation step");
            decreaseLengthButton.onClick = [this, playerIndex, lane]
            {
                const auto modulation =
                    processor_.modulationForUi(playerIndex, lane);
                if (modulation.length > 1)
                {
                    processor_.setPlayerModulationLength(
                        playerIndex, lane, modulation.length - 1);
                    refreshModulationControls(playerIndex, lane, true);
                    updateModulationModifiedIndicators(true);
                    content_.repaint();
                }
            };
            content_.addAndMakeVisible(decreaseLengthButton);

            increaseModulationLengthButtons_[laneSlot].push_back(
                std::make_unique<juce::TextButton>());
            auto& increaseLengthButton =
                *increaseModulationLengthButtons_[laneSlot].back();
            increaseLengthButton.setButtonText("+");
            increaseLengthButton.setTooltip(
                "Append a " + laneName + " modulation step");
            increaseLengthButton.onClick = [this, playerIndex, lane]
            {
                const auto modulation =
                    processor_.modulationForUi(playerIndex, lane);
                if (modulation.length < lps::Modulation::maxLength)
                {
                    processor_.setPlayerModulationLength(
                        playerIndex,
                        lane,
                        std::max<std::size_t>(1, modulation.length + 1));
                    refreshModulationControls(playerIndex, lane, true);
                    updateModulationModifiedIndicators(true);
                    content_.repaint();
                }
            };
            content_.addAndMakeVisible(increaseLengthButton);

            const auto modulation =
                processor_.modulationForUi(playerIndex, lane);
            for (std::size_t step = 0;
                 step < lps::Modulation::maxLength;
                 ++step)
            {
                modulationSliders_[laneSlot].push_back(
                    std::make_unique<ModulationCell>());
                const auto sliderIndex =
                    modulationSliders_[laneSlot].size() - 1;
                auto& slider = *modulationSliders_[laneSlot].back();
                slider.setName(laneName + " modulation step "
                    + juce::String(static_cast<int>(step + 1)));
                slider.setTooltip(laneName + " step "
                    + juce::String(static_cast<int>(step + 1))
                    + " (0-255)");
                slider.setDoubleClickReturnValue(true, 255.0);
                slider.setValue(
                    static_cast<double>(modulation.values[step].raw / 257u),
                    juce::dontSendNotification);
                slider.onValueChange =
                    [this, playerIndex, lane, laneSlot, step, sliderIndex]
                    {
                        const auto value = juce::jlimit(
                            0,
                            255,
                            juce::roundToInt(
                                modulationSliders_[laneSlot][sliderIndex]
                                    ->getValue()));
                        processor_.setPlayerModulationValue(
                            playerIndex,
                            lane,
                            step,
                            static_cast<std::uint8_t>(value));
                        updateModulationModifiedIndicators(true);
                        content_.repaint();
                    };
                content_.addAndMakeVisible(slider);
                slider.setVisible(
                    supportsModulation && step < modulation.length);
            }

            menuButton.setVisible(supportsModulation);
            decreaseLengthButton.setVisible(supportsModulation);
            increaseLengthButton.setVisible(supportsModulation);
        }

        patternMenuButton.setVisible(supportsPattern);
        offsetLeftButton.setVisible(supportsPattern);
        offsetRightButton.setVisible(supportsPattern);
    }

    selectedVoices_.assign(playerCount_, false);
    if (playerCount_ > 0)
        selectedVoices_[0] = true;

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        patternMenuButtons_[playerIndex]->setVisible(
            processor_.playerSupportsPatternEditingForUi(playerIndex));
        offsetLeftButtons_[playerIndex]->setVisible(false);
        offsetRightButtons_[playerIndex]->setVisible(false);
        for (const auto lane : modulationLanes)
        {
            const auto laneSlot = laneIndex(lane);
            modulationMenuButtons_[laneSlot][playerIndex]->setVisible(false);
            decreaseModulationLengthButtons_[laneSlot][playerIndex]->setVisible(false);
            increaseModulationLengthButtons_[laneSlot][playerIndex]->setVisible(false);
            for (std::size_t step = 0; step < lps::Modulation::maxLength; ++step)
            {
                modulationSliders_[laneSlot]
                    [playerIndex * lps::Modulation::maxLength + step]
                        ->setVisible(false);
            }
        }
    }

    displayedPatternModified_.assign(playerCount_, false);
    for (const auto lane : modulationLanes)
        displayedModulationModified_[laneIndex(lane)].assign(
            playerCount_, false);
    displayedResetToMasterPending_.assign(playerCount_, false);
    updatePatternModifiedIndicators(true);
    updateModulationModifiedIndicators(true);
    updateResetToMasterIndicators(true);

    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(false, false);
    viewport_.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    viewport_.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport_);

    controlPatternMenuButton_ = std::make_unique<LaneMenuButton>(
        LaneMenuButton::Kind::pattern);
    controlPatternMenuButton_->setButtonText("PATTERN");
    controlPatternMenuButton_->setTooltip(
        "Choose a pattern for every selected voice or save the primary voice");
    controlPatternMenuButton_->onClick = [this]
    {
        const auto patternPlayerIndex = controlPatternPlayerIndex();
        if (patternPlayerIndex < playerCount_)
            showPatternMenu(
                patternPlayerIndex, controlPatternMenuButton_.get());
    };
    controlPane_.addAndMakeVisible(*controlPatternMenuButton_);

    controlSpeedSelector_.addItem("0.5x", 1);
    controlSpeedSelector_.addItem("1x", 2);
    controlSpeedSelector_.addItem("2x", 3);
    controlSpeedSelector_.setTooltip(
        "Set playback speed for every selected pattern voice");
    controlSpeedSelector_.onChange = [this]
    {
        const auto selected = controlSpeedSelector_.getSelectedItemIndex();
        if (selected < 0)
            return;
        applyToSelectedPatternPlayers([this, selected](std::size_t playerIndex)
        {
            processor_.setPlayerPlaybackSpeed(
                playerIndex, static_cast<std::size_t>(selected));
        });
    };
    controlPane_.addAndMakeVisible(controlSpeedSelector_);

    controlResetButton_.setButtonText("R");
    controlResetButton_.setTooltip(
        "Queue every eligible selected voice to restart with the master");
    controlResetButton_.onClick = [this]
    {
        applyToSelectedPlayers([this](std::size_t playerIndex)
        {
            if (processor_.playerCanResetToMasterForUi(playerIndex)
                && !processor_.playerIsMasterForUi(playerIndex))
            {
                (void) processor_.resetPlayerToMaster(playerIndex);
            }
        });
        updateResetToMasterIndicators(true);
        refreshSelectedControls(true);
    };
    controlPane_.addAndMakeVisible(controlResetButton_);

    controlShiftLeftButton_.setButtonText("<");
    controlShiftLeftButton_.setTooltip(
        "Rotate every selected pattern left by one step");
    controlShiftLeftButton_.onClick = [this]
    {
        applyToSelectedPatternPlayers([this](std::size_t playerIndex)
        {
            processor_.offsetPlayerPatternLeft(playerIndex);
        });
        content_.repaint();
    };
    controlPane_.addAndMakeVisible(controlShiftLeftButton_);

    controlShiftRightButton_.setButtonText(">");
    controlShiftRightButton_.setTooltip(
        "Rotate every selected pattern right by one step");
    controlShiftRightButton_.onClick = [this]
    {
        applyToSelectedPatternPlayers([this](std::size_t playerIndex)
        {
            processor_.offsetPlayerPatternRight(playerIndex);
        });
        content_.repaint();
    };
    controlPane_.addAndMakeVisible(controlShiftRightButton_);

    controlMuteButton_.setButtonText("M");
    controlMuteButton_.setClickingTogglesState(true);
    controlMuteButton_.setTooltip(
        "Mute every selected voice; press again with the same selection to "
        "unmute them");
    controlMuteButton_.setColour(
        juce::TextButton::buttonColourId,
        juce::Colour(muteRed).darker(0.35f));
    controlMuteButton_.setColour(
        juce::TextButton::buttonOnColourId, juce::Colour(muteRed));
    controlMuteButton_.setColour(
        juce::TextButton::textColourOffId, juce::Colour(muteRedDark));
    controlMuteButton_.setColour(
        juce::TextButton::textColourOnId, juce::Colour(muteRedDark));
    controlMuteButton_.onClick = [this]
    {
        const bool shouldMute = controlMuteButton_.getToggleState();
        applyToSelectedPlayers([this, shouldMute](std::size_t playerIndex)
        {
            processor_.setPlayerMuted(playerIndex, shouldMute);
        });
        content_.repaint();
    };
    controlPane_.addAndMakeVisible(controlMuteButton_);

    controlUnmuteButton_.setButtonText("U");
    controlUnmuteButton_.setTooltip("Unmute every selected voice");
    controlUnmuteButton_.setColour(
        juce::TextButton::buttonColourId, juce::Colour(uiBlue).darker(0.35f));
    controlUnmuteButton_.onClick = [this]
    {
        applyToSelectedPlayers([this](std::size_t playerIndex)
        {
            processor_.setPlayerMuted(playerIndex, false);
        });
        controlMuteButton_.setToggleState(false, juce::dontSendNotification);
        content_.repaint();
    };
    controlPane_.addAndMakeVisible(controlUnmuteButton_);

    selectionLabel_.setFont(juce::Font(
        juce::FontOptions(13.0f, juce::Font::bold)));
    selectionLabel_.setJustificationType(juce::Justification::centredLeft);
    controlPane_.addAndMakeVisible(selectionLabel_);

    previousPageButton_.setTooltip(
        "Show the previous full page of voices");
    previousPageButton_.onClick = [this] { pageVoices(-1); };
    addAndMakeVisible(previousPageButton_);

    nextPageButton_.setTooltip(
        "Show the next completely new page of voices");
    nextPageButton_.onClick = [this] { pageVoices(1); };
    addAndMakeVisible(nextPageButton_);

    globalPlayButton_.setTooltip(
        "Start the internal audition clock from REAPER's current cursor; "
        "REAPER's own transport remains the authority");
    globalPlayButton_.setColour(
        juce::TextButton::buttonColourId, juce::Colour(uiBlue).darker(0.35f));
    globalPlayButton_.onClick = [this]
    {
        processor_.setInternalTransportPlayingForUi(true);
    };
    addAndMakeVisible(globalPlayButton_);

    globalStopButton_.setTooltip(
        "Pause the internal audition clock; pause REAPER from its host "
        "transport");
    globalStopButton_.setColour(
        juce::TextButton::buttonColourId, juce::Colour(muteRed));
    globalStopButton_.setColour(
        juce::TextButton::textColourOffId, juce::Colour(muteRedDark));
    globalStopButton_.setEnabled(false);
    globalStopButton_.onClick = [this]
    {
        processor_.setInternalTransportPlayingForUi(false);
    };
    addAndMakeVisible(globalStopButton_);

    suppressionButton_.setTooltip("Open or close the suppression matrix");
    suppressionButton_.setClickingTogglesState(true);
    suppressionButton_.onClick = [this] { showSuppressionMatrix(); };
    addAndMakeVisible(suppressionButton_);
    addAndMakeVisible(controlPane_);

    refreshSelectedControls(true);

    content_.setSize(requiredContentWidth(), 1);

    constexpr int initialEditorHeight = 860;
    constexpr int editorWidth = 1466;
    setResizable(true, false);
    setResizeLimits(800, 400, 4096, 2160);
    setSize(editorWidth, initialEditorHeight);
    startTimerHz(30);

    const auto catalogError = processor_.patternCatalogErrorForUi();
    if (catalogError.isNotEmpty())
        showPatternLibraryWarning(catalogError);

    const auto modulationCatalogError =
        processor_.modulationCatalogErrorForUi();
    if (modulationCatalogError.isNotEmpty())
        showModulationLibraryWarning(modulationCatalogError);
}

LivePatternSequencerEditor::~LivePatternSequencerEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
    viewport_.setViewedComponent(nullptr, false);
}

void LivePatternSequencerEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
}

void LivePatternSequencerEditor::resized()
{
    auto bounds = getLocalBounds().reduced(outerPadding);
    bounds.removeFromTop(headerHeight + sectionGap);

    auto controls = bounds.removeFromBottom(controlPaneHeight);
    bounds.removeFromBottom(sectionGap);
    auto utilityBar = bounds.removeFromBottom(controlUtilityBarHeight);
    bounds.removeFromBottom(sectionGap);
    const int candidateContentWidth = std::max(
        requiredContentWidth(), bounds.getWidth());
    const int utilityButtonSize =
        patternCellSizeForContentWidth(candidateContentWidth);
    const int utilityY = utilityBar.getY()
        + (utilityBar.getHeight() - utilityButtonSize) / 2;
    int utilityLeft = utilityBar.getX();
    previousPageButton_.setBounds(
        utilityLeft, utilityY, utilityButtonSize, utilityButtonSize);
    utilityLeft += utilityButtonSize + perVoiceControlGap;
    nextPageButton_.setBounds(
        utilityLeft, utilityY, utilityButtonSize, utilityButtonSize);
    utilityLeft += utilityButtonSize + perVoiceControlGap;
    globalPlayButton_.setBounds(
        utilityLeft, utilityY, utilityButtonSize, utilityButtonSize);
    utilityLeft += utilityButtonSize + perVoiceControlGap;
    globalStopButton_.setBounds(
        utilityLeft, utilityY, utilityButtonSize, utilityButtonSize);

    int utilityRight = utilityBar.getRight();
    utilityRight -= utilityButtonSize;
    suppressionButton_.setBounds(
        utilityRight, utilityY, utilityButtonSize, utilityButtonSize);

    const int rowHeight = std::max(
        minimumPatternPanelHeight,
        voiceNameHeight
            + patternCellSizeForContentWidth(candidateContentWidth) + 2);
    const int fullRowHeight = bounds.getHeight() >= rowHeight
        ? (bounds.getHeight() / rowHeight) * rowHeight
        : bounds.getHeight();
    viewport_.setBounds(bounds.withHeight(fullRowHeight));
    controlPane_.setBounds(controls);

    updateContentSize();
    viewport_.setViewPosition(
        0, static_cast<int>(firstVisiblePlayer_) * playerStride());
    updatePageButtons();
}

void LivePatternSequencerEditor::resizedContent()
{
    constexpr int controlsX = contentHorizontalPadding + playerPanelHorizontalPadding;
    constexpr int resetX = controlsX + clickTargetSize
        + perVoiceControlGap;
    constexpr int muteX = resetX + clickTargetSize
        + perVoiceControlGap;
    constexpr int patternPickerX = muteX + clickTargetSize
        + perVoiceControlGap;
    for (std::size_t index = 0; index < voiceSelectButtons_.size(); ++index)
    {
        const int y = static_cast<int>(index) * playerStride();
        const int buttonSize = cellWidth();
        const int controlY = y + voiceNameHeight
            + (patternPanelHeight() - voiceNameHeight - buttonSize) / 2;
        const int slotInset = (clickTargetSize - buttonSize) / 2;
        voiceSelectButtons_[index]->setBounds(
            controlsX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
        resetToMasterButtons_[index]->setBounds(
            resetX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
        muteButtons_[index]->setBounds(
            muteX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
        patternMenuButtons_[index]->setBounds(
            patternPickerX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
    }
}

void LivePatternSequencerEditor::resizedControlPane()
{
    auto bounds = controlPane_.getLocalBounds().reduced(controlPanePadding);
    auto topRow = bounds.removeFromTop(controlPaneTopRowHeight);
    bounds.removeFromTop(sectionGap);

    constexpr int gap = 4;
    const int buttonSize = cellWidth();
    const auto placeTopControl = [&](juce::Component& component)
    {
        auto slot = topRow.removeFromLeft(buttonSize);
        component.setBounds(
            slot.getX(),
            slot.getY() + (slot.getHeight() - buttonSize) / 2,
            buttonSize,
            buttonSize);
        topRow.removeFromLeft(gap);
    };
    placeTopControl(*controlPatternMenuButton_);
    placeTopControl(controlSpeedSelector_);
    placeTopControl(controlShiftLeftButton_);
    placeTopControl(controlShiftRightButton_);
    placeTopControl(controlMuteButton_);
    placeTopControl(controlUnmuteButton_);
    placeTopControl(controlResetButton_);
    selectionLabel_.setBounds(topRow);

    const int modulationControlsWidth = buttonSize * 3 + gap * 3;
    const int availableGridWidth = std::max(
        static_cast<int>(lps::Modulation::maxLength),
        bounds.getWidth() - modulationControlsWidth);
    const int dynamicCellWidth = std::max(
        1,
        (availableGridWidth
            - (static_cast<int>(lps::Modulation::maxLength) - 1)
                * modulationCellGap)
            / static_cast<int>(lps::Modulation::maxLength));

    for (const auto lane : modulationLanes)
    {
        const auto laneSlot = laneIndex(lane);
        auto laneBounds = bounds.removeFromTop(modulationPanelHeight);
        if (primaryPlayerIndex_ >= playerCount_)
            continue;
        const int buttonY = laneBounds.getY()
            + (laneBounds.getHeight() - buttonSize) / 2;
        modulationMenuButtons_[laneSlot][primaryPlayerIndex_]->setBounds(
            laneBounds.getX(), buttonY, buttonSize, buttonSize);
        laneBounds.removeFromLeft(buttonSize);
        laneBounds.removeFromLeft(gap);
        decreaseModulationLengthButtons_[laneSlot][primaryPlayerIndex_]->setBounds(
            laneBounds.getX(), buttonY, buttonSize, buttonSize);
        laneBounds.removeFromLeft(buttonSize);
        laneBounds.removeFromLeft(gap);
        increaseModulationLengthButtons_[laneSlot][primaryPlayerIndex_]->setBounds(
            laneBounds.getX(), buttonY, buttonSize, buttonSize);
        laneBounds.removeFromLeft(buttonSize);
        laneBounds.removeFromLeft(gap);

        for (std::size_t step = 0; step < lps::Modulation::maxLength; ++step)
        {
            const auto sliderIndex = primaryPlayerIndex_
                * lps::Modulation::maxLength + step;
            modulationSliders_[laneSlot][sliderIndex]->setBounds(
                laneBounds.getX()
                    + static_cast<int>(step)
                        * (dynamicCellWidth + modulationCellGap),
                laneBounds.getY() + 1,
                dynamicCellWidth,
                modulationCellHeight);
        }
    }

}

void LivePatternSequencerEditor::matrixMouseDown(
    const juce::MouseEvent& event,
    juce::Component& matrix)
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
    matrix.repaint();
}

void LivePatternSequencerEditor::timerCallback()
{
    updateContentSize();
    updatePatternModifiedIndicators();
    for (std::size_t playerIndex = 0;
         playerIndex < playerCount_;
         ++playerIndex)
    {
        for (const auto lane : modulationLanes)
            refreshModulationControls(playerIndex, lane);
    }
    updateModulationModifiedIndicators();
    updateResetToMasterIndicators();
    refreshSelectedControls();
    const bool hostPlaying = processor_.hostTransportPlayingForUi();
    const bool internalPlaying =
        processor_.internalTransportPlayingForUi();
    globalPlayButton_.setToggleState(
        hostPlaying || internalPlaying,
        juce::dontSendNotification);
    globalPlayButton_.setEnabled(!hostPlaying && !internalPlaying);
    globalStopButton_.setEnabled(!hostPlaying && internalPlaying);
    content_.repaint();
}

void LivePatternSequencerEditor::beginSavePlayerPattern(std::size_t playerIndex)
{
    handleSaveResult(processor_.savePlayerPattern(playerIndex));
}

void LivePatternSequencerEditor::handleSaveResult(
    LivePatternSequencerProcessor::SavePatternResult result)
{
    using Status = LivePatternSequencerProcessor::SavePatternStatus;
    switch (result.status)
    {
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

void LivePatternSequencerEditor::beginSavePlayerModulation(
    std::size_t playerIndex,
    ModulationLane lane)
{
    handleModulationSaveResult(
        playerIndex,
        lane,
        processor_.savePlayerModulation(playerIndex, lane));
}

void LivePatternSequencerEditor::promptForModulationName(
    std::size_t playerIndex,
    ModulationLane lane,
    lps::Modulation candidateModulation)
{
    constexpr auto nameEditorId = "modulationName";
    const auto laneName = modulationLaneName(lane);
    auto* alert = new juce::AlertWindow(
        "Save " + laneName + " Modulation",
        "These " + laneName.toLowerCase()
            + " values are not in the library yet. Enter a name for the new "
              "modulation.",
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
            [safeThis, enteredName, playerIndex, lane,
                candidateModulation](int result)
            {
                if (result != 1 || safeThis == nullptr)
                    return;

                const auto name = enteredName->trim();
                if (name.isEmpty())
                {
                    safeThis->showModulationSaveWarning(
                        lane,
                        "Enter a name before saving the new modulation.");
                    return;
                }

                const auto saveResult =
                    safeThis->processor_.savePlayerModulation(
                        playerIndex, lane, candidateModulation, name);
                if (saveResult.status
                    == LivePatternSequencerProcessor::
                        SaveModulationStatus::needsName)
                {
                    safeThis->showModulationSaveWarning(
                        lane,
                        "Enter a name before saving the new modulation.");
                    return;
                }

                safeThis->handleModulationSaveResult(
                    playerIndex, lane, saveResult);
            }),
        true);
}

void LivePatternSequencerEditor::handleModulationSaveResult(
    std::size_t playerIndex,
    ModulationLane lane,
    LivePatternSequencerProcessor::SaveModulationResult result)
{
    using Status =
        LivePatternSequencerProcessor::SaveModulationStatus;
    switch (result.status)
    {
        case Status::needsName:
            promptForModulationName(
                playerIndex, lane, result.candidateModulation);
            return;

        case Status::selectedExisting:
        case Status::savedNew:
            refreshModulationSelectors();
            refreshModulationControls(playerIndex, lane, true);
            updateModulationModifiedIndicators(true);
            content_.repaint();
            return;

        case Status::failed:
        {
            const auto catalogError =
                processor_.modulationCatalogErrorForUi();
            showModulationSaveWarning(lane, catalogError.isNotEmpty()
                ? "The modulation could not be saved.\n\n"
                    + catalogError
                : juce::String("The modulation could not be saved."));
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

void LivePatternSequencerEditor::selectVoice(
    std::size_t playerIndex,
    bool additiveSelection)
{
    if (playerIndex >= selectedVoices_.size()
        || playerIndex >= voiceSelectButtons_.size())
    {
        return;
    }

    const auto previousSelection = selectedVoices_;

    if (!additiveSelection)
    {
        std::fill(selectedVoices_.begin(), selectedVoices_.end(), false);
        selectedVoices_[playerIndex] = true;
    }
    else
    {
        selectedVoices_[playerIndex] =
            voiceSelectButtons_[playerIndex]->getToggleState();
    }
    if (selectedVoices_[playerIndex])
    {
        primaryPlayerIndex_ = playerIndex;
    }
    else if (playerIndex == primaryPlayerIndex_)
    {
        const auto nextSelected = std::find(
            selectedVoices_.begin(), selectedVoices_.end(), true);
        if (nextSelected != selectedVoices_.end())
        {
            primaryPlayerIndex_ = static_cast<std::size_t>(
                std::distance(selectedVoices_.begin(), nextSelected));
        }
    }

    if (selectedVoices_ != previousSelection)
        controlMuteButton_.setToggleState(false, juce::dontSendNotification);

    refreshSelectedControls(true);
    content_.repaint();
}

void LivePatternSequencerEditor::refreshSelectedControls(bool force)
{
    const auto selectionCount = selectedVoiceCount();
    const bool hasSelection = selectionCount > 0;
    const auto patternPlayerIndex = controlPatternPlayerIndex();
    const bool hasPatternSelection = patternPlayerIndex < playerCount_;

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        voiceSelectButtons_[playerIndex]->setButtonText(
            juce::String(static_cast<int>(playerIndex + 1)));
        voiceSelectButtons_[playerIndex]->setToggleState(
            playerIndex < selectedVoices_.size() && selectedVoices_[playerIndex],
            juce::dontSendNotification);
        muteButtons_[playerIndex]->setToggleState(
            processor_.playerMutedForUi(playerIndex),
            juce::dontSendNotification);

        for (const auto lane : modulationLanes)
        {
            const auto laneSlot = laneIndex(lane);
            auto& menu = *modulationMenuButtons_[laneSlot][playerIndex];
            auto& decrease =
                *decreaseModulationLengthButtons_[laneSlot][playerIndex];
            auto& increase =
                *increaseModulationLengthButtons_[laneSlot][playerIndex];
            const bool show = hasSelection
                && playerIndex == primaryPlayerIndex_
                && processor_.playerSupportsModulationEditingForUi(
                    playerIndex, lane);

            if (show)
            {
                if (menu.getParentComponent() != &controlPane_)
                {
                    controlPane_.addAndMakeVisible(menu);
                    controlPane_.addAndMakeVisible(decrease);
                    controlPane_.addAndMakeVisible(increase);
                }
                menu.setVisible(true);
                decrease.setVisible(true);
                increase.setVisible(true);
            }
            else
            {
                menu.setVisible(false);
                decrease.setVisible(false);
                increase.setVisible(false);
                if (menu.getParentComponent() != &content_)
                {
                    content_.addChildComponent(menu);
                    content_.addChildComponent(decrease);
                    content_.addChildComponent(increase);
                }
            }

            for (std::size_t step = 0;
                 step < lps::Modulation::maxLength;
                 ++step)
            {
                auto& slider = *modulationSliders_[laneSlot]
                    [playerIndex * lps::Modulation::maxLength + step];
                if (show)
                {
                    if (slider.getParentComponent() != &controlPane_)
                        controlPane_.addChildComponent(slider);
                }
                else
                {
                    slider.setVisible(false);
                    if (slider.getParentComponent() != &content_)
                        content_.addChildComponent(slider);
                }
            }

            if (show)
                refreshModulationControls(playerIndex, lane, force);
        }
    }

    juce::String selectionText;
    if (!hasSelection)
    {
        selectionText = "SELECT VOICES";
    }
    else
    {
        selectionText = juce::String(static_cast<int>(selectionCount))
            + (selectionCount == 1 ? " VOICE" : " VOICES")
            + " SELECTED";
        if (primaryPlayerIndex_ < playerCount_)
            selectionText += " · EDITING "
                + processor_.playerNameForUi(primaryPlayerIndex_).toUpperCase();
    }
    selectionLabel_.setText(selectionText, juce::dontSendNotification);

    controlPatternMenuButton_->setEnabled(hasPatternSelection);
    controlSpeedSelector_.setEnabled(hasPatternSelection);
    controlShiftLeftButton_.setEnabled(hasPatternSelection);
    controlShiftRightButton_.setEnabled(hasPatternSelection);
    controlMuteButton_.setEnabled(hasSelection);
    controlUnmuteButton_.setEnabled(hasSelection);

    std::size_t resettableCount = 0;
    std::size_t pendingResetCount = 0;
    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        if (playerIndex >= selectedVoices_.size() || !selectedVoices_[playerIndex]
            || processor_.playerIsMasterForUi(playerIndex)
            || !processor_.playerCanResetToMasterForUi(playerIndex))
        {
            continue;
        }
        ++resettableCount;
        if (processor_.playerResetToMasterPendingForUi(playerIndex))
            ++pendingResetCount;
    }
    controlResetButton_.setEnabled(resettableCount > pendingResetCount);
    controlResetButton_.setButtonText(
        resettableCount > 0 && resettableCount == pendingResetCount
            ? "Q"
            : "R");

    if (hasPatternSelection)
    {
        bool anyModified = false;
        std::size_t speed = processor_.playerPlaybackSpeed(patternPlayerIndex);
        bool speedsMatch = true;
        for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
        {
            if (playerIndex >= selectedVoices_.size()
                || !selectedVoices_[playerIndex]
                || !processor_.playerSupportsPatternEditingForUi(playerIndex))
            {
                continue;
            }
            speedsMatch = speedsMatch
                && processor_.playerPlaybackSpeed(playerIndex) == speed;
            anyModified = anyModified
                || processor_.playerPatternModifiedForUi(playerIndex);
        }
        controlPatternMenuButton_->setButtonText(
            juce::String("PATTERN") + (anyModified ? " *" : ""));
        controlSpeedSelector_.setSelectedItemIndex(
            speedsMatch ? static_cast<int>(speed) : -1,
            juce::dontSendNotification);
    }
    else
    {
        controlPatternMenuButton_->setButtonText("PATTERN");
        controlSpeedSelector_.setSelectedItemIndex(
            -1, juce::dontSendNotification);
    }

    controlPane_.resized();
    controlPane_.repaint();
}

void LivePatternSequencerEditor::applyToSelectedPatternPlayers(
    const std::function<void(std::size_t)>& action)
{
    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        if (playerIndex < selectedVoices_.size()
            && selectedVoices_[playerIndex]
            && processor_.playerSupportsPatternEditingForUi(playerIndex))
        {
            action(playerIndex);
        }
    }
}

void LivePatternSequencerEditor::applyToSelectedPlayers(
    const std::function<void(std::size_t)>& action)
{
    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        if (playerIndex < selectedVoices_.size() && selectedVoices_[playerIndex])
            action(playerIndex);
    }
}

std::size_t LivePatternSequencerEditor::selectedVoiceCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count(selectedVoices_.begin(), selectedVoices_.end(), true));
}

std::size_t LivePatternSequencerEditor::controlPatternPlayerIndex() const noexcept
{
    if (primaryPlayerIndex_ < selectedVoices_.size()
        && selectedVoices_[primaryPlayerIndex_]
        && processor_.playerSupportsPatternEditingForUi(primaryPlayerIndex_))
    {
        return primaryPlayerIndex_;
    }

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        if (playerIndex < selectedVoices_.size()
            && selectedVoices_[playerIndex]
            && processor_.playerSupportsPatternEditingForUi(playerIndex))
        {
            return playerIndex;
        }
    }
    return playerCount_;
}

std::size_t LivePatternSequencerEditor::pageSize() const noexcept
{
    const auto stride = std::max(1, playerStride());
    return static_cast<std::size_t>(
        std::max(1, viewport_.getHeight() / stride));
}

void LivePatternSequencerEditor::pageVoices(int direction)
{
    const auto voicesPerPage = pageSize();
    if (direction > 0)
    {
        const auto next = firstVisiblePlayer_ + voicesPerPage;
        if (next < playerCount_)
            firstVisiblePlayer_ = next;
    }
    else if (direction < 0)
    {
        firstVisiblePlayer_ = firstVisiblePlayer_ > voicesPerPage
            ? firstVisiblePlayer_ - voicesPerPage
            : 0;
    }

    viewport_.setViewPosition(
        0, static_cast<int>(firstVisiblePlayer_) * playerStride());
    updatePageButtons();
}

void LivePatternSequencerEditor::updatePageButtons()
{
    previousPageButton_.setEnabled(firstVisiblePlayer_ > 0);
    nextPageButton_.setEnabled(
        firstVisiblePlayer_ + pageSize() < playerCount_);
}

void LivePatternSequencerEditor::refreshModulationSelectors()
{
    for (const auto lane : modulationLanes)
    {
        const auto laneSlot = laneIndex(lane);
        for (std::size_t playerIndex = 0;
             playerIndex < modulationSelectors_[laneSlot].size();
             ++playerIndex)
        {
            if (!processor_.playerSupportsModulationEditingForUi(
                    playerIndex, lane))
                continue;
            auto& selector = *modulationSelectors_[laneSlot][playerIndex];
            selector.clear(juce::dontSendNotification);
            for (std::size_t modulationIndex = 0;
                 modulationIndex < processor_.modulationCountForUi();
                 ++modulationIndex)
            {
                selector.addItem(
                    processor_.modulationNameForUi(modulationIndex),
                    static_cast<int>(modulationIndex + 1));
            }

            selector.setSelectedItemIndex(
                static_cast<int>(processor_.selectedModulationForPlayer(
                    playerIndex, lane)),
                juce::dontSendNotification);
        }
    }
}

void LivePatternSequencerEditor::refreshModulationControls(
    std::size_t playerIndex,
    ModulationLane lane,
    bool force)
{
    const auto laneSlot = laneIndex(lane);
    if (playerIndex >= playerCount_
        || !processor_.playerSupportsModulationEditingForUi(playerIndex, lane)
        || playerIndex >= decreaseModulationLengthButtons_[laneSlot].size()
        || playerIndex >= increaseModulationLengthButtons_[laneSlot].size())
    {
        return;
    }

    const auto modulation =
        processor_.modulationForUi(playerIndex, lane);
    const auto length = std::min<std::size_t>(
        modulation.length, lps::Modulation::maxLength);
    const bool playing = processor_.playingForUi();
    const int currentStep =
        processor_.currentModulationStepForUi(playerIndex, lane);
    const bool isDisplayedControl = selectedVoiceCount() > 0
        && playerIndex == primaryPlayerIndex_;
    decreaseModulationLengthButtons_[laneSlot][playerIndex]->setEnabled(
        length > 1);
    increaseModulationLengthButtons_[laneSlot][playerIndex]->setEnabled(
        length < lps::Modulation::maxLength);

    for (std::size_t step = 0;
         step < lps::Modulation::maxLength;
         ++step)
    {
        const auto sliderIndex =
            playerIndex * lps::Modulation::maxLength + step;
        if (sliderIndex >= modulationSliders_[laneSlot].size())
            break;

        auto& slider = *modulationSliders_[laneSlot][sliderIndex];
        const bool shouldBeVisible = isDisplayedControl && step < length;
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

void LivePatternSequencerEditor::updateModulationModifiedIndicators(
    bool force)
{
    for (const auto lane : modulationLanes)
    {
        const auto laneSlot = laneIndex(lane);
        for (std::size_t playerIndex = 0;
             playerIndex < saveModulationButtons_[laneSlot].size();
             ++playerIndex)
        {
            if (!processor_.playerSupportsModulationEditingForUi(
                    playerIndex, lane))
                continue;
            const bool modified =
                processor_.playerModulationModifiedForUi(playerIndex, lane);
            if (!force
                && displayedModulationModified_[laneSlot][playerIndex]
                    == modified)
                continue;

            displayedModulationModified_[laneSlot][playerIndex] = modified;
            auto& selector =
                *modulationSelectors_[laneSlot][playerIndex];
            auto& saveButton =
                *saveModulationButtons_[laneSlot][playerIndex];
            auto& menuButton =
                *modulationMenuButtons_[laneSlot][playerIndex];
            saveButton.setEnabled(modified);
            saveButton.setButtonText(modified ? "Save *" : "Save");
            menuButton.setButtonText(modulationMenuLabel(lane, modified));

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
            button.setButtonText("—");
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
            button.setButtonText(pending ? "Q" : "R");
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

void LivePatternSequencerEditor::showModulationSaveWarning(
    ModulationLane lane,
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        modulationLaneName(lane) + " Modulation Not Saved",
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

void LivePatternSequencerEditor::showModulationLibraryWarning(
    const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::WarningIcon,
        "Modulation Library Unavailable",
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

juce::Rectangle<int>
LivePatternSequencerEditor::modulationPreviewAreaForPlayer(
    std::size_t playerIndex) const
{
    return {
        patternGridLeft - controlsToGridGap - modulationPreviewWidth,
        static_cast<int>(playerIndex) * playerStride() + 2,
        modulationPreviewWidth,
        patternPanelHeight() - 5
    };
}

int LivePatternSequencerEditor::cellWidth() const
{
    return patternCellSizeForContentWidth(content_.getWidth());
}

int LivePatternSequencerEditor::patternPanelHeight() const
{
    return std::max(
        minimumPatternPanelHeight,
        voiceNameHeight + cellWidth() + 2);
}

int LivePatternSequencerEditor::playerStride() const
{
    return patternPanelHeight();
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
        const int size = patternCellSizeForContentWidth(candidateWidth);
        const int stride = std::max(
            minimumPatternPanelHeight,
            voiceNameHeight + size + 2);
        const int visibleRows = std::max(1, viewport_.getHeight() / stride);
        const int playerRows = static_cast<int>(playerCount_);
        const int paddedRows = playerRows == 0
            ? 1
            : ((playerRows + visibleRows - 1) / visibleRows) * visibleRows;
        return std::max(
            1,
            paddedRows * stride + viewport_.getHeight() % stride
                + playerBottomPadding);
    };

    const int height = heightForWidth(width);

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
        if (modulationPreviewAreaForPlayer(playerIndex).contains(
                event.getPosition()))
        {
            selectVoice(playerIndex, false);
            return;
        }

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
            selectVoice(playerIndex, false);
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
            selectVoice(playerIndex, false);
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
        const bool selected = playerIndex < selectedVoices_.size()
            && selectedVoices_[playerIndex];
        graphics.setColour(selected
            ? juce::Colour(panel).brighter(0.10f)
            : juce::Colour(panel));
        graphics.fillRect(patternPanel);
        if (selected)
        {
            graphics.setColour(juce::Colour(uiYellow));
            graphics.fillRect(
                patternPanel.getX(), patternPanel.getY(), 3,
                patternPanel.getHeight());
        }
        graphics.setColour(juce::Colour(border).brighter(0.08f));
        graphics.fillRect(
            patternPanel.getX(), patternPanel.getBottom() - 2,
            patternPanel.getWidth(), 2);

        auto voiceName = processor_.playerNameForUi(playerIndex);
        if (voiceName.isEmpty())
            voiceName = "VOICE " + juce::String(static_cast<int>(playerIndex + 1));
        graphics.setColour(juce::Colour(secondaryText));
        graphics.setFont(juce::Font(
            juce::FontOptions(8.5f, juce::Font::bold)));
        graphics.drawFittedText(
            voiceName.toUpperCase(),
            contentHorizontalPadding + playerPanelHorizontalPadding,
            patternPanel.getY() + 1,
            clickTargetSize * 3 + perVoiceControlGap * 2,
            voiceNameHeight,
            juce::Justification::centred,
            1,
            0.65f);

        graphics.setColour(juce::Colour(border).withMultipliedAlpha(0.65f));
        graphics.drawVerticalLine(
            patternGridLeft - controlsToGridGap / 2,
            static_cast<float>(patternPanel.getY() + 1),
            static_cast<float>(patternPanel.getBottom() - 2));

        paintModulationPreview(
            graphics,
            playerIndex,
            modulationPreviewAreaForPlayer(playerIndex));

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

void LivePatternSequencerEditor::paintModulationPreview(
    juce::Graphics& graphics,
    std::size_t playerIndex,
    juce::Rectangle<int> bounds)
{
    graphics.setColour(juce::Colour(inactive).darker(0.08f));
    graphics.fillRoundedRectangle(bounds.toFloat(), 2.0f);
    graphics.setColour(juce::Colour(border).brighter(0.12f));
    graphics.drawRoundedRectangle(bounds.toFloat(), 2.0f, 1.0f);

    constexpr int labelWidth = 14;
    const std::array<const char*, 3> laneLabels {"V", "P", "G"};
    const auto graphColour = juce::Colour(warmIvory);
    const int laneHeight = std::max(
        1, bounds.getHeight() / static_cast<int>(modulationLanes.size()));

    graphics.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    for (std::size_t laneSlot = 0;
         laneSlot < modulationLanes.size();
         ++laneSlot)
    {
        const auto lane = modulationLanes[laneSlot];
        juce::Rectangle<int> laneBounds(
            bounds.getX(),
            bounds.getY() + static_cast<int>(laneSlot) * laneHeight,
            bounds.getWidth(),
            laneSlot + 1 == modulationLanes.size()
                ? bounds.getBottom()
                    - (bounds.getY() + static_cast<int>(laneSlot) * laneHeight)
                : laneHeight);

        if (laneSlot > 0)
        {
            graphics.setColour(juce::Colour(border).withAlpha(0.65f));
            graphics.drawHorizontalLine(
                laneBounds.getY(),
                static_cast<float>(laneBounds.getX() + 1),
                static_cast<float>(laneBounds.getRight() - 1));
        }

        graphics.setColour(graphColour);
        graphics.drawText(
            laneLabels[laneSlot],
            laneBounds.removeFromLeft(labelWidth),
            juce::Justification::centred);

        if (!processor_.playerSupportsModulationEditingForUi(
                playerIndex, lane))
        {
            graphics.setColour(juce::Colour(secondaryText).withAlpha(0.35f));
            graphics.drawHorizontalLine(
                laneBounds.getCentreY(),
                static_cast<float>(laneBounds.getX() + 2),
                static_cast<float>(laneBounds.getRight() - 2));
            continue;
        }

        const auto modulation = processor_.modulationForUi(playerIndex, lane);
        const auto valueCount = std::min<std::size_t>(
            modulation.length, lps::Modulation::maxLength);
        if (valueCount == 0)
            continue;

        auto graphBounds = laneBounds.reduced(2, 1);
        const float stepWidth = static_cast<float>(graphBounds.getWidth())
            / static_cast<float>(valueCount);
        const int currentStep = processor_.playingForUi()
            ? processor_.currentModulationStepForUi(playerIndex, lane)
            : -1;

        for (std::size_t step = 0; step < valueCount; ++step)
        {
            const float value = modulation.values[step].toFloat();
            const float barHeight = std::max(
                1.0f,
                static_cast<float>(graphBounds.getHeight()) * value);
            const float barX = static_cast<float>(graphBounds.getX())
                + static_cast<float>(step) * stepWidth;
            const float gap = stepWidth >= 3.0f ? 1.0f : 0.0f;
            const juce::Rectangle<float> bar(
                barX,
                static_cast<float>(graphBounds.getBottom()) - barHeight,
                std::max(1.0f, stepWidth - gap),
                barHeight);
            const bool isCurrent = static_cast<int>(step) == currentStep;
            graphics.setColour(isCurrent
                ? juce::Colour(uiBlue)
                : graphColour.withAlpha(0.72f));
            graphics.fillRect(bar);
        }
    }
}

void LivePatternSequencerEditor::paintControlPane(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    const auto paneBounds = controlPane_.getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(paneBounds.reduced(0.5f), 6.0f);
    graphics.setColour(juce::Colour(uiBlack));
    graphics.drawRoundedRectangle(paneBounds.reduced(0.5f), 6.0f, 1.0f);

    const int modulationTop = controlPanePadding + controlPaneTopRowHeight
        + sectionGap;
    graphics.setColour(juce::Colour(border).brighter(0.08f));
    for (std::size_t lane = 0; lane <= modulationLanes.size(); ++lane)
    {
        const int y = modulationTop
            + static_cast<int>(lane) * modulationPanelHeight;
        graphics.drawHorizontalLine(
            y,
            static_cast<float>(controlPanePadding),
            static_cast<float>(controlPane_.getWidth() - controlPanePadding));
    }

    if (selectedVoiceCount() == 0)
    {
        graphics.setColour(juce::Colour(secondaryText));
        graphics.setFont(juce::Font(juce::FontOptions(12.0f)));
        graphics.drawText(
            "Select one or more voices above to enable batch controls.",
            controlPanePadding,
            modulationTop,
            controlPane_.getWidth() - controlPanePadding * 2,
            static_cast<int>(modulationLanes.size()) * modulationPanelHeight,
            juce::Justification::centred);
    }
}

void LivePatternSequencerEditor::showSuppressionMatrix()
{
    if (suppressionMenuOpen_)
    {
        suppressionMenuOpen_ = false;
        suppressionButton_.setToggleState(false, juce::dontSendNotification);
        juce::PopupMenu::dismissAllActiveMenus();
        return;
    }

    suppressionMenuOpen_ = true;
    suppressionButton_.setToggleState(true, juce::dontSendNotification);
    juce::PopupMenu menu;
    menu.addSectionHeader("SUPPRESSION MATRIX");
    menu.addCustomItem(
        1,
        std::make_unique<MatrixComponent>(*this),
        nullptr,
        "Suppression Matrix");

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}.withTargetComponent(&suppressionButton_),
        [safeThis](int)
        {
            if (safeThis == nullptr)
                return;
            safeThis->suppressionMenuOpen_ = false;
            safeThis->suppressionButton_.setToggleState(
                false, juce::dontSendNotification);
        });
}

void LivePatternSequencerEditor::showPatternMenu(
    std::size_t playerIndex,
    juce::Component* targetComponent)
{
    if (playerIndex >= playerCount_ || playerIndex >= patternMenuButtons_.size())
        return;

    constexpr int saveItemId = 20000;
    juce::PopupMenu menu;
    menu.addSectionHeader("SELECT PATTERN");
    const auto selectedPattern =
        processor_.selectedPatternForPlayer(playerIndex);
    for (std::size_t patternIndex = 0;
         patternIndex < processor_.patternCountForUi();
         ++patternIndex)
    {
        menu.addCustomItem(
            static_cast<int>(patternIndex + 1),
            std::make_unique<PatternPreviewMenuItem>(
                processor_.patternAtForUi(patternIndex),
                patternIndex == selectedPattern),
            nullptr,
            processor_.patternNameForUi(patternIndex));
    }

    menu.addSeparator();
    menu.addItem(
        saveItemId,
        "SAVE PATTERN",
        processor_.playerPatternModifiedForUi(playerIndex));

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(targetComponent),
        [safeThis, playerIndex](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == saveItemId)
            {
                safeThis->beginSavePlayerPattern(playerIndex);
                return;
            }

            const auto patternIndex = static_cast<std::size_t>(result - 1);
            if (patternIndex < safeThis->processor_.patternCountForUi())
            {
                safeThis->applyToSelectedPatternPlayers(
                    [safeThis, patternIndex](std::size_t selectedPlayerIndex)
                    {
                        if (safeThis != nullptr)
                        {
                            safeThis->processor_.selectPatternForPlayer(
                                selectedPlayerIndex, patternIndex);
                        }
                    });
                safeThis->updatePatternModifiedIndicators(true);
                safeThis->refreshSelectedControls(true);
                safeThis->content_.repaint();
            }
        });
}

void LivePatternSequencerEditor::showModulationMenu(
    std::size_t playerIndex,
    ModulationLane lane)
{
    const auto laneSlot = laneIndex(lane);
    if (playerIndex >= playerCount_
        || playerIndex >= modulationMenuButtons_[laneSlot].size())
        return;

    constexpr int saveItemId = 20000;
    juce::PopupMenu menu;
    const auto selected =
        processor_.selectedModulationForPlayer(playerIndex, lane);
    for (std::size_t modulationIndex = 0;
         modulationIndex < processor_.modulationCountForUi();
         ++modulationIndex)
    {
        const auto modulation = processor_.modulationAtForUi(modulationIndex);
        if (modulation.length <= 1)
            continue;

        menu.addCustomItem(
            static_cast<int>(modulationIndex + 1),
            std::make_unique<ModulationPreviewMenuItem>(
                modulation,
                modulationIndex == selected),
            nullptr,
            processor_.modulationNameForUi(modulationIndex));
    }
    menu.addSeparator();
    const auto editedModulation =
        processor_.modulationForUi(playerIndex, lane);
    menu.addItem(
        saveItemId,
        "SAVE MODULATION",
        editedModulation.length > 1
            && processor_.playerModulationModifiedForUi(playerIndex, lane));

    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(
                modulationMenuButtons_[laneSlot][playerIndex].get()),
        [safeThis, playerIndex, lane](int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == saveItemId)
            {
                safeThis->beginSavePlayerModulation(playerIndex, lane);
                return;
            }

            const auto modulationIndex = static_cast<std::size_t>(result - 1);
            if (modulationIndex
                < safeThis->processor_.modulationCountForUi())
            {
                safeThis->processor_.selectModulationForPlayer(
                    playerIndex, lane, modulationIndex);
                safeThis->refreshModulationControls(
                    playerIndex, lane, true);
                safeThis->updateModulationModifiedIndicators(true);
                safeThis->content_.repaint();
            }
        });
}

void LivePatternSequencerEditor::paintMatrix(
    juce::Graphics& graphics,
    const juce::Component& matrix)
{
    graphics.fillAll(juce::Colour(background));
    const auto panelBounds = matrix.getLocalBounds();
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(panelBounds.toFloat(), 7.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(
        panelBounds.toFloat().reduced(0.5f), 7.0f, 1.0f);

    graphics.setColour(juce::Colour(primaryText));
    graphics.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    graphics.drawText("SUPPRESSION: ROW suppresses COLUMN", 16, 14,
        matrix.getWidth() - 32, 24,
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
