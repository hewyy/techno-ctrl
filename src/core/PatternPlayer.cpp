#include "core/PatternPlayer.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>

namespace lps
{
namespace
{
constexpr double boundaryTolerance = 1.0e-9;

std::int32_t normalizedOffset(std::int32_t offset, PatternTick cycle) noexcept
{
    if (cycle == 0)
        return 0;
    auto normalized = static_cast<std::int64_t>(offset)
        % static_cast<std::int64_t>(cycle);
    if (normalized < 0)
        normalized += cycle;
    return static_cast<std::int32_t>(normalized);
}

Pattern rotatedPatternEndingAt(
    Pattern pattern,
    std::int32_t offset,
    PatternTick endTick) noexcept
{
    const auto sourceCycle = pattern.cycleLengthTicks;
    const auto rotation = normalizedOffset(offset, sourceCycle);
    std::size_t retainedHitCount = 0;

    for (std::size_t index = 0; index < pattern.hitCount; ++index)
    {
        auto hit = pattern.hits[index];
        hit.startTick = static_cast<PatternTick>((
            static_cast<std::uint64_t>(hit.startTick)
                + static_cast<std::uint32_t>(rotation)) % sourceCycle);
        if (hit.startTick < endTick)
            pattern.hits[retainedHitCount++] = hit;
    }

    std::sort(pattern.hits.begin(),
        pattern.hits.begin() + static_cast<std::ptrdiff_t>(retainedHitCount),
        [](const PatternHit& left, const PatternHit& right)
        {
            return left.startTick < right.startTick;
        });
    pattern.cycleLengthTicks = endTick;
    pattern.hitCount = static_cast<std::uint16_t>(retainedHitCount);
    return normalizedPattern(pattern);
}
} // namespace

PatternPlayer::PatternPlayer(const PatternLibrary& patternLibrary) noexcept
    : patternLibrary_(patternLibrary)
{
    if (const auto* initial = patternLibrary_.recordAt(0))
    {
        requestedPatternSelection_.store(initial->id.value(), std::memory_order_relaxed);
        (void) activatePattern(initial->id, false);
    }
}

void PatternPlayer::setAdvanceSource(AdvanceSource source) noexcept
{
    if (const auto* clock = std::get_if<ClockAdvance>(&source);
        clock != nullptr
        && (!std::isfinite(clock->stepLengthPpq) || clock->stepLengthPpq <= 0.0))
    {
        Logger::logf(LogLevel::warning, "pattern_player", "setting_rejected",
            "player_id=%u setting=advance_source reason=invalid_step_length value=%.9f",
            id_.value, clock->stepLengthPpq);
        return;
    }
    advanceSource_ = source;
}

const AdvanceSource& PatternPlayer::advanceSource() const noexcept
{
    return advanceSource_;
}

void PatternPlayer::setTransitionPolicy(PatternTransitionPolicy policy) noexcept
{
    if (policy.type == PatternTransitionPolicyType::externalCycle
        && !policy.externalSource.isValid())
    {
        Logger::logf(LogLevel::warning, "pattern_player", "setting_rejected",
            "player_id=%u setting=transition_policy reason=invalid_external_source",
            id_.value);
        return;
    }
    transitionPolicy_ = policy;
}

void PatternPlayer::selectPattern(PatternId id) noexcept
{
    selectSavedPattern(id);
}

void PatternPlayer::selectSavedPattern(PatternId id) noexcept
{
    if (canSelectSavedPattern(id))
    {
        requestedPatternSelection_.store(
            id.value() | resetOffsetOnActivationFlag,
            std::memory_order_release);
    }
    else
    {
        Logger::logf(LogLevel::warning, "pattern_player", "selection_rejected",
            "player_id=%u pattern_id=%llu", id_.value,
            static_cast<unsigned long long>(id.value()));
    }
}

bool PatternPlayer::adoptSavedDraft(PatternId id) noexcept
{
    const auto* record = patternLibrary_.find(id);
    if (record == nullptr)
        return false;

    auto draft = draftSnapshot();
    const auto savedPattern = rotatedPatternEndingAt(
        draft.pattern, draft.patternOffsetTicks, draft.playbackEndTick);
    if (!patternsEqual(savedPattern, record->pattern))
        return false;

    draft.pattern = savedPattern;
    draft.activePatternId = id;
    draft.patternOffsetTicks = 0;
    {
        const DraftWriteGuard guard {*this, true};
        storeDraft(draft);
    }
    requestedPatternSelection_.store(id.value(), std::memory_order_release);
    return true;
}

bool PatternPlayer::canSelectSavedPattern(PatternId id) const noexcept
{
    return patternLibrary_.find(id) != nullptr
        && (id.value() & resetOffsetOnActivationFlag) == 0;
}

PatternId PatternPlayer::selectedPatternId() const noexcept
{
    return patternIdFromSelection(requestedPatternSelection());
}

PatternId PatternPlayer::activePatternId() const noexcept
{
    return PatternId {activePatternId_.load(std::memory_order_acquire)};
}

void PatternPlayer::offsetPatternLeft(PatternTick amount) noexcept
{
    setPatternOffsetTicks(patternOffsetTicks() - static_cast<std::int32_t>(amount));
}

void PatternPlayer::offsetPatternRight(PatternTick amount) noexcept
{
    setPatternOffsetTicks(patternOffsetTicks() + static_cast<std::int32_t>(amount));
}

int PatternPlayer::patternOffset() const noexcept
{
    return patternOffsetTicks();
}

std::int32_t PatternPlayer::patternOffsetTicks() const noexcept
{
    return patternOffsetTicks_.load(std::memory_order_relaxed);
}

void PatternPlayer::setPatternOffsetTicks(std::int32_t offset) noexcept
{
    const DraftWriteGuard guard {*this, true};
    const auto cycle = draftCycleLengthTicks_.load(std::memory_order_relaxed);
    patternOffsetTicks_.store(normalizedOffset(offset, cycle), std::memory_order_relaxed);
}

void PatternPlayer::setPlaybackSpeed(std::size_t index) noexcept
{
    if (index < playbackSpeedCount)
        playbackSpeed_.store(index, std::memory_order_relaxed);
}

std::size_t PatternPlayer::playbackSpeed() const noexcept
{
    return playbackSpeed_.load(std::memory_order_relaxed);
}

double PatternPlayer::playbackSpeedMultiplier(std::size_t index) noexcept
{
    constexpr std::array<double, playbackSpeedCount> speeds {0.5, 1.0, 2.0};
    return index < speeds.size() ? speeds[index] : 1.0;
}

void PatternPlayer::setPlaybackWindow(
    std::size_t startTick, std::size_t endTick) noexcept
{
    const auto draft = draftSnapshot();
    if (startTick >= endTick || endTick > draft.pattern.cycleLengthTicks)
    {
        Logger::logf(LogLevel::warning, "pattern_player", "setting_rejected",
            "player_id=%u setting=playback_window start=%zu end=%zu",
            id_.value, startTick, endTick);
        return;
    }
    const DraftWriteGuard guard {*this, true};
    requestedPlaybackStartTick_.store(
        static_cast<PatternTick>(startTick), std::memory_order_relaxed);
    requestedPlaybackEndTick_.store(
        static_cast<PatternTick>(endTick), std::memory_order_relaxed);
}

std::size_t PatternPlayer::requestedPlaybackStart() const noexcept
{
    return requestedPlaybackStartTick_.load(std::memory_order_relaxed);
}

std::size_t PatternPlayer::requestedPlaybackEnd() const noexcept
{
    return requestedPlaybackEndTick_.load(std::memory_order_relaxed);
}

bool PatternPlayer::addHit(PatternTick start, PatternTick duration) noexcept
{
    auto draft = draftSnapshot();
    auto& pattern = draft.pattern;
    if (start >= pattern.cycleLengthTicks || duration == 0
        || pattern.hitCount == Pattern::maximumHitCount)
        return false;
    std::size_t insertion = 0;
    while (insertion < pattern.hitCount
        && pattern.hits[insertion].startTick < start)
        ++insertion;
    if (insertion < pattern.hitCount
        && pattern.hits[insertion].startTick == start)
        return false;
    for (std::size_t index = pattern.hitCount; index > insertion; --index)
        pattern.hits[index] = pattern.hits[index - 1];
    pattern.hits[insertion] = {start, duration};
    ++pattern.hitCount;
    return replaceDraftPattern(pattern);
}

bool PatternPlayer::removeHit(PatternTick start) noexcept
{
    auto draft = draftSnapshot();
    auto& pattern = draft.pattern;
    std::size_t index = 0;
    while (index < pattern.hitCount && pattern.hits[index].startTick != start)
        ++index;
    if (index == pattern.hitCount)
        return false;
    for (; index + 1 < pattern.hitCount; ++index)
        pattern.hits[index] = pattern.hits[index + 1];
    --pattern.hitCount;
    pattern.hits[pattern.hitCount] = {};
    return replaceDraftPattern(pattern);
}

bool PatternPlayer::moveHit(PatternTick oldStart, PatternTick newStart) noexcept
{
    auto draft = draftSnapshot();
    auto& pattern = draft.pattern;
    if (newStart >= pattern.cycleLengthTicks)
        return false;
    std::size_t oldIndex = pattern.hitCount;
    for (std::size_t index = 0; index < pattern.hitCount; ++index)
    {
        if (pattern.hits[index].startTick == newStart && newStart != oldStart)
            return false;
        if (pattern.hits[index].startTick == oldStart)
            oldIndex = index;
    }
    if (oldIndex == pattern.hitCount)
        return false;
    pattern.hits[oldIndex].startTick = newStart;
    std::sort(
        pattern.hits.begin(),
        pattern.hits.begin() + static_cast<std::ptrdiff_t>(pattern.hitCount),
        [](const auto& left, const auto& right)
        {
            return left.startTick < right.startTick;
        });
    return replaceDraftPattern(pattern);
}

bool PatternPlayer::resizeHit(PatternTick start, PatternTick duration) noexcept
{
    if (duration == 0)
        return false;
    auto draft = draftSnapshot();
    for (std::size_t index = 0; index < draft.pattern.hitCount; ++index)
    {
        if (draft.pattern.hits[index].startTick == start)
        {
            draft.pattern.hits[index].durationTicks = duration;
            return replaceDraftPattern(draft.pattern);
        }
    }
    return false;
}

bool PatternPlayer::setCycleLength(PatternTick cycle) noexcept
{
    auto draft = draftSnapshot();
    if (cycle == 0 || cycle > Pattern::maximumCycleLengthTicks)
        return false;
    for (std::size_t index = 0; index < draft.pattern.hitCount; ++index)
        if (draft.pattern.hits[index].startTick >= cycle)
            return false;
    draft.pattern.cycleLengthTicks = cycle;
    draft.playbackStartTick = std::min(draft.playbackStartTick, cycle - 1);
    draft.playbackEndTick = std::min(draft.playbackEndTick, cycle);
    if (draft.playbackStartTick >= draft.playbackEndTick)
    {
        draft.playbackStartTick = 0;
        draft.playbackEndTick = cycle;
    }
    draft.patternOffsetTicks = normalizedOffset(draft.patternOffsetTicks, cycle);
    const DraftWriteGuard guard {*this, true};
    storeDraft(draft);
    return true;
}

void PatternPlayer::toggleStep(std::size_t step) noexcept
{
    const auto tick64 = step * static_cast<std::size_t>(Pattern::legacyStepTicks);
    if (tick64 >= Pattern::maximumCycleLengthTicks)
        return;
    const auto tick = static_cast<PatternTick>(tick64);
    auto draft = draftSnapshot();
    if (tick >= draft.pattern.cycleLengthTicks)
    {
        const auto cycle = static_cast<PatternTick>(tick + Pattern::legacyStepTicks);
        if (!setCycleLength(cycle))
            return;
        draft = draftSnapshot();
    }
    for (std::size_t index = 0; index < draft.pattern.hitCount; ++index)
    {
        if (rotatedTick(draft.pattern.hits[index].startTick, draft) == tick)
        {
            (void) removeHit(draft.pattern.hits[index].startTick);
            return;
        }
    }
    const auto source = static_cast<PatternTick>((
        static_cast<std::int64_t>(tick)
        - normalizedOffset(draft.patternOffsetTicks, draft.pattern.cycleLengthTicks)
        + draft.pattern.cycleLengthTicks) % draft.pattern.cycleLengthTicks);
    (void) addHit(source, Pattern::legacyStepTicks);
}

Pattern PatternPlayer::patternForSave() const noexcept
{
    const auto draft = draftSnapshot();
    return rotatedPatternEndingAt(
        draft.pattern, draft.patternOffsetTicks, draft.playbackEndTick);
}

bool PatternPlayer::hasUnsavedPatternChanges() const noexcept
{
    const auto draft = draftSnapshot();
    const auto* record = patternLibrary_.find(draft.activePatternId);
    return record != nullptr
        && !patternsEqual(
            rotatedPatternEndingAt(
                draft.pattern,
                draft.patternOffsetTicks,
                draft.playbackEndTick),
            record->pattern);
}

bool PatternPlayer::installNewDraft(Pattern pattern) noexcept
{
    if (!patternIsValid(pattern))
        return false;

    auto draft = draftSnapshot();
    draft.pattern = normalizedPattern(pattern);
    draft.patternOffsetTicks = 0;
    draft.playbackStartTick = 0;
    draft.playbackEndTick = draft.pattern.cycleLengthTicks;
    {
        const DraftWriteGuard guard {*this, true};
        storeDraft(draft);
    }
    playbackSpeed_.store(1, std::memory_order_relaxed);
    return true;
}

PatternPlayerPersistentState PatternPlayer::capturePersistentState() const noexcept
{
    const auto draft = draftSnapshot();
    return {draft.activePatternId, draft.pattern, draft.patternOffsetTicks,
        draft.playbackStartTick, draft.playbackEndTick,
        static_cast<std::uint8_t>(playbackSpeed())};
}

bool PatternPlayer::restorePersistentState(
    const PatternPlayerPersistentState& state) noexcept
{
    auto resolved = state;
    if (patternLibrary_.find(resolved.patternId) == nullptr)
    {
        const auto* fallback = patternLibrary_.recordAt(0);
        if (fallback == nullptr || !patternIsValid(fallback->pattern))
            return false;
        resolved.patternId = fallback->id;
        resolved.pattern = fallback->pattern;
        resolved.patternOffsetTicks = 0;
        resolved.playbackStartTick = 0;
        resolved.playbackEndTick = fallback->pattern.cycleLengthTicks;
        resolved.playbackSpeed = 1;
    }

    if (!patternIsValid(resolved.pattern)
        || resolved.playbackStartTick >= resolved.playbackEndTick
        || resolved.playbackEndTick > resolved.pattern.cycleLengthTicks
        || resolved.playbackSpeed >= playbackSpeedCount)
        return false;
    DraftSnapshot draft {resolved.patternId, normalizedPattern(resolved.pattern),
        normalizedOffset(
            resolved.patternOffsetTicks, resolved.pattern.cycleLengthTicks),
        resolved.playbackStartTick, resolved.playbackEndTick};
    {
        const DraftWriteGuard guard {*this, true};
        storeDraft(draft);
    }
    audioDraft_ = draft;
    activePlaybackStartTick_ = draft.playbackStartTick;
    activePlaybackEndTick_ = draft.playbackEndTick;
    playbackSpeed_.store(resolved.playbackSpeed, std::memory_order_relaxed);
    requestedPatternSelection_.store(
        resolved.patternId.value(), std::memory_order_release);
    reset();
    return true;
}

std::uint64_t PatternPlayer::packHit(PatternHit hit) noexcept
{
    return static_cast<std::uint64_t>(hit.startTick)
        | (static_cast<std::uint64_t>(hit.durationTicks) << 32u);
}

PatternHit PatternPlayer::unpackHit(std::uint64_t packed) noexcept
{
    return {static_cast<PatternTick>(packed), static_cast<PatternTick>(packed >> 32u)};
}

PatternPlayer::DraftWriteGuard::DraftWriteGuard(
    PatternPlayer& owner, bool wait) noexcept
{
    for (;;)
    {
        auto revision = owner.draftRevision_.load(std::memory_order_acquire);
        if ((revision & 1u) == 0)
        {
            auto expected = revision;
            if (owner.draftRevision_.compare_exchange_strong(
                    expected, revision + 1, std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                owner_ = &owner;
                previousRevision_ = revision;
                return;
            }
        }
        if (!wait)
            return;
    }
}

PatternPlayer::DraftWriteGuard::~DraftWriteGuard()
{
    if (owner_ != nullptr)
        owner_->draftRevision_.store(previousRevision_ + 2, std::memory_order_release);
}

void PatternPlayer::storeDraft(const DraftSnapshot& draft) noexcept
{
    activePatternId_.store(draft.activePatternId.value(), std::memory_order_relaxed);
    draftCycleLengthTicks_.store(draft.pattern.cycleLengthTicks, std::memory_order_relaxed);
    draftHitCount_.store(draft.pattern.hitCount, std::memory_order_relaxed);
    for (std::size_t index = 0; index < Pattern::maximumHitCount; ++index)
        draftHits_[index].store(packHit(draft.pattern.hits[index]), std::memory_order_relaxed);
    patternOffsetTicks_.store(draft.patternOffsetTicks, std::memory_order_relaxed);
    requestedPlaybackStartTick_.store(draft.playbackStartTick, std::memory_order_relaxed);
    requestedPlaybackEndTick_.store(draft.playbackEndTick, std::memory_order_relaxed);
}

bool PatternPlayer::tryDraftSnapshot(DraftSnapshot& result) const noexcept
{
    const auto before = draftRevision_.load(std::memory_order_acquire);
    if ((before & 1u) != 0)
        return false;
    result.activePatternId = PatternId {activePatternId_.load(std::memory_order_relaxed)};
    result.pattern.cycleLengthTicks = draftCycleLengthTicks_.load(std::memory_order_relaxed);
    result.pattern.hitCount = draftHitCount_.load(std::memory_order_relaxed);
    if (result.pattern.hitCount > Pattern::maximumHitCount)
        return false;
    for (std::size_t index = 0; index < result.pattern.hitCount; ++index)
        result.pattern.hits[index] = unpackHit(draftHits_[index].load(std::memory_order_relaxed));
    result.patternOffsetTicks = patternOffsetTicks_.load(std::memory_order_relaxed);
    result.playbackStartTick = requestedPlaybackStartTick_.load(std::memory_order_relaxed);
    result.playbackEndTick = requestedPlaybackEndTick_.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    const auto after = draftRevision_.load(std::memory_order_acquire);
    return before == after && (after & 1u) == 0 && patternIsValid(result.pattern)
        && result.playbackStartTick < result.playbackEndTick
        && result.playbackEndTick <= result.pattern.cycleLengthTicks;
}

PatternPlayer::DraftSnapshot PatternPlayer::draftSnapshot() const noexcept
{
    DraftSnapshot result;
    while (!tryDraftSnapshot(result)) {}
    return result;
}

void PatternPlayer::adoptDraftForAudio() noexcept
{
    DraftSnapshot candidate;
    if (tryDraftSnapshot(candidate))
        audioDraft_ = candidate;
}

bool PatternPlayer::replaceDraftPattern(Pattern pattern) noexcept
{
    if (!patternIsValid(pattern))
        return false;
    auto draft = draftSnapshot();
    draft.pattern = normalizedPattern(pattern);
    const DraftWriteGuard guard {*this, true};
    storeDraft(draft);
    return true;
}

std::uint64_t PatternPlayer::requestedPatternSelection() const noexcept
{
    return requestedPatternSelection_.load(std::memory_order_acquire);
}

PatternId PatternPlayer::patternIdFromSelection(std::uint64_t value) noexcept
{
    return PatternId {value & requestedPatternIdMask};
}

bool PatternPlayer::selectionResetsOffset(std::uint64_t value) noexcept
{
    return (value & resetOffsetOnActivationFlag) != 0;
}

void PatternPlayer::consumeSaveSelectionRequest(std::uint64_t selection) noexcept
{
    if (!selectionResetsOffset(selection))
        return;
    auto expected = selection;
    (void) requestedPatternSelection_.compare_exchange_strong(
        expected, selection & requestedPatternIdMask,
        std::memory_order_release, std::memory_order_relaxed);
}

bool PatternPlayer::activatePattern(PatternId id, bool resetOffset) noexcept
{
    const auto* record = patternLibrary_.find(id);
    if (record == nullptr || !patternIsValid(record->pattern))
        return false;
    const DraftWriteGuard guard {*this, false};
    if (!guard)
        return false;
    DraftSnapshot draft;
    draft.activePatternId = id;
    draft.pattern = record->pattern;
    draft.patternOffsetTicks = resetOffset ? 0 : patternOffsetTicks();
    draft.playbackEndTick = record->pattern.cycleLengthTicks;
    storeDraft(draft);
    audioDraft_ = draft;
    activePlaybackStartTick_ = 0;
    activePlaybackEndTick_ = draft.playbackEndTick;
    return true;
}

void PatternPlayer::prepare(const PrepareSpec&) noexcept
{
    const auto selection = requestedPatternSelection();
    const auto pending = patternIdFromSelection(selection);
    if ((pending != activePatternId() || selectionResetsOffset(selection))
        && activatePattern(pending, selectionResetsOffset(selection)))
        consumeSaveSelectionRequest(selection);
    adoptDraftForAudio();
    reset();
}

void PatternPlayer::reset() noexcept
{
    playbackOriginPpq_ = 0.0;
    externalAdvanceCount_ = 0;
    patternPlaybackSnapshot_ = {};
    completed_ = false;
    lastResetAndPlayPpq_ = -std::numeric_limits<double>::infinity();
}

PatternView PatternPlayer::patternView() const noexcept
{
    const auto draft = draftSnapshot();
    return {draft.pattern, draft.patternOffsetTicks,
        draft.playbackStartTick, draft.playbackEndTick};
}

PatternPlaybackSnapshot PatternPlayer::patternPlaybackSnapshot() const noexcept
{
    return patternPlaybackSnapshot_;
}

PlayerSyncCapabilities PatternPlayer::syncCapabilities() const noexcept
{
    return {true, true, true};
}

PatternTick PatternPlayer::rotatedTick(
    PatternTick source, const DraftSnapshot& draft) const noexcept
{
    const auto cycle = draft.pattern.cycleLengthTicks;
    if (cycle == 0)
        return 0;
    return static_cast<PatternTick>((static_cast<std::uint64_t>(source)
        + static_cast<std::uint32_t>(normalizedOffset(
            draft.patternOffsetTicks, cycle))) % cycle);
}

double PatternPlayer::tickDurationPpq(PatternTick ticks) const noexcept
{
    return static_cast<double>(ticks)
        / static_cast<double>(Pattern::ticksPerQuarterNote)
        / playbackSpeedMultiplier(playbackSpeed());
}

void PatternPlayer::processClockRange(
    double rangeStart, double rangeEnd, PlayerSignalBuffer& output,
    PlayerProcessResult& result) noexcept
{
    if (rangeEnd <= rangeStart || activePlaybackStartTick_ >= activePlaybackEndTick_)
        return;
    const auto windowTicks = activePlaybackEndTick_ - activePlaybackStartTick_;
    const auto cyclePpq = tickDurationPpq(windowTicks);
    if (!(cyclePpq > 0.0))
        return;

    auto firstCycle = static_cast<std::int64_t>(std::floor(
        (rangeStart - playbackOriginPpq_) / cyclePpq + boundaryTolerance));
    firstCycle = std::max<std::int64_t>(0, firstCycle);

    for (auto cycle = firstCycle;; ++cycle)
    {
        const auto cycleStart = playbackOriginPpq_ + cycle * cyclePpq;
        if (cycleStart >= rangeEnd)
            break;
        if (playMode_ == PlayMode::oneShot && cycle > 0)
            break;

        if (cycleStart + boundaryTolerance >= rangeStart)
        {
            const auto requestedStart = requestedPlaybackStartTick_.load(
                std::memory_order_relaxed);
            const auto requestedEnd = requestedPlaybackEndTick_.load(
                std::memory_order_relaxed);
            if ((requestedStart != activePlaybackStartTick_
                    || requestedEnd != activePlaybackEndTick_)
                && requestedStart < requestedEnd
                && requestedEnd <= audioDraft_.pattern.cycleLengthTicks)
            {
                activePlaybackStartTick_ = requestedStart;
                activePlaybackEndTick_ = requestedEnd;
                playbackOriginPpq_ = cycleStart;
                processClockRange(cycleStart, rangeEnd, output, result);
                return;
            }

            const auto selection = requestedPatternSelection();
            const auto pending = patternIdFromSelection(selection);
            if ((pending != audioDraft_.activePatternId
                    || selectionResetsOffset(selection))
                && transitionPolicy_.type == PatternTransitionPolicyType::localCycle
                && activatePattern(pending, selectionResetsOffset(selection)))
            {
                consumeSaveSelectionRequest(selection);
                playbackOriginPpq_ = cycleStart;
                processClockRange(cycleStart, rangeEnd, output, result);
                return;
            }

            if (!result.firstCycleBoundaryPpq)
                result.firstCycleBoundaryPpq = cycleStart;
            (void) output.push(PlayerSignal::patternCycleBoundary(cycleStart, id_));
        }

        const auto& pattern = audioDraft_.pattern;
        const auto offset = static_cast<PatternTick>(normalizedOffset(
            audioDraft_.patternOffsetTicks, pattern.cycleLengthTicks));
        std::size_t pivot = 0;
        if (offset != 0)
        {
            const auto wrapSource = pattern.cycleLengthTicks - offset;
            while (pivot < pattern.hitCount
                && pattern.hits[pivot].startTick < wrapSource)
                ++pivot;
        }
        for (std::size_t ordered = 0; ordered < pattern.hitCount; ++ordered)
        {
            const auto index = (pivot + ordered) % pattern.hitCount;
            const auto& hit = pattern.hits[index];
            const auto tick = rotatedTick(hit.startTick, audioDraft_);
            if (tick < activePlaybackStartTick_ || tick >= activePlaybackEndTick_)
                continue;
            const auto ppq = cycleStart
                + tickDurationPpq(tick - activePlaybackStartTick_);
            if (ppq + boundaryTolerance < rangeStart || ppq >= rangeEnd)
                continue;
            (void) output.push(PlayerSignal::patternHit(
                std::max(ppq, rangeStart), id_, TriggerId {nextTriggerId_++},
                tickDurationPpq(hit.durationTicks)));
            if (output.overflowed())
                return;
        }

        if (playMode_ == PlayMode::oneShot
            && cycleStart + cyclePpq <= rangeEnd + boundaryTolerance)
        {
            completed_ = true;
            commandPlaying_ = false;
            break;
        }
    }

    const auto position = std::max(rangeStart,
        rangeEnd - std::numeric_limits<double>::epsilon());
    auto cyclePosition = std::fmod(position - playbackOriginPpq_, cyclePpq);
    if (cyclePosition < 0.0)
        cyclePosition += cyclePpq;
    patternPlaybackSnapshot_.currentTick = activePlaybackStartTick_
        + static_cast<PatternTick>(std::min<double>(
            windowTicks - 1,
            cyclePosition / cyclePpq * windowTicks));
    patternPlaybackSnapshot_.cycleProgress = static_cast<float>(cyclePosition / cyclePpq);
    patternPlaybackSnapshot_.playing = !completed_;
}

PlayerProcessResult PatternPlayer::process(
    const TimelineBlock& block, PlayerSignalBuffer& output) noexcept
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
        completed_ = false;
        commandPlaying_ = block.playing;
        if (block.playing)
            playbackOriginPpq_ = block.ppqStart;
    }

    adoptDraftForAudio();
    auto selection = requestedPatternSelection();
    const auto pending = patternIdFromSelection(selection);
    const bool immediate = transitionPolicy_.type == PatternTransitionPolicyType::immediate;
    if ((pending != audioDraft_.activePatternId || selectionResetsOffset(selection))
        && (!block.playing || block.transportDiscontinuity || immediate)
        && activatePattern(pending, selectionResetsOffset(selection)))
    {
        consumeSaveSelectionRequest(selection);
        if (immediate && block.playing && !block.transportDiscontinuity)
            playbackOriginPpq_ = block.ppqStart;
    }

    if (!block.playing || block.transportDiscontinuity)
    {
        activePlaybackStartTick_ = audioDraft_.playbackStartTick;
        activePlaybackEndTick_ = audioDraft_.playbackEndTick;
    }

    if (!block.playing || !commandPlaying_ || completed_
        || block.ppqEnd <= block.ppqStart
        || !patternIsValid(audioDraft_.pattern))
    {
        patternPlaybackSnapshot_.playing = false;
        result.eventOverflow = output.overflowed();
        return result;
    }

    processClockRange(block.ppqStart, block.ppqEnd, output, result);
    result.active = patternPlaybackSnapshot_.playing;
    result.eventOverflow = output.overflowed();
    return result;
}

bool PatternPlayer::observeCycleBoundary(
    const PlayerSignal& boundary, PlayerSignalBuffer& output) noexcept
{
    if (boundary.type != PlayerSignalType::patternCycleBoundary
        || transitionPolicy_.type != PatternTransitionPolicyType::externalCycle
        || transitionPolicy_.externalSource != boundary.patternPlayerId)
        return false;
    return activateSelectedPatternAtBoundary(boundary.ppqPosition, output);
}

bool PatternPlayer::activateSelectedPatternAtBoundary(
    double ppq, PlayerSignalBuffer& output) noexcept
{
    const auto selection = requestedPatternSelection();
    const auto pending = patternIdFromSelection(selection);
    if ((pending == audioDraft_.activePatternId && !selectionResetsOffset(selection))
        || !activatePattern(pending, selectionResetsOffset(selection)))
        return false;
    consumeSaveSelectionRequest(selection);
    command(PlayerCommand::resetAndPlay, ppq, output);
    return true;
}

void PatternPlayer::command(
    PlayerCommand value, double ppq, PlayerSignalBuffer& output) noexcept
{
    if (value == PlayerCommand::resetAndPlay
        && std::abs(ppq - lastResetAndPlayPpq_) <= 1.0e-12)
        return;
    if (value == PlayerCommand::stop)
    {
        commandPlaying_ = false;
        patternPlaybackSnapshot_.playing = false;
        return;
    }
    if (value == PlayerCommand::reset)
    {
        reset();
        commandPlaying_ = false;
        return;
    }
    if (value == PlayerCommand::play && completed_)
        return;

    const bool resetFirst = value == PlayerCommand::resetAndPlay;
    if (resetFirst)
    {
        reset();
        lastResetAndPlayPpq_ = ppq;
    }
    commandPlaying_ = true;
    if (resetFirst || !patternPlaybackSnapshot_.playing)
    {
        playbackOriginPpq_ = ppq;
        externalAdvanceCount_ = 0;
        if (std::holds_alternative<PatternHitAdvance>(advanceSource_))
        {
            adoptDraftForAudio();
            (void) output.push(PlayerSignal::patternCycleBoundary(ppq, id_));
            const auto sliceEnd = std::min<PatternTick>(
                activePlaybackEndTick_ - activePlaybackStartTick_,
                Pattern::legacyStepTicks);
            for (std::size_t index = 0;
                 index < audioDraft_.pattern.hitCount;
                 ++index)
            {
                const auto& patternHit = audioDraft_.pattern.hits[index];
                const auto tick = rotatedTick(patternHit.startTick, audioDraft_);
                if (tick >= activePlaybackStartTick_
                    && tick - activePlaybackStartTick_ < sliceEnd)
                {
                    (void) output.push(PlayerSignal::patternHit(
                        ppq, id_, TriggerId {nextTriggerId_++},
                        tickDurationPpq(patternHit.durationTicks)));
                }
            }
            externalAdvanceCount_ = 1;
            patternPlaybackSnapshot_.playing = true;
            return;
        }
        PlayerSignalBuffer immediate;
        processingExternalAdvance_ = true;
        (void) process({ppq, std::nextafter(ppq,
            std::numeric_limits<double>::infinity()), 120.0, 48'000.0, 1,
            true, false}, immediate);
        processingExternalAdvance_ = false;
        for (const auto& signal : immediate)
            (void) output.push(signal);
        externalAdvanceCount_ = 1;
    }
}

void PatternPlayer::advanceFromPatternHit(
    const PlayerSignal& hit, PlayerSignalBuffer& output) noexcept
{
    const auto* source = std::get_if<PatternHitAdvance>(&advanceSource_);
    if (source == nullptr || hit.type != PlayerSignalType::patternHit
        || source->source != hit.patternPlayerId || !commandPlaying_ || completed_
        || std::abs(hit.ppqPosition - lastResetAndPlayPpq_) <= 1.0e-12)
        return;

    // Compatibility mode: one source hit advances one legacy 240-tick slice.
    // Any timed hits in that slice fire deterministically at the source PPQ.
    adoptDraftForAudio();
    const auto window = activePlaybackEndTick_ - activePlaybackStartTick_;
    if (window == 0)
        return;
    const auto sliceStart = static_cast<PatternTick>((externalAdvanceCount_
        * Pattern::legacyStepTicks) % window);
    const auto sliceEnd = std::min<PatternTick>(
        window, sliceStart + Pattern::legacyStepTicks);
    if (sliceStart == 0)
        (void) output.push(PlayerSignal::patternCycleBoundary(hit.ppqPosition, id_));
    const auto& pattern = audioDraft_.pattern;
    for (std::size_t index = 0; index < pattern.hitCount; ++index)
    {
        const auto tick = rotatedTick(pattern.hits[index].startTick, audioDraft_);
        if (tick < activePlaybackStartTick_ || tick >= activePlaybackEndTick_)
            continue;
        const auto local = tick - activePlaybackStartTick_;
        if (local >= sliceStart && local < sliceEnd)
            (void) output.push(PlayerSignal::patternHit(
                hit.ppqPosition, id_, TriggerId {nextTriggerId_++},
                tickDurationPpq(pattern.hits[index].durationTicks)));
    }
    ++externalAdvanceCount_;
    if (sliceEnd == window && playMode_ == PlayMode::oneShot)
    {
        completed_ = true;
        commandPlaying_ = false;
    }
}

} // namespace lps
