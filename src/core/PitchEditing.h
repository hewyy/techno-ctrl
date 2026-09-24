#pragma once

#include "core/ModulationLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lps
{

enum class PitchEditMode : std::uint8_t
{
    chromatic,
    scaleAware
};

enum class ScaleId : std::uint8_t
{
    major,
    naturalMinor,
    harmonicMinor,
    melodicMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    phrygianDominant
};

struct ScaleDefinition
{
    ScaleId id = ScaleId::major;
    const char* name = "Major";
    std::uint16_t pitchClassMask = 0;
};

struct PitchEditContext
{
    PitchEditMode mode = PitchEditMode::chromatic;
    std::uint8_t rootPitchClass = 0;
    ScaleId scaleId = ScaleId::major;
};

// Tracks the direction of the user's unquantized input separately from the
// value accepted by a quantized editor. During a drag, snapping the displayed
// value must not make the next upward input look like a downward edit.
class QuantizedValueEditTracker
{
public:
    void beginGesture(int value) noexcept
    {
        gestureActive_ = true;
        previousInput_ = value;
    }

    [[nodiscard]] int directionForInput(int value) noexcept
    {
        const int direction = value > previousInput_
            ? 1 : value < previousInput_ ? -1 : 0;
        previousInput_ = value;
        return direction;
    }

    void syncAcceptedValue(int value) noexcept
    {
        if (!gestureActive_)
            previousInput_ = value;
    }

    void endGesture(int value) noexcept
    {
        gestureActive_ = false;
        previousInput_ = value;
    }

private:
    int previousInput_ = 0;
    bool gestureActive_ = false;
};

[[nodiscard]] constexpr int midiNoteFromModulation(
    std::uint8_t modulationValue) noexcept
{
    return modulationValue / 2;
}

[[nodiscard]] constexpr std::uint8_t canonicalModulationValue(
    int midiNote) noexcept
{
    return static_cast<std::uint8_t>(
        (midiNote < 0 ? 0 : midiNote > 127 ? 127 : midiNote) * 2);
}

[[nodiscard]] constexpr std::size_t pitchClassCount() noexcept { return 12; }
[[nodiscard]] const char* pitchClassName(std::size_t pitchClass) noexcept;
[[nodiscard]] std::size_t scaleCount() noexcept;
[[nodiscard]] const ScaleDefinition* scaleDefinitionAt(
    std::size_t index) noexcept;
[[nodiscard]] const ScaleDefinition* scaleDefinition(ScaleId id) noexcept;

[[nodiscard]] bool midiNoteBelongsToScale(
    int midiNote,
    std::uint8_t rootPitchClass,
    ScaleId scaleId) noexcept;

// Returns a canonical modulation value. At the MIDI boundaries, where no
// next/previous note exists, the current note is retained.
[[nodiscard]] std::uint8_t editedPitchValue(
    std::uint8_t currentValue,
    int direction,
    PitchEditContext context) noexcept;

[[nodiscard]] int nearestScaleMidiNote(
    int midiNote,
    std::uint8_t rootPitchClass,
    ScaleId scaleId) noexcept;

// Returns an edited copy; source is never modified.
[[nodiscard]] Modulation adjustedToScale(
    const Modulation& source,
    std::uint8_t rootPitchClass,
    ScaleId scaleId) noexcept;

[[nodiscard]] std::string midiNoteName(int midiNote);
[[nodiscard]] std::string pitchValueDisplay(std::uint8_t modulationValue);

} // namespace lps
