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
constexpr auto uiGreen = 0xff79a86b;
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
constexpr auto patternStepOff = lightGray;
constexpr auto patternStepOffAlternate = 0xff92928a;
constexpr auto patternStepOn = 0xff747672;
constexpr auto patternStepTextOff = darkGray;
constexpr auto patternStepTextOn = uiYellow;
constexpr auto patternPlayhead = uiBlue;
constexpr std::array<std::uint32_t,
    LivePatternSequencerProcessor::groupCount> groupColourValues {
    uiRed, uiBlue, uiYellow, uiGreen
};

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
constexpr int modulationPanelHeight = clickTargetSize + 2;
constexpr int playerBottomPadding = 2;
constexpr int controlPanePadding = 8;
constexpr int controlPaneLabelHeight = 12;
constexpr int controlPaneControlGap = 4;
constexpr int controlPaneStackHeight = controlPaneLabelHeight + 2
    + 3 * clickTargetSize + 2 * controlPaneControlGap;
constexpr int controlPaneHeight = controlPanePadding * 2
    + controlPaneStackHeight;
constexpr float barSchedulerLabelWidth = 44.0f;
constexpr float minimumBarTransitionWidth = 40.0f;
constexpr float maximumBarCellWidth = 72.0f;
constexpr int barSchedulerHeight = 116;
constexpr int modulationEditorPadding = 6;
constexpr int modulationEditorHeight = modulationEditorPadding * 2
    + 3 * modulationPanelHeight;
constexpr int preferredModulationValueWidth = 28;
constexpr int matrixGridLeft = 64;
constexpr int matrixGridTop = 82;
constexpr int matrixCellWidth = 32;
constexpr int matrixCellHeight = 32;
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

[[nodiscard]] constexpr std::size_t targetBarForOffset(
    std::size_t currentBar,
    std::size_t barsFromNow) noexcept
{
    constexpr auto maximum =
        LivePatternSequencerProcessor::maximumScheduledBars;
    if (barsFromNow == 0)
        return 0;
    return currentBar == 0
        ? (barsFromNow > maximum ? maximum : barsFromNow)
        : ((currentBar + barsFromNow - 2) % maximum) + 1;
}

[[nodiscard]] constexpr std::size_t offsetForTargetBar(
    std::size_t currentBar,
    std::size_t targetBar) noexcept
{
    constexpr auto maximum =
        LivePatternSequencerProcessor::maximumScheduledBars;
    if (currentBar == 0)
        return targetBar > maximum ? maximum : targetBar;
    return ((targetBar + maximum - currentBar) % maximum) + 1;
}

static_assert(targetBarForOffset(7, 2) == 8);
static_assert(targetBarForOffset(8, 1) == 8);
static_assert(targetBarForOffset(8, 2) == 1);
static_assert(offsetForTargetBar(7, 8) == 2);
static_assert(offsetForTargetBar(8, 8) == 1);
static_assert(offsetForTargetBar(8, 1) == 2);

void paintScheduleMarker(
    juce::Graphics& graphics,
    std::size_t targetBar,
    juce::Point<float> centre,
    float radius,
    juce::Colour colour,
    bool filled = true)
{
    if (targetBar == 0)
        return;

    graphics.setColour(colour);
    juce::Path shape;
    switch ((targetBar - 1) % 8)
    {
        case 0:
            if (filled)
                graphics.fillEllipse(
                    centre.x - radius, centre.y - radius,
                    radius * 2.0f, radius * 2.0f);
            else
                graphics.drawEllipse(
                    centre.x - radius, centre.y - radius,
                    radius * 2.0f, radius * 2.0f, 1.0f);
            return;
        case 1:
            shape.addTriangle(
                centre.x, centre.y - radius,
                centre.x + radius, centre.y + radius,
                centre.x - radius, centre.y + radius);
            break;
        case 2:
            shape.addQuadrilateral(
                centre.x, centre.y - radius,
                centre.x + radius, centre.y,
                centre.x, centre.y + radius,
                centre.x - radius, centre.y);
            break;
        case 3:
            shape.addRectangle(
                centre.x - radius * 0.82f,
                centre.y - radius * 0.82f,
                radius * 1.64f,
                radius * 1.64f);
            break;
        case 4:
            shape.startNewSubPath(centre.x - radius, centre.y);
            shape.lineTo(centre.x + radius, centre.y);
            shape.startNewSubPath(centre.x, centre.y - radius);
            shape.lineTo(centre.x, centre.y + radius);
            break;
        case 5:
            shape.startNewSubPath(
                centre.x - radius * 0.75f,
                centre.y - radius * 0.75f);
            shape.lineTo(
                centre.x + radius * 0.75f,
                centre.y + radius * 0.75f);
            shape.startNewSubPath(
                centre.x + radius * 0.75f,
                centre.y - radius * 0.75f);
            shape.lineTo(
                centre.x - radius * 0.75f,
                centre.y + radius * 0.75f);
            break;
        case 6:
        {
            constexpr int points = 10;
            for (int point = 0; point < points; ++point)
            {
                const float angle = -juce::MathConstants<float>::halfPi
                    + static_cast<float>(point)
                        * juce::MathConstants<float>::twoPi
                        / static_cast<float>(points);
                const float pointRadius = point % 2 == 0
                    ? radius : radius * 0.43f;
                const juce::Point<float> position {
                    centre.x + std::cos(angle) * pointRadius,
                    centre.y + std::sin(angle) * pointRadius
                };
                if (point == 0)
                    shape.startNewSubPath(position);
                else
                    shape.lineTo(position);
            }
            shape.closeSubPath();
            break;
        }
        case 7:
        {
            constexpr int points = 6;
            for (int point = 0; point < points; ++point)
            {
                const float angle = static_cast<float>(point)
                    * juce::MathConstants<float>::twoPi
                    / static_cast<float>(points);
                const juce::Point<float> position {
                    centre.x + std::cos(angle) * radius,
                    centre.y + std::sin(angle) * radius
                };
                if (point == 0)
                    shape.startNewSubPath(position);
                else
                    shape.lineTo(position);
            }
            shape.closeSubPath();
            break;
        }
    }

    if ((targetBar - 1) % 8 == 4 || (targetBar - 1) % 8 == 5)
        graphics.strokePath(
            shape,
            juce::PathStrokeType(std::max(1.8f, radius * 0.24f)));
    else if (filled)
        graphics.fillPath(shape);
    else
        graphics.strokePath(
            shape,
            juce::PathStrokeType(std::max(1.0f, radius * 0.16f)));
}

enum class ScheduledActionIcon { mute, unmute, pattern };

[[nodiscard]] juce::Colour scheduledActionIconColour(
    ScheduledActionIcon icon) noexcept
{
    switch (icon)
    {
        case ScheduledActionIcon::mute: return juce::Colour(uiRed);
        case ScheduledActionIcon::unmute: return juce::Colour(uiBlue);
        case ScheduledActionIcon::pattern: return juce::Colour(uiYellow);
    }
    return juce::Colour(warmIvory);
}

void paintScheduledActionIcon(
    juce::Graphics& graphics,
    ScheduledActionIcon icon,
    juce::Rectangle<float> bounds,
    juce::Colour colour)
{
    graphics.setColour(colour);
    if (icon == ScheduledActionIcon::pattern)
    {
        const auto y = bounds.getCentreY();
        const float strokeWidth = std::max(1.8f, bounds.getHeight() * 0.12f);
        graphics.drawLine(
            bounds.getX(), y, bounds.getRight() - 2.0f, y, strokeWidth);
        juce::Path arrow;
        arrow.startNewSubPath(bounds.getRight() - 6.0f, y - 4.0f);
        arrow.lineTo(bounds.getRight() - 1.0f, y);
        arrow.lineTo(bounds.getRight() - 6.0f, y + 4.0f);
        graphics.strokePath(arrow, juce::PathStrokeType(strokeWidth));
        return;
    }

    const float scale = std::min(bounds.getWidth(), bounds.getHeight()) / 14.0f;
    const auto point = [scale, &bounds](float x, float y)
    {
        return juce::Point<float> {
            bounds.getCentreX() + (x - 7.0f) * scale,
            bounds.getCentreY() + (y - 7.0f) * scale};
    };
    juce::Path speaker;
    speaker.startNewSubPath(point(1.0f, 5.0f));
    speaker.lineTo(point(4.5f, 5.0f));
    speaker.lineTo(point(8.0f, 2.0f));
    speaker.lineTo(point(8.0f, 12.0f));
    speaker.lineTo(point(4.5f, 9.0f));
    speaker.lineTo(point(1.0f, 9.0f));
    speaker.closeSubPath();
    graphics.fillPath(speaker);

    if (icon == ScheduledActionIcon::mute)
    {
        const float strokeWidth = std::max(1.8f, 1.7f * scale);
        graphics.drawLine(
            juce::Line<float>(point(9.0f, 4.0f), point(13.0f, 10.0f)),
            strokeWidth);
        graphics.drawLine(
            juce::Line<float>(point(13.0f, 4.0f), point(9.0f, 10.0f)),
            strokeWidth);
    }
    else
    {
        juce::Path wave;
        wave.startNewSubPath(point(9.5f, 4.0f));
        wave.quadraticTo(point(13.0f, 7.0f), point(9.5f, 10.0f));
        graphics.strokePath(
            wave,
            juce::PathStrokeType(std::max(1.8f, 1.7f * scale)));
    }
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

    void setScheduledTarget(std::size_t targetBar)
    {
        if (scheduledTargetBar_ == targetBar)
            return;
        scheduledTargetBar_ = targetBar;
        repaint();
    }

    void setLocked(bool locked)
    {
        if (locked_ == locked)
            return;
        locked_ = locked;
        repaint();
    }

    void paintButton(
        juce::Graphics& graphics,
        bool isMouseOverButton,
        bool isButtonDown) override
    {
        auto face = juce::Colour(panel);
        if (locked_)
            face = face.darker(0.28f);
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

        if (scheduledTargetBar_ != 0)
        {
            paintScheduleMarker(
                graphics,
                scheduledTargetBar_,
                {bounds.getRight() - 5.0f, bounds.getY() + 5.0f},
                3.8f,
                juce::Colour(uiYellow));
        }

        if (locked_)
        {
            const float right = bounds.getRight() - 4.0f;
            const float bottom = bounds.getBottom() - 3.0f;
            const juce::Rectangle<float> lockBody {
                right - 7.0f, bottom - 6.0f, 7.0f, 6.0f};
            graphics.setColour(juce::Colour(secondaryText));
            graphics.fillRoundedRectangle(lockBody, 1.0f);
            juce::Path shackle;
            shackle.addCentredArc(
                lockBody.getCentreX(), lockBody.getY(),
                2.4f, 3.0f, 0.0f,
                juce::MathConstants<float>::pi,
                juce::MathConstants<float>::twoPi,
                true);
            graphics.strokePath(shackle, juce::PathStrokeType(1.3f));
        }
    }

private:
    Kind kind_;
    ModulationLane modulationLane_;
    std::size_t scheduledTargetBar_ = 0;
    bool locked_ = false;
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
    ModulationPreviewMenuItem(
        lps::Modulation modulation,
        bool selected,
        bool locked)
        : juce::PopupMenu::CustomComponent(true),
          modulation_(modulation),
          selected_(selected),
          locked_(locked)
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

        graphics.setOpacity(locked_ ? 0.35f : 1.0f);

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
    bool locked_ = false;
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

LivePatternSequencerEditor::BarSchedulerComponent::BarSchedulerComponent(
    LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
}

void LivePatternSequencerEditor::BarSchedulerComponent::paint(
    juce::Graphics& graphics)
{
    editor_.paintBarScheduler(graphics);
}

LivePatternSequencerEditor::ModulationEditorComponent::
ModulationEditorComponent(LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setOpaque(true);
    setAlwaysOnTop(true);
}

void LivePatternSequencerEditor::ModulationEditorComponent::paint(
    juce::Graphics& graphics)
{
    editor_.paintModulationEditor(graphics);
}

void LivePatternSequencerEditor::ModulationEditorComponent::resized()
{
    editor_.resizedModulationEditor();
}

LivePatternSequencerEditor::ModulationBlockLayer::ModulationBlockLayer(
    LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setAlwaysOnTop(true);
    setInterceptsMouseClicks(true, false);
}

void LivePatternSequencerEditor::ModulationBlockLayer::mouseDown(
    const juce::MouseEvent&)
{
    editor_.modulationBlockLayerMouseDown();
}

LivePatternSequencerEditor::MenuDismissLayer::MenuDismissLayer(
    LivePatternSequencerEditor& editor) noexcept
    : editor_(editor)
{
    setAlwaysOnTop(true);
    setInterceptsMouseClicks(true, false);
}

void LivePatternSequencerEditor::MenuDismissLayer::mouseDown(
    const juce::MouseEvent& event)
{
    editor_.menuDismissLayerMouseDown(event);
}

void LivePatternSequencerEditor::PagedViewport::mouseWheelMove(
    const juce::MouseEvent&,
    const juce::MouseWheelDetails&)
{
    // Voice navigation is deliberately page-based. The fixed navigation
    // buttons keep every transition aligned to a completely new set of rows.
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
    graphics.setOpacity(isEnabled() ? 1.0f : 0.32f);
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

LivePatternSequencerEditor::SpeakerButton::SpeakerButton(Kind kind) noexcept
    : kind_(kind)
{
}

void LivePatternSequencerEditor::SpeakerButton::setKind(Kind kind) noexcept
{
    if (kind_ == kind)
        return;
    kind_ = kind;
    repaint();
}

void LivePatternSequencerEditor::SpeakerButton::setScheduledTarget(
    std::size_t targetBar)
{
    if (scheduledTargetBar_ == targetBar)
        return;
    scheduledTargetBar_ = targetBar;
    repaint();
}

void LivePatternSequencerEditor::SpeakerButton::paintButton(
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
    const float opacity = isEnabled() ? 1.0f : 0.38f;
    graphics.setColour(face.withMultipliedAlpha(opacity));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(
        juce::Colour(border).withMultipliedAlpha(opacity));
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    auto iconColour = findColour(engaged
        ? juce::TextButton::textColourOnId
        : juce::TextButton::textColourOffId);
    graphics.setColour(iconColour.withMultipliedAlpha(opacity));

    const float scale = std::min(bounds.getWidth(), bounds.getHeight()) / 41.0f;
    const auto point = [scale, &bounds](float x, float y)
    {
        return juce::Point<float> {
            bounds.getCentreX() + (x - 20.5f) * scale,
            bounds.getCentreY() + (y - 20.5f) * scale};
    };

    juce::Path speaker;
    speaker.startNewSubPath(point(8.5f, 17.0f));
    speaker.lineTo(point(14.0f, 17.0f));
    speaker.lineTo(point(21.5f, 11.0f));
    speaker.lineTo(point(21.5f, 30.0f));
    speaker.lineTo(point(14.0f, 24.0f));
    speaker.lineTo(point(8.5f, 24.0f));
    speaker.closeSubPath();
    graphics.fillPath(speaker);

    const float strokeWidth = std::max(1.6f, 2.0f * scale);
    if (kind_ == Kind::mute)
    {
        graphics.drawLine(
            juce::Line<float>(point(9.0f, 31.0f), point(32.0f, 8.0f)),
            strokeWidth + 0.5f);
    }
    else
    {
        juce::Path waves;
        waves.startNewSubPath(point(25.0f, 15.5f));
        waves.quadraticTo(point(30.0f, 20.5f), point(25.0f, 25.5f));
        waves.startNewSubPath(point(28.5f, 11.5f));
        waves.quadraticTo(point(37.0f, 20.5f), point(28.5f, 29.5f));
        graphics.strokePath(
            waves,
            juce::PathStrokeType(
                strokeWidth,
                juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
    }

    if (scheduledTargetBar_ != 0)
    {
        paintScheduleMarker(
            graphics,
            scheduledTargetBar_,
            {bounds.getRight() - 5.0f, bounds.getY() + 5.0f},
            3.8f,
            juce::Colour(uiYellow));
    }
}

void LivePatternSequencerEditor::UtilityButton::setKind(Kind kind) noexcept
{
    if (kind_ == kind)
        return;
    kind_ = kind;
    repaint();
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

LivePatternSequencerEditor::GroupAssignmentButton::GroupAssignmentButton()
    : juce::Button("Voice groups")
{
    setTooltip(
        "Assign this voice to groups: red upper-left, blue upper-right, "
        "yellow lower-left, green lower-right");
}

void LivePatternSequencerEditor::GroupAssignmentButton::setMembershipMask(
    std::uint8_t mask)
{
    if (membershipMask_ == mask)
        return;
    membershipMask_ = mask;
    repaint();
}

void LivePatternSequencerEditor::GroupAssignmentButton::paintButton(
    juce::Graphics& graphics,
    bool isMouseOverButton,
    bool isButtonDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    const auto leaf = bounds.reduced(bounds.getWidth() * 0.19f);
    constexpr float gap = 1.5f;
    const float petalWidth = (leaf.getWidth() - gap) * 0.5f;
    const float petalHeight = (leaf.getHeight() - gap) * 0.5f;
    for (std::size_t group = 0; group < groupColourValues.size(); ++group)
    {
        const int column = static_cast<int>(group % 2);
        const int row = static_cast<int>(group / 2);
        const juce::Rectangle<float> petal {
            leaf.getX() + static_cast<float>(column) * (petalWidth + gap),
            leaf.getY() + static_cast<float>(row) * (petalHeight + gap),
            petalWidth,
            petalHeight
        };
        const bool active = (membershipMask_ & (1u << group)) != 0;
        auto colour = juce::Colour(active
            ? groupColourValues[group]
            : inactive);
        if (active && (isMouseOverButton || isButtonDown))
            colour = colour.brighter(isButtonDown ? 0.02f : 0.10f);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(petal, 2.5f);
        graphics.setColour(juce::Colour(border).withMultipliedAlpha(
            active ? 0.9f : 0.55f));
        graphics.drawRoundedRectangle(petal, 2.5f, 1.0f);
    }
}

void LivePatternSequencerEditor::GroupAssignmentButton::clicked(
    const juce::ModifierKeys&)
{
    const auto point = getMouseXYRelative();
    const auto group = static_cast<std::size_t>(
        (point.x >= getWidth() / 2 ? 1 : 0)
        + (point.y >= getHeight() / 2 ? 2 : 0));
    if (onGroupClicked)
        onGroupClicked(group);
}

LivePatternSequencerEditor::LivePatternSequencerEditor(
    LivePatternSequencerProcessor& processorToEdit)
    : juce::AudioProcessorEditor(processorToEdit),
      processor_(processorToEdit),
      playerCount_(processorToEdit.playerCountForUi()),
      content_(*this),
      controlPane_(*this),
      barScheduler_(*this),
      modulationBlockLayer_(*this),
      modulationEditor_(*this),
      menuDismissLayer_(*this),
      suppressionButton_(UtilityButton::Kind::matrix),
      previousPageButton_(UtilityButton::Kind::previousPage),
      nextPageButton_(UtilityButton::Kind::nextPage),
      globalPlayButton_(UtilityButton::Kind::play)
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
    groupAssignmentButtons_.reserve(playerCount_);
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

        groupAssignmentButtons_.push_back(
            std::make_unique<GroupAssignmentButton>());
        auto& groupButton = *groupAssignmentButtons_.back();
        std::uint8_t membershipMask = 0;
        for (std::size_t groupIndex = 0;
             groupIndex < LivePatternSequencerProcessor::groupCount;
             ++groupIndex)
        {
            if (processor_.playerInGroupForUi(playerIndex, groupIndex))
                membershipMask |= static_cast<std::uint8_t>(1u << groupIndex);
        }
        groupButton.setMembershipMask(membershipMask);
        groupButton.onGroupClicked = [this, playerIndex](std::size_t groupIndex)
        {
            processor_.setPlayerInGroup(
                playerIndex,
                groupIndex,
                !processor_.playerInGroupForUi(playerIndex, groupIndex));
            refreshSelectedControls(true);
            content_.repaint();
            barScheduler_.repaint();
        };
        content_.addAndMakeVisible(groupButton);

        patternMenuButtons_.push_back(std::make_unique<LaneMenuButton>(
            LaneMenuButton::Kind::pattern));
        auto& patternMenuButton = *patternMenuButtons_.back();
        patternMenuButton.setButtonText(patternMenuLabel(playerIndex, false));
        patternMenuButton.setTooltip(
            "Schedule a pattern for this voice at the next bass-drum bar");
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
                (void) processor_.schedulePatternForPlayer(
                    playerIndex,
                    static_cast<std::size_t>(selected),
                    scheduledBarsFromNow());
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
        speedSelector.setTooltip("Set this voice's playback speed");
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

        muteButtons_.push_back(std::make_unique<SpeakerButton>(
            SpeakerButton::Kind::mute));
        auto& muteButton = *muteButtons_.back();
        muteButton.setClickingTogglesState(false);
        muteButton.setToggleState(
            processor_.playerMutedForUi(playerIndex),
            juce::dontSendNotification);
        muteButton.setTooltip(
            "Schedule mute or unmute for "
            + processor_.playerNameForUi(playerIndex)
            + " at the next bass-drum bar");
        muteButton.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour(uiGreen).darker(0.12f));
        muteButton.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour(muteRed));
        muteButton.setColour(
            juce::TextButton::textColourOffId,
            juce::Colour(secondaryText).darker(0.12f));
        muteButton.setColour(
            juce::TextButton::textColourOnId,
            juce::Colour(primaryText).brighter(0.10f));
        muteButton.onClick = [this, playerIndex]
        {
            const auto barsFromNow = scheduledBarsFromNow();
            (void) processor_.schedulePlayerMute(
                playerIndex,
                !playerMutedAtBarOffset(playerIndex, barsFromNow),
                barsFromNow);
            refreshSelectedControls(true);
            content_.repaint();
            controlPane_.repaint();
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
        speedSelector.setVisible(supportsPattern);
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
        speedSelectors_[playerIndex]->setVisible(
            processor_.playerSupportsPatternEditingForUi(playerIndex));
        offsetLeftButtons_[playerIndex]->setVisible(
            processor_.playerSupportsPatternEditingForUi(playerIndex));
        offsetRightButtons_[playerIndex]->setVisible(
            processor_.playerSupportsPatternEditingForUi(playerIndex));
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

    controlVoiceLabels_.reserve(playerCount_);
    controlVoiceMuteButtons_.reserve(playerCount_);
    const auto configureSpeakerActionButton = [](
        SpeakerButton& button,
        juce::Colour activeColour)
    {
        button.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour(inactive).darker(0.12f));
        button.setColour(
            juce::TextButton::buttonOnColourId,
            activeColour.darker(0.12f));
        button.setColour(
            juce::TextButton::textColourOffId,
            juce::Colour(secondaryText).darker(0.12f));
        button.setColour(
            juce::TextButton::textColourOnId,
            juce::Colour(primaryText).brighter(0.10f));
    };
    for (std::size_t playerIndex = 0;
         playerIndex < playerCount_;
         ++playerIndex)
    {
        const auto voiceName = processor_.playerNameForUi(playerIndex);
        controlVoiceLabels_.push_back(std::make_unique<juce::Label>());
        auto& label = *controlVoiceLabels_.back();
        label.setText(voiceName.toUpperCase(), juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour(primaryText));
        controlPane_.addAndMakeVisible(label);

        controlVoiceMuteButtons_.push_back(
            std::make_unique<SpeakerButton>(SpeakerButton::Kind::mute));
        auto& mute = *controlVoiceMuteButtons_.back();
        mute.setColour(
            juce::TextButton::buttonColourId,
            juce::Colour(uiGreen).darker(0.12f));
        mute.setColour(
            juce::TextButton::buttonOnColourId,
            juce::Colour(muteRed).darker(0.12f));
        mute.setColour(
            juce::TextButton::textColourOffId,
            juce::Colour(primaryText).brighter(0.10f));
        mute.setColour(
            juce::TextButton::textColourOnId,
            juce::Colour(primaryText).brighter(0.10f));
        mute.setTooltip(
            "Toggle " + voiceName
            + " mute at the next bar, or hold 1-8 while clicking to choose "
              "the bar");
        mute.onClick = [this, playerIndex]
        {
            const auto barsFromNow = scheduledBarsFromNow();
            (void) processor_.schedulePlayerMute(
                playerIndex,
                !playerMutedAtBarOffset(playerIndex, barsFromNow),
                barsFromNow);
            refreshSelectedControls(true);
        };
        controlPane_.addAndMakeVisible(mute);
    }

    for (std::size_t groupIndex = 0;
         groupIndex < LivePatternSequencerProcessor::groupCount;
         ++groupIndex)
    {
        const auto groupColour = juce::Colour(groupColourValues[groupIndex]);
        auto& label = groupLabels_[groupIndex];
        label.setText(
            "G" + juce::String(static_cast<int>(groupIndex + 1)),
            juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, groupColour);
        controlPane_.addAndMakeVisible(label);

        const auto configureGroupButton = [&groupColour](
            juce::TextButton& button,
            const juce::String& text)
        {
            button.setButtonText(text);
            button.setColour(
                juce::TextButton::buttonColourId,
                groupColour.darker(0.48f));
            button.setColour(
                juce::TextButton::buttonOnColourId,
                groupColour.darker(0.30f));
            button.setColour(
                juce::TextButton::textColourOffId,
                groupColour.brighter(0.18f));
            button.setColour(
                juce::TextButton::textColourOnId,
                groupColour.brighter(0.22f));
        };

        auto& mute = groupMuteButtons_[groupIndex];
        mute.setKind(SpeakerButton::Kind::mute);
        configureSpeakerActionButton(mute, juce::Colour(muteRed));
        mute.setTooltip(
            "Schedule group " + juce::String(static_cast<int>(groupIndex + 1))
            + " to mute at the next bar, or hold 1-8 while clicking "
              "to choose the bar");
        mute.onClick = [this, groupIndex]
        {
            (void) processor_.scheduleGroupMute(
                groupIndex, true, scheduledBarsFromNow());
            refreshSelectedControls(true);
        };
        controlPane_.addAndMakeVisible(mute);

        auto& unmute = groupUnmuteButtons_[groupIndex];
        unmute.setKind(SpeakerButton::Kind::unmute);
        configureSpeakerActionButton(unmute, juce::Colour(uiGreen));
        unmute.setTooltip(
            "Schedule group " + juce::String(static_cast<int>(groupIndex + 1))
            + " to unmute at the next bar, or hold 1-8 while clicking "
              "to choose the bar");
        unmute.onClick = [this, groupIndex]
        {
            (void) processor_.scheduleGroupMute(
                groupIndex, false, scheduledBarsFromNow());
            refreshSelectedControls(true);
        };
        controlPane_.addAndMakeVisible(unmute);

        auto& reset = groupResetButtons_[groupIndex];
        configureGroupButton(reset, "R");
        reset.setTooltip(
            "Queue group " + juce::String(static_cast<int>(groupIndex + 1))
            + " to restart at the master voice's next loop");
        reset.onClick = [this, groupIndex]
        {
            (void) processor_.resetGroupToMaster(groupIndex);
            updateResetToMasterIndicators(true);
            refreshSelectedControls(true);
        };
        controlPane_.addAndMakeVisible(reset);
    }

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
    globalPlayButton_.setClickingTogglesState(true);
    globalPlayButton_.setColour(
        juce::TextButton::buttonColourId, juce::Colour(uiBlue).darker(0.35f));
    globalPlayButton_.onClick = [this]
    {
        processor_.setInternalTransportPlayingForUi(
            globalPlayButton_.getToggleState());
    };
    addAndMakeVisible(globalPlayButton_);
    addAndMakeVisible(barScheduler_);

    suppressionButton_.setTooltip("Open or close the suppression matrix");
    suppressionButton_.setClickingTogglesState(true);
    suppressionButton_.onClick = [this] { showSuppressionMatrix(); };
    addAndMakeVisible(suppressionButton_);
    addAndMakeVisible(controlPane_);
    addChildComponent(modulationBlockLayer_);
    addChildComponent(modulationEditor_);
    addChildComponent(menuDismissLayer_);

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
    modulationBlockLayer_.setBounds(getLocalBounds());
    menuDismissLayer_.setBounds(getLocalBounds());
    auto bounds = getLocalBounds().reduced(outerPadding);
    bounds.removeFromTop(headerHeight + sectionGap);

    auto controls = bounds.removeFromBottom(controlPaneHeight);
    bounds.removeFromBottom(sectionGap);
    auto utilityBar = bounds.removeFromBottom(barSchedulerHeight);
    bounds.removeFromBottom(sectionGap);
    const int candidateContentWidth = std::max(
        requiredContentWidth(), bounds.getWidth());
    const int utilityButtonSize =
        patternCellSizeForContentWidth(candidateContentWidth);
    const int utilityY = utilityBar.getY();
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

    const int availableSchedulerWidth = std::max(
        0,
        utilityBar.getRight() - utilityButtonSize - perVoiceControlGap
            - utilityLeft);
    barScheduler_.setBounds(
        utilityLeft,
        utilityBar.getY(),
        availableSchedulerWidth,
        utilityBar.getHeight());

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
    positionModulationEditor();
}

void LivePatternSequencerEditor::resizedContent()
{
    constexpr int controlsX = contentHorizontalPadding + playerPanelHorizontalPadding;
    constexpr int resetX = controlsX + clickTargetSize
        + perVoiceControlGap;
    constexpr int muteX = resetX + clickTargetSize
        + perVoiceControlGap;
    constexpr int shiftLeftX = muteX + clickTargetSize
        + perVoiceControlGap;
    constexpr int shiftRightX = shiftLeftX + clickTargetSize
        + perVoiceControlGap;
    constexpr int speedX = shiftRightX + clickTargetSize
        + perVoiceControlGap;
    constexpr int patternPickerX = speedX + clickTargetSize
        + perVoiceControlGap;
    for (std::size_t index = 0; index < groupAssignmentButtons_.size(); ++index)
    {
        const int y = static_cast<int>(index) * playerStride();
        const int buttonSize = cellWidth();
        const int controlY = y + voiceNameHeight
            + (patternPanelHeight() - voiceNameHeight - buttonSize) / 2;
        const int slotInset = (clickTargetSize - buttonSize) / 2;
        groupAssignmentButtons_[index]->setBounds(
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
        speedSelectors_[index]->setBounds(
            speedX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
        patternMenuButtons_[index]->setBounds(
            patternPickerX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
        offsetLeftButtons_[index]->setBounds(
            shiftLeftX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
        offsetRightButtons_[index]->setBounds(
            shiftRightX + slotInset,
            controlY,
            buttonSize,
            buttonSize);
    }
}

void LivePatternSequencerEditor::resizedControlPane()
{
    auto bounds = controlPane_.getLocalBounds().reduced(controlPanePadding);
    const int buttonSize = cellWidth();
    constexpr int minimumSelectionWidth = 100;
    constexpr int groupSectionGap = 12;
    const int voiceCount = static_cast<int>(controlVoiceMuteButtons_.size());
    const int groupCount = static_cast<int>(
        LivePatternSequencerProcessor::groupCount);
    const int groupControlsWidth = groupCount * buttonSize
        + std::max(0, groupCount - 1) * controlPaneControlGap;
    const int voiceGaps = std::max(0, voiceCount - 1)
        * controlPaneControlGap;
    const int availableVoiceWidth = std::max(
        voiceCount * buttonSize,
        bounds.getWidth() - groupControlsWidth - groupSectionGap
            - minimumSelectionWidth);
    const int voiceColumnWidth = voiceCount == 0
        ? 0
        : juce::jlimit(
            buttonSize,
            58,
            (availableVoiceWidth - voiceGaps) / voiceCount);

    if (!controlVoiceMuteButtons_.empty())
    {
        for (std::size_t playerIndex = 0;
             playerIndex < controlVoiceMuteButtons_.size();
             ++playerIndex)
        {
            auto column = bounds.removeFromLeft(voiceColumnWidth);
            controlVoiceLabels_[playerIndex]->setBounds(
                column.removeFromTop(controlPaneLabelHeight));
            column.removeFromTop(2);
            const int buttonX = column.getX()
                + (column.getWidth() - buttonSize) / 2;
            controlVoiceMuteButtons_[playerIndex]->setBounds(
                buttonX, column.getY(), buttonSize, buttonSize);
            bounds.removeFromLeft(controlPaneControlGap);
        }
    }

    bounds.removeFromLeft(groupSectionGap);

    for (std::size_t groupIndex = 0;
         groupIndex < LivePatternSequencerProcessor::groupCount;
         ++groupIndex)
    {
        auto column = bounds.removeFromLeft(buttonSize);
        groupLabels_[groupIndex].setBounds(
            column.removeFromTop(controlPaneLabelHeight));
        column.removeFromTop(2);
        groupMuteButtons_[groupIndex].setBounds(
            column.removeFromTop(buttonSize));
        column.removeFromTop(controlPaneControlGap);
        groupUnmuteButtons_[groupIndex].setBounds(
            column.removeFromTop(buttonSize));
        column.removeFromTop(controlPaneControlGap);
        groupResetButtons_[groupIndex].setBounds(
            column.removeFromTop(buttonSize));
        bounds.removeFromLeft(controlPaneControlGap);
    }
    selectionLabel_.setBounds(bounds);
}

void LivePatternSequencerEditor::resizedModulationEditor()
{
    if (!openModulationPlayer_
        || *openModulationPlayer_ >= playerCount_)
    {
        return;
    }

    constexpr int gap = 4;
    const int buttonSize = std::min(
        clickTargetSize,
        std::max(20, modulationEditor_.getHeight() / 3 - 2));
    const int controlsWidth = buttonSize * 3 + gap * 3;
    std::size_t longestVisibleLane = 1;
    for (const auto lane : modulationLanes)
    {
        longestVisibleLane = std::max(
            longestVisibleLane,
            std::min<std::size_t>(
                lps::Modulation::maxLength,
                processor_.modulationForUi(
                    *openModulationPlayer_, lane).length));
    }
    const int availableGridWidth = std::max(
        1,
        modulationEditor_.getWidth()
            - modulationEditorPadding * 2 - controlsWidth);
    const int desiredCellWidth = std::max(
        preferredModulationValueWidth,
        buttonSize / 2);
    const int dynamicCellWidth = std::max(
        18,
        std::min(
            desiredCellWidth,
            (availableGridWidth
                - (static_cast<int>(longestVisibleLane) - 1)
                    * modulationCellGap)
                / static_cast<int>(longestVisibleLane)));

    auto bounds = modulationEditor_.getLocalBounds().reduced(
        modulationEditorPadding);
    const auto playerIndex = *openModulationPlayer_;
    for (const auto lane : modulationLanes)
    {
        const auto laneSlot = laneIndex(lane);
        auto laneBounds = bounds.removeFromTop(modulationPanelHeight);
        const int buttonY = laneBounds.getY()
            + (laneBounds.getHeight() - buttonSize) / 2;
        modulationMenuButtons_[laneSlot][playerIndex]->setBounds(
            laneBounds.getX(), buttonY, buttonSize, buttonSize);
        laneBounds.removeFromLeft(buttonSize + gap);
        decreaseModulationLengthButtons_[laneSlot][playerIndex]->setBounds(
            laneBounds.getX(), buttonY, buttonSize, buttonSize);
        laneBounds.removeFromLeft(buttonSize + gap);
        increaseModulationLengthButtons_[laneSlot][playerIndex]->setBounds(
            laneBounds.getX(), buttonY, buttonSize, buttonSize);
        laneBounds.removeFromLeft(buttonSize + gap);

        for (std::size_t step = 0; step < lps::Modulation::maxLength; ++step)
        {
            const auto sliderIndex = playerIndex
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

void LivePatternSequencerEditor::toggleModulationEditor(
    std::size_t playerIndex)
{
    if (playerIndex >= playerCount_)
        return;

    if (activePopupKind_ == PopupKind::pattern)
    {
        ++popupGeneration_;
        activePopupKind_ = PopupKind::none;
        activePopupTarget_ = nullptr;
        menuDismissLayer_.setVisible(false);
        juce::PopupMenu::dismissAllActiveMenus();
    }

    if (openModulationPlayer_ && *openModulationPlayer_ == playerIndex)
        openModulationPlayer_.reset();
    else
        openModulationPlayer_ = playerIndex;

    refreshSelectedControls(true);
    positionModulationEditor();
}

void LivePatternSequencerEditor::positionModulationEditor()
{
    if (!openModulationPlayer_
        || *openModulationPlayer_ >= playerCount_)
    {
        modulationBlockLayer_.setVisible(false);
        modulationEditor_.setVisible(false);
        return;
    }

    const auto anchor = getLocalArea(
        &content_, modulationPreviewAreaForPlayer(*openModulationPlayer_));
    if (anchor.getBottom() <= viewport_.getY()
        || anchor.getY() >= viewport_.getBottom())
    {
        modulationBlockLayer_.setVisible(false);
        modulationEditor_.setVisible(false);
        return;
    }
    constexpr int gap = 7;
    const int x = anchor.getRight() + gap;
    const int availableWidth = std::max(0, getWidth() - x - outerPadding);
    if (availableWidth < 180)
    {
        modulationBlockLayer_.setVisible(false);
        modulationEditor_.setVisible(false);
        return;
    }

    const int width = availableWidth;
    const int maximumY = std::max(
        outerPadding,
        viewport_.getBottom() - modulationEditorHeight);
    const int y = juce::jlimit(
        outerPadding,
        maximumY,
        anchor.getY());
    modulationEditor_.setBounds(x, y, width, modulationEditorHeight);
    modulationBlockLayer_.setVisible(true);
    modulationBlockLayer_.toFront(false);
    modulationEditor_.setVisible(true);
    modulationEditor_.toFront(false);
}

void LivePatternSequencerEditor::modulationBlockLayerMouseDown()
{
    if (openModulationPlayer_)
        toggleModulationEditor(*openModulationPlayer_);
}

std::size_t LivePatternSequencerEditor::scheduledBarsFromNow() const noexcept
{
    const auto targetBar = heldScheduleBar();
    if (!targetBar)
        return 1;

    const auto current = processor_.currentBarForUi();
    return offsetForTargetBar(current, *targetBar);
}

bool LivePatternSequencerEditor::playerMutedAtBarOffset(
    std::size_t playerIndex,
    std::size_t barsFromNow) const noexcept
{
    bool muted = processor_.playerMutedForUi(playerIndex);
    for (std::size_t offset = 1; offset <= barsFromNow; ++offset)
    {
        if (processor_.playerMuteScheduledAtBarOffsetForUi(
                playerIndex, true, offset))
        {
            muted = true;
        }
        else if (processor_.playerMuteScheduledAtBarOffsetForUi(
                     playerIndex, false, offset))
        {
            muted = false;
        }
    }
    return muted;
}

std::optional<std::size_t>
LivePatternSequencerEditor::heldScheduleBar() const noexcept
{
    constexpr auto maximum =
        LivePatternSequencerProcessor::maximumScheduledBars;
    for (std::size_t bar = 1; bar <= maximum; ++bar)
    {
        const auto numberKey = static_cast<int>('0' + bar);
        const auto numberPadKey = juce::KeyPress::numberPad0
            + static_cast<int>(bar);
        if (juce::KeyPress::isKeyCurrentlyDown(numberKey)
            || juce::KeyPress::isKeyCurrentlyDown(numberPadKey))
        {
            return bar;
        }
    }
    return std::nullopt;
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
    const auto heldBar = heldScheduleBar();
    if (heldBar != selectedScheduleBar_)
    {
        selectedScheduleBar_ = heldBar;
        barScheduler_.repaint();
    }
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
    const bool transportPlaying = hostPlaying || internalPlaying;
    globalPlayButton_.setToggleState(
        transportPlaying,
        juce::dontSendNotification);
    globalPlayButton_.setKind(transportPlaying
        ? UtilityButton::Kind::pause
        : UtilityButton::Kind::play);
    globalPlayButton_.setEnabled(!hostPlaying);
    globalPlayButton_.setTooltip(hostPlaying
        ? "REAPER is playing; use REAPER's transport to pause or stop"
        : (internalPlaying
            ? "Pause the internal audition clock"
            : "Start the internal audition clock from REAPER's current cursor"));
    content_.repaint();
    controlPane_.repaint();
    barScheduler_.repaint();
    if (modulationEditor_.isVisible())
        modulationEditor_.repaint();
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

void LivePatternSequencerEditor::handleModulationSaveResult(
    std::size_t playerIndex,
    ModulationLane lane,
    LivePatternSequencerProcessor::SaveModulationResult result)
{
    using Status =
        LivePatternSequencerProcessor::SaveModulationStatus;
    switch (result.status)
    {
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
    bool)
{
    if (playerIndex >= selectedVoices_.size())
        return;

    std::fill(selectedVoices_.begin(), selectedVoices_.end(), false);
    selectedVoices_[playerIndex] = true;
    primaryPlayerIndex_ = playerIndex;

    refreshSelectedControls(true);
    content_.repaint();
}

void LivePatternSequencerEditor::refreshSelectedControls(bool force)
{
    const auto currentBar = processor_.currentBarForUi();
    const auto targetOffset = scheduledBarsFromNow();
    const auto selectionCount = selectedVoiceCount();
    const bool hasSelection = selectionCount > 0;

    for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
    {
        std::uint8_t membershipMask = 0;
        for (std::size_t groupIndex = 0;
             groupIndex < LivePatternSequencerProcessor::groupCount;
             ++groupIndex)
        {
            if (processor_.playerInGroupForUi(playerIndex, groupIndex))
                membershipMask |= static_cast<std::uint8_t>(1u << groupIndex);
        }
        groupAssignmentButtons_[playerIndex]->setMembershipMask(membershipMask);
        auto& mute = *muteButtons_[playerIndex];
        const bool targetMuted = playerMutedAtBarOffset(
            playerIndex, targetOffset);
        mute.setToggleState(targetMuted, juce::dontSendNotification);
        mute.setKind(targetMuted
            ? SpeakerButton::Kind::unmute
            : SpeakerButton::Kind::mute);
        mute.setScheduledTarget(targetBarForOffset(
            currentBar,
            processor_.playerMuteChangeBarsRemainingForUi(playerIndex)));
        if (auto* pattern = dynamic_cast<LaneMenuButton*>(
                patternMenuButtons_[playerIndex].get()))
        {
            pattern->setScheduledTarget(targetBarForOffset(
                currentBar,
                processor_.playerPatternChangeBarsRemainingForUi(playerIndex)));
        }

        for (const auto lane : modulationLanes)
        {
            const auto laneSlot = laneIndex(lane);
            auto& menu = *modulationMenuButtons_[laneSlot][playerIndex];
            auto& decrease =
                *decreaseModulationLengthButtons_[laneSlot][playerIndex];
            auto& increase =
                *increaseModulationLengthButtons_[laneSlot][playerIndex];
            const bool locked = processor_.playerModulationLockedForUi(
                playerIndex, lane);
            if (auto* laneMenu = dynamic_cast<LaneMenuButton*>(&menu))
                laneMenu->setLocked(locked);
            menu.setTooltip(locked
                ? modulationLaneName(lane)
                    + " modulation is locked; open this menu to unlock it"
                : "Choose, save, or lock the "
                    + modulationLaneName(lane) + " modulation");
            modulationSelectors_[laneSlot][playerIndex]->setEnabled(!locked);
            saveModulationButtons_[laneSlot][playerIndex]->setEnabled(
                !locked && processor_.playerModulationModifiedForUi(
                    playerIndex, lane));
            const bool show = openModulationPlayer_
                && playerIndex == *openModulationPlayer_
                && processor_.playerSupportsModulationEditingForUi(
                    playerIndex, lane);

            if (show)
            {
                if (menu.getParentComponent() != &modulationEditor_)
                {
                    modulationEditor_.addAndMakeVisible(menu);
                    modulationEditor_.addAndMakeVisible(decrease);
                    modulationEditor_.addAndMakeVisible(increase);
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
                    if (slider.getParentComponent() != &modulationEditor_)
                        modulationEditor_.addChildComponent(slider);
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

    for (std::size_t playerIndex = 0;
         playerIndex < controlVoiceMuteButtons_.size();
         ++playerIndex)
    {
        const bool targetMuted = playerMutedAtBarOffset(
            playerIndex, targetOffset);
        const auto voiceName =
            processor_.playerNameForUi(playerIndex).toUpperCase();
        controlVoiceLabels_[playerIndex]->setText(
            voiceName, juce::dontSendNotification);
        controlVoiceLabels_[playerIndex]->setColour(
            juce::Label::textColourId,
            juce::Colour(primaryText));
        controlVoiceMuteButtons_[playerIndex]->setToggleState(
            targetMuted, juce::dontSendNotification);
        controlVoiceMuteButtons_[playerIndex]->setKind(targetMuted
            ? SpeakerButton::Kind::unmute
            : SpeakerButton::Kind::mute);
        controlVoiceMuteButtons_[playerIndex]->setScheduledTarget(
            targetBarForOffset(
                currentBar,
                processor_.playerMuteChangeBarsRemainingForUi(playerIndex)));
        controlVoiceMuteButtons_[playerIndex]->setEnabled(
            playerIndex < playerCount_);
    }

    for (std::size_t groupIndex = 0;
         groupIndex < LivePatternSequencerProcessor::groupCount;
         ++groupIndex)
    {
        const auto memberCount = processor_.groupPlayerCountForUi(groupIndex);
        std::size_t mutedMemberCount = 0;
        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
            ++playerIndex)
        {
            if (processor_.playerInGroupForUi(playerIndex, groupIndex)
                && playerMutedAtBarOffset(playerIndex, targetOffset))
            {
                ++mutedMemberCount;
            }
        }
        groupMuteButtons_[groupIndex].setEnabled(memberCount != 0);
        groupUnmuteButtons_[groupIndex].setEnabled(memberCount != 0);
        groupMuteButtons_[groupIndex].setToggleState(
            memberCount != 0 && mutedMemberCount == memberCount,
            juce::dontSendNotification);
        groupUnmuteButtons_[groupIndex].setToggleState(
            memberCount != 0 && mutedMemberCount == 0,
            juce::dontSendNotification);
        groupLabels_[groupIndex].setTooltip(
            "Group " + juce::String(static_cast<int>(groupIndex + 1))
            + ": " + juce::String(static_cast<int>(memberCount))
            + (memberCount == 1 ? " voice" : " voices"));

        std::size_t resettableCount = 0;
        std::size_t pendingResetCount = 0;
        for (std::size_t playerIndex = 0; playerIndex < playerCount_; ++playerIndex)
        {
            if (!processor_.playerInGroupForUi(playerIndex, groupIndex)
                || processor_.playerIsMasterForUi(playerIndex)
                || !processor_.playerCanResetToMasterForUi(playerIndex))
            {
                continue;
            }
            ++resettableCount;
            if (processor_.playerResetToMasterPendingForUi(playerIndex))
                ++pendingResetCount;
        }
        groupResetButtons_[groupIndex].setEnabled(
            resettableCount > pendingResetCount);
        groupResetButtons_[groupIndex].setButtonText(
            resettableCount > 0 && resettableCount == pendingResetCount
                ? "Q"
                : "R");

        std::size_t muteOffset = 0;
        std::size_t unmuteOffset = 0;
        for (std::size_t offset = 1;
             offset <= LivePatternSequencerProcessor::maximumScheduledBars;
             ++offset)
        {
            if (muteOffset == 0
                && processor_.groupMuteScheduledAtBarOffsetForUi(
                    groupIndex, true, offset))
            {
                muteOffset = offset;
            }
            if (unmuteOffset == 0
                && processor_.groupMuteScheduledAtBarOffsetForUi(
                    groupIndex, false, offset))
            {
                unmuteOffset = offset;
            }
        }
        groupMuteButtons_[groupIndex].setScheduledTarget(
            muteOffset != 0
                ? targetBarForOffset(currentBar, muteOffset)
                : 0);
        groupUnmuteButtons_[groupIndex].setScheduledTarget(
            unmuteOffset != 0
                ? targetBarForOffset(currentBar, unmuteOffset)
                : 0);
    }

    controlPane_.resized();
    controlPane_.repaint();
    modulationEditor_.resized();
    modulationEditor_.repaint();
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
    positionModulationEditor();
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
    const bool isDisplayedControl = openModulationPlayer_
        && playerIndex == *openModulationPlayer_;
    const bool locked = processor_.playerModulationLockedForUi(
        playerIndex, lane);
    decreaseModulationLengthButtons_[laneSlot][playerIndex]->setEnabled(
        !locked && length > 1);
    increaseModulationLengthButtons_[laneSlot][playerIndex]->setEnabled(
        !locked && length < lps::Modulation::maxLength);

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
        slider.setEnabled(!locked);
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
            saveButton.setEnabled(modified
                && !processor_.playerModulationLockedForUi(
                    playerIndex, lane));
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
            toggleModulationEditor(playerIndex);
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
            clickTargetSize * 6 + perVoiceControlGap * 5,
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
    if (openModulationPlayer_ && *openModulationPlayer_ == playerIndex)
    {
        graphics.setColour(juce::Colour(uiYellow));
        graphics.drawRoundedRectangle(
            bounds.toFloat().reduced(0.5f), 2.0f, 2.0f);
    }

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
        const int currentStep = processor_.playingForUi()
            ? processor_.currentModulationStepForUi(playerIndex, lane)
            : -1;
        constexpr float slotWidth = 5.0f;
        constexpr float modulationBarWidth = 3.5f;
        const auto visibleSteps = std::max<std::size_t>(
            1,
            static_cast<std::size_t>(std::floor(
                static_cast<float>(graphBounds.getWidth()) / slotWidth)));
        std::size_t firstVisibleStep = 0;
        if (valueCount > visibleSteps && currentStep >= 0)
        {
            const auto current = std::min<std::size_t>(
                static_cast<std::size_t>(currentStep), valueCount - 1);
            const auto halfWindow = visibleSteps / 2;
            firstVisibleStep = current > halfWindow
                ? current - halfWindow : 0;
            firstVisibleStep = std::min(
                firstVisibleStep, valueCount - visibleSteps);
        }
        const auto lastVisibleStep = std::min(
            valueCount, firstVisibleStep + visibleSteps);

        for (std::size_t step = firstVisibleStep;
             step < lastVisibleStep;
             ++step)
        {
            const float value = modulation.values[step].toFloat();
            const float barHeight = std::max(
                1.0f,
                static_cast<float>(graphBounds.getHeight()) * value);
            const float barX = static_cast<float>(graphBounds.getX())
                + static_cast<float>(step - firstVisibleStep) * slotWidth;
            const juce::Rectangle<float> bar(
                barX,
                static_cast<float>(graphBounds.getBottom()) - barHeight,
                modulationBarWidth,
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

}

void LivePatternSequencerEditor::paintBarScheduler(juce::Graphics& graphics)
{
    constexpr float trackTopPadding = 5.0f;
    constexpr float cellHeight = 22.0f;
    constexpr float eventTop = trackTopPadding + cellHeight + 6.0f;
    constexpr float eventRowHeight = 19.0f;

    auto bounds = barScheduler_.getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    auto label = bounds.removeFromLeft(
        std::min(barSchedulerLabelWidth, bounds.getWidth()));
    graphics.setColour(juce::Colour(secondaryText));
    graphics.setFont(juce::Font(juce::FontOptions(8.0f, juce::Font::bold)));
    graphics.drawFittedText(
        selectedScheduleBar_
            ? "BAR " + juce::String(static_cast<int>(*selectedScheduleBar_))
                + "\nHELD"
            : juce::String("HOLD\n1-8"),
        label.withY(label.getY() + trackTopPadding)
            .withHeight(cellHeight)
            .toNearestInt(),
        juce::Justification::centred,
        2);

    constexpr auto maximum =
        LivePatternSequencerProcessor::maximumScheduledBars;
    struct ScheduledAction
    {
        ScheduledActionIcon icon;
        juce::String text;
    };
    std::array<std::size_t, maximum> pending {};
    std::array<std::vector<ScheduledAction>, maximum> actions {};
    const auto currentBar = processor_.currentBarForUi();
    const auto appendAction = [&actions](
        std::size_t targetBar,
        ScheduledActionIcon icon,
        const juce::String& text)
    {
        actions[targetBar - 1].push_back({icon, text});
    };
    for (std::size_t offset = 1; offset <= maximum; ++offset)
    {
        const auto count =
            processor_.scheduledChangeCountAtBarOffsetForUi(offset);
        const auto targetBar = targetBarForOffset(currentBar, offset);
        pending[targetBar - 1] += count;

        for (std::size_t playerIndex = 0;
             playerIndex < playerCount_;
             ++playerIndex)
        {
            const auto voiceName =
                processor_.playerNameForUi(playerIndex).toUpperCase();

            if (processor_.playerMuteScheduledAtBarOffsetForUi(
                    playerIndex, true, offset))
            {
                appendAction(
                    targetBar, ScheduledActionIcon::mute, voiceName);
            }
            else if (processor_.playerMuteScheduledAtBarOffsetForUi(
                         playerIndex, false, offset))
            {
                appendAction(
                    targetBar, ScheduledActionIcon::unmute, voiceName);
            }

            const auto pattern =
                processor_.playerPatternScheduledAtBarOffsetForUi(
                    playerIndex, offset);
            if (pattern)
            {
                appendAction(targetBar,
                    ScheduledActionIcon::pattern,
                    voiceName + "  "
                        + processor_.patternNameForUi(*pattern).toUpperCase());
            }
        }
    }

    const float slotWidth = bounds.getWidth() / static_cast<float>(maximum);
    const float cellWidth = std::max(
        1.0f,
        std::min(maximumBarCellWidth, slotWidth - minimumBarTransitionWidth));
    const float transitionWidth = slotWidth - cellWidth;
    const float progress = juce::jlimit(
        0.0f, 1.0f, processor_.currentBarProgressForUi());
    for (std::size_t index = 0; index < maximum; ++index)
    {
        const float x = bounds.getX() + static_cast<float>(index) * slotWidth;
        const juce::Rectangle<float> cell {
            x,
            bounds.getY() + trackTopPadding,
            cellWidth,
            cellHeight
        };
        const bool current = currentBar == index + 1;
        const bool selected = selectedScheduleBar_
            && *selectedScheduleBar_ == index + 1;

        const float markerX = x + cellWidth + transitionWidth * 0.5f;
        if (!actions[index].empty())
        {
            auto actionBounds = juce::Rectangle<float> {
                x + 3.0f,
                bounds.getY() + eventTop,
                std::max(1.0f, slotWidth - 6.0f),
                std::max(
                    1.0f,
                    bounds.getBottom() - bounds.getY() - eventTop)
            };
            const auto availableRows = static_cast<std::size_t>(
                std::max(1.0f, std::floor(
                    actionBounds.getHeight() / eventRowHeight)));
            const auto visibleCount = std::min(
                availableRows, actions[index].size());
            graphics.setFont(juce::Font(
                juce::FontOptions(14.5f, juce::Font::bold)));
            for (std::size_t actionIndex = 0;
                 actionIndex < visibleCount;
                 ++actionIndex)
            {
                auto row = actionBounds.removeFromTop(eventRowHeight);
                auto iconBounds = row.removeFromLeft(22.0f).reduced(0.5f);
                paintScheduledActionIcon(
                    graphics,
                    actions[index][actionIndex].icon,
                    iconBounds,
                    scheduledActionIconColour(
                        actions[index][actionIndex].icon));

                const bool hasOverflow = actionIndex + 1 == visibleCount
                    && actions[index].size() > visibleCount;
                juce::Rectangle<float> overflowBounds;
                if (hasOverflow)
                    overflowBounds = row.removeFromRight(22.0f);
                graphics.setColour(juce::Colour(warmIvory));
                graphics.drawFittedText(
                    actions[index][actionIndex].text,
                    row.toNearestInt(),
                    juce::Justification::centredLeft,
                    1,
                    0.86f);
                if (hasOverflow)
                {
                    graphics.setColour(juce::Colour(secondaryText));
                    graphics.setFont(juce::Font(
                        juce::FontOptions(10.0f, juce::Font::bold)));
                    graphics.drawText(
                        "+" + juce::String(static_cast<int>(
                            actions[index].size() - visibleCount)),
                        overflowBounds,
                        juce::Justification::centredRight);
                    graphics.setFont(juce::Font(
                        juce::FontOptions(14.5f, juce::Font::bold)));
                }
            }
        }

        graphics.setColour(juce::Colour(inactive));
        graphics.fillRoundedRectangle(cell, 2.0f);
        if (current)
        {
            auto swept = cell;
            swept.setWidth(cell.getWidth() * progress);
            graphics.saveState();
            graphics.reduceClipRegion(cell.toNearestInt());
            graphics.setColour(juce::Colour(uiBlue));
            graphics.fillRect(swept);
            graphics.setColour(juce::Colour(uiBlue).brighter(0.25f));
            graphics.fillRect(
                cell.getX() + cell.getWidth() * progress - 1.0f,
                cell.getY(), 2.0f, cell.getHeight());
            graphics.restoreState();
        }

        graphics.setColour(juce::Colour(
            current ? uiBlue : border));
        graphics.drawRoundedRectangle(
            cell, 2.0f, current ? 1.5f : 1.0f);
        graphics.setColour(juce::Colour(
            current && progress > 0.52f ? uiBlack : primaryText));
        graphics.setFont(juce::Font(
            juce::FontOptions(9.0f, juce::Font::bold)));
        graphics.drawText(
            juce::String(static_cast<int>(index + 1)),
            cell,
            juce::Justification::centred);

        const float markerY = cell.getCentreY();
        const float markerRadius = std::min(
            selected ? 11.0f : 10.0f,
            std::max(1.0f, transitionWidth * 0.5f - 3.0f));
        paintScheduleMarker(
            graphics,
            index + 1,
            {markerX, markerY},
            markerRadius,
            juce::Colour(selected
                ? uiYellow
                : (pending[index] != 0 ? warmIvory : lightGray))
                .withMultipliedAlpha(
                    selected || pending[index] != 0 ? 1.0f : 0.45f),
            selected || pending[index] != 0);

        if (pending[index] > 1
            && markerX + 11.0f < x + slotWidth)
        {
            graphics.setColour(juce::Colour(primaryText));
            graphics.setFont(juce::Font(
                juce::FontOptions(8.0f, juce::Font::bold)));
            graphics.drawText(
                juce::String(static_cast<int>(pending[index])),
                juce::Rectangle<float> {
                    markerX + 10.0f,
                    cell.getY(),
                    std::max(1.0f, x + slotWidth - markerX - 11.0f),
                    cell.getHeight()},
                juce::Justification::centredLeft);
        }
    }
}

void LivePatternSequencerEditor::paintModulationEditor(
    juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    const auto bounds = modulationEditor_.getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bounds.reduced(0.5f), 5.0f);
    graphics.setColour(juce::Colour(uiYellow));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.5f);

    graphics.setColour(juce::Colour(border).brighter(0.08f));
    for (std::size_t lane = 1; lane < modulationLanes.size(); ++lane)
    {
        const int y = modulationEditorPadding
            + static_cast<int>(lane) * modulationPanelHeight;
        graphics.drawHorizontalLine(
            y,
            static_cast<float>(modulationEditorPadding),
            static_cast<float>(modulationEditor_.getWidth()
                - modulationEditorPadding));
    }
}

std::uint64_t LivePatternSequencerEditor::beginPopupSession(
    PopupKind kind,
    juce::Component* target)
{
    if (activePopupKind_ != PopupKind::none)
        juce::PopupMenu::dismissAllActiveMenus();

    ++popupGeneration_;
    activePopupKind_ = kind;
    activePopupTarget_ = target;
    menuDismissLayer_.setVisible(true);
    menuDismissLayer_.toFront(false);
    return popupGeneration_;
}

void LivePatternSequencerEditor::finishPopupSession(
    std::uint64_t generation)
{
    if (generation != popupGeneration_)
        return;
    activePopupKind_ = PopupKind::none;
    activePopupTarget_ = nullptr;
    menuDismissLayer_.setVisible(false);
}

void LivePatternSequencerEditor::menuDismissLayerMouseDown(
    const juce::MouseEvent& event)
{
    const auto screenPosition = event.getScreenPosition();
    juce::Button* requestedButton = nullptr;
    const auto consider = [&](juce::Button* button)
    {
        if (requestedButton == nullptr
            && button != nullptr
            && button->isShowing()
            && button->isEnabled()
            && button->getScreenBounds().contains(screenPosition))
        {
            requestedButton = button;
        }
    };

    for (auto& button : patternMenuButtons_)
        consider(button.get());
    for (auto& laneButtons : modulationMenuButtons_)
        for (auto& button : laneButtons)
            consider(button.get());
    consider(&suppressionButton_);
    for (auto& button : groupAssignmentButtons_)
        consider(button.get());
    for (auto& button : resetToMasterButtons_)
        consider(button.get());
    for (auto& button : muteButtons_)
        consider(button.get());
    for (auto& button : offsetLeftButtons_)
        consider(button.get());
    for (auto& button : offsetRightButtons_)
        consider(button.get());

    // The popup's async dismissal callback can clear activePopupKind_ before
    // this mouse-down reaches the overlay.  Base the hand-off on the button
    // under the pointer instead, so clicking another menu button both closes
    // the current popup and opens the requested one with the same click.
    const bool activateDifferentButton = requestedButton != nullptr
        && requestedButton != activePopupTarget_;
    const juce::Component::SafePointer<juce::Button> safeButton(
        requestedButton);

    ++popupGeneration_;
    activePopupKind_ = PopupKind::none;
    activePopupTarget_ = nullptr;
    menuDismissLayer_.setVisible(false);
    juce::PopupMenu::dismissAllActiveMenus();

    if (activateDifferentButton)
    {
        juce::Timer::callAfterDelay(1, [safeButton]
        {
            if (safeButton != nullptr)
                safeButton->triggerClick();
        });
    }
}

void LivePatternSequencerEditor::showSuppressionMatrix()
{
    if (suppressionMenuOpen_)
    {
        suppressionMenuOpen_ = false;
        suppressionButton_.setToggleState(false, juce::dontSendNotification);
        juce::PopupMenu::dismissAllActiveMenus();
        finishPopupSession(popupGeneration_);
        return;
    }

    suppressionMenuOpen_ = true;
    suppressionButton_.setToggleState(true, juce::dontSendNotification);
    juce::PopupMenu menu;
    menu.addCustomItem(
        1,
        std::make_unique<MatrixComponent>(*this),
        nullptr,
        "Suppression Matrix");

    const auto popupGeneration = beginPopupSession(
        PopupKind::matrix, &suppressionButton_);
    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    const auto buttonBounds = suppressionButton_.getScreenBounds();
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(&suppressionButton_)
            .withTargetScreenArea(
                juce::Rectangle<int> {}.withPosition(
                    buttonBounds.getX(), buttonBounds.getCentreY())),
        [safeThis, popupGeneration](int)
        {
            if (safeThis == nullptr)
                return;
            safeThis->finishPopupSession(popupGeneration);
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

    if (openModulationPlayer_)
    {
        openModulationPlayer_.reset();
        refreshSelectedControls(true);
        modulationBlockLayer_.setVisible(false);
        modulationEditor_.setVisible(false);
        content_.repaint();
    }

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

    const auto popupGeneration = beginPopupSession(
        PopupKind::pattern, targetComponent);
    const auto targetBar = heldScheduleBar();
    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    auto menuOptions = juce::PopupMenu::Options {}
        .withTargetComponent(targetComponent);
    if (targetComponent != nullptr)
    {
        const auto buttonBounds = targetComponent->getScreenBounds();
        const auto gridAnchor = content_.localPointToGlobal(
            juce::Point<int> {patternGridLeft, 0});
        menuOptions = menuOptions.withTargetScreenArea(
            juce::Rectangle<int> {}.withPosition(
                gridAnchor.x, buttonBounds.getCentreY()));
    }
    menu.showMenuAsync(
        menuOptions,
        [safeThis, playerIndex, popupGeneration, targetBar](int result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->finishPopupSession(popupGeneration);
            if (result == 0)
                return;

            if (result == saveItemId)
            {
                safeThis->beginSavePlayerPattern(playerIndex);
                return;
            }

            const auto patternIndex = static_cast<std::size_t>(result - 1);
            if (patternIndex < safeThis->processor_.patternCountForUi())
            {
                const auto barsFromNow = targetBar
                    ? offsetForTargetBar(
                        safeThis->processor_.currentBarForUi(), *targetBar)
                    : 1;
                safeThis->applyToSelectedPatternPlayers(
                    [safeThis, patternIndex, barsFromNow](
                        std::size_t selectedPlayerIndex)
                    {
                        if (safeThis != nullptr)
                        {
                            (void) safeThis->processor_.schedulePatternForPlayer(
                                selectedPlayerIndex,
                                patternIndex,
                                barsFromNow);
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
    constexpr int lockItemId = 20001;
    juce::PopupMenu menu;
    const bool locked = processor_.playerModulationLockedForUi(
        playerIndex, lane);
    menu.addItem(
        lockItemId,
        locked ? "UNLOCK MODULATION" : "LOCK MODULATION",
        true,
        locked);
    menu.addSeparator();
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
                modulationIndex == selected,
                locked),
            nullptr,
            processor_.modulationNameForUi(modulationIndex));
    }
    menu.addSeparator();
    const auto editedModulation =
        processor_.modulationForUi(playerIndex, lane);
    menu.addItem(
        saveItemId,
        "SAVE MODULATION",
        !locked && editedModulation.length > 1
            && processor_.playerModulationModifiedForUi(playerIndex, lane));

    auto* target = modulationMenuButtons_[laneSlot][playerIndex].get();
    const auto popupGeneration = beginPopupSession(
        PopupKind::modulation, target);
    const juce::Component::SafePointer<LivePatternSequencerEditor> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(target),
        [safeThis, playerIndex, lane, popupGeneration](int result)
        {
            if (safeThis == nullptr)
                return;
            safeThis->finishPopupSession(popupGeneration);
            if (result == 0)
                return;

            if (result == saveItemId)
            {
                safeThis->beginSavePlayerModulation(playerIndex, lane);
                return;
            }

            if (result == lockItemId)
            {
                const bool currentlyLocked =
                    safeThis->processor_.playerModulationLockedForUi(
                        playerIndex, lane);
                safeThis->processor_.setPlayerModulationLocked(
                    playerIndex, lane, !currentlyLocked);
                safeThis->refreshModulationControls(
                    playerIndex, lane, true);
                safeThis->refreshSelectedControls(true);
                safeThis->content_.repaint();
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
    graphics.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    graphics.drawText("ROW suppresses COLUMN", 8, 3,
        matrix.getWidth() - 16, 16,
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
        constexpr float columnCentreY = static_cast<float>(matrixGridTop - 32);
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
            4,
            matrixGridTop + static_cast<int>(index) * matrixCellHeight + 4,
            matrixGridLeft - 9,
            24,
            juce::Justification::centredRight,
            1,
            0.8f);
    }
}
