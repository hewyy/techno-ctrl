#include "core/RuntimeGraph.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lps
{

bool RuntimeGraphConfig::add(TriggerBinding binding) noexcept
{
    if (triggerBindingCount == triggerBindings.size())
        return false;
    triggerBindings[triggerBindingCount++] = binding;
    return true;
}

bool RuntimeGraphConfig::add(ParameterBinding binding) noexcept
{
    if (parameterBindingCount == parameterBindings.size())
        return false;
    parameterBindings[parameterBindingCount++] = binding;
    return true;
}

bool RuntimeGraphConfig::add(CommandBinding binding) noexcept
{
    if (commandBindingCount == commandBindings.size())
        return false;
    commandBindings[commandBindingCount++] = binding;
    return true;
}

bool ResolvedVoiceEventBuffer::push(ResolvedVoiceEvent event) noexcept
{
    if (size_ == events_.size())
    {
        overflowed_ = true;
        return false;
    }
    events_[size_++] = event;
    return true;
}

bool RuntimeGraph::registerPatternPlayer(PatternPlayer& player) noexcept
{
    const auto ref = player.playerRef();
    if (prepared_ || ref.type != PlayerRefType::pattern
        || !PatternPlayerId {ref.value}.isValid()
        || patternPlayerCount_ == patternPlayers_.size()
        || find(PatternPlayerId {ref.value}) != nullptr)
        return false;
    patternPlayers_[patternPlayerCount_++] = &player;
    return true;
}

bool RuntimeGraph::registerModulationPlayer(ModulationPlayer& player) noexcept
{
    const auto ref = player.playerRef();
    if (prepared_ || ref.type != PlayerRefType::modulation
        || !ModulationPlayerId {ref.value}.isValid()
        || modulationPlayerCount_ == modulationPlayers_.size()
        || find(ModulationPlayerId {ref.value}) != nullptr)
        return false;
    modulationPlayers_[modulationPlayerCount_++] = &player;
    return true;
}

bool RuntimeGraph::registerVoice(Voice& voice) noexcept
{
    if (prepared_ || !voice.id().isValid()
        || voiceCount_ == voices_.size() || find(voice.id()) != nullptr)
        return false;
    voices_[voiceCount_++] = &voice;
    return true;
}

bool RuntimeGraph::configureArmedCycleCommand(
    std::size_t slot,
    PatternPlayerId source,
    PlayerRef destination,
    PlayerCommand command) noexcept
{
    if (prepared_ || slot >= armedCycleCommands_.size()
        || find(source) == nullptr || find(destination) == nullptr
        || (destination.type == PlayerRefType::pattern
            && destination.value == source.value))
    {
        return false;
    }

    armedCycleCommands_[slot] = {source, destination, command, true};
    armedCyclePending_[slot].store(false, std::memory_order_relaxed);
    return true;
}

bool RuntimeGraph::armCycleCommand(std::size_t slot) noexcept
{
    if (slot >= armedCycleCommands_.size()
        || !armedCycleCommands_[slot].configured)
    {
        return false;
    }
    armedCyclePending_[slot].store(true, std::memory_order_release);
    return true;
}

bool RuntimeGraph::cycleCommandPending(std::size_t slot) const noexcept
{
    return slot < armedCycleCommands_.size()
        && armedCycleCommands_[slot].configured
        && armedCyclePending_[slot].load(std::memory_order_acquire);
}

bool RuntimeGraph::patternHitOccurred(
    PatternPlayerId source,
    double ppqPosition) const noexcept
{
    constexpr double simultaneousTolerancePpq = 1.0e-9;
    for (std::size_t index = 0; index < workSize_; ++index)
    {
        const auto& signal = work_[index].signal;
        if (signal.type == PlayerSignalType::patternHit
            && signal.patternPlayerId == source
            && std::abs(signal.ppqPosition - ppqPosition)
                <= simultaneousTolerancePpq)
        {
            return true;
        }
    }
    return false;
}

PatternPlayer* RuntimeGraph::find(PatternPlayerId id) const noexcept
{
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        if (patternPlayers_[index]->playerRef().value == id.value)
            return patternPlayers_[index];
    return nullptr;
}

ModulationPlayer* RuntimeGraph::find(ModulationPlayerId id) const noexcept
{
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        if (modulationPlayers_[index]->playerRef().value == id.value)
            return modulationPlayers_[index];
    return nullptr;
}

Voice* RuntimeGraph::find(VoiceId id) const noexcept
{
    for (std::size_t index = 0; index < voiceCount_; ++index)
        if (voices_[index]->id() == id)
            return voices_[index];
    return nullptr;
}

IPlayer* RuntimeGraph::find(PlayerRef ref) const noexcept
{
    return ref.type == PlayerRefType::pattern
        ? static_cast<IPlayer*>(find(PatternPlayerId {ref.value}))
        : static_cast<IPlayer*>(find(ModulationPlayerId {ref.value}));
}

GraphValidationError RuntimeGraph::validate(
    const RuntimeGraphConfig& candidate) const noexcept
{
    if (candidate.triggerBindingCount > candidate.triggerBindings.size()
        || candidate.parameterBindingCount > candidate.parameterBindings.size()
        || candidate.commandBindingCount > candidate.commandBindings.size())
        return GraphValidationError::capacityExceeded;

    for (std::size_t index = 0; index < candidate.triggerBindingCount; ++index)
    {
        const auto& binding = candidate.triggerBindings[index];
        if (!binding.id.isValid() || find(binding.source) == nullptr
            || find(binding.destination) == nullptr)
            return GraphValidationError::missingNode;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const auto& other = candidate.triggerBindings[previous];
            if (other.id == binding.id)
                return GraphValidationError::duplicateBindingId;
            if (other.source == binding.source
                && other.destination == binding.destination)
                return GraphValidationError::duplicateBinding;
            if (other.destination == binding.destination)
                return GraphValidationError::multipleTriggerSources;
        }
    }

    for (std::size_t index = 0; index < candidate.parameterBindingCount; ++index)
    {
        const auto& binding = candidate.parameterBindings[index];
        const auto* voice = find(binding.voice);
        if (!binding.id.isValid() || find(binding.source) == nullptr
            || voice == nullptr || voice->parameter(binding.parameter) == nullptr)
            return GraphValidationError::missingNode;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const auto& other = candidate.parameterBindings[previous];
            if (other.id == binding.id)
                return GraphValidationError::duplicateBindingId;
            if (other.source == binding.source && other.voice == binding.voice
                && other.parameter == binding.parameter)
                return GraphValidationError::duplicateBinding;
            if (other.voice == binding.voice
                && other.parameter == binding.parameter)
                return GraphValidationError::multipleParameterSources;
        }
    }

    for (std::size_t index = 0; index < candidate.commandBindingCount; ++index)
    {
        const auto& binding = candidate.commandBindings[index];
        if (!binding.id.isValid() || find(binding.source.player) == nullptr
            || find(binding.destination) == nullptr)
            return GraphValidationError::missingNode;
        if (binding.destination.type == PlayerRefType::pattern
            && binding.destination.value == binding.source.player.value)
            return GraphValidationError::selfEdge;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const auto& other = candidate.commandBindings[previous];
            if (other.id == binding.id)
                return GraphValidationError::duplicateBindingId;
            if (other.source.player == binding.source.player
                && other.source.port == binding.source.port
                && other.destination.value == binding.destination.value
                && other.destination.type == binding.destination.type
                && other.command == binding.command)
                return GraphValidationError::duplicateBinding;
        }
    }
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
    {
        if (const auto* hit = std::get_if<PatternHitAdvance>(
                &patternPlayers_[index]->advanceSource()))
        {
            if (find(hit->source) == nullptr)
                return GraphValidationError::missingNode;
            if (hit->source.value == patternPlayers_[index]->playerRef().value)
                return GraphValidationError::selfEdge;
        }
    }
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        if (const auto* hit = std::get_if<PatternHitAdvance>(
                &modulationPlayers_[index]->advanceSource()))
            if (find(hit->source) == nullptr)
                return GraphValidationError::missingNode;

    return hasControlCycle(candidate)
        ? GraphValidationError::controlCycle
        : GraphValidationError::none;
}

bool RuntimeGraph::hasControlCycle(
    const RuntimeGraphConfig& candidate) const noexcept
{
    constexpr std::size_t maximumNodes = maximumPatternPlayers
        + maximumModulationPlayers;
    std::array<std::array<bool, maximumNodes>, maximumNodes> edges {};
    const auto nodeIndex = [this](PlayerRef ref) -> std::size_t
    {
        if (ref.type == PlayerRefType::pattern)
            for (std::size_t index = 0; index < patternPlayerCount_; ++index)
                if (patternPlayers_[index]->playerRef().value == ref.value)
                    return index;
        if (ref.type == PlayerRefType::modulation)
            for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
                if (modulationPlayers_[index]->playerRef().value == ref.value)
                    return patternPlayerCount_ + index;
        return maximumNodes;
    };

    for (std::size_t index = 0; index < candidate.commandBindingCount; ++index)
    {
        const auto& binding = candidate.commandBindings[index];
        const auto from = nodeIndex(PlayerRef::pattern(binding.source.player));
        const auto to = nodeIndex(binding.destination);
        if (from < maximumNodes && to < maximumNodes)
            edges[from][to] = true;
    }
    const auto addAdvance = [&](IPlayer& player)
    {
        const AdvanceSource* advance = nullptr;
        if (player.playerRef().type == PlayerRefType::pattern)
            advance = &static_cast<PatternPlayer&>(player).advanceSource();
        else
            advance = &static_cast<ModulationPlayer&>(player).advanceSource();
        if (const auto* hit = std::get_if<PatternHitAdvance>(advance))
        {
            const auto from = nodeIndex(PlayerRef::pattern(hit->source));
            const auto to = nodeIndex(player.playerRef());
            if (from < maximumNodes && to < maximumNodes)
                edges[from][to] = true;
        }
    };
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        addAdvance(*patternPlayers_[index]);
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        addAdvance(*modulationPlayers_[index]);

    const auto nodeCount = patternPlayerCount_ + modulationPlayerCount_;
    std::array<std::uint8_t, maximumNodes> state {};
    const auto visit = [&](auto&& self, std::size_t node) -> bool
    {
        state[node] = 1;
        for (std::size_t next = 0; next < nodeCount; ++next)
        {
            if (!edges[node][next])
                continue;
            if (state[next] == 1 || (state[next] == 0 && self(self, next)))
                return true;
        }
        state[node] = 2;
        return false;
    };
    for (std::size_t node = 0; node < nodeCount; ++node)
        if (state[node] == 0 && visit(visit, node))
            return true;
    return false;
}

bool RuntimeGraph::activate(const RuntimeGraphConfig& candidate) noexcept
{
    if (prepared_)
        return publish(candidate);
    lastValidationError_ = validate(candidate);
    if (lastValidationError_ != GraphValidationError::none)
        return false;
    configSnapshots_[0] = candidate;
    snapshotGenerations_[0] = ++nextPublicationGeneration_;
    audioSnapshot_ = 0;
    publishedSnapshot_.store(0, std::memory_order_release);
    acknowledgedSnapshot_.store(0, std::memory_order_release);
    publishedGeneration_.store(
        snapshotGenerations_[0], std::memory_order_release);
    activeGeneration_.store(
        snapshotGenerations_[0], std::memory_order_release);
    return true;
}

bool RuntimeGraph::publish(const RuntimeGraphConfig& candidate) noexcept
{
    lastValidationError_ = validate(candidate);
    if (lastValidationError_ != GraphValidationError::none)
        return false;

    const auto published = publishedSnapshot_.load(std::memory_order_acquire);
    const auto active = acknowledgedSnapshot_.load(std::memory_order_acquire);
    std::uint8_t destination = 0;
    while (destination == published || destination == active)
        ++destination;
    configSnapshots_[destination] = candidate;
    snapshotGenerations_[destination] = ++nextPublicationGeneration_;
    publishedGeneration_.store(
        snapshotGenerations_[destination], std::memory_order_relaxed);
    publishedSnapshot_.store(destination, std::memory_order_release);
    return true;
}

const RuntimeGraphConfig& RuntimeGraph::activeConfig() const noexcept
{
    return configSnapshots_[audioSnapshot_];
}

void RuntimeGraph::adoptPublishedConfig() noexcept
{
    const auto published = publishedSnapshot_.load(std::memory_order_acquire);
    audioSnapshot_ = published;
    activeGeneration_.store(
        snapshotGenerations_[published], std::memory_order_release);
    acknowledgedSnapshot_.store(published, std::memory_order_release);
}

void RuntimeGraph::prepare(const PrepareSpec& spec) noexcept
{
    prepareSpec_ = spec;
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        patternPlayers_[index]->prepare(spec);
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        modulationPlayers_[index]->prepare(spec);
    for (std::size_t index = 0; index < voiceCount_; ++index)
        voices_[index]->reset();
    prepared_ = true;
}

void RuntimeGraph::reset() noexcept
{
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        patternPlayers_[index]->reset();
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        modulationPlayers_[index]->reset();
    for (std::size_t index = 0; index < voiceCount_; ++index)
        voices_[index]->reset();
}

bool RuntimeGraph::appendSignals(const PlayerSignalBuffer& signals) noexcept
{
    for (const auto& signal : signals)
    {
        if (workSize_ == work_.size())
        {
            workOverflowed_ = true;
            return false;
        }
        work_[workSize_++] = {signal, nextWorkOrder_++, false, false};
    }
    return !signals.overflowed();
}

void RuntimeGraph::appendVoiceEvents(
    Voice& voice,
    PatternPlayerId triggerSource,
    const SequencerEventBuffer& events,
    ResolvedVoiceEventBuffer& output) noexcept
{
    for (const auto& event : events)
        (void) output.push({
            event, voice.id(), triggerSource, nextResolvedOrder_++, true});
}

void RuntimeGraph::resolveTimestamp(
    double ppq, ResolvedVoiceEventBuffer& output) noexcept
{
    const auto& config = activeConfig();
    bool propagatedAny = true;
    while (propagatedAny && !workOverflowed_)
    {
        propagatedAny = false;
        const auto scanSize = workSize_;
        for (std::size_t index = 0; index < scanSize; ++index)
        {
            auto& item = work_[index];
            if (item.propagated || item.signal.ppqPosition != ppq)
                continue;
            item.propagated = true;
            propagatedAny = true;

            if (item.signal.type == PlayerSignalType::patternHit)
            {
                for (std::size_t player = 0; player < patternPlayerCount_; ++player)
                {
                    PlayerSignalBuffer generated;
                    patternPlayers_[player]->advanceFromPatternHit(
                        item.signal, generated);
                    (void) appendSignals(generated);
                }
                for (std::size_t player = 0; player < modulationPlayerCount_; ++player)
                {
                    PlayerSignalBuffer generated;
                    modulationPlayers_[player]->advanceFromPatternHit(
                        item.signal, generated);
                    (void) appendSignals(generated);
                }
            }

            if (item.signal.type == PlayerSignalType::patternCycleBoundary)
            {
                for (std::size_t slot = 0;
                     slot < armedCycleCommands_.size();
                     ++slot)
                {
                    const auto& armed = armedCycleCommands_[slot];
                    if (!armed.configured || armed.source != item.signal.patternPlayerId
                        || !armedCyclePending_[slot].exchange(
                            false, std::memory_order_acq_rel))
                    {
                        continue;
                    }
                    if (auto* destination = find(armed.destination))
                    {
                        PlayerSignalBuffer generated;
                        destination->command(armed.command, ppq, generated);
                        (void) appendSignals(generated);
                    }
                }
            }

            for (std::size_t bindingIndex = 0;
                 bindingIndex < config.commandBindingCount;
                 ++bindingIndex)
            {
                const auto& binding = config.commandBindings[bindingIndex];
                const bool sourceMatches =
                    binding.source.player == item.signal.patternPlayerId
                    && ((binding.source.port == ControlSourcePort::hit
                            && item.signal.type == PlayerSignalType::patternHit)
                        || (binding.source.port == ControlSourcePort::cycleBoundary
                            && item.signal.type
                                == PlayerSignalType::patternCycleBoundary));
                if (!sourceMatches)
                    continue;
                if (auto* destination = find(binding.destination))
                {
                    PlayerSignalBuffer generated;
                    destination->command(binding.command, ppq, generated);
                    (void) appendSignals(generated);
                }
            }
        }
    }

    for (std::size_t voiceIndex = 0; voiceIndex < voiceCount_; ++voiceIndex)
    {
        SequencerEventBuffer events;
        voices_[voiceIndex]->emitEndsBefore(ppq, true, events);
        appendVoiceEvents(*voices_[voiceIndex], {}, events, output);
    }

    for (std::size_t bindingIndex = 0;
         bindingIndex < config.parameterBindingCount;
         ++bindingIndex)
    {
        const auto& binding = config.parameterBindings[bindingIndex];
        for (std::size_t signalIndex = 0; signalIndex < workSize_; ++signalIndex)
        {
            auto& item = work_[signalIndex];
            if (item.signal.ppqPosition != ppq
                || item.signal.type != PlayerSignalType::modulationValue
                || item.signal.modulationPlayerId != binding.source)
                continue;
            SequencerEventBuffer events;
            if (auto* voice = find(binding.voice))
            {
                (void) voice->applyParameterValue(
                    binding.parameter, item.signal, events);
                appendVoiceEvents(*voice, {}, events, output);
            }
        }
    }

    for (std::size_t bindingIndex = 0;
         bindingIndex < config.triggerBindingCount;
         ++bindingIndex)
    {
        const auto& binding = config.triggerBindings[bindingIndex];
        for (std::size_t signalIndex = 0; signalIndex < workSize_; ++signalIndex)
        {
            auto& item = work_[signalIndex];
            if (item.signal.ppqPosition != ppq
                || item.signal.type != PlayerSignalType::patternHit
                || item.signal.patternPlayerId != binding.source)
                continue;
            SequencerEventBuffer events;
            if (auto* voice = find(binding.destination))
            {
                (void) voice->trigger(
                    item.signal, currentPpqPerSample_, events);
                appendVoiceEvents(*voice, binding.source, events, output);
            }
        }
    }
    for (std::size_t index = 0; index < workSize_; ++index)
        if (work_[index].signal.ppqPosition == ppq)
            work_[index].resolved = true;
}

bool RuntimeGraph::process(
    const TimelineBlock& block,
    ResolvedVoiceEventBuffer& output) noexcept
{
    adoptPublishedConfig();
    output.clear();
    workSize_ = 0;
    nextWorkOrder_ = 0;
    nextResolvedOrder_ = 0;
    workOverflowed_ = false;
    currentPpqPerSample_ = block.sampleRate > 0.0
        && block.tempoBpm > 0.0
        ? block.tempoBpm / (60.0 * block.sampleRate)
        : 0.0;
    if (!prepared_ || !block.playing)
    {
        reset();
        return prepared_;
    }

    if (block.transportDiscontinuity)
    {
        for (std::size_t index = 0; index < voiceCount_; ++index)
            voices_[index]->reset();
        for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        {
            PlayerSignalBuffer generated;
            modulationPlayers_[index]->command(
                PlayerCommand::resetAndPlay, block.ppqStart, generated);
            (void) appendSignals(generated);
        }
    }

    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
    {
        PlayerSignalBuffer generated;
        (void) patternPlayers_[index]->process(block, {}, generated);
        (void) appendSignals(generated);
    }
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
    {
        PlayerSignalBuffer generated;
        (void) modulationPlayers_[index]->process(block, {}, generated);
        (void) appendSignals(generated);
    }

    for (;;)
    {
        double nextPpq = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < workSize_; ++index)
            if (!work_[index].resolved)
                nextPpq = std::min(nextPpq, work_[index].signal.ppqPosition);
        if (!std::isfinite(nextPpq))
            break;
        resolveTimestamp(nextPpq, output);
    }

    for (std::size_t voiceIndex = 0; voiceIndex < voiceCount_; ++voiceIndex)
    {
        SequencerEventBuffer events;
        voices_[voiceIndex]->emitEndsBefore(block.ppqEnd, false, events);
        appendVoiceEvents(*voices_[voiceIndex], {}, events, output);
    }
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right)
    {
        if (left.event.ppqPosition != right.event.ppqPosition)
            return left.event.ppqPosition < right.event.ppqPosition;
        if (left.event.type != right.event.type)
            return left.event.type < right.event.type;
        return left.stableOrder < right.stableOrder;
    });
    return !workOverflowed_ && !output.overflowed();
}

} // namespace lps
