#include "core/RuntimeGraph.h"
#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lps
{
namespace
{
const char* validationErrorName(GraphValidationError error) noexcept
{
    switch (error)
    {
        case GraphValidationError::none: return "none";
        case GraphValidationError::capacityExceeded: return "capacity_exceeded";
        case GraphValidationError::missingNode: return "missing_node";
        case GraphValidationError::duplicateBindingId: return "duplicate_binding_id";
        case GraphValidationError::duplicateBinding: return "duplicate_binding";
        case GraphValidationError::selfEdge: return "self_edge";
        case GraphValidationError::controlCycle: return "control_cycle";
        case GraphValidationError::multipleParameterSources: return "multiple_parameter_sources";
        case GraphValidationError::unsupportedOutputSignal: return "unsupported_output_signal";
    }
    return "unknown";
}
}

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

bool RuntimeGraphConfig::add(OutputBinding binding) noexcept
{
    if (outputBindingCount == outputBindings.size())
        return false;
    outputBindings[outputBindingCount++] = binding;
    return true;
}

bool RuntimeGraphConfig::addPlayerConfig(
    PatternPlayerRuntimeConfig config) noexcept
{
    if (patternPlayerConfigCount == patternPlayerConfigs.size())
        return false;
    patternPlayerConfigs[patternPlayerConfigCount++] = config;
    return true;
}

bool RuntimeGraphConfig::addPlayerConfig(
    ModulationPlayerRuntimeConfig config) noexcept
{
    if (modulationPlayerConfigCount == modulationPlayerConfigs.size())
        return false;
    modulationPlayerConfigs[modulationPlayerConfigCount++] = config;
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
    {
        Logger::logf(LogLevel::warning, "runtime_graph", "registration_rejected",
            "node=pattern_player id=%u prepared=%d count=%zu",
            ref.value, prepared_ ? 1 : 0, patternPlayerCount_);
        return false;
    }
    patternPlayers_[patternPlayerCount_++] = &player;
    Logger::logf(LogLevel::debug, "runtime_graph", "node_registered",
        "node=pattern_player id=%u", ref.value);
    return true;
}

bool RuntimeGraph::registerModulationPlayer(ModulationPlayer& player) noexcept
{
    const auto ref = player.playerRef();
    if (prepared_ || ref.type != PlayerRefType::modulation
        || !ModulationPlayerId {ref.value}.isValid()
        || modulationPlayerCount_ == modulationPlayers_.size()
        || find(ModulationPlayerId {ref.value}) != nullptr)
    {
        Logger::logf(LogLevel::warning, "runtime_graph", "registration_rejected",
            "node=modulation_player id=%u prepared=%d count=%zu",
            ref.value, prepared_ ? 1 : 0, modulationPlayerCount_);
        return false;
    }
    modulationPlayers_[modulationPlayerCount_++] = &player;
    Logger::logf(LogLevel::debug, "runtime_graph", "node_registered",
        "node=modulation_player id=%u", ref.value);
    return true;
}

bool RuntimeGraph::registerVoice(Voice& voice) noexcept
{
    if (prepared_ || !voice.id().isValid()
        || voiceCount_ == voices_.size() || find(voice.id()) != nullptr)
    {
        Logger::logf(LogLevel::warning, "runtime_graph", "registration_rejected",
            "node=voice id=%u prepared=%d count=%zu",
            voice.id().value, prepared_ ? 1 : 0, voiceCount_);
        return false;
    }
    voices_[voiceCount_++] = &voice;
    Logger::logf(LogLevel::debug, "runtime_graph", "node_registered",
        "node=voice id=%u", voice.id().value);
    return true;
}

bool RuntimeGraph::registerOutputEndpoint(
    OutputEndpointId id,
    IOutputRenderer& renderer) noexcept
{
    if (prepared_ || !id.isValid()
        || outputEndpointCount_ == outputEndpoints_.size()
        || find(id) != nullptr)
    {
        Logger::logf(LogLevel::warning, "runtime_graph", "registration_rejected",
            "node=output_endpoint id=%u prepared=%d count=%zu",
            static_cast<unsigned>(id.value), prepared_ ? 1 : 0, outputEndpointCount_);
        return false;
    }
    auto& endpoint = outputEndpoints_[outputEndpointCount_++];
    endpoint.id = id;
    endpoint.renderer = &renderer;
    Logger::logf(LogLevel::debug, "runtime_graph", "node_registered",
        "node=output_endpoint id=%u", static_cast<unsigned>(id.value));
    return true;
}

bool RuntimeGraph::configureArmedCycleCommand(
    std::size_t slot,
    PatternPlayerId source,
    PlayerRef destination,
    PlayerCommand command) noexcept
{
    return configureArmedCommand(
        slot,
        {source, ControlSourcePort::cycleBoundary},
        destination,
        command);
}

bool RuntimeGraph::configureArmedCommand(
    std::size_t slot,
    ControlSource source,
    PlayerRef destination,
    PlayerCommand command) noexcept
{
    if (prepared_ || slot >= armedCommandPending_.size()
        || armedCommandCount_ == armedCommands_.size()
        || find(source.player) == nullptr || find(destination) == nullptr
        || (destination.type == PlayerRefType::pattern
            && destination.value == source.player.value))
    {
        return false;
    }

    for (std::size_t index = 0; index < armedCommandCount_; ++index)
    {
        const auto& configured = armedCommands_[index];
        if (configured.group != slot)
            continue;
        if (configured.source.player != source.player
            || configured.source.port != source.port
            || (configured.destination.type == destination.type
                && configured.destination.value == destination.value))
            return false;
    }

    armedCommands_[armedCommandCount_++] = {
        slot, source, destination, command, true};
    armedCommandPending_[slot].store(false, std::memory_order_relaxed);
    return true;
}

bool RuntimeGraph::armCommand(std::size_t slot) noexcept
{
    if (slot >= armedCommandPending_.size())
    {
        return false;
    }
    const bool configured = std::any_of(
        armedCommands_.begin(),
        armedCommands_.begin()
            + static_cast<std::ptrdiff_t>(armedCommandCount_),
        [slot](const auto& command)
        {
            return command.configured && command.group == slot;
        });
    if (!configured)
        return false;
    armedCommandPending_[slot].store(true, std::memory_order_release);
    return true;
}

bool RuntimeGraph::commandPending(std::size_t slot) const noexcept
{
    return slot < armedCommandPending_.size()
        && armedCommandPending_[slot].load(std::memory_order_acquire);
}

bool RuntimeGraph::armCycleCommand(std::size_t slot) noexcept
{
    return armCommand(slot);
}

bool RuntimeGraph::cycleCommandPending(std::size_t slot) const noexcept
{
    return commandPending(slot);
}

bool RuntimeGraph::configureBarClock(PatternPlayerId source) noexcept
{
    if (prepared_ || find(source) == nullptr)
        return false;
    barClockSource_ = source;
    currentBar_.store(0, std::memory_order_relaxed);
    lastBarBoundaryPpq_ = -std::numeric_limits<double>::infinity();
    return true;
}

std::uint64_t RuntimeGraph::encodeScheduledChange(
    const ScheduledChangeView& change) noexcept
{
    constexpr std::uint64_t valueShift = 32;
    constexpr std::uint64_t targetShift = 41;
    constexpr std::uint64_t barsShift = 46;
    constexpr std::uint64_t typeShift = 50;
    constexpr std::uint64_t activeBit = std::uint64_t {1} << 52;
    return activeBit
        | (static_cast<std::uint64_t>(change.type) << typeShift)
        | (static_cast<std::uint64_t>(change.barsRemaining) << barsShift)
        | (static_cast<std::uint64_t>(change.targetIndex) << targetShift)
        | (static_cast<std::uint64_t>(change.value) << valueShift)
        | change.sequence;
}

RuntimeGraph::ScheduledChangeView RuntimeGraph::decodeScheduledChange(
    std::uint64_t encoded) noexcept
{
    constexpr std::uint64_t valueShift = 32;
    constexpr std::uint64_t targetShift = 41;
    constexpr std::uint64_t barsShift = 46;
    constexpr std::uint64_t typeShift = 50;
    return {
        static_cast<ScheduledChangeType>((encoded >> typeShift) & 0x3u),
        static_cast<std::size_t>((encoded >> targetShift) & 0x1fu),
        static_cast<std::uint16_t>((encoded >> valueShift) & 0x1ffu),
        static_cast<std::size_t>((encoded >> barsShift) & 0xfu),
        static_cast<std::uint32_t>(encoded & 0xffffffffu)
    };
}

bool RuntimeGraph::scheduledChangeActive(std::uint64_t encoded) noexcept
{
    constexpr std::uint64_t activeBit = std::uint64_t {1} << 52;
    return (encoded & activeBit) != 0;
}

bool RuntimeGraph::scheduleChange(
    ScheduledChangeType type,
    std::size_t targetIndex,
    std::uint16_t value,
    std::size_t barsFromNow) noexcept
{
    if (!barClockSource_.isValid()
        || targetIndex >= maximumPatternPlayers
        || barsFromNow == 0 || barsFromNow > maximumScheduledBars)
    {
        return false;
    }

    const auto sequence = nextScheduledSequence_.fetch_add(
        1, std::memory_order_relaxed);
    const auto replacement = encodeScheduledChange({
        type, targetIndex, value, barsFromNow, sequence});

    // A second gesture aimed at the same target and boundary replaces the
    // earlier intent. Different future bars remain independent, allowing a
    // mute at bar 2 and an unmute at bar 6, for example.
    for (auto& slot : scheduledChanges_)
    {
        auto current = slot.load(std::memory_order_acquire);
        if (!scheduledChangeActive(current))
            continue;
        const auto decoded = decodeScheduledChange(current);
        if (decoded.type == type && decoded.targetIndex == targetIndex
            && decoded.barsRemaining == barsFromNow)
        {
            if (slot.compare_exchange_strong(
                    current, replacement,
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                return true;
            }
        }
    }

    for (auto& slot : scheduledChanges_)
    {
        std::uint64_t empty = 0;
        if (slot.compare_exchange_strong(
                empty, replacement,
                std::memory_order_release,
                std::memory_order_relaxed))
        {
            return true;
        }
    }
    return false;
}

bool RuntimeGraph::scheduleVoiceMute(
    VoiceId voice,
    bool muted,
    std::size_t barsFromNow) noexcept
{
    const auto target = voiceIndex(voice);
    return target < voiceCount_
        && scheduleChange(
            ScheduledChangeType::voiceMute,
            target,
            static_cast<std::uint16_t>(muted ? 1 : 0),
            barsFromNow);
}

bool RuntimeGraph::schedulePatternPlayerMute(
    PatternPlayerId player,
    bool muted,
    std::size_t barsFromNow) noexcept
{
    const auto target = patternPlayerIndex(player);
    return target < patternPlayerCount_
        && scheduleChange(
            ScheduledChangeType::patternPlayerMute,
            target,
            static_cast<std::uint16_t>(muted ? 1 : 0),
            barsFromNow);
}

bool RuntimeGraph::schedulePatternSelection(
    PatternPlayerId player,
    PatternId pattern,
    std::size_t barsFromNow) noexcept
{
    const auto target = patternPlayerIndex(player);
    return target < patternPlayerCount_ && pattern.isValid()
        && pattern.value() <= PatternLibrary::maxEntryCount
        && patternPlayers_[target]->canSelectSavedPattern(pattern)
        && scheduleChange(
            ScheduledChangeType::patternSelection,
            target,
            static_cast<std::uint16_t>(pattern.value()),
            barsFromNow);
}

std::optional<RuntimeGraph::ScheduledChangeView>
RuntimeGraph::nextScheduledChange(
    ScheduledChangeType type,
    std::size_t targetIndex) const noexcept
{
    std::optional<ScheduledChangeView> next;
    for (const auto& slot : scheduledChanges_)
    {
        const auto encoded = slot.load(std::memory_order_acquire);
        if (!scheduledChangeActive(encoded))
            continue;
        const auto change = decodeScheduledChange(encoded);
        if (change.type != type || change.targetIndex != targetIndex)
            continue;
        if (!next || change.barsRemaining < next->barsRemaining
            || (change.barsRemaining == next->barsRemaining
                && change.sequence > next->sequence))
        {
            next = change;
        }
    }
    return next;
}

std::optional<bool> RuntimeGraph::scheduledVoiceMute(
    VoiceId voice) const noexcept
{
    const auto target = voiceIndex(voice);
    if (target >= voiceCount_)
        return std::nullopt;
    const auto change = nextScheduledChange(
        ScheduledChangeType::voiceMute, target);
    return change ? std::optional<bool> {change->value != 0} : std::nullopt;
}

std::optional<bool> RuntimeGraph::scheduledPatternPlayerMute(
    PatternPlayerId player) const noexcept
{
    const auto target = patternPlayerIndex(player);
    if (target >= patternPlayerCount_)
        return std::nullopt;
    const auto change = nextScheduledChange(
        ScheduledChangeType::patternPlayerMute, target);
    return change ? std::optional<bool> {change->value != 0} : std::nullopt;
}

std::optional<PatternId> RuntimeGraph::scheduledPatternSelection(
    PatternPlayerId player) const noexcept
{
    const auto target = patternPlayerIndex(player);
    if (target >= patternPlayerCount_)
        return std::nullopt;
    const auto change = nextScheduledChange(
        ScheduledChangeType::patternSelection, target);
    return change
        ? std::optional<PatternId> {PatternId {change->value}}
        : std::nullopt;
}

std::size_t RuntimeGraph::scheduledVoiceMuteBarsRemaining(
    VoiceId voice) const noexcept
{
    const auto target = voiceIndex(voice);
    const auto change = target < voiceCount_
        ? nextScheduledChange(ScheduledChangeType::voiceMute, target)
        : std::nullopt;
    return change ? change->barsRemaining : 0;
}

std::size_t RuntimeGraph::scheduledPatternPlayerMuteBarsRemaining(
    PatternPlayerId player) const noexcept
{
    const auto target = patternPlayerIndex(player);
    const auto change = target < patternPlayerCount_
        ? nextScheduledChange(ScheduledChangeType::patternPlayerMute, target)
        : std::nullopt;
    return change ? change->barsRemaining : 0;
}

bool RuntimeGraph::voiceMuteScheduledAtBarOffset(
    VoiceId voice,
    bool muted,
    std::size_t barsFromNow) const noexcept
{
    const auto target = voiceIndex(voice);
    if (target >= voiceCount_ || barsFromNow == 0
        || barsFromNow > maximumScheduledBars)
    {
        return false;
    }

    for (const auto& slot : scheduledChanges_)
    {
        const auto encoded = slot.load(std::memory_order_acquire);
        if (!scheduledChangeActive(encoded))
            continue;
        const auto change = decodeScheduledChange(encoded);
        if (change.type == ScheduledChangeType::voiceMute
            && change.targetIndex == target
            && change.barsRemaining == barsFromNow
            && (change.value != 0) == muted)
        {
            return true;
        }
    }
    return false;
}

bool RuntimeGraph::patternPlayerMuteScheduledAtBarOffset(
    PatternPlayerId player,
    bool muted,
    std::size_t barsFromNow) const noexcept
{
    const auto target = patternPlayerIndex(player);
    if (target >= patternPlayerCount_ || barsFromNow == 0
        || barsFromNow > maximumScheduledBars)
    {
        return false;
    }

    for (const auto& slot : scheduledChanges_)
    {
        const auto encoded = slot.load(std::memory_order_acquire);
        if (!scheduledChangeActive(encoded))
            continue;
        const auto change = decodeScheduledChange(encoded);
        if (change.type == ScheduledChangeType::patternPlayerMute
            && change.targetIndex == target
            && change.barsRemaining == barsFromNow
            && (change.value != 0) == muted)
        {
            return true;
        }
    }
    return false;
}

std::size_t RuntimeGraph::scheduledPatternBarsRemaining(
    PatternPlayerId player) const noexcept
{
    const auto target = patternPlayerIndex(player);
    const auto change = target < patternPlayerCount_
        ? nextScheduledChange(ScheduledChangeType::patternSelection, target)
        : std::nullopt;
    return change ? change->barsRemaining : 0;
}

std::optional<PatternId>
RuntimeGraph::patternSelectionScheduledAtBarOffset(
    PatternPlayerId player,
    std::size_t barsFromNow) const noexcept
{
    const auto target = patternPlayerIndex(player);
    if (target >= patternPlayerCount_ || barsFromNow == 0
        || barsFromNow > maximumScheduledBars)
    {
        return std::nullopt;
    }

    for (const auto& slot : scheduledChanges_)
    {
        const auto encoded = slot.load(std::memory_order_acquire);
        if (!scheduledChangeActive(encoded))
            continue;
        const auto change = decodeScheduledChange(encoded);
        if (change.type == ScheduledChangeType::patternSelection
            && change.targetIndex == target
            && change.barsRemaining == barsFromNow)
        {
            return PatternId {change.value};
        }
    }
    return std::nullopt;
}

std::size_t RuntimeGraph::scheduledChangeCountAtBarOffset(
    std::size_t barsFromNow) const noexcept
{
    if (barsFromNow == 0 || barsFromNow > maximumScheduledBars)
        return 0;
    std::size_t count = 0;
    for (const auto& slot : scheduledChanges_)
    {
        const auto encoded = slot.load(std::memory_order_acquire);
        if (scheduledChangeActive(encoded)
            && decodeScheduledChange(encoded).barsRemaining == barsFromNow)
        {
            ++count;
        }
    }
    return count;
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

RuntimeGraph::OutputEndpoint* RuntimeGraph::find(OutputEndpointId id) noexcept
{
    for (std::size_t index = 0; index < outputEndpointCount_; ++index)
        if (outputEndpoints_[index].id == id)
            return &outputEndpoints_[index];
    return nullptr;
}

const RuntimeGraph::OutputEndpoint* RuntimeGraph::find(
    OutputEndpointId id) const noexcept
{
    for (std::size_t index = 0; index < outputEndpointCount_; ++index)
        if (outputEndpoints_[index].id == id)
            return &outputEndpoints_[index];
    return nullptr;
}

std::size_t RuntimeGraph::patternPlayerIndex(PatternPlayerId id) const noexcept
{
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        if (patternPlayers_[index]->playerRef().value == id.value)
            return index;
    return patternPlayerCount_;
}

std::size_t RuntimeGraph::voiceIndex(VoiceId id) const noexcept
{
    for (std::size_t index = 0; index < voiceCount_; ++index)
        if (voices_[index]->id() == id)
            return index;
    return voiceCount_;
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
        || candidate.commandBindingCount > candidate.commandBindings.size()
        || candidate.outputBindingCount > candidate.outputBindings.size()
        || candidate.patternPlayerConfigCount
            > candidate.patternPlayerConfigs.size()
        || candidate.modulationPlayerConfigCount
            > candidate.modulationPlayerConfigs.size())
        return GraphValidationError::capacityExceeded;

    const auto validAdvance = [this](
        const AdvanceSource& source,
        std::optional<PatternPlayerId> destination) noexcept
    {
        if (const auto* clock = std::get_if<ClockAdvance>(&source))
            return std::isfinite(clock->stepLengthPpq)
                && clock->stepLengthPpq > 0.0;
        const auto hit = std::get<PatternHitAdvance>(source);
        return find(hit.source) != nullptr
            && (!destination.has_value() || hit.source != *destination);
    };
    for (std::size_t index = 0;
         index < candidate.patternPlayerConfigCount;
         ++index)
    {
        const auto& config = candidate.patternPlayerConfigs[index];
        if (find(config.player) == nullptr
            || !validAdvance(config.advanceSource, config.player))
            return GraphValidationError::missingNode;
        if (config.transitionPolicy.type
            == PatternTransitionPolicyType::externalCycle)
        {
            if (find(config.transitionPolicy.externalSource) == nullptr)
                return GraphValidationError::missingNode;
            if (config.transitionPolicy.externalSource == config.player)
                return GraphValidationError::selfEdge;
        }
        for (std::size_t previous = 0; previous < index; ++previous)
            if (candidate.patternPlayerConfigs[previous].player == config.player)
                return GraphValidationError::duplicateBinding;
    }
    for (std::size_t index = 0;
         index < candidate.modulationPlayerConfigCount;
         ++index)
    {
        const auto& config = candidate.modulationPlayerConfigs[index];
        if (find(config.player) == nullptr
            || !validAdvance(config.advanceSource, std::nullopt))
            return GraphValidationError::missingNode;
        for (std::size_t previous = 0; previous < index; ++previous)
            if (candidate.modulationPlayerConfigs[previous].player == config.player)
                return GraphValidationError::duplicateBinding;
    }

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
        }
    }

    for (std::size_t index = 0; index < candidate.parameterBindingCount; ++index)
    {
        const auto& binding = candidate.parameterBindings[index];
        const auto* voice = find(binding.voice);
        const auto* parameter = voice != nullptr
            ? voice->parameter(binding.parameter) : nullptr;
        bool scopeTargetsVoice = !binding.triggerScope.isValid();
        for (std::size_t triggerIndex = 0;
             !scopeTargetsVoice
                && triggerIndex < candidate.triggerBindingCount;
             ++triggerIndex)
        {
            const auto& trigger = candidate.triggerBindings[triggerIndex];
            scopeTargetsVoice = trigger.source == binding.triggerScope
                && trigger.destination == binding.voice;
        }
        if (!binding.id.isValid() || find(binding.source) == nullptr
            || voice == nullptr || parameter == nullptr
            || (binding.triggerScope.isValid()
                && find(binding.triggerScope) == nullptr)
            || !scopeTargetsVoice
            || (parameter != nullptr
                && parameter->behavior == VoiceParameterBehavior::continuous
                && binding.triggerScope.isValid()))
            return GraphValidationError::missingNode;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const auto& other = candidate.parameterBindings[previous];
            if (other.id == binding.id)
                return GraphValidationError::duplicateBindingId;
            if (other.source == binding.source && other.voice == binding.voice
                && other.parameter == binding.parameter
                && other.triggerScope == binding.triggerScope)
                return GraphValidationError::duplicateBinding;
            if (other.voice == binding.voice
                && other.parameter == binding.parameter
                && other.triggerScope == binding.triggerScope)
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
    for (std::size_t index = 0; index < candidate.outputBindingCount; ++index)
    {
        const auto& binding = candidate.outputBindings[index];
        const auto* voice = find(binding.source);
        if (!binding.id.isValid() || voice == nullptr
            || find(binding.endpoint) == nullptr || !binding.route.isValid())
        {
            return GraphValidationError::missingNode;
        }
        if (binding.signal != OutputSignalType::triggers
            && binding.signal != OutputSignalType::continuousParameter)
        {
            return GraphValidationError::unsupportedOutputSignal;
        }
        if (binding.signal == OutputSignalType::continuousParameter)
        {
            const auto* parameter = voice->parameter(binding.parameter);
            if (parameter == nullptr
                || parameter->behavior != VoiceParameterBehavior::continuous)
            {
                return GraphValidationError::unsupportedOutputSignal;
            }
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            const auto& other = candidate.outputBindings[previous];
            if (other.id == binding.id)
                return GraphValidationError::duplicateBindingId;
            if (other.source == binding.source
                && other.endpoint == binding.endpoint
                && other.route == binding.route
                && other.signal == binding.signal
                && (binding.signal == OutputSignalType::triggers
                    || other.parameter == binding.parameter))
            {
                return GraphValidationError::duplicateBinding;
            }
        }
    }
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
    {
        auto advance = patternPlayers_[index]->advanceSource();
        auto policy = patternPlayers_[index]->transitionPolicy();
        for (std::size_t configIndex = 0;
             configIndex < candidate.patternPlayerConfigCount;
             ++configIndex)
        {
            const auto& config = candidate.patternPlayerConfigs[configIndex];
            if (config.player.value == patternPlayers_[index]->playerRef().value)
            {
                advance = config.advanceSource;
                policy = config.transitionPolicy;
                break;
            }
        }
        if (policy.type == PatternTransitionPolicyType::externalCycle)
        {
            if (find(policy.externalSource) == nullptr)
                return GraphValidationError::missingNode;
            if (policy.externalSource.value
                == patternPlayers_[index]->playerRef().value)
            {
                return GraphValidationError::selfEdge;
            }
        }
        if (const auto* hit = std::get_if<PatternHitAdvance>(&advance))
        {
            if (find(hit->source) == nullptr)
                return GraphValidationError::missingNode;
            if (hit->source.value == patternPlayers_[index]->playerRef().value)
                return GraphValidationError::selfEdge;
        }
    }
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
    {
        auto advance = modulationPlayers_[index]->advanceSource();
        for (std::size_t configIndex = 0;
             configIndex < candidate.modulationPlayerConfigCount;
             ++configIndex)
        {
            const auto& config = candidate.modulationPlayerConfigs[configIndex];
            if (config.player.value
                == modulationPlayers_[index]->playerRef().value)
            {
                advance = config.advanceSource;
                break;
            }
        }
        if (const auto* hit = std::get_if<PatternHitAdvance>(&advance))
            if (find(hit->source) == nullptr)
                return GraphValidationError::missingNode;
    }

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
    const auto addAdvance = [&](PlayerRef player, const AdvanceSource& advance)
    {
        if (const auto* hit = std::get_if<PatternHitAdvance>(&advance))
        {
            const auto from = nodeIndex(PlayerRef::pattern(hit->source));
            const auto to = nodeIndex(player);
            if (from < maximumNodes && to < maximumNodes)
                edges[from][to] = true;
        }
    };
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
    {
        auto advance = patternPlayers_[index]->advanceSource();
        auto policy = patternPlayers_[index]->transitionPolicy();
        for (std::size_t configIndex = 0;
             configIndex < candidate.patternPlayerConfigCount;
             ++configIndex)
        {
            const auto& config = candidate.patternPlayerConfigs[configIndex];
            if (config.player.value == patternPlayers_[index]->playerRef().value)
            {
                advance = config.advanceSource;
                policy = config.transitionPolicy;
                break;
            }
        }
        addAdvance(patternPlayers_[index]->playerRef(), advance);
        if (policy.type == PatternTransitionPolicyType::externalCycle)
        {
            const auto from = nodeIndex(
                PlayerRef::pattern(policy.externalSource));
            const auto to = nodeIndex(patternPlayers_[index]->playerRef());
            if (from < maximumNodes && to < maximumNodes)
                edges[from][to] = true;
        }
    }
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
    {
        auto advance = modulationPlayers_[index]->advanceSource();
        for (std::size_t configIndex = 0;
             configIndex < candidate.modulationPlayerConfigCount;
             ++configIndex)
        {
            const auto& config = candidate.modulationPlayerConfigs[configIndex];
            if (config.player.value
                == modulationPlayers_[index]->playerRef().value)
            {
                advance = config.advanceSource;
                break;
            }
        }
        addAdvance(modulationPlayers_[index]->playerRef(), advance);
    }

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
    {
        Logger::logf(LogLevel::error, "runtime_graph", "config_rejected",
            "operation=activate reason=%s", validationErrorName(lastValidationError_));
        return false;
    }
    configSnapshots_[0] = candidate;
    snapshotGenerations_[0] = ++nextPublicationGeneration_;
    audioSnapshot_ = 0;
    publishedSnapshot_.store(0, std::memory_order_release);
    acknowledgedSnapshot_.store(0, std::memory_order_release);
    publishedGeneration_.store(
        snapshotGenerations_[0], std::memory_order_release);
    activeGeneration_.store(
        snapshotGenerations_[0], std::memory_order_release);
    applyActivePlayerConfigs();
    Logger::logf(LogLevel::info, "runtime_graph", "config_activated",
        "generation=%llu pattern_players=%zu modulation_players=%zu voices=%zu outputs=%zu trigger_bindings=%zu parameter_bindings=%zu command_bindings=%zu output_bindings=%zu",
        static_cast<unsigned long long>(snapshotGenerations_[0]),
        patternPlayerCount_, modulationPlayerCount_, voiceCount_, outputEndpointCount_,
        candidate.triggerBindingCount, candidate.parameterBindingCount,
        candidate.commandBindingCount, candidate.outputBindingCount);
    return true;
}

bool RuntimeGraph::publish(const RuntimeGraphConfig& candidate) noexcept
{
    lastValidationError_ = validate(candidate);
    if (lastValidationError_ != GraphValidationError::none)
    {
        Logger::logf(LogLevel::error, "runtime_graph", "config_rejected",
            "operation=publish reason=%s", validationErrorName(lastValidationError_));
        return false;
    }

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
    Logger::logf(LogLevel::info, "runtime_graph", "config_published",
        "generation=%llu snapshot=%u",
        static_cast<unsigned long long>(snapshotGenerations_[destination]),
        static_cast<unsigned>(destination));
    return true;
}

const RuntimeGraphConfig& RuntimeGraph::activeConfig() const noexcept
{
    return configSnapshots_[audioSnapshot_];
}

void RuntimeGraph::adoptPublishedConfig() noexcept
{
    const auto published = publishedSnapshot_.load(std::memory_order_acquire);
    const bool changed = published != audioSnapshot_;
    audioSnapshot_ = published;
    if (changed)
    {
        applyActivePlayerConfigs();
        Logger::logf(LogLevel::info, "runtime_graph", "config_adopted",
            "generation=%llu snapshot=%u",
            static_cast<unsigned long long>(snapshotGenerations_[published]),
            static_cast<unsigned>(published));
    }
    activeGeneration_.store(
        snapshotGenerations_[published], std::memory_order_release);
    acknowledgedSnapshot_.store(published, std::memory_order_release);
}

void RuntimeGraph::applyActivePlayerConfigs() noexcept
{
    const auto& config = activeConfig();
    for (std::size_t index = 0; index < config.patternPlayerConfigCount; ++index)
    {
        const auto& playerConfig = config.patternPlayerConfigs[index];
        if (auto* player = find(playerConfig.player))
        {
            player->setAdvanceSource(playerConfig.advanceSource);
            player->setPlayMode(playerConfig.playMode);
            player->setTransitionPolicy(playerConfig.transitionPolicy);
        }
    }
    for (std::size_t index = 0;
         index < config.modulationPlayerConfigCount;
         ++index)
    {
        const auto& playerConfig = config.modulationPlayerConfigs[index];
        if (auto* player = find(playerConfig.player))
        {
            player->setAdvanceSource(playerConfig.advanceSource);
            player->setPlayMode(playerConfig.playMode);
            player->setSelectionQuantizedToPatternCycle(
                playerConfig.quantizeSelectionToPatternCycle);
        }
    }
}

void RuntimeGraph::prepare(const PrepareSpec& spec) noexcept
{
    prepareSpec_ = spec;
    currentBar_.store(0, std::memory_order_release);
    lastBarBoundaryPpq_ = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        patternPlayers_[index]->prepare(spec);
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        modulationPlayers_[index]->prepare(spec);
    for (std::size_t index = 0; index < voiceCount_; ++index)
        voices_[index]->reset();
    for (std::size_t index = 0; index < outputEndpointCount_; ++index)
        outputEndpoints_[index].renderer->prepare(
            {spec.sampleRate, spec.maximumBlockSize});
    prepared_ = true;
    unpreparedProcessReported_ = false;
    Logger::logf(LogLevel::info, "runtime_graph", "prepared",
        "sample_rate=%.1f maximum_block_size=%u pattern_players=%zu modulation_players=%zu voices=%zu outputs=%zu",
        spec.sampleRate, spec.maximumBlockSize, patternPlayerCount_,
        modulationPlayerCount_, voiceCount_, outputEndpointCount_);
}

void RuntimeGraph::reset() noexcept
{
    for (std::size_t index = 0; index < patternPlayerCount_; ++index)
        patternPlayers_[index]->reset();
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
        modulationPlayers_[index]->reset();
    for (std::size_t index = 0; index < voiceCount_; ++index)
        voices_[index]->reset();
    currentBar_.store(0, std::memory_order_release);
    lastBarBoundaryPpq_ = -std::numeric_limits<double>::infinity();
}

void RuntimeGraph::resetOutputs() noexcept
{
    for (std::size_t index = 0; index < voiceCount_; ++index)
        for (auto& trigger : audibleTriggers_[index])
            trigger = {};
    for (std::size_t index = 0; index < outputEndpointCount_; ++index)
    {
        outputEndpoints_[index].eventCount = 0;
        outputEndpoints_[index].renderer->resetOutputs();
    }
}

void RuntimeGraph::setVoiceMuted(VoiceId voice, bool muted) noexcept
{
    const auto index = voiceIndex(voice);
    if (index < voiceCount_)
        voiceMuted_[index].store(muted, std::memory_order_relaxed);
}

bool RuntimeGraph::voiceMuted(VoiceId voice) const noexcept
{
    const auto index = voiceIndex(voice);
    return index < voiceCount_
        && voiceMuted_[index].load(std::memory_order_relaxed);
}

void RuntimeGraph::setPatternPlayerMuted(
    PatternPlayerId player,
    bool muted) noexcept
{
    const auto index = patternPlayerIndex(player);
    if (index < patternPlayerCount_)
        patternPlayerMuted_[index].store(muted, std::memory_order_relaxed);
}

bool RuntimeGraph::patternPlayerMuted(
    PatternPlayerId player) const noexcept
{
    const auto index = patternPlayerIndex(player);
    return index < patternPlayerCount_
        && patternPlayerMuted_[index].load(std::memory_order_relaxed);
}

void RuntimeGraph::setSuppression(
    PatternPlayerId suppressor,
    PatternPlayerId suppressed,
    bool enabled) noexcept
{
    const auto from = patternPlayerIndex(suppressor);
    const auto to = patternPlayerIndex(suppressed);
    if (from < patternPlayerCount_ && to < patternPlayerCount_ && from != to)
        suppression_[from][to].store(enabled, std::memory_order_relaxed);
}

bool RuntimeGraph::suppression(
    PatternPlayerId suppressor,
    PatternPlayerId suppressed) const noexcept
{
    const auto from = patternPlayerIndex(suppressor);
    const auto to = patternPlayerIndex(suppressed);
    return from < patternPlayerCount_ && to < patternPlayerCount_ && from != to
        && suppression_[from][to].load(std::memory_order_relaxed);
}

bool RuntimeGraph::appendSignals(const PlayerSignalBuffer& signals) noexcept
{
    if (signals.overflowed())
        workOverflowed_ = true;
    for (const auto& signal : signals)
    {
        if (workSize_ == work_.size())
        {
            workOverflowed_ = true;
            return false;
        }
        work_[workSize_++] = {signal, nextWorkOrder_++, false, false};
    }
    return !workOverflowed_;
}

void RuntimeGraph::invalidatePlayerSignalsFrom(
    PlayerRef player,
    double ppqPosition,
    std::size_t limit) noexcept
{
    limit = std::min(limit, workSize_);
    for (std::size_t index = 0; index < limit; ++index)
    {
        auto& item = work_[index];
        if (item.resolved || item.signal.ppqPosition < ppqPosition)
            continue;
        const bool belongsToPlayer = player.type == PlayerRefType::pattern
            ? ((item.signal.type == PlayerSignalType::patternHit
                    || item.signal.type
                        == PlayerSignalType::patternCycleBoundary)
                && item.signal.patternPlayerId.value == player.value)
            : (item.signal.type == PlayerSignalType::modulationValue
                && item.signal.modulationPlayerId.value == player.value);
        if (belongsToPlayer)
        {
            item.resolved = true;
            item.propagated = true;
        }
    }
}

void RuntimeGraph::appendPlayerRemainder(
    IPlayer& player,
    double ppqPosition) noexcept
{
    if (!currentBlock_.playing || ppqPosition >= currentBlock_.ppqEnd)
        return;
    auto remainder = currentBlock_;
    remainder.ppqStart = ppqPosition + 2.0e-9;
    remainder.transportDiscontinuity = false;
    if (remainder.ppqStart >= remainder.ppqEnd)
        return;
    PlayerSignalBuffer generated;
    (void) player.process(remainder, generated);
    (void) appendSignals(generated);
}

void RuntimeGraph::applyArmedCommands(const PlayerSignal& source) noexcept
{
    const auto sourcePort = source.type == PlayerSignalType::patternHit
        ? std::optional<ControlSourcePort> {ControlSourcePort::hit}
        : source.type == PlayerSignalType::patternCycleBoundary
            ? std::optional<ControlSourcePort> {
                ControlSourcePort::cycleBoundary}
            : std::nullopt;
    if (!sourcePort)
        return;

    for (std::size_t group = 0;
         group < armedCommandPending_.size();
         ++group)
    {
        bool sourceMatches = false;
        for (std::size_t commandIndex = 0;
             commandIndex < armedCommandCount_;
             ++commandIndex)
        {
            const auto& armed = armedCommands_[commandIndex];
            if (armed.configured && armed.group == group
                && armed.source.player == source.patternPlayerId
                && armed.source.port == *sourcePort)
            {
                sourceMatches = true;
                break;
            }
        }
        if (!sourceMatches
            || !armedCommandPending_[group].exchange(
                false, std::memory_order_acq_rel))
        {
            continue;
        }

        for (std::size_t commandIndex = 0;
             commandIndex < armedCommandCount_;
             ++commandIndex)
        {
            const auto& armed = armedCommands_[commandIndex];
            if (!armed.configured || armed.group != group)
                continue;
            auto* destination = find(armed.destination);
            if (destination == nullptr)
                continue;
            const auto oldWorkSize = workSize_;
            PlayerSignalBuffer generated;
            destination->command(
                armed.command, source.ppqPosition, generated);
            invalidatePlayerSignalsFrom(
                armed.destination, source.ppqPosition, oldWorkSize);
            (void) appendSignals(generated);
            appendPlayerRemainder(*destination, source.ppqPosition);
        }
    }
}

void RuntimeGraph::applyScheduledBarBoundary(
    const PlayerSignal& boundary) noexcept
{
    constexpr double simultaneousTolerancePpq = 1.0e-9;
    if (!barClockSource_.isValid()
        || boundary.type != PlayerSignalType::patternCycleBoundary
        || boundary.patternPlayerId != barClockSource_
        || std::abs(boundary.ppqPosition - lastBarBoundaryPpq_)
            <= simultaneousTolerancePpq)
    {
        return;
    }

    lastBarBoundaryPpq_ = boundary.ppqPosition;
    const auto previousBar = currentBar_.load(std::memory_order_relaxed);
    currentBar_.store(
        static_cast<std::uint8_t>(previousBar >= maximumScheduledBars
            ? 1 : previousBar + 1),
        std::memory_order_release);

    // The first boundary starts bar 1; it does not finish a bar. Scheduled
    // changes target transitions between bars, so their countdown must begin
    // at the next boundary (the transition from bar 1 to bar 2).
    if (previousBar == 0)
        return;

    std::array<ScheduledChangeView, maximumScheduledChanges> due {};
    std::size_t dueCount = 0;
    for (auto& slot : scheduledChanges_)
    {
        auto encoded = slot.load(std::memory_order_acquire);
        while (scheduledChangeActive(encoded))
        {
            auto change = decodeScheduledChange(encoded);
            if (change.barsRemaining > 1)
            {
                --change.barsRemaining;
                const auto decremented = encodeScheduledChange(change);
                if (slot.compare_exchange_weak(
                        encoded, decremented,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    break;
                }
                continue;
            }

            if (slot.compare_exchange_weak(
                    encoded, 0,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                due[dueCount++] = change;
                break;
            }
        }
    }

    std::sort(
        due.begin(),
        due.begin() + static_cast<std::ptrdiff_t>(dueCount),
        [](const auto& left, const auto& right)
        {
            return left.sequence < right.sequence;
        });

    for (std::size_t index = 0; index < dueCount; ++index)
    {
        const auto& change = due[index];
        if (change.type == ScheduledChangeType::patternPlayerMute)
        {
            if (change.targetIndex < patternPlayerCount_)
            {
                patternPlayerMuted_[change.targetIndex].store(
                    change.value != 0, std::memory_order_relaxed);
            }
            continue;
        }

        if (change.type == ScheduledChangeType::voiceMute)
        {
            if (change.targetIndex < voiceCount_)
            {
                voiceMuted_[change.targetIndex].store(
                    change.value != 0, std::memory_order_relaxed);
            }
            continue;
        }

        if (change.targetIndex >= patternPlayerCount_)
            continue;
        auto& player = *patternPlayers_[change.targetIndex];
        player.selectSavedPattern(PatternId {change.value});
        const auto oldWorkSize = workSize_;
        PlayerSignalBuffer generated;
        if (player.activateSelectedPatternAtBoundary(
                boundary.ppqPosition, generated))
        {
            invalidatePlayerSignalsFrom(
                player.playerRef(), boundary.ppqPosition, oldWorkSize);
            (void) appendSignals(generated);
            appendPlayerRemainder(player, boundary.ppqPosition);
        }
        else
        {
            // A UI edit can briefly own the player's draft seqlock. Keep the
            // musical request alive and retry at the next master boundary.
            (void) scheduleChange(
                ScheduledChangeType::patternSelection,
                change.targetIndex,
                change.value,
                1);
        }
    }
}

bool RuntimeGraph::triggerEligible(
    std::size_t sourceVoiceIndex,
    PatternPlayerId triggerSource,
    const SequencerEvent& event) noexcept
{
    if (event.type == SemanticEventType::triggerStart)
    {
        const auto suppressed = patternPlayerIndex(triggerSource);
        bool eligible = !voiceMuted_[sourceVoiceIndex].load(
            std::memory_order_relaxed)
            && suppressed < patternPlayerCount_
            && !patternPlayerMuted_[suppressed].load(
                std::memory_order_relaxed);
        for (std::size_t suppressor = 0;
             eligible && suppressed < patternPlayerCount_
                && suppressor < patternPlayerCount_;
             ++suppressor)
        {
            if (suppression_[suppressor][suppressed].load(
                    std::memory_order_relaxed)
                && patternHitOccurred(
                    PatternPlayerId {
                        patternPlayers_[suppressor]->playerRef().value },
                    event.ppqPosition))
            {
                eligible = false;
            }
        }
        for (auto& trigger : audibleTriggers_[sourceVoiceIndex])
        {
            if (!trigger.active)
            {
                trigger = {event.triggerId, eligible, true};
                return eligible;
            }
        }
        return false;
    }

    if (event.type == SemanticEventType::triggerEnd)
    {
        for (auto& trigger : audibleTriggers_[sourceVoiceIndex])
        {
            if (trigger.active && trigger.id == event.triggerId)
            {
                const bool eligible = trigger.eligible;
                trigger = {};
                return eligible;
            }
        }
        return false;
    }

    return true;
}

void RuntimeGraph::appendVoiceEvents(
    Voice& voice,
    PatternPlayerId triggerSource,
    const SequencerEventBuffer& events,
    ResolvedVoiceEventBuffer& output) noexcept
{
    for (const auto& event : events)
    {
        const auto sourceVoiceIndex = voiceIndex(voice.id());
        const bool eligible = sourceVoiceIndex < voiceCount_
            && triggerEligible(sourceVoiceIndex, triggerSource, event);
        (void) output.push({
            event, voice.id(), triggerSource, nextResolvedOrder_++, eligible});
    }
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
                // A hit-armed reset must publish step zero before the same
                // hit would normally advance the destination. The player
                // suppresses that duplicate same-timestamp advance.
                applyArmedCommands(item.signal);
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
                applyScheduledBarBoundary(item.signal);
                for (std::size_t player = 0;
                     player < patternPlayerCount_;
                     ++player)
                {
                    PlayerSignalBuffer generated;
                    const auto oldWorkSize = workSize_;
                    if (patternPlayers_[player]->observeCycleBoundary(
                            item.signal, generated))
                    {
                        invalidatePlayerSignalsFrom(
                            patternPlayers_[player]->playerRef(), ppq,
                            oldWorkSize);
                        (void) appendSignals(generated);
                        appendPlayerRemainder(*patternPlayers_[player], ppq);
                    }
                }
                for (std::size_t player = 0;
                     player < modulationPlayerCount_;
                     ++player)
                {
                    (void) modulationPlayers_[player]
                        ->observeCycleBoundary(item.signal);
                }
                applyArmedCommands(item.signal);
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
                    const auto oldWorkSize = workSize_;
                    PlayerSignalBuffer generated;
                    destination->command(binding.command, ppq, generated);
                    invalidatePlayerSignalsFrom(
                        binding.destination, ppq, oldWorkSize);
                    (void) appendSignals(generated);
                    appendPlayerRemainder(*destination, ppq);
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
            if (item.resolved
                || item.signal.ppqPosition != ppq
                || item.signal.type != PlayerSignalType::modulationValue
                || item.signal.modulationPlayerId != binding.source)
                continue;
            SequencerEventBuffer events;
            if (auto* voice = find(binding.voice))
            {
                (void) voice->applyParameterValue(
                    binding.parameter, item.signal, events,
                    binding.triggerScope);
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
            if (item.resolved
                || item.signal.ppqPosition != ppq
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
    if (!transportStateKnown_ || block.playing != lastTransportPlaying_)
    {
        Logger::logf(LogLevel::info, "runtime_graph", "transport_state",
            "playing=%d ppq=%.9f tempo_bpm=%.3f source_discontinuity=%d",
            block.playing ? 1 : 0, block.ppqStart, block.tempoBpm,
            block.transportDiscontinuity ? 1 : 0);
        lastTransportPlaying_ = block.playing;
        transportStateKnown_ = true;
    }
    else if (block.transportDiscontinuity)
    {
        Logger::logf(LogLevel::warning, "runtime_graph", "transport_discontinuity",
            "ppq=%.9f tempo_bpm=%.3f", block.ppqStart, block.tempoBpm);
    }
    currentBlock_ = block;
    if (block.transportDiscontinuity)
    {
        resetOutputs();
        currentBar_.store(0, std::memory_order_release);
        lastBarBoundaryPpq_ = -std::numeric_limits<double>::infinity();
    }
    output.clear();
    workSize_ = 0;
    nextWorkOrder_ = 0;
    nextResolvedOrder_ = 0;
    workOverflowed_ = false;
    currentPpqPerSample_ = block.sampleRate > 0.0
        && block.tempoBpm > 0.0
        ? block.tempoBpm / (60.0 * block.sampleRate)
        : 0.0;
    if (!prepared_)
    {
        if (!unpreparedProcessReported_)
        {
            Logger::write(LogLevel::error, "runtime_graph", "process_before_prepare");
            unpreparedProcessReported_ = true;
        }
        reset();
        return false;
    }
    if (!block.playing)
    {
        reset();
        return true;
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
        (void) patternPlayers_[index]->process(block, generated);
        (void) appendSignals(generated);
    }
    for (std::size_t index = 0; index < modulationPlayerCount_; ++index)
    {
        PlayerSignalBuffer generated;
        (void) modulationPlayers_[index]->process(block, generated);
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
    const bool healthy = !workOverflowed_ && !output.overflowed();
    if (!healthy && !processIssueActive_)
    {
        Logger::logf(LogLevel::error, "runtime_graph", "process_overflow",
            "work_overflow=%d output_overflow=%d work_signals=%zu resolved_events=%zu",
            workOverflowed_ ? 1 : 0, output.overflowed() ? 1 : 0,
            workSize_, output.size());
        processIssueActive_ = true;
    }
    else if (healthy && processIssueActive_)
    {
        Logger::write(LogLevel::info, "runtime_graph", "process_recovered");
        processIssueActive_ = false;
    }

    statusSampleCount_ += block.sampleCount;
    ++statusBlockCount_;
    const auto oneSecond = block.sampleRate > 0.0
        ? static_cast<std::uint64_t>(block.sampleRate) : 48'000u;
    if (statusSampleCount_ >= oneSecond)
    {
        Logger::logf(LogLevel::info, "runtime_graph", "status",
            "playing=%d generation=%llu blocks=%llu work_signals=%zu resolved_events=%zu logger_dropped=%llu logger_write_failures=%llu",
            block.playing ? 1 : 0,
            static_cast<unsigned long long>(activeGeneration()),
            static_cast<unsigned long long>(statusBlockCount_),
            workSize_, output.size(),
            static_cast<unsigned long long>(Logger::diagnostics().dropped),
            static_cast<unsigned long long>(Logger::diagnostics().writeFailures));
        statusSampleCount_ = 0;
        statusBlockCount_ = 0;
    }
    return healthy;
}

std::optional<std::uint32_t> RuntimeGraph::frameOffsetFor(
    const SequencerEvent& event,
    const TimelineBlock& block) const noexcept
{
    if (!std::isfinite(event.ppqPosition))
        return std::nullopt;
    if (block.sampleCount == 0)
        return std::uint32_t {0};
    if (!std::isfinite(block.ppqStart)
        || !std::isfinite(block.tempoBpm)
        || !std::isfinite(block.sampleRate)
        || block.tempoBpm <= 0.0 || block.sampleRate <= 0.0)
    {
        return std::nullopt;
    }

    const auto ppqPerSample = block.tempoBpm / (60.0 * block.sampleRate);
    const auto rawOffset = (event.ppqPosition - block.ppqStart) / ppqPerSample;
    // Events may occur anywhere in the half-open block, including its final
    // half-sample. Rounding those positions produces sampleCount, so accept
    // them here and let the clamp below place them on the last valid frame.
    if (!std::isfinite(rawOffset) || rawOffset < -0.5
        || rawOffset > static_cast<double>(block.sampleCount))
    {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        std::llround(rawOffset), 0,
        static_cast<std::int64_t>(block.sampleCount - 1)));
}

bool RuntimeGraph::render(
    const TimelineBlock& block,
    const ResolvedVoiceEventBuffer& events) noexcept
{
    for (std::size_t index = 0; index < outputEndpointCount_; ++index)
        outputEndpoints_[index].eventCount = 0;

    const auto& config = activeConfig();
    for (const auto& resolved : events)
    {
        const auto sourceVoiceIndex = voiceIndex(resolved.sourceVoiceId);
        if (sourceVoiceIndex >= voiceCount_)
        {
            Logger::logf(LogLevel::error, "runtime_graph", "render_rejected",
                "reason=missing_voice voice_id=%u", resolved.sourceVoiceId.value);
            resetOutputs();
            return false;
        }

        if (!resolved.eligible
            && resolved.event.type != SemanticEventType::controlPoint)
            continue;
        const auto frameOffset = frameOffsetFor(resolved.event, block);
        if (!frameOffset.has_value())
        {
            Logger::logf(LogLevel::error, "runtime_graph", "render_rejected",
                "reason=invalid_frame_offset voice_id=%u ppq=%.9f block_start=%.9f block_end=%.9f",
                resolved.sourceVoiceId.value, resolved.event.ppqPosition,
                block.ppqStart, block.ppqEnd);
            resetOutputs();
            return false;
        }

        for (std::size_t bindingIndex = 0;
             bindingIndex < config.outputBindingCount;
             ++bindingIndex)
        {
            const auto& binding = config.outputBindings[bindingIndex];
            if (binding.source != resolved.sourceVoiceId)
                continue;
            const bool routesTrigger = binding.signal == OutputSignalType::triggers
                && resolved.event.type != SemanticEventType::controlPoint;
            const bool routesControl =
                binding.signal == OutputSignalType::continuousParameter
                && resolved.event.type == SemanticEventType::controlPoint
                && resolved.event.voiceParameterId == binding.parameter;
            if (!routesTrigger && !routesControl)
                continue;

            auto* endpoint = find(binding.endpoint);
            if (endpoint == nullptr
                || endpoint->eventCount == endpoint->events.size())
            {
                Logger::logf(LogLevel::error, "runtime_graph", "render_rejected",
                    "reason=endpoint_missing_or_full endpoint_id=%u event_count=%zu",
                    static_cast<unsigned>(binding.endpoint.value),
                    endpoint == nullptr ? std::size_t {0} : endpoint->eventCount);
                resetOutputs();
                return false;
            }
            auto& routed = endpoint->events[endpoint->eventCount++];
            routed = {};
            routed.event = resolved.event;
            routed.sourceVoiceId = resolved.sourceVoiceId;
            routed.routeId = binding.route;
            routed.frameOffset = *frameOffset;
            routed.stableOrder = resolved.stableOrder
                * RuntimeGraphConfig::maximumOutputBindings + bindingIndex;
        }
    }

    for (std::size_t index = 0; index < outputEndpointCount_; ++index)
    {
        auto& endpoint = outputEndpoints_[index];
        if (!endpoint.renderer->renderBlock(
                block, {endpoint.events.data(), endpoint.eventCount}))
        {
            Logger::logf(LogLevel::error, "runtime_graph", "renderer_failed",
                "endpoint_id=%u event_count=%zu",
                static_cast<unsigned>(endpoint.id.value), endpoint.eventCount);
            resetOutputs();
            return false;
        }
    }
    return true;
}

} // namespace lps
