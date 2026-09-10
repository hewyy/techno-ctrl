#pragma once

#include "core/IPlayer.h"

#include <array>
#include <cstdint>
#include <limits>

namespace lps
{

// This class is intentionally small and replaceable. It proves the complete
// host-clock -> core -> MIDI path while the real playback core is developed.
class DemoPlaybackEngine final : public IPlayer
{
public:
    static constexpr std::uint16_t patternStepCount = 13;
    static constexpr std::array<std::uint16_t, 4> patternHits { 0, 3, 7, 12 };

    void prepare(double sampleRate) noexcept override;
    void reset() noexcept override;
    void process(const ClockBlock& block, NoteEventBuffer& output) noexcept override;

    [[nodiscard]] PatternView pattern() const noexcept override;
    [[nodiscard]] PlaybackSnapshot snapshot() const noexcept override;

private:
    static constexpr double stepLengthPpq = 0.25; // One sixteenth note.
    static constexpr double gateRatio = 0.5;
    static constexpr std::uint8_t midiChannel = 1;
    static constexpr std::uint8_t midiNote = 60;
    static constexpr std::uint8_t midiVelocity = 100;

    [[nodiscard]] static int wrapStep(std::int64_t step) noexcept;
    [[nodiscard]] static std::uint32_t sampleOffsetFor(
        double eventPpq,
        const ClockBlock& block,
        double ppqPerSample) noexcept;

    void addNoteOn(NoteEventBuffer& output, std::uint32_t sampleOffset) noexcept;
    void addNoteOff(NoteEventBuffer& output, std::uint32_t sampleOffset) noexcept;

    double configuredSampleRate_ = 44'100.0;
    double pendingNoteOffPpq_ = std::numeric_limits<double>::infinity();
    std::int64_t lastTriggeredAbsoluteStep_ = std::numeric_limits<std::int64_t>::min();
    bool noteIsOn_ = false;
    PlaybackSnapshot snapshot_ {};
};

} // namespace lps

