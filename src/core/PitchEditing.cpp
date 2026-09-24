#include "core/PitchEditing.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <initializer_list>

namespace lps
{
namespace
{
constexpr std::uint16_t mask(std::initializer_list<int> pitchClasses) noexcept
{
    std::uint16_t result = 0;
    for (const auto pitchClass : pitchClasses)
        result |= static_cast<std::uint16_t>(1u << pitchClass);
    return result;
}

constexpr std::array<ScaleDefinition, 10> scales {{
    {ScaleId::major, "Major", mask({0, 2, 4, 5, 7, 9, 11})},
    {ScaleId::naturalMinor, "Natural Minor", mask({0, 2, 3, 5, 7, 8, 10})},
    {ScaleId::harmonicMinor, "Harmonic Minor", mask({0, 2, 3, 5, 7, 8, 11})},
    {ScaleId::melodicMinor, "Melodic Minor", mask({0, 2, 3, 5, 7, 9, 11})},
    {ScaleId::dorian, "Dorian", mask({0, 2, 3, 5, 7, 9, 10})},
    {ScaleId::phrygian, "Phrygian", mask({0, 1, 3, 5, 7, 8, 10})},
    {ScaleId::lydian, "Lydian", mask({0, 2, 4, 6, 7, 9, 11})},
    {ScaleId::mixolydian, "Mixolydian", mask({0, 2, 4, 5, 7, 9, 10})},
    {ScaleId::locrian, "Locrian", mask({0, 1, 3, 5, 6, 8, 10})},
    {ScaleId::phrygianDominant, "Phrygian Dominant",
        mask({0, 1, 4, 5, 7, 8, 10})}
}};

constexpr std::array<const char*, 12> pitchClassNames {{
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
}};
} // namespace

const char* pitchClassName(std::size_t pitchClass) noexcept
{
    return pitchClassNames[pitchClass % pitchClassNames.size()];
}

std::size_t scaleCount() noexcept
{
    return scales.size();
}

const ScaleDefinition* scaleDefinitionAt(std::size_t index) noexcept
{
    return index < scales.size() ? &scales[index] : nullptr;
}

const ScaleDefinition* scaleDefinition(ScaleId id) noexcept
{
    const auto found = std::find_if(
        scales.begin(), scales.end(),
        [id](const auto& scale) { return scale.id == id; });
    return found != scales.end() ? &*found : nullptr;
}

bool midiNoteBelongsToScale(
    int midiNote,
    std::uint8_t rootPitchClass,
    ScaleId scaleId) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return false;
    const auto* scale = scaleDefinition(scaleId);
    if (scale == nullptr)
        return false;
    const int relativePitchClass =
        (midiNote - static_cast<int>(rootPitchClass % 12u) + 12) % 12;
    return (scale->pitchClassMask & (1u << relativePitchClass)) != 0;
}

std::uint8_t editedPitchValue(
    std::uint8_t currentValue,
    int direction,
    PitchEditContext context) noexcept
{
    const int currentNote = midiNoteFromModulation(currentValue);
    if (direction == 0)
        return canonicalModulationValue(currentNote);

    const int step = direction > 0 ? 1 : -1;
    if (context.mode == PitchEditMode::chromatic)
    {
        const int candidate = currentNote + step;
        return candidate >= 0 && candidate <= 127
            ? canonicalModulationValue(candidate)
            : canonicalModulationValue(currentNote);
    }

    for (int candidate = currentNote + step;
         candidate >= 0 && candidate <= 127;
         candidate += step)
    {
        if (midiNoteBelongsToScale(
                candidate, context.rootPitchClass, context.scaleId))
        {
            return canonicalModulationValue(candidate);
        }
    }
    return canonicalModulationValue(currentNote);
}

int nearestScaleMidiNote(
    int midiNote,
    std::uint8_t rootPitchClass,
    ScaleId scaleId) noexcept
{
    midiNote = std::clamp(midiNote, 0, 127);
    if (midiNoteBelongsToScale(midiNote, rootPitchClass, scaleId))
        return midiNote;

    for (int distance = 1; distance <= 127; ++distance)
    {
        const int lower = midiNote - distance;
        if (lower >= 0
            && midiNoteBelongsToScale(lower, rootPitchClass, scaleId))
        {
            return lower;
        }
        const int upper = midiNote + distance;
        if (upper <= 127
            && midiNoteBelongsToScale(upper, rootPitchClass, scaleId))
        {
            return upper;
        }
    }
    return midiNote;
}

Modulation adjustedToScale(
    const Modulation& source,
    std::uint8_t rootPitchClass,
    ScaleId scaleId) noexcept
{
    auto result = source;
    const auto length = std::min<std::size_t>(
        result.length, Modulation::maxLength);
    for (std::size_t step = 0; step < length; ++step)
    {
        const auto raw = static_cast<std::uint8_t>(
            result.values[step].raw / 257u);
        result.values[step] = NormalizedValue::fromUnipolar8(
            canonicalModulationValue(nearestScaleMidiNote(
                midiNoteFromModulation(raw), rootPitchClass, scaleId)));
    }
    return result;
}

std::string midiNoteName(int midiNote)
{
    midiNote = std::clamp(midiNote, 0, 127);
    return std::string(pitchClassName(static_cast<std::size_t>(midiNote % 12)))
        + std::to_string(midiNote / 12 - 1);
}

std::string pitchValueDisplay(std::uint8_t modulationValue)
{
    return midiNoteName(midiNoteFromModulation(modulationValue))
        + " \xC2\xB7 " + std::to_string(modulationValue);
}

} // namespace lps
