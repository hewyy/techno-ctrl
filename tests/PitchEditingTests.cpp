#include "core/PitchEditing.h"

#include <cstdlib>
#include <iostream>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: "
              << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

void testFixedMappingAndNames()
{
    CHECK(lps::midiNoteFromModulation(120) == 60);
    CHECK(lps::midiNoteFromModulation(121) == 60);
    CHECK(lps::canonicalModulationValue(60) == 120);
    CHECK(lps::pitchValueDisplay(120) == "C4 \xC2\xB7 120");
    CHECK(lps::pitchValueDisplay(126) == "D#4 \xC2\xB7 126");
}

void testChromaticAndScaleAwareEditing()
{
    lps::PitchEditContext context;
    CHECK(lps::editedPitchValue(120, 1, context) == 122);
    CHECK(lps::editedPitchValue(120, -1, context) == 118);
    CHECK(lps::editedPitchValue(0, -1, context) == 0);
    CHECK(lps::editedPitchValue(255, 1, context) == 254);

    context.mode = lps::PitchEditMode::scaleAware;
    context.rootPitchClass = 11; // B
    context.scaleId = lps::ScaleId::phrygianDominant;
    CHECK(lps::midiNoteBelongsToScale(59, 11,
        lps::ScaleId::phrygianDominant)); // B3
    CHECK(lps::midiNoteBelongsToScale(60, 11,
        lps::ScaleId::phrygianDominant)); // C4
    CHECK(lps::midiNoteBelongsToScale(63, 11,
        lps::ScaleId::phrygianDominant)); // D#4
    CHECK(lps::editedPitchValue(118, 1, context) == 120); // B3 -> C4
    CHECK(lps::editedPitchValue(120, 1, context) == 126); // C4 -> D#4
    CHECK(lps::editedPitchValue(126, 1, context) == 128); // D#4 -> E4
    CHECK(lps::editedPitchValue(122, 1, context) == 126); // off-scale C#4
    CHECK(lps::editedPitchValue(122, -1, context) == 120);
}

void testQuantizedValueEditDirectionTracksInputRatherThanSnappedValue()
{
    lps::QuantizedValueEditTracker tracker;
    tracker.syncAcceptedValue(120);
    tracker.beginGesture(120);

    CHECK(tracker.directionForInput(121) == 1);
    tracker.syncAcceptedValue(126);
    CHECK(tracker.directionForInput(122) == 1);
    tracker.syncAcceptedValue(128);
    CHECK(tracker.directionForInput(121) == -1);

    tracker.endGesture(126);
    CHECK(tracker.directionForInput(127) == 1);
    tracker.syncAcceptedValue(132);
    CHECK(tracker.directionForInput(131) == -1);
}

void testAdjustToScaleIsCopyingCanonicalAndTieBreaksDown()
{
    lps::Modulation original;
    original.length = 4;
    original.values[0] = lps::NormalizedValue::fromUnipolar8(118); // B3
    original.values[1] = lps::NormalizedValue::fromUnipolar8(122); // C#4
    original.values[2] = lps::NormalizedValue::fromUnipolar8(126); // D#4
    original.values[3] = lps::NormalizedValue::fromUnipolar8(130); // F4

    const auto adjusted = lps::adjustedToScale(
        original, 11, lps::ScaleId::phrygianDominant);
    CHECK(original.values[1]
        == lps::NormalizedValue::fromUnipolar8(122));
    CHECK(adjusted.values[0]
        == lps::NormalizedValue::fromUnipolar8(118));
    CHECK(adjusted.values[1]
        == lps::NormalizedValue::fromUnipolar8(120));
    CHECK(adjusted.values[2]
        == lps::NormalizedValue::fromUnipolar8(126));
    CHECK(adjusted.values[3]
        == lps::NormalizedValue::fromUnipolar8(128));

    // F is equidistant from E and F# in B Phrygian Dominant.
    CHECK(lps::nearestScaleMidiNote(
        65, 11, lps::ScaleId::phrygianDominant) == 64);
    for (std::size_t step = 0; step < adjusted.length; ++step)
    {
        const auto raw = static_cast<std::uint8_t>(
            adjusted.values[step].raw / 257u);
        CHECK((raw % 2u) == 0);
        CHECK(lps::midiNoteBelongsToScale(
            lps::midiNoteFromModulation(raw), 11,
            lps::ScaleId::phrygianDominant));
    }
}
} // namespace

int main()
{
    testFixedMappingAndNames();
    testChromaticAndScaleAwareEditing();
    testQuantizedValueEditDirectionTracksInputRatherThanSnappedValue();
    testAdjustToScaleIsCopyingCanonicalAndTieBreaksDown();
    std::cout << "Pitch editing tests passed\n";
    return EXIT_SUCCESS;
}
