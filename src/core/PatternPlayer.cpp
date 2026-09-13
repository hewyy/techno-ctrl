#include "core/PatternPlayer.h"

#include <algorithm>
#include <cmath>

namespace lps
{

PatternPlayer::PatternPlayer(const PatternLibrary& patternLibrary) noexcept
    : patternLibrary_(patternLibrary)
{
    if (const auto* initialPattern = patternLibrary_.recordAt(0))
    {
        requestedPatternSelection_.store(
            initialPattern->id.value(), std::memory_order_relaxed);
        (void) activatePattern(initialPattern->id, false);
    }
}

void PatternPlayer::setAdvanceSource(AdvanceSource source) noexcept
{
    if (const auto* clock = std::get_if<ClockAdvance>(&source);
        clock != nullptr
        && (!std::isfinite(clock->stepLengthPpq) || clock->stepLengthPpq <= 0.0))
        return;
    advanceSource_ = source;
}

const AdvanceSource& PatternPlayer::advanceSource() const noexcept
{
    return advanceSource_;
}

void PatternPlayer::selectPattern(PatternId patternId) noexcept
{
    if (patternLibrary_.find(patternId) != nullptr
        && (patternId.value() & resetOffsetOnActivationFlag) == 0)
    {
        requestedPatternSelection_.store(patternId.value(), std::memory_order_release);
    }
}

void PatternPlayer::selectSavedPattern(PatternId patternId) noexcept
{
    if (patternLibrary_.find(patternId) != nullptr
        && (patternId.value() & resetOffsetOnActivationFlag) == 0)
    {
        requestedPatternSelection_.store(
            patternId.value() | resetOffsetOnActivationFlag,
            std::memory_order_release);
    }
}

PatternId PatternPlayer::selectedPatternId() const noexcept
{
    return patternIdFromSelection(requestedPatternSelection());
}

PatternId PatternPlayer::activePatternId() const noexcept
{
    return PatternId { activePatternId_.load(std::memory_order_acquire) };
}

void PatternPlayer::offsetPatternLeft() noexcept
{
    const DraftWriteGuard guard { *this, true };
    patternOffset_.fetch_sub(1, std::memory_order_relaxed);
}

void PatternPlayer::offsetPatternRight() noexcept
{
    const DraftWriteGuard guard { *this, true };
    patternOffset_.fetch_add(1, std::memory_order_relaxed);
}

int PatternPlayer::patternOffset() const noexcept
{
    return patternOffset_.load(std::memory_order_relaxed);
}

void PatternPlayer::setPlaybackSpeed(std::size_t speedIndex) noexcept
{
    if (speedIndex < playbackSpeedCount)
        playbackSpeed_.store(speedIndex, std::memory_order_relaxed);
}

std::size_t PatternPlayer::playbackSpeed() const noexcept
{
    return playbackSpeed_.load(std::memory_order_relaxed);
}

double PatternPlayer::playbackSpeedMultiplier(std::size_t speedIndex) noexcept
{
    constexpr std::array<double, playbackSpeedCount> speeds { 0.5, 1.0, 2.0 };
    return speedIndex < speeds.size() ? speeds[speedIndex] : 1.0;
}

std::uint32_t PatternPlayer::packPlaybackWindow(
    std::size_t startStep, std::size_t endStep) noexcept
{
    return static_cast<std::uint32_t>((startStep & 0xffffu) | ((endStep & 0xffffu) << 16u));
}

void PatternPlayer::unpackPlaybackWindow(
    std::uint32_t packed, std::size_t& startStep, std::size_t& endStep) noexcept
{
    startStep = packed & 0xffffu;
    endStep = (packed >> 16u) & 0xffffu;
}

std::size_t PatternPlayer::patternLength(const Pattern& pattern) noexcept
{
    return std::min(pattern.length, Pattern::maxLength);
}

std::uint32_t PatternPlayer::patternHitMask(const Pattern& pattern) noexcept
{
    std::uint32_t hitMask = 0;
    const auto length = patternLength(pattern);

    for (std::size_t step = 0; step < length; ++step)
    {
        if (pattern.hits[step])
            hitMask |= std::uint32_t { 1 } << step;
    }

    return hitMask;
}

std::uint64_t PatternPlayer::requestedPatternSelection() const noexcept
{
    return requestedPatternSelection_.load(std::memory_order_acquire);
}

PatternId PatternPlayer::patternIdFromSelection(std::uint64_t selection) noexcept
{
    return PatternId { selection & requestedPatternIdMask };
}

bool PatternPlayer::selectionResetsOffset(std::uint64_t selection) noexcept
{
    return (selection & resetOffsetOnActivationFlag) != 0;
}

void PatternPlayer::consumeSaveSelectionRequest(std::uint64_t selection) noexcept
{
    if (!selectionResetsOffset(selection))
        return;

    auto expected = selection;
    (void) requestedPatternSelection_.compare_exchange_strong(
        expected,
        selection & requestedPatternIdMask,
        std::memory_order_release,
        std::memory_order_relaxed);
}

PatternPlayer::DraftWriteGuard::DraftWriteGuard(
    PatternPlayer& owner,
    bool waitForAccess) noexcept
{
    for (;;)
    {
        auto revision = owner.activationRevision_.load(std::memory_order_acquire);
        if ((revision & 1u) == 0)
        {
            auto expected = revision;
            if (owner.activationRevision_.compare_exchange_strong(
                    expected,
                    revision + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                owner_ = &owner;
                previousRevision_ = revision;
                return;
            }
        }

        // The audio thread uses a single attempt and leaves pattern
        // activation pending rather than ever waiting for the UI thread.
        if (!waitForAccess)
            return;
    }
}

PatternPlayer::DraftWriteGuard::~DraftWriteGuard()
{
    if (owner_ != nullptr)
    {
        owner_->activationRevision_.store(
            previousRevision_ + 2,
            std::memory_order_release);
    }
}

bool PatternPlayer::activatePattern(PatternId patternId, bool resetOffset) noexcept
{
    const auto* record = patternLibrary_.find(patternId);
    if (record == nullptr || patternLength(record->pattern) == 0)
        return false;

    const auto end = patternLength(record->pattern) - 1;
    const auto fullWindow = packPlaybackWindow(0, end);

    const DraftWriteGuard guard { *this, false };
    if (!guard)
        return false;

    editableHitMask_.store(patternHitMask(record->pattern), std::memory_order_relaxed);
    if (resetOffset)
        patternOffset_.store(0, std::memory_order_relaxed);
    requestedPlaybackWindow_.store(fullWindow, std::memory_order_relaxed);
    activePlaybackWindow_.store(fullWindow, std::memory_order_relaxed);
    activePatternId_.store(patternId.value(), std::memory_order_release);
    return true;
}

void PatternPlayer::setPlaybackWindow(std::size_t startStep, std::size_t endStep) noexcept
{
    if (patternLibrary_.find(activePatternId()) == nullptr)
        return;

    startStep = std::min(startStep, longestPatternLength - 1);
    endStep = std::min(endStep, longestPatternLength - 1);
    if (startStep > endStep)
        return;

    const DraftWriteGuard guard { *this, true };
    requestedPlaybackWindow_.store(
        packPlaybackWindow(startStep, endStep), std::memory_order_relaxed);
}

std::size_t PatternPlayer::requestedPlaybackStart() const noexcept
{
    std::size_t start = 0;
    std::size_t end = 0;
    unpackPlaybackWindow(requestedPlaybackWindow_.load(std::memory_order_relaxed), start, end);
    return start;
}

std::size_t PatternPlayer::requestedPlaybackEnd() const noexcept
{
    std::size_t start = 0;
    std::size_t end = 0;
    unpackPlaybackWindow(requestedPlaybackWindow_.load(std::memory_order_relaxed), start, end);
    return end;
}

void PatternPlayer::toggleStep(std::size_t visibleStep) noexcept
{
    if (patternLibrary_.find(activePatternId()) == nullptr
        || visibleStep >= longestPatternLength)
        return;

    const DraftWriteGuard guard { *this, true };
    auto sourceStep = (static_cast<int>(visibleStep)
            - patternOffset_.load(std::memory_order_relaxed))
        % static_cast<int>(longestPatternLength);
    if (sourceStep < 0)
        sourceStep += static_cast<int>(longestPatternLength);
    editableHitMask_.fetch_xor(
        std::uint32_t { 1 } << sourceStep, std::memory_order_relaxed);
}

PatternPlayer::DraftSnapshot PatternPlayer::draftSnapshot() const noexcept
{
    for (;;)
    {
        const auto revisionBefore = activationRevision_.load(
            std::memory_order_acquire);
        if ((revisionBefore & 1u) != 0)
            continue;

        DraftSnapshot draft;
        draft.activePatternId = PatternId {
            activePatternId_.load(std::memory_order_relaxed)
        };
        draft.hitMask = editableHitMask_.load(std::memory_order_relaxed);
        draft.patternOffset = patternOffset_.load(std::memory_order_relaxed);
        unpackPlaybackWindow(
            requestedPlaybackWindow_.load(std::memory_order_relaxed),
            draft.playbackStart,
            draft.playbackEnd);

        std::atomic_thread_fence(std::memory_order_acquire);
        const auto revisionAfter = activationRevision_.load(
            std::memory_order_acquire);
        if (revisionBefore == revisionAfter && (revisionAfter & 1u) == 0)
            return draft;
    }
}

Pattern PatternPlayer::makePatternForSave(const DraftSnapshot& draft) noexcept
{
    Pattern pattern;
    const PatternView view {
        static_cast<std::uint16_t>(longestPatternLength),
        draft.hitMask,
        draft.patternOffset,
        static_cast<std::uint16_t>(draft.playbackStart),
        static_cast<std::uint16_t>(draft.playbackEnd)
    };

    const auto start = std::min<std::size_t>(view.playbackStart, view.stepCount - 1);
    const auto end = std::min<std::size_t>(view.playbackEnd, view.stepCount - 1);
    if (start > end)
        return pattern;

    pattern.length = end - start + 1;
    for (std::size_t destinationStep = 0; destinationStep < pattern.length;
         ++destinationStep)
    {
        pattern.hits[destinationStep] = view.isHit(
            static_cast<std::uint16_t>(start + destinationStep));
    }

    return pattern;
}

Pattern PatternPlayer::patternForSave() const noexcept
{
    const auto draft = draftSnapshot();
    const auto* activeRecord = patternLibrary_.find(draft.activePatternId);
    if (activeRecord == nullptr || patternLength(activeRecord->pattern) == 0)
        return {};

    return makePatternForSave(draft);
}

bool PatternPlayer::hasUnsavedPatternChanges() const noexcept
{
    const auto draft = draftSnapshot();
    const auto* activeRecord = patternLibrary_.find(draft.activePatternId);
    return activeRecord != nullptr
        && (draft.hitMask != patternHitMask(activeRecord->pattern)
            || !patternsEqual(makePatternForSave(draft), activeRecord->pattern));
}

PatternPlayerPersistentState PatternPlayer::capturePersistentState() const noexcept
{
    const auto pattern = draftSnapshot();
    return {
        pattern.activePatternId,
        pattern.hitMask,
        pattern.patternOffset,
        static_cast<std::uint16_t>(pattern.playbackStart),
        static_cast<std::uint16_t>(pattern.playbackEnd),
        static_cast<std::uint8_t>(playbackSpeed())
    };
}

bool PatternPlayer::restorePersistentState(
    const PatternPlayerPersistentState& state) noexcept
{
    if (patternLibrary_.find(state.patternId) == nullptr
        || state.playbackStart > state.playbackEnd
        || state.playbackEnd >= longestPatternLength
        || state.playbackSpeed >= playbackSpeedCount)
    {
        return false;
    }

    if (!activatePattern(state.patternId, false))
    {
        return false;
    }

    {
        const DraftWriteGuard guard { *this, true };
        editableHitMask_.store(state.hitMask, std::memory_order_relaxed);
        patternOffset_.store(state.patternOffset, std::memory_order_relaxed);
        const auto window = packPlaybackWindow(
            state.playbackStart, state.playbackEnd);
        requestedPlaybackWindow_.store(window, std::memory_order_relaxed);
        activePlaybackWindow_.store(window, std::memory_order_relaxed);
    }
    playbackSpeed_.store(state.playbackSpeed, std::memory_order_relaxed);
    requestedPatternSelection_.store(
        state.patternId.value(), std::memory_order_release);
    reset();
    return true;
}

void PatternPlayer::prepare(const PrepareSpec& /*spec*/) noexcept
{
    const auto selection = requestedPatternSelection();
    const auto requestedPattern = patternIdFromSelection(selection);
    if ((requestedPattern != activePatternId() || selectionResetsOffset(selection))
        && activatePattern(requestedPattern, selectionResetsOffset(selection)))
    {
        consumeSaveSelectionRequest(selection);
    }

    reset();
}

void PatternPlayer::reset() noexcept
{
    playbackOriginPpq_ = 0.0;
    lastTriggeredPlaybackStep_ = std::numeric_limits<std::int64_t>::min();
    playbackWindowOriginStep_ = 0;
    patternPlaybackSnapshot_ = {};
    completed_ = false;
}

PatternView PatternPlayer::patternView() const noexcept
{
    const auto draft = draftSnapshot();
    const auto* activeRecord = patternLibrary_.find(draft.activePatternId);
    if (activeRecord == nullptr || patternLength(activeRecord->pattern) == 0)
        return {};

    auto start = draft.playbackStart;
    auto end = draft.playbackEnd;
    start = std::min(start, longestPatternLength - 1);
    end = std::min(end, longestPatternLength - 1);
    return {
        static_cast<std::uint16_t>(longestPatternLength),
        draft.hitMask, draft.patternOffset,
        static_cast<std::uint16_t>(start), static_cast<std::uint16_t>(end)
    };
}

PatternPlaybackSnapshot PatternPlayer::patternPlaybackSnapshot() const noexcept
{
    return patternPlaybackSnapshot_;
}

PlayerSyncCapabilities PatternPlayer::syncCapabilities() const noexcept
{
    return { true, true, true };
}

PlayerProcessResult PatternPlayer::process(
    const TimelineBlock& block,
    const PlayerDirectives& directives,
    PlayerSignalBuffer& output) noexcept
{
    output.clear();
    PlayerProcessResult result;

    if (std::holds_alternative<PatternHitAdvance>(advanceSource_)
        && !processingExternalAdvance_)
    {
        result.active = commandPlaying_ && !completed_;
        return result;
    }

    if (block.transportDiscontinuity)
    {
        lastTriggeredPlaybackStep_ = std::numeric_limits<std::int64_t>::min();
        completed_ = false;
        commandPlaying_ = block.playing;

        if (block.playing)
        {
            // Treat this transport position as step zero so every player starts
            // from its playback-window beginning, independent of host PPQ.
            playbackOriginPpq_ = block.ppqStart;
            playbackWindowOriginStep_ = 0;
        }
    }

    auto activePattern = activePatternId();
    auto selection = requestedPatternSelection();
    auto requestedPattern = patternIdFromSelection(selection);
    if ((requestedPattern != activePattern || selectionResetsOffset(selection))
        && (!block.playing || block.transportDiscontinuity)
        && activatePattern(requestedPattern, selectionResetsOffset(selection)))
    {
        activePattern = requestedPattern;
        playbackWindowOriginStep_ = 0;
        consumeSaveSelectionRequest(selection);
    }

    // A range chosen while stopped should be the range used when transport
    // starts. Changes made during continuous playback remain quantized to the
    // end of the currently active range below.
    if (!block.playing || block.transportDiscontinuity)
    {
        activePlaybackWindow_.store(
            requestedPlaybackWindow_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    const auto* activeRecord = patternLibrary_.find(activePattern);
    const Pattern* pattern = activeRecord != nullptr ? &activeRecord->pattern : nullptr;
    auto currentDraftStepCount = pattern != nullptr && patternLength(*pattern) != 0
        ? longestPatternLength : 0;
    std::uint32_t hitMask = editableHitMask_.load(std::memory_order_relaxed);
    const double stepLengthPpq = baseStepLengthPpq / playbackSpeedMultiplier(playbackSpeed());
    std::size_t playbackStart = 0;
    std::size_t playbackEnd = 0;
    unpackPlaybackWindow(
        activePlaybackWindow_.load(std::memory_order_relaxed), playbackStart, playbackEnd);

    if (!block.playing || !commandPlaying_ || completed_
        || block.ppqEnd <= block.ppqStart || currentDraftStepCount == 0)
    {
        patternPlaybackSnapshot_ = { -1, false };
        result.eventOverflow = output.overflowed();
        return result;
    }

    constexpr double stepBoundaryTolerance = 1.0e-9;

    // Process a half-open portion of this host block without clearing output.
    // Keeping the range half-open lets an external reset replace an old-phase
    // step at the same PPQ with the restarted sequence's first step.
    const auto processRange = [&](double rangeStart, double rangeEnd)
    {
        if (rangeEnd <= rangeStart)
            return;

        const auto playbackStepAtStart = static_cast<std::int64_t>(std::floor(
            (rangeStart - playbackOriginPpq_) / stepLengthPpq
                + stepBoundaryTolerance));
        const auto playbackLength = playbackEnd - playbackStart + 1;
        auto snapshotCycleStep = (playbackStepAtStart - playbackWindowOriginStep_)
            % static_cast<std::int64_t>(playbackLength);
        if (snapshotCycleStep < 0)
            snapshotCycleStep += static_cast<std::int64_t>(playbackLength);
        patternPlaybackSnapshot_.currentStep = static_cast<int>(playbackStart
            + static_cast<std::size_t>(snapshotCycleStep));
        patternPlaybackSnapshot_.playing = true;

        auto playbackStep = static_cast<std::int64_t>(std::ceil(
            (rangeStart - playbackOriginPpq_) / stepLengthPpq
                - stepBoundaryTolerance));

        while (playbackOriginPpq_ + static_cast<double>(playbackStep) * stepLengthPpq
            < rangeEnd)
        {
            const auto currentPlaybackLength = playbackEnd - playbackStart + 1;
            const auto stepsSinceWindowOrigin = playbackStep - playbackWindowOriginStep_;
            const bool atInitialPlaybackBoundary = stepsSinceWindowOrigin == 0;
            const bool atPlaybackBoundary = stepsSinceWindowOrigin > 0
                && stepsSinceWindowOrigin
                    % static_cast<std::int64_t>(currentPlaybackLength) == 0;
            const auto requestedWindow = requestedPlaybackWindow_.load(
                std::memory_order_relaxed);
            const auto activeWindow = activePlaybackWindow_.load(
                std::memory_order_relaxed);
            if (requestedWindow != activeWindow && atPlaybackBoundary)
            {
                unpackPlaybackWindow(requestedWindow, playbackStart, playbackEnd);
                playbackStart = std::min(playbackStart, currentDraftStepCount - 1);
                playbackEnd = std::min(playbackEnd, currentDraftStepCount - 1);
                if (playbackStart > playbackEnd)
                    playbackStart = playbackEnd;
                activePlaybackWindow_.store(
                    packPlaybackWindow(playbackStart, playbackEnd),
                    std::memory_order_relaxed);
                playbackWindowOriginStep_ = playbackStep;
            }

            selection = requestedPatternSelection();
            const auto pendingPattern = patternIdFromSelection(selection);
            if ((pendingPattern != activePattern || selectionResetsOffset(selection))
                && !directives.quantizePendingTransitionsExternally
                && atPlaybackBoundary
                && activatePattern(pendingPattern, selectionResetsOffset(selection)))
            {
                activePattern = pendingPattern;
                activeRecord = patternLibrary_.find(activePattern);
                pattern = activeRecord != nullptr ? &activeRecord->pattern : nullptr;
                currentDraftStepCount = pattern != nullptr && patternLength(*pattern) != 0
                    ? longestPatternLength : 0;
                hitMask = editableHitMask_.load(std::memory_order_relaxed);
                unpackPlaybackWindow(
                    activePlaybackWindow_.load(std::memory_order_relaxed),
                    playbackStart,
                    playbackEnd);
                playbackWindowOriginStep_ = playbackStep;
                consumeSaveSelectionRequest(selection);
            }

            if (playbackStep == playbackStepAtStart
                && playbackWindowOriginStep_ == playbackStep)
            {
                patternPlaybackSnapshot_.currentStep = static_cast<int>(playbackStart);
                patternPlaybackSnapshot_.playing = true;
            }

            const double stepPpq = playbackOriginPpq_
                + static_cast<double>(playbackStep) * stepLengthPpq;

            if ((atInitialPlaybackBoundary || atPlaybackBoundary)
                && stepPpq + stepBoundaryTolerance >= block.ppqStart
                && stepPpq < block.ppqEnd)
            {
                const auto boundaryPpq = std::max(stepPpq, block.ppqStart);
                if (!result.firstCycleBoundaryPpq.has_value())
                    result.firstCycleBoundaryPpq = boundaryPpq;
                (void) output.push(PlayerSignal::patternCycleBoundary(
                    boundaryPpq, id_));
            }

            const auto activeLength = playbackEnd - playbackStart + 1;
            auto cycleStep = (playbackStep - playbackWindowOriginStep_)
                % static_cast<std::int64_t>(activeLength);
            if (cycleStep < 0)
                cycleStep += static_cast<std::int64_t>(activeLength);
            const auto patternStep = static_cast<std::uint16_t>(playbackStart
                + static_cast<std::size_t>(cycleStep));
            const PatternView effectivePattern {
                static_cast<std::uint16_t>(currentDraftStepCount), hitMask, patternOffset(),
                static_cast<std::uint16_t>(playbackStart),
                static_cast<std::uint16_t>(playbackEnd)
            };

            if (playbackStep > lastTriggeredPlaybackStep_
                && effectivePattern.isHit(patternStep))
            {
                (void) output.push(PlayerSignal::patternHit(
                    stepPpq,
                    id_,
                    TriggerId {nextTriggerId_++},
                    stepLengthPpq));
                lastTriggeredPlaybackStep_ = playbackStep;
            }

            ++playbackStep;
            if (playMode_ == PlayMode::oneShot
                && stepsSinceWindowOrigin + 1
                    >= static_cast<std::int64_t>(currentPlaybackLength))
            {
                completed_ = true;
                commandPlaying_ = false;
                patternPlaybackSnapshot_.playing = false;
                break;
            }
        }
    };

    const auto pendingSelection = requestedPatternSelection();
    const bool patternChangePending =
        patternIdFromSelection(pendingSelection) != activePattern
        || selectionResetsOffset(pendingSelection);
    const auto masterBoundaryPpq = directives.externalCycleBoundaryPpq.value_or(0.0);
    const bool hasSynchronizedPatternChange =
        directives.quantizePendingTransitionsExternally
        && directives.externalCycleBoundaryPpq.has_value()
        && patternChangePending
        && std::isfinite(masterBoundaryPpq)
        && masterBoundaryPpq + stepBoundaryTolerance >= block.ppqStart
        && masterBoundaryPpq < block.ppqEnd;

    const auto externalResetPpq = directives.restartAtPpq.value_or(0.0);
    const bool hasExternalReset = directives.restartAtPpq.has_value()
        && std::isfinite(externalResetPpq)
        && externalResetPpq + stepBoundaryTolerance >= block.ppqStart
        && externalResetPpq < block.ppqEnd;

    if (!hasExternalReset && !hasSynchronizedPatternChange)
    {
        processRange(block.ppqStart, block.ppqEnd);
        result.active = patternPlaybackSnapshot_.playing;
        result.eventOverflow = output.overflowed();
        return result;
    }

    // Both directives originate from the same validated master boundary in
    // SequencerEngine. A synchronized pattern activation itself restarts the
    // follower, while a manual reset can do so without a pattern change.
    auto transitionPpq = hasSynchronizedPatternChange
        ? masterBoundaryPpq
        : externalResetPpq;
    if (transitionPpq <= block.ppqStart + stepBoundaryTolerance)
        transitionPpq = block.ppqStart;
    else
        processRange(block.ppqStart, transitionPpq);

    bool patternActivated = false;
    if (hasSynchronizedPatternChange)
    {
        selection = requestedPatternSelection();
        const auto pendingPattern = patternIdFromSelection(selection);
        if ((pendingPattern != activePattern || selectionResetsOffset(selection))
            && activatePattern(pendingPattern, selectionResetsOffset(selection)))
        {
            activePattern = pendingPattern;
            activeRecord = patternLibrary_.find(activePattern);
            pattern = activeRecord != nullptr ? &activeRecord->pattern : nullptr;
            currentDraftStepCount = pattern != nullptr && patternLength(*pattern) != 0
                ? longestPatternLength : 0;
            hitMask = editableHitMask_.load(std::memory_order_relaxed);
            unpackPlaybackWindow(
                activePlaybackWindow_.load(std::memory_order_relaxed),
                playbackStart,
                playbackEnd);
            consumeSaveSelectionRequest(selection);
            patternActivated = true;
        }
    }

    // Make the active playback-window start step occur exactly at the master's
    // loop boundary. A failed activation remains pending for the next master
    // boundary unless a manual phase reset was also requested.
    if (patternActivated || hasExternalReset)
    {
        playbackOriginPpq_ = transitionPpq;
        playbackWindowOriginStep_ = 0;
        lastTriggeredPlaybackStep_ = std::numeric_limits<std::int64_t>::min();
    }

    processRange(transitionPpq, block.ppqEnd);
    result.active = patternPlaybackSnapshot_.playing;
    result.eventOverflow = output.overflowed();
    return result;
}

void PatternPlayer::command(
    PlayerCommand commandValue,
    double ppqPosition,
    PlayerSignalBuffer& output) noexcept
{
    if (commandValue == PlayerCommand::stop)
    {
        commandPlaying_ = false;
        patternPlaybackSnapshot_.playing = false;
        return;
    }
    if (commandValue == PlayerCommand::reset)
    {
        reset();
        commandPlaying_ = false;
        return;
    }
    if (commandValue == PlayerCommand::play && completed_)
        return;

    const bool resetFirst = commandValue == PlayerCommand::resetAndPlay;
    if (resetFirst)
    {
        reset();
        lastResetAndPlayPpq_ = ppqPosition;
    }
    commandPlaying_ = true;

    if (resetFirst || patternPlaybackSnapshot_.currentStep < 0)
    {
        playbackOriginPpq_ = ppqPosition;
        externalAdvanceCount_ = 0;
        PlayerSignalBuffer immediate;
        processingExternalAdvance_ = true;
        const auto end = std::nextafter(
            ppqPosition, std::numeric_limits<double>::infinity());
        (void) process(
            {ppqPosition, end, 120.0, 48'000.0, 1, true, false},
            {},
            immediate);
        processingExternalAdvance_ = false;
        for (const auto& signal : immediate)
            (void) output.push(signal);
        externalAdvanceCount_ = 1;
    }
}

void PatternPlayer::advanceFromPatternHit(
    const PlayerSignal& hit,
    PlayerSignalBuffer& output) noexcept
{
    const auto* source = std::get_if<PatternHitAdvance>(&advanceSource_);
    if (source == nullptr
        || hit.type != PlayerSignalType::patternHit
        || source->source != hit.patternPlayerId
        || !commandPlaying_
        || completed_
        || std::abs(hit.ppqPosition - lastResetAndPlayPpq_) <= 1.0e-12)
        return;

    const auto stepLength = baseStepLengthPpq
        / playbackSpeedMultiplier(playbackSpeed());
    playbackOriginPpq_ = hit.ppqPosition
        - static_cast<double>(externalAdvanceCount_) * stepLength;
    PlayerSignalBuffer advanced;
    processingExternalAdvance_ = true;
    const auto end = std::nextafter(
        hit.ppqPosition, std::numeric_limits<double>::infinity());
    (void) process(
        {hit.ppqPosition, end, 120.0, 48'000.0, 1, true, false},
        {},
        advanced);
    processingExternalAdvance_ = false;
    for (const auto& signal : advanced)
        (void) output.push(signal);
    ++externalAdvanceCount_;
}

} // namespace lps
