#pragma once

#include "core/SequencerTypes.h"

#include <cstddef>

namespace lps
{

struct RenderSpec
{
    double sampleRate = 44'100.0;
    std::uint32_t maximumBlockSize = 0;
};

class RoutedEventView
{
public:
    RoutedEventView() noexcept = default;
    RoutedEventView(const RoutedEvent* events, std::size_t size) noexcept
        : events_(events), size_(size)
    {
    }

    [[nodiscard]] const RoutedEvent* begin() const noexcept { return events_; }
    [[nodiscard]] const RoutedEvent* end() const noexcept { return events_ + size_; }
    [[nodiscard]] const RoutedEvent& operator[](std::size_t index) const noexcept
    {
        return events_[index];
    }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

private:
    const RoutedEvent* events_ = nullptr;
    std::size_t size_ = 0;
};

class IOutputRenderer
{
public:
    virtual ~IOutputRenderer() = default;
    virtual void prepare(const RenderSpec& spec) noexcept = 0;
    [[nodiscard]] virtual bool renderBlock(
        const TimelineBlock& block,
        RoutedEventView events) noexcept = 0;
    virtual void resetOutputs() noexcept = 0;
};

} // namespace lps
