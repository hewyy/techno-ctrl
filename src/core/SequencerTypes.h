#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lps
{

struct ClockBlock
{
    double ppqStart = 0.0;
    double ppqEnd = 0.0;
    double tempoBpm = 120.0;
    double sampleRate = 44'100.0;
    std::uint32_t sampleCount = 0;
    bool playing = false;
    bool transportDiscontinuity = false;
    // Per-player directive supplied by SequencerEngine. Unlike a transport
    // discontinuity, this only restarts the sequence phase; it does not imply
    // that the host transport moved.
    bool sequenceReset = false;
    double sequenceResetPpq = 0.0;
    // Followers use the master's loop starts as their pattern-selection
    // quantization grid. The synchronization flag remains set between
    // boundaries so they cannot activate a pending selection at their own
    // loop start instead.
    bool patternChangesFollowMaster = false;
    bool masterCycleBoundary = false;
    double masterCycleBoundaryPpq = 0.0;
};

struct CycleBoundarySnapshot
{
    // The first loop start produced in the most recent half-open ClockBlock
    // interval [ppqStart, ppqEnd). A loop start is reported independently of
    // whether the first pattern step contains a hit.
    double ppqPosition = 0.0;
    bool valid = false;
};

enum class SequencerEventType : std::uint8_t
{
    triggerOn,
    triggerOff
};

struct SequencerEvent
{
    double ppqPosition = 0.0;
    std::uint16_t output = 0;
    float pitchSemitones = 60.0f;
    float value = 0.0f;
    SequencerEventType type = SequencerEventType::triggerOff;
};

class SequencerEventBuffer
{
public:
    static constexpr std::size_t capacity = 128;

    void clear() noexcept { size_ = 0; }

    [[nodiscard]] bool push(SequencerEvent event) noexcept
    {
        if (size_ == capacity)
            return false;

        events_[size_++] = event;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] const SequencerEvent& operator[](std::size_t index) const noexcept
    {
        return events_[index];
    }

    [[nodiscard]] const SequencerEvent* begin() const noexcept { return events_.data(); }
    [[nodiscard]] const SequencerEvent* end() const noexcept { return events_.data() + size_; }

private:
    std::array<SequencerEvent, capacity> events_ {};
    std::size_t size_ = 0;
};

struct Pattern
{
    static constexpr std::size_t maxLength = 32;
    std::array<bool, maxLength> hits {};
    std::size_t length = 0;
};

struct PitchList
{
    std::array<std::uint8_t, 4> values {60, 60, 60, 60};
};

struct PatternView
{
    // The per-player draft capacity. A library Pattern::length instead sets
    // the default playback end when that pattern is loaded.
    std::uint16_t stepCount = 0;
    std::uint32_t hitMask = 0;
    int stepOffset = 0;
    std::uint16_t playbackStart = 0;
    std::uint16_t playbackEnd = 0;

    [[nodiscard]] bool isHit(std::uint16_t step) const noexcept
    {
        if (stepCount == 0 || step >= stepCount)
            return false;

        const auto length = static_cast<int>(stepCount);
        auto sourceStep = (static_cast<int>(step) - stepOffset) % length;
        if (sourceStep < 0)
            sourceStep += length;
        return (hitMask & (std::uint32_t { 1 } << sourceStep)) != 0;
    }

    [[nodiscard]] bool isInsidePlaybackWindow(std::uint16_t step) const noexcept
    {
        return step < stepCount && step >= playbackStart && step <= playbackEnd;
    }
};

struct PlaybackSnapshot
{
    int currentStep = -1;
    bool playing = false;
    // Velocity modulations advance on hits rather than pattern steps, so this
    // playhead cannot be derived from currentStep.
    int currentVelocityModulationStep = -1;
};


} // namespace lps
