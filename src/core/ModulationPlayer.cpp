#include "core/ModulationPlayer.h"

#include <algorithm>
#include <cmath>

namespace lps
{

ModulationPlayer::ModulationPlayer(
    const ModulationLibrary& library,
    ModulationPlayerId id) noexcept
    : library_(library), id_(id)
{
    if (const auto* initial = library_.recordAt(0))
    {
        requestedModulationId_.store(initial->id.value(), std::memory_order_relaxed);
        (void) activateRequestedSelection();
    }
}

void ModulationPlayer::setAdvanceSource(AdvanceSource source) noexcept
{
    if (const auto* clock = std::get_if<ClockAdvance>(&source);
        clock != nullptr
        && (!std::isfinite(clock->stepLengthPpq) || clock->stepLengthPpq <= 0.0))
    {
        return;
    }
    advanceSource_ = source;
}

const AdvanceSource& ModulationPlayer::advanceSource() const noexcept
{
    return advanceSource_;
}

void ModulationPlayer::selectModulation(ModulationId id) noexcept
{
    if (library_.find(id) != nullptr)
        requestedModulationId_.store(id.value(), std::memory_order_release);
}

ModulationId ModulationPlayer::selectedModulationId() const noexcept
{
    return ModulationId {
        requestedModulationId_.load(std::memory_order_acquire) };
}

ModulationId ModulationPlayer::activeModulationId() const noexcept
{
    return ModulationId {
        activeModulationId_.load(std::memory_order_acquire) };
}

ModulationPlayer::DraftWriteGuard::DraftWriteGuard(
    ModulationPlayer& owner) noexcept
    : owner_(owner)
{
    for (;;)
    {
        auto revision = owner_.draftRevision_.load(std::memory_order_acquire);
        if ((revision & 1u) != 0)
            continue;
        if (owner_.draftRevision_.compare_exchange_weak(
                revision,
                revision + 1,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            previousRevision_ = revision;
            return;
        }
    }
}

ModulationPlayer::DraftWriteGuard::~DraftWriteGuard()
{
    owner_.draftRevision_.store(previousRevision_ + 2, std::memory_order_release);
}

bool ModulationPlayer::activateRequestedSelection() noexcept
{
    const auto requested = selectedModulationId();
    if (requested == activeModulationId())
        return true;

    const auto* entry = library_.find(requested);
    if (entry == nullptr || entry->modulation.length == 0)
        return false;

    const DraftWriteGuard guard {*this};
    for (std::size_t step = 0; step < Modulation::maxLength; ++step)
        values_[step].store(entry->modulation.values[step].raw, std::memory_order_relaxed);
    length_.store(entry->modulation.length, std::memory_order_relaxed);
    activeModulationId_.store(requested.value(), std::memory_order_release);
    resetPosition();
    return true;
}

void ModulationPlayer::setValue(
    std::size_t step, NormalizedValue value) noexcept
{
    const DraftWriteGuard guard {*this};
    if (step < length_.load(std::memory_order_relaxed))
        values_[step].store(value.raw, std::memory_order_relaxed);
}

void ModulationPlayer::setUnipolar8Value(
    std::size_t step, std::uint8_t value) noexcept
{
    setValue(step, NormalizedValue::fromUnipolar8(value));
}

void ModulationPlayer::setLength(std::size_t length) noexcept
{
    if (length == 0 || length > Modulation::maxLength)
        return;

    const DraftWriteGuard guard {*this};
    const auto oldLength = static_cast<std::size_t>(
        length_.load(std::memory_order_relaxed));
    const auto fill = oldLength == 0
        ? NormalizedValue::maximum
        : values_[oldLength - 1].load(std::memory_order_relaxed);
    for (auto step = oldLength; step < length; ++step)
        values_[step].store(fill, std::memory_order_relaxed);
    length_.store(static_cast<std::uint8_t>(length), std::memory_order_relaxed);
    if (nextStep_ >= length)
        nextStep_ = 0;
}

bool ModulationPlayer::readDraft(Modulation& result) const noexcept
{
    const auto before = draftRevision_.load(std::memory_order_acquire);
    if ((before & 1u) != 0)
        return false;

    result.length = length_.load(std::memory_order_relaxed);
    for (std::size_t step = 0; step < Modulation::maxLength; ++step)
        result.values[step].raw = values_[step].load(std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_acquire);
    return before == draftRevision_.load(std::memory_order_acquire);
}

Modulation ModulationPlayer::modulationForUi() const noexcept
{
    Modulation result;
    (void) readDraft(result);
    return result;
}

Modulation ModulationPlayer::modulationForSave() const noexcept
{
    return modulationForUi();
}

bool ModulationPlayer::hasUnsavedChanges() const noexcept
{
    const auto* active = library_.find(activeModulationId());
    Modulation draft;
    return active != nullptr
        && readDraft(draft)
        && !modulationsEqual(draft, active->modulation);
}

void ModulationPlayer::prepare(const PrepareSpec&) noexcept
{
    (void) activateRequestedSelection();
    reset();
}

void ModulationPlayer::resetPosition() noexcept
{
    nextStep_ = 0;
    completed_ = false;
    currentStep_.store(-1, std::memory_order_release);
}

void ModulationPlayer::reset() noexcept
{
    playing_.store(false, std::memory_order_release);
    resetPosition();
    nextClockPpq_ = std::numeric_limits<double>::infinity();
    lastResetAndPlayPpq_ = -std::numeric_limits<double>::infinity();
}

void ModulationPlayer::scheduleNextClock(double commandPpq) noexcept
{
    if (const auto* clock = std::get_if<ClockAdvance>(&advanceSource_))
        nextClockPpq_ = commandPpq + clock->stepLengthPpq;
}

bool ModulationPlayer::evaluateStep(
    double ppqPosition, PlayerSignalBuffer& output) noexcept
{
    if (!std::isfinite(ppqPosition) || !activateRequestedSelection())
        return false;

    Modulation modulation;
    if (!readDraft(modulation) || modulation.length == 0)
        return false;

    if (completed_ || nextStep_ >= modulation.length)
        return false;

    const auto step = nextStep_;
    const bool emitted = output.push(PlayerSignal::modulationValue(
        ppqPosition, id_, modulation.values[step], step));
    currentStep_.store(step, std::memory_order_release);

    if (playMode_ == PlayMode::oneShot && step + 1u >= modulation.length)
    {
        completed_ = true;
        playing_.store(false, std::memory_order_release);
        nextStep_ = modulation.length;
        nextClockPpq_ = std::numeric_limits<double>::infinity();
    }
    else
    {
        nextStep_ = static_cast<std::uint8_t>((step + 1u) % modulation.length);
    }
    return emitted;
}

void ModulationPlayer::command(
    PlayerCommand commandValue,
    double ppqPosition,
    PlayerSignalBuffer& output) noexcept
{
    switch (commandValue)
    {
        case PlayerCommand::stop:
            playing_.store(false, std::memory_order_release);
            nextClockPpq_ = std::numeric_limits<double>::infinity();
            return;
        case PlayerCommand::reset:
            reset();
            return;
        case PlayerCommand::resetAndPlay:
            resetPosition();
            playing_.store(true, std::memory_order_release);
            lastResetAndPlayPpq_ = ppqPosition;
            (void) evaluateStep(ppqPosition, output);
            scheduleNextClock(ppqPosition);
            return;
        case PlayerCommand::play:
            if (completed_)
                return;
            playing_.store(true, std::memory_order_release);
            if (currentStep_.load(std::memory_order_acquire) < 0)
                (void) evaluateStep(ppqPosition, output);
            scheduleNextClock(ppqPosition);
            return;
    }
}

void ModulationPlayer::processClock(
    const TimelineBlock& block,
    PlayerSignalBuffer& output) noexcept
{
    const auto* clock = std::get_if<ClockAdvance>(&advanceSource_);
    if (clock == nullptr
        || !block.playing
        || !playing_.load(std::memory_order_acquire)
        || block.ppqEnd <= block.ppqStart)
    {
        return;
    }

    while (nextClockPpq_ < block.ppqEnd
        && playing_.load(std::memory_order_relaxed))
    {
        if (nextClockPpq_ >= block.ppqStart)
            (void) evaluateStep(nextClockPpq_, output);
        nextClockPpq_ += clock->stepLengthPpq;
    }
}

void ModulationPlayer::advanceFromPatternHit(
    PatternPlayerId source,
    double ppqPosition,
    PlayerSignalBuffer& output) noexcept
{
    const auto* hit = std::get_if<PatternHitAdvance>(&advanceSource_);
    if (hit == nullptr
        || hit->source != source
        || !playing_.load(std::memory_order_acquire)
        || std::abs(ppqPosition - lastResetAndPlayPpq_) <= 1.0e-12)
    {
        return;
    }
    (void) evaluateStep(ppqPosition, output);
}

ModulationPlayerStatus ModulationPlayer::status() const noexcept
{
    return {
        selectedModulationId(),
        activeModulationId(),
        currentStep_.load(std::memory_order_acquire),
        playing_.load(std::memory_order_acquire)
    };
}

ModulationPlaybackSnapshot
ModulationPlayer::modulationPlaybackSnapshot() const noexcept
{
    return { currentStep_.load(std::memory_order_acquire) };
}

} // namespace lps
