#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lps
{

struct PrepareSpec
{
    double sampleRate = 44'100.0;
    std::uint32_t maximumBlockSize = 0;
};

struct TimelineBlock
{
    double ppqStart = 0.0;
    double ppqEnd = 0.0;
    double tempoBpm = 120.0;
    double sampleRate = 44'100.0;
    std::uint32_t sampleCount = 0;
    bool playing = false;
    bool transportDiscontinuity = false;
};

struct PlayerDirectives
{
    // Unlike a transport discontinuity, this restarts only player phase and
    // does not imply that the host timeline moved.
    std::optional<double> restartAtPpq;
    // Followers may use an external cycle as the quantization grid for their
    // own pending transitions.
    std::optional<double> externalCycleBoundaryPpq;
    bool quantizePendingTransitionsExternally = false;
};

struct PlayerProcessResult
{
    bool active = false;
    // First cycle start in the half-open TimelineBlock interval
    // [ppqStart, ppqEnd), whether or not it emitted a trigger.
    std::optional<double> firstCycleBoundaryPpq;
    bool eventOverflow = false;
};

struct PlayerSyncCapabilities
{
    bool providesCycleBoundaries = false;
    bool acceptsExternalCycleBoundaries = false;
    bool acceptsExternalRestart = false;
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

    void clear() noexcept
    {
        size_ = 0;
        overflowed_ = false;
        droppedCount_ = 0;
    }

    [[nodiscard]] bool push(SequencerEvent event) noexcept
    {
        if (size_ == capacity)
        {
            overflowed_ = true;
            ++droppedCount_;
            return false;
        }

        events_[size_++] = event;
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] std::size_t droppedCount() const noexcept { return droppedCount_; }
    [[nodiscard]] const SequencerEvent& operator[](std::size_t index) const noexcept
    {
        return events_[index];
    }

    [[nodiscard]] const SequencerEvent* begin() const noexcept { return events_.data(); }
    [[nodiscard]] const SequencerEvent* end() const noexcept { return events_.data() + size_; }

private:
    std::array<SequencerEvent, capacity> events_ {};
    std::size_t size_ = 0;
    bool overflowed_ = false;
    std::size_t droppedCount_ = 0;
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
        if (sourceStep >= 32)
            return false;
        return (hitMask & (std::uint32_t { 1 } << sourceStep)) != 0;
    }

    [[nodiscard]] bool isInsidePlaybackWindow(std::uint16_t step) const noexcept
    {
        return step < stepCount && step >= playbackStart && step <= playbackEnd;
    }
};

struct PatternPlaybackSnapshot
{
    int currentStep = -1;
    bool playing = false;
};

struct ModulationPlaybackSnapshot
{
    // Modulation can advance independently of pattern steps.
    int currentStep = -1;
};

} // namespace lps
