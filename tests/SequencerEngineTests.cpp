#include "core/SequencerEngine.h"
#include "core/PatternLibrary.h"
#include "core/PatternPlayer.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace
{

[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

constexpr double epsilon = 1.0e-9;

[[nodiscard]] bool close(double left, double right)
{
    return std::abs(left - right) < epsilon;
}

[[nodiscard]] const lps::PatternLibraryEntry& entryAt(
    const lps::PatternLibrary& library,
    std::size_t index)
{
    const auto* entry = library.recordAt(index);
    CHECK(entry != nullptr);
    return *entry;
}

class TestPlayer final : public lps::IPlayer
{
public:
    void prepare(const lps::PrepareSpec& spec) noexcept override
    {
        preparedRate = spec.sampleRate;
        preparedMaximumBlockSize = spec.maximumBlockSize;
    }
    void reset() noexcept override
    {
        wasReset = true;
        ++resetCount;
    }
    [[nodiscard]] lps::PlayerProcessResult process(
        const lps::TimelineBlock& block,
        const lps::PlayerDirectives& directives,
        lps::SequencerEventBuffer& output) noexcept override
    {
        ++processCount;
        lastDirectives = directives;
        const auto ppq = block.ppqStart + eventOffset;
        const auto event = eventType == lps::SemanticEventType::triggerStart
            ? lps::SequencerEvent::triggerStart(
                ppq, lps::TriggerId { 2 }, 0.75f, 64.0f)
            : lps::SequencerEvent::triggerEnd(ppq, lps::TriggerId { 2 });
        (void) output.push(event);
        lps::PlayerProcessResult result;
        result.active = true;
        if (capabilities.providesCycleBoundaries)
            result.firstCycleBoundaryPpq = block.ppqStart + cycleBoundaryOffset;
        result.eventOverflow = output.overflowed();
        return result;
    }
    [[nodiscard]] lps::PlayerSyncCapabilities syncCapabilities() const noexcept override
    {
        return capabilities;
    }
    double preparedRate = 0.0;
    std::uint32_t preparedMaximumBlockSize = 0;
    int processCount = 0;
    bool wasReset = false;
    int resetCount = 0;
    double eventOffset = 0.25;
    lps::SemanticEventType eventType = lps::SemanticEventType::triggerStart;
    double cycleBoundaryOffset = 0.0;
    lps::PlayerDirectives lastDirectives;
    lps::PlayerSyncCapabilities capabilities;
};

struct DeliveredEvent
{
    double ppqPosition = 0.0;
    float pitchSemitones = 0.0f;
    lps::TriggerId triggerId;
    lps::SemanticEventType type = lps::SemanticEventType::triggerEnd;
    lps::PlayerId sourcePlayerId;
    lps::RouteId routeId;
    std::uint32_t frameOffset = 0;
};

[[nodiscard]] DeliveredEvent delivered(const lps::RoutedEvent& routed)
{
    return {
        routed.event.ppqPosition,
        routed.mappedPitchSemitones,
        routed.event.triggerId,
        routed.event.type,
        routed.sourcePlayerId,
        routed.routeId,
        routed.frameOffset
    };
}

class TestTransport final : public lps::ITransport
{
public:
    void prepare(const lps::PrepareSpec& spec) noexcept override
    {
        preparedRate = spec.sampleRate;
        preparedMaximumBlockSize = spec.maximumBlockSize;
        ++prepareCount;
    }
    [[nodiscard]] bool send(const lps::RoutedEvent& event) noexcept override
    {
        lastEvent = delivered(event);
        ++sendCount;
        return deliverySucceeds;
    }
    void resetOutputs(const lps::TimelineBlock&) noexcept override
    {
        ++resetCount;
    }
    DeliveredEvent lastEvent {};
    double preparedRate = 0.0;
    std::uint32_t preparedMaximumBlockSize = 0;
    int prepareCount = 0;
    int sendCount = 0;
    int resetCount = 0;
    bool deliverySucceeds = true;
};

class CollectingTransport final : public lps::ITransport
{
public:
    void prepare(const lps::PrepareSpec&) noexcept override {}
    [[nodiscard]] bool send(const lps::RoutedEvent& event) noexcept override
    {
        events.push_back(delivered(event));
        return true;
    }
    void resetOutputs(const lps::TimelineBlock&) noexcept override { ++resetCount; }

    std::vector<DeliveredEvent> events;
    int resetCount = 0;
};

class OverflowingPlayer final : public lps::IPlayer
{
public:
    void prepare(const lps::PrepareSpec&) noexcept override {}
    void reset() noexcept override { ++resetCount; }
    [[nodiscard]] lps::PlayerProcessResult process(
        const lps::TimelineBlock& block,
        const lps::PlayerDirectives&,
        lps::SequencerEventBuffer& output) noexcept override
    {
        for (std::size_t index = 0;
             index < lps::SequencerEventBuffer::capacity + 1;
             ++index)
        {
            (void) output.push(lps::SequencerEvent::triggerStart(
                block.ppqStart + static_cast<double>(index) * 0.001,
                lps::TriggerId { index + 1 },
                1.0f,
                60.0f));
        }
        return { true, std::nullopt, output.overflowed() };
    }
    [[nodiscard]] lps::PlayerSyncCapabilities syncCapabilities() const noexcept override
    {
        return {};
    }
    int resetCount = 0;
};

class MalformedEventPlayer final : public lps::IPlayer
{
public:
    void prepare(const lps::PrepareSpec&) noexcept override {}
    void reset() noexcept override { ++resetCount; }
    [[nodiscard]] lps::PlayerProcessResult process(
        const lps::TimelineBlock&,
        const lps::PlayerDirectives&,
        lps::SequencerEventBuffer& output) noexcept override
    {
        (void) output.push(lps::SequencerEvent::triggerStart(
            0.2, { 1 }, 1.0f, 60.0f));
        (void) output.push(lps::SequencerEvent::triggerStart(
            std::numeric_limits<double>::quiet_NaN(), { 2 }, 1.0f, 61.0f));
        (void) output.push(lps::SequencerEvent::triggerStart(
            0.1, { 3 }, 1.0f, 62.0f));
        (void) output.push(lps::SequencerEvent::triggerStart(
            0.3, { 4 }, 1.0f, 63.0f));
        return { true, std::nullopt, output.overflowed() };
    }
    [[nodiscard]] lps::PlayerSyncCapabilities syncCapabilities() const noexcept override
    {
        return {};
    }
    int resetCount = 0;
};

class EventListPlayer final : public lps::IPlayer
{
public:
    void prepare(const lps::PrepareSpec&) noexcept override {}
    void reset() noexcept override {}
    [[nodiscard]] lps::PlayerProcessResult process(
        const lps::TimelineBlock&,
        const lps::PlayerDirectives&,
        lps::SequencerEventBuffer& output) noexcept override
    {
        for (const auto& event : events)
            (void) output.push(event);
        return { true, std::nullopt, output.overflowed() };
    }
    [[nodiscard]] lps::PlayerSyncCapabilities syncCapabilities() const noexcept override
    {
        return {};
    }

    std::vector<lps::SequencerEvent> events;
};

[[nodiscard]] std::vector<double> triggerStartPositions(
    const CollectingTransport& transport)
{
    std::vector<double> positions;
    for (const auto& event : transport.events)
        if (event.type == lps::SemanticEventType::triggerStart)
            positions.push_back(event.ppqPosition);
    return positions;
}

void checkPositions(
    const std::vector<double>& actual,
    std::initializer_list<double> expected)
{
    CHECK(actual.size() == expected.size());
    std::size_t index = 0;
    for (const auto position : expected)
        CHECK(close(actual[index++], position));
}

[[nodiscard]] lps::PlayerId registerAndConnect(
    lps::SequencerEngine& engine,
    lps::IPlayer& player,
    lps::ITransport& transport,
    lps::RouteMapping mapping = {})
{
    const auto playerId = engine.registerPlayer(player);
    CHECK(playerId.has_value());
    const auto routeId = engine.connect(*playerId, transport, mapping);
    CHECK(routeId.has_value());
    return *playerId;
}

[[nodiscard]] lps::TimelineBlock playingBlock(
    double start,
    double end,
    bool discontinuity = false)
{
    lps::TimelineBlock block;
    block.ppqStart = start;
    block.ppqEnd = end;
    block.playing = true;
    block.transportDiscontinuity = discontinuity;
    return block;
}

void testLifecycleAndRouting()
{
    lps::SequencerEngine engine;
    TestPlayer player;
    TestTransport transport;
    (void) registerAndConnect(engine, player, transport);

    engine.prepare({ 48'000.0, 512 });
    CHECK(player.preparedRate == 48'000.0);
    CHECK(player.preparedMaximumBlockSize == 512);
    CHECK(transport.preparedRate == 48'000.0);
    CHECK(transport.preparedMaximumBlockSize == 512);
    CHECK(transport.prepareCount == 1);

    lps::TimelineBlock block;
    block.ppqStart = 4.0;
    block.ppqEnd = 4.5;
    block.playing = true;
    engine.run(block);

    CHECK(player.processCount == 1);
    CHECK(transport.sendCount == 1);
    CHECK(transport.lastEvent.ppqPosition == 4.25);
    CHECK(transport.lastEvent.pitchSemitones == 64.0f);
    CHECK(transport.lastEvent.type == lps::SemanticEventType::triggerStart);
    CHECK(transport.lastEvent.triggerId == lps::TriggerId { 2 });
    CHECK(transport.lastEvent.sourcePlayerId == lps::PlayerId { 0 });
    CHECK(transport.lastEvent.routeId == lps::RouteId { 0 });
    // The custom player deliberately does not clear its output. A second run
    // still routes only the newly emitted event because buffers belong to the
    // engine.
    engine.run(block);
    CHECK(player.processCount == 2);
    CHECK(transport.sendCount == 2);

    engine.reset();
    CHECK(player.wasReset);
    CHECK(transport.resetCount == 1);
}

void testRegistrationConnectionsAndTopologyFreeze()
{
    lps::SequencerEngine engine;
    TestPlayer first;
    TestPlayer second;
    TestPlayer late;
    TestTransport firstTransport;
    TestTransport secondTransport;

    const auto firstId = engine.registerPlayer(first);
    const auto secondId = engine.registerPlayer(second);
    CHECK(firstId == lps::PlayerId { 0 });
    CHECK(secondId == lps::PlayerId { 1 });
    CHECK(!engine.registerPlayer(first).has_value());
    CHECK(engine.playerIdAt(0) == firstId);
    CHECK(engine.playerIdAt(1) == secondId);
    CHECK(!engine.playerIdAt(2).has_value());

    const auto firstRoute = engine.connect(*firstId, firstTransport);
    const auto secondRoute = engine.connect(*secondId, firstTransport);
    CHECK(firstRoute == lps::RouteId { 0 });
    CHECK(secondRoute == lps::RouteId { 1 });
    CHECK(!engine.connect(*firstId, firstTransport).has_value());
    CHECK(!engine.connect(lps::PlayerId { 99 }, secondTransport).has_value());

    CHECK(!engine.topologyFrozen());
    engine.prepare({ 48'000.0, 512 });
    CHECK(engine.topologyFrozen());
    CHECK(!engine.registerPlayer(late).has_value());
    CHECK(!engine.connect(*firstId, secondTransport).has_value());

    // Re-preparation is a normal lifecycle operation and keeps the topology.
    engine.prepare({ 96'000.0, 1'024 });
    CHECK(engine.playerCount() == 2);
    CHECK(firstTransport.prepareCount == 2);
}

void testEventBufferReportsOverflowUntilCleared()
{
    lps::SequencerEventBuffer events;
    for (std::size_t index = 0; index < lps::SequencerEventBuffer::capacity; ++index)
        CHECK(events.push({}));

    CHECK(!events.overflowed());
    CHECK(events.droppedCount() == 0);
    CHECK(!events.push({}));
    CHECK(!events.push({}));
    CHECK(events.overflowed());
    CHECK(events.droppedCount() == 2);

    events.clear();
    CHECK(events.empty());
    CHECK(!events.overflowed());
    CHECK(events.droppedCount() == 0);
}

void testEnginePublishesPlayerOverflowDiagnostics()
{
    lps::SequencerEngine engine;
    OverflowingPlayer player;
    TestTransport transport;
    (void) registerAndConnect(engine, player, transport);

    engine.run({});

    CHECK(engine.playerEventOverflowed(lps::PlayerId { 0 }));
    CHECK(engine.playerDroppedEventCount(lps::PlayerId { 0 }) == 1);
    CHECK(transport.sendCount == 0);
    CHECK(transport.resetCount == 1);
    CHECK(player.resetCount == 1);
    CHECK(!engine.playerEventOverflowed(lps::PlayerId { 1 }));
    CHECK(engine.playerDroppedEventCount(lps::PlayerId { 1 }) == 0);
}

void testEngineRejectsNonFiniteAndOutOfOrderEvents()
{
    lps::SequencerEngine engine;
    MalformedEventPlayer player;
    CollectingTransport transport;
    (void) registerAndConnect(engine, player, transport);

    engine.run({});

    CHECK(transport.events.empty());
    CHECK(transport.resetCount == 1);
    CHECK(player.resetCount == 1);
    CHECK(engine.playerInvalidEventCount(lps::PlayerId { 0 }) == 2);
    CHECK(engine.playerInvalidEventCount(lps::PlayerId { 1 }) == 0);
}

void testSharedTransportLifecycleRunsOnce()
{
    lps::SequencerEngine engine;
    TestPlayer first;
    TestPlayer second;
    TestTransport transport;
    (void) registerAndConnect(engine, first, transport);
    (void) registerAndConnect(engine, second, transport);

    engine.prepare({ 96'000.0, 1'024 });
    CHECK(transport.prepareCount == 1);
    CHECK(transport.preparedRate == 96'000.0);
    CHECK(transport.preparedMaximumBlockSize == 1'024);

    auto block = playingBlock(2.0, 2.5, true);
    engine.run(block);
    CHECK(transport.resetCount == 1);
    CHECK(transport.sendCount == 2);

    engine.reset();
    CHECK(transport.resetCount == 2);
}

void testTransportDeliveryFailureResetsOutputAndPlayer()
{
    lps::SequencerEngine engine;
    TestPlayer player;
    TestTransport transport;
    transport.deliverySucceeds = false;
    (void) registerAndConnect(engine, player, transport);

    engine.run({});

    CHECK(transport.sendCount == 1);
    CHECK(transport.resetCount == 1);
    CHECK(player.resetCount == 1);
}

void testMasterRequiresCycleBoundaryCapability()
{
    lps::SequencerEngine engine;
    TestPlayer nonCyclicPlayer;
    TestTransport transport;
    (void) registerAndConnect(engine, nonCyclicPlayer, transport);

    CHECK(!engine.setMasterPlayer(lps::PlayerId { 0 }));
    CHECK(!engine.masterPlayerId().has_value());
}

void testCyclicNonPatternPlayerCanDriveGenericDirectives()
{
    lps::SequencerEngine engine;
    TestPlayer master;
    TestPlayer follower;
    TestTransport masterTransport;
    TestTransport followerTransport;
    master.capabilities.providesCycleBoundaries = true;
    follower.capabilities.acceptsExternalCycleBoundaries = true;
    follower.capabilities.acceptsExternalRestart = true;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, follower, followerTransport);

    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    CHECK(engine.requestResetToMaster(lps::PlayerId { 1 }));
    engine.run(playingBlock(3.0, 3.5));

    CHECK(follower.lastDirectives.quantizePendingTransitionsExternally);
    CHECK(follower.lastDirectives.externalCycleBoundaryPpq.has_value());
    CHECK(close(*follower.lastDirectives.externalCycleBoundaryPpq, 3.0));
    CHECK(follower.lastDirectives.restartAtPpq.has_value());
    CHECK(close(*follower.lastDirectives.restartAtPpq, 3.0));
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 1 }));
}

void testPatternViewRejectsStepsOutsideItsMaskWidth()
{
    lps::PatternView view;
    view.stepCount = 64;
    view.hitMask = 1;

    CHECK(view.isHit(0));
    CHECK(!view.isHit(32));
    CHECK(!view.isHit(63));
}

void testSimultaneousTriggerSuppression()
{
    lps::SequencerEngine engine;
    TestPlayer player1;
    TestPlayer player2;
    TestTransport transport1;
    TestTransport transport2;
    (void) registerAndConnect(engine, player1, transport1);
    (void) registerAndConnect(engine, player2, transport2);
    engine.setSuppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }, true);

    lps::TimelineBlock block;
    block.ppqStart = 2.0;
    block.ppqEnd = 2.5;
    block.playing = true;
    engine.run(block);

    CHECK(transport1.sendCount == 1);
    CHECK(transport2.sendCount == 0);
    CHECK(engine.suppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }));
    CHECK(!engine.suppression(lps::PlayerId { 1 }, lps::PlayerId { 0 }));
}

void testNonSimultaneousTriggersAreNotSuppressed()
{
    lps::SequencerEngine engine;
    TestPlayer player1;
    TestPlayer player2;
    TestTransport transport1;
    TestTransport transport2;
    player2.eventOffset = 0.30;
    (void) registerAndConnect(engine, player1, transport1);
    (void) registerAndConnect(engine, player2, transport2);
    engine.setSuppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }, true);
    engine.run({});

    CHECK(transport1.sendCount == 1);
    CHECK(transport2.sendCount == 1);
}

void testTriggerOffIsNeverSuppressed()
{
    lps::SequencerEngine engine;
    TestPlayer player1;
    TestPlayer player2;
    TestTransport transport1;
    TestTransport transport2;
    player2.eventType = lps::SemanticEventType::triggerEnd;
    (void) registerAndConnect(engine, player1, transport1);
    (void) registerAndConnect(engine, player2, transport2);
    engine.setSuppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }, true);
    engine.run({});

    CHECK(transport2.sendCount == 1);
    CHECK(transport2.lastEvent.type == lps::SemanticEventType::triggerEnd);
}

void testPlayerMuteDefaultsAndBounds()
{
    lps::SequencerEngine engine;
    TestPlayer player;
    TestTransport transport;

    CHECK(!engine.playerMuted(lps::PlayerId { 0 }));
    engine.setPlayerMuted(lps::PlayerId { 0 }, true);
    CHECK(!engine.playerMuted(lps::PlayerId { 0 }));

    (void) registerAndConnect(engine, player, transport);
    CHECK(!engine.playerMuted(lps::PlayerId { 0 }));
    CHECK(!engine.playerMuted(lps::PlayerId { 1 }));

    engine.setPlayerMuted(lps::PlayerId { 1 }, true);
    CHECK(!engine.playerMuted(lps::PlayerId { 0 }));
    CHECK(!engine.playerMuted(lps::PlayerId { 1 }));

    engine.setPlayerMuted(lps::PlayerId { 0 }, true);
    CHECK(engine.playerMuted(lps::PlayerId { 0 }));
    engine.setPlayerMuted(lps::PlayerId { 0 }, false);
    CHECK(!engine.playerMuted(lps::PlayerId { 0 }));
}

void testMutedPlayerStillRoutesTriggerOff()
{
    lps::SequencerEngine engine;
    TestPlayer player;
    TestTransport transport;
    player.eventType = lps::SemanticEventType::triggerEnd;
    (void) registerAndConnect(engine, player, transport);
    engine.setPlayerMuted(lps::PlayerId { 0 }, true);

    engine.run({});

    CHECK(player.processCount == 1);
    CHECK(transport.sendCount == 1);
    CHECK(transport.lastEvent.type == lps::SemanticEventType::triggerEnd);
}

void testMutedPlayerStillParticipatesInSuppression()
{
    lps::SequencerEngine engine;
    TestPlayer mutedSuppressor;
    TestPlayer suppressedPlayer;
    TestTransport mutedTransport;
    TestTransport suppressedTransport;
    (void) registerAndConnect(engine, mutedSuppressor, mutedTransport);
    (void) registerAndConnect(engine, suppressedPlayer, suppressedTransport);
    engine.setPlayerMuted(lps::PlayerId { 0 }, true);
    engine.setSuppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }, true);

    engine.run({});

    CHECK(mutedSuppressor.processCount == 1);
    CHECK(suppressedPlayer.processCount == 1);
    CHECK(mutedTransport.sendCount == 0);
    CHECK(suppressedTransport.sendCount == 0);
}

void testMutedPlayerContinuesAndUnmutePreservesPhase()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library };
    CollectingTransport transport;
    lps::SequencerEngine engine;
    player.selectPattern(entryAt(library, 1).id);
    (void) registerAndConnect(engine, player, transport);
    engine.prepare({ 48'000.0, 512 });
    engine.setPlayerMuted(lps::PlayerId { 0 }, true);

    engine.run(playingBlock(0.0, 0.1, true));
    engine.run(playingBlock(0.1, 0.3));
    engine.run(playingBlock(0.3, 0.35));

    CHECK(player.patternPlaybackSnapshot().playing);
    CHECK(player.patternPlaybackSnapshot().currentStep == 1);
    CHECK(triggerStartPositions(transport).empty());

    transport.events.clear();
    engine.setPlayerMuted(lps::PlayerId { 0 }, false);
    engine.run(playingBlock(0.35, 0.55));

    CHECK(!engine.playerMuted(lps::PlayerId { 0 }));
    checkPositions(triggerStartPositions(transport), {0.5});
}

void testEnginePlayerCountIsDefinedByCaller()
{
    constexpr std::size_t count = 20;
    lps::SequencerEngine engine;
    std::vector<std::unique_ptr<TestPlayer>> players;
    std::vector<std::unique_ptr<TestTransport>> transports;
    players.reserve(count);
    transports.reserve(count);

    for (std::size_t index = 0; index < count; ++index)
    {
        players.push_back(std::make_unique<TestPlayer>());
        transports.push_back(std::make_unique<TestTransport>());
        (void) registerAndConnect(engine, *players.back(), *transports.back());
    }

    CHECK(engine.playerCount() == count);
    engine.setSuppression(lps::PlayerId { 18 }, lps::PlayerId { 19 }, true);
    engine.run({});

    for (std::size_t index = 0; index < count - 1; ++index)
        CHECK(transports[index]->sendCount == 1);
    CHECK(transports.back()->sendCount == 0);
}

void testFixedNotePlayersUseRegularRoutingAndSuppression()
{
    const lps::PatternLibrary library;
    lps::SequencerEngine engine;
    lps::PatternPlayer bassDrum { library };
    lps::PatternPlayer snare { library };
    bassDrum.setPlaybackSpeed(0);
    snare.setPlaybackSpeed(2);
    CollectingTransport sharedTransport;
    (void) registerAndConnect(
        engine, bassDrum, sharedTransport,
        lps::RouteMapping::fixedPitch(36.0f));
    (void) registerAndConnect(
        engine, snare, sharedTransport,
        lps::RouteMapping::fixedPitch(39.0f));

    lps::TimelineBlock block;
    block.ppqStart = 2.1;
    block.ppqEnd = 2.15;
    block.playing = true;
    block.transportDiscontinuity = true;
    engine.run(block);

    CHECK(sharedTransport.events.size() == 2);
    CHECK(sharedTransport.events[0].type == lps::SemanticEventType::triggerStart);
    CHECK(sharedTransport.events[0].pitchSemitones == 36.0f);
    CHECK(sharedTransport.events[0].ppqPosition == block.ppqStart);
    CHECK(sharedTransport.events[1].type == lps::SemanticEventType::triggerStart);
    CHECK(sharedTransport.events[1].pitchSemitones == 39.0f);
    CHECK(sharedTransport.events[1].ppqPosition == block.ppqStart);
    CHECK(bassDrum.patternPlaybackSnapshot().currentStep == 0);
    CHECK(snare.patternPlaybackSnapshot().currentStep == 0);

    engine.reset();
    sharedTransport.events.clear();
    engine.setSuppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }, true);
    engine.run(block);

    CHECK(engine.suppression(lps::PlayerId { 0 }, lps::PlayerId { 1 }));
    CHECK(sharedTransport.events.size() == 1);
    CHECK(sharedTransport.events[0].pitchSemitones == 36.0f);
}

void testSequenceResetUsesTheNextMasterBoundaryOnlyOnce()
{
    const lps::PatternLibrary library;
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library };
    lps::PatternPlayer target { library };
    target.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, target, targetTransport);
    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    engine.prepare({ 48'000.0, 512 });

    CHECK(engine.masterPlayerId() == lps::PlayerId { 0 });
    engine.run(playingBlock(0.0, 0.3, true));
    masterTransport.events.clear();
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(lps::PlayerId { 1 }));
    CHECK(engine.resetToMasterPending(lps::PlayerId { 1 }));
    engine.run(playingBlock(0.3, 1.1));

    // The target first finishes the part of its old three-step cycle that is
    // before PPQ 1.0, then its first step is aligned with the master's next
    // four-step cycle. The event at 0.75 must not be discarded.
    checkPositions(triggerStartPositions(targetTransport), {0.75, 1.0});
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 1 }));

    targetTransport.events.clear();
    engine.run(playingBlock(1.1, 2.1));

    // A reset is a one-shot operation. The target keeps its own three-step
    // cycle after alignment instead of restarting at every master boundary.
    checkPositions(triggerStartPositions(targetTransport), {1.75});
}

void testPatternSelectionWaitsForTheNextMasterBoundary()
{
    const lps::PatternLibrary library;
    const auto& allSteps = entryAt(library, 1);
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library };
    lps::PatternPlayer target { library };
    target.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, target, targetTransport);
    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    engine.prepare({ 48'000.0, 512 });

    engine.run(playingBlock(0.0, 0.3, true));
    targetTransport.events.clear();

    target.selectPattern(allSteps.id);
    CHECK(target.selectedPatternId() == allSteps.id);
    CHECK(target.activePatternId() == threeStepPulse.id);

    // The target's own three-step loop restarts at PPQ 0.75, but its pending
    // selection must remain on the old pattern until the four-step master's
    // next loop start at PPQ 1.0.
    engine.run(playingBlock(0.3, 0.8));
    CHECK(target.activePatternId() == threeStepPulse.id);
    checkPositions(triggerStartPositions(targetTransport), {0.75});

    targetTransport.events.clear();
    engine.run(playingBlock(0.8, 1.0));
    CHECK(target.activePatternId() == threeStepPulse.id);
    CHECK(triggerStartPositions(targetTransport).empty());

    // Loop boundaries use half-open blocks, so a boundary exactly at the prior
    // block's end is applied once at the beginning of this block.
    engine.run(playingBlock(1.0, 1.1));

    CHECK(target.activePatternId() == allSteps.id);
    CHECK(target.patternView().playbackStart == 0);
    CHECK(target.patternView().playbackEnd == 3);
    checkPositions(triggerStartPositions(targetTransport), {1.0});
}

void testMasterBoundaryUsesItsActiveWindowAndSpeedWithoutRequiringAHit()
{
    lps::PatternLibrary library;
    const auto& nineStepPulse = entryAt(library, 8);

    lps::Pattern targetPattern;
    targetPattern.length = 5;
    targetPattern.hits[1] = true;
    const auto inserted = library.addOrFind("Test Window Start", targetPattern);
    CHECK(inserted.entry != nullptr);

    lps::SequencerEngine engine;
    lps::PatternPlayer master { library };
    lps::PatternPlayer target { library };
    master.selectPattern(nineStepPulse.id);
    target.selectPattern(inserted.entry->id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, target, targetTransport);
    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    engine.prepare({ 48'000.0, 512 });

    // Master: four steps at 2x = 0.5 PPQ per cycle. Its only hit is outside
    // [2, 5], so the engine cannot infer this boundary from trigger output.
    master.setPlaybackSpeed(2);
    master.setPlaybackWindow(2, 5);
    // Target: three steps at 0.5x = 1.5 PPQ per cycle. Only its window start
    // is a hit, making a reset-produced trigger unambiguous.
    target.setPlaybackSpeed(0);
    target.setPlaybackWindow(1, 3);

    engine.run(playingBlock(0.0, 0.1, true));
    CHECK(triggerStartPositions(masterTransport).empty());
    checkPositions(triggerStartPositions(targetTransport), {0.0});
    masterTransport.events.clear();
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(lps::PlayerId { 1 }));
    engine.run(playingBlock(0.1, 0.6));

    CHECK(triggerStartPositions(masterTransport).empty());
    checkPositions(triggerStartPositions(targetTransport), {0.5});
    CHECK(target.activePatternId() == inserted.entry->id);
    CHECK(target.playbackSpeed() == 0);
    CHECK(target.requestedPlaybackStart() == 1);
    CHECK(target.requestedPlaybackEnd() == 3);

    targetTransport.events.clear();
    engine.run(playingBlock(0.6, 1.6));
    // Master boundaries at 1.0 and 1.5 must not continuously reset target.
    CHECK(triggerStartPositions(targetTransport).empty());
}

void testSequenceResetOnlyAffectsTheRequestedNonMasterPlayer()
{
    const lps::PatternLibrary library;
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library };
    lps::PatternPlayer requestedTarget { library };
    lps::PatternPlayer otherTarget { library };
    requestedTarget.selectPattern(threeStepPulse.id);
    otherTarget.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport requestedTransport;
    CollectingTransport otherTransport;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, requestedTarget, requestedTransport);
    (void) registerAndConnect(engine, otherTarget, otherTransport);
    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    engine.prepare({ 48'000.0, 512 });

    engine.run(playingBlock(0.0, 0.3, true));
    requestedTransport.events.clear();
    otherTransport.events.clear();

    // The master and invalid indices reject reset requests defensively.
    CHECK(!engine.requestResetToMaster(lps::PlayerId { 0 }));
    CHECK(!engine.requestResetToMaster(lps::PlayerId { 99 }));
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 0 }));
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 99 }));

    CHECK(engine.requestResetToMaster(lps::PlayerId { 1 }));
    CHECK(engine.resetToMasterPending(lps::PlayerId { 1 }));
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 2 }));
    engine.run(playingBlock(0.3, 1.1));

    checkPositions(triggerStartPositions(requestedTransport), {0.75, 1.0});
    checkPositions(triggerStartPositions(otherTransport), {0.75});
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 1 }));
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 2 }));
}

void testSequenceResetQueuedWhileStoppedIsConsumedAtTransportStart()
{
    const lps::PatternLibrary library;
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library };
    lps::PatternPlayer target { library };
    target.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, target, targetTransport);
    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    engine.prepare({ 48'000.0, 512 });

    engine.run(playingBlock(0.0, 0.4, true));

    lps::TimelineBlock stopped;
    stopped.ppqStart = 0.4;
    stopped.ppqEnd = 0.4;
    stopped.transportDiscontinuity = true;
    engine.run(stopped);
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(lps::PlayerId { 1 }));
    CHECK(engine.resetToMasterPending(lps::PlayerId { 1 }));

    constexpr double resumedAt = 6.125;
    engine.run(playingBlock(resumedAt, resumedAt + 0.1, true));
    checkPositions(triggerStartPositions(targetTransport), {resumedAt});
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 1 }));

    targetTransport.events.clear();
    engine.run(playingBlock(resumedAt + 0.1, resumedAt + 1.075));
    // The target's own next three-step boundary is 0.75 PPQ after restart.
    // It must not also restart at the master's boundary one PPQ later.
    checkPositions(triggerStartPositions(targetTransport), {resumedAt + 0.75});
}

void testSequenceResetTurnsOffAHeldTriggerBeforeRestartingIt()
{
    const lps::PatternLibrary library;
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library };
    lps::PatternPlayer target { library };
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    (void) registerAndConnect(engine, master, masterTransport);
    (void) registerAndConnect(engine, target, targetTransport);
    CHECK(engine.setMasterPlayer(lps::PlayerId { 0 }));
    engine.prepare({ 48'000.0, 512 });

    // A one-step master at 2x cycles every 0.125 PPQ. The target's initial
    // normal-speed trigger is held until exactly that next master boundary.
    master.setPlaybackSpeed(2);
    master.setPlaybackWindow(0, 0);
    engine.run(playingBlock(0.0, 0.05, true));
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(lps::PlayerId { 1 }));
    engine.run(playingBlock(0.05, 0.2));

    CHECK(targetTransport.events.size() == 2);
    CHECK(targetTransport.events[0].type == lps::SemanticEventType::triggerEnd);
    CHECK(close(targetTransport.events[0].ppqPosition, 0.125));
    CHECK(targetTransport.events[1].type == lps::SemanticEventType::triggerStart);
    CHECK(close(targetTransport.events[1].ppqPosition, 0.125));
    CHECK(!engine.resetToMasterPending(lps::PlayerId { 1 }));
}

void testRoutesAreGloballyFrameOrderedWithDeterministicSameFramePriority()
{
    lps::SequencerEngine engine;
    EventListPlayer laterPlayer;
    EventListPlayer earlierPlayer;
    EventListPlayer controlPlayer;
    CollectingTransport transport;

    laterPlayer.events = {
        lps::SequencerEvent::triggerStart(0.010, { 1 }, 1.0f),
        lps::SequencerEvent::triggerEnd(0.015, { 1 })
    };
    earlierPlayer.events = {
        lps::SequencerEvent::triggerEnd(0.005, { 2 }),
        lps::SequencerEvent::triggerStart(0.015, { 3 }, 1.0f)
    };
    controlPlayer.events = {
        lps::SequencerEvent::controlPoint(0.015, 7, 0.5f)
    };

    (void) registerAndConnect(
        engine, laterPlayer, transport,
        lps::RouteMapping::fixedPitch(36.0f));
    (void) registerAndConnect(
        engine, earlierPlayer, transport,
        lps::RouteMapping::fixedPitch(39.0f));
    (void) registerAndConnect(engine, controlPlayer, transport);
    engine.prepare({ 48'000.0, 512 });

    lps::TimelineBlock block;
    block.ppqEnd = 0.02;
    block.tempoBpm = 120.0;
    block.sampleRate = 48'000.0;
    block.sampleCount = 512;
    block.playing = true;
    engine.run(block);

    CHECK(transport.events.size() == 5);
    CHECK(transport.events[0].frameOffset == 120);
    CHECK(transport.events[0].type == lps::SemanticEventType::triggerEnd);
    CHECK(transport.events[1].frameOffset == 240);
    CHECK(transport.events[1].type == lps::SemanticEventType::triggerStart);
    CHECK(transport.events[2].frameOffset == 360);
    CHECK(transport.events[2].type == lps::SemanticEventType::controlPoint);
    CHECK(transport.events[3].type == lps::SemanticEventType::triggerEnd);
    CHECK(transport.events[4].type == lps::SemanticEventType::triggerStart);
    CHECK(transport.events[3].sourcePlayerId == lps::PlayerId { 0 });
    CHECK(transport.events[4].routeId == lps::RouteId { 1 });
}

} // namespace

int main()
{
    testLifecycleAndRouting();
    testRegistrationConnectionsAndTopologyFreeze();
    testEventBufferReportsOverflowUntilCleared();
    testEnginePublishesPlayerOverflowDiagnostics();
    testEngineRejectsNonFiniteAndOutOfOrderEvents();
    testSharedTransportLifecycleRunsOnce();
    testTransportDeliveryFailureResetsOutputAndPlayer();
    testMasterRequiresCycleBoundaryCapability();
    testCyclicNonPatternPlayerCanDriveGenericDirectives();
    testPatternViewRejectsStepsOutsideItsMaskWidth();
    testSimultaneousTriggerSuppression();
    testNonSimultaneousTriggersAreNotSuppressed();
    testTriggerOffIsNeverSuppressed();
    testPlayerMuteDefaultsAndBounds();
    testMutedPlayerStillRoutesTriggerOff();
    testMutedPlayerStillParticipatesInSuppression();
    testMutedPlayerContinuesAndUnmutePreservesPhase();
    testEnginePlayerCountIsDefinedByCaller();
    testFixedNotePlayersUseRegularRoutingAndSuppression();
    testSequenceResetUsesTheNextMasterBoundaryOnlyOnce();
    testPatternSelectionWaitsForTheNextMasterBoundary();
    testMasterBoundaryUsesItsActiveWindowAndSpeedWithoutRequiringAHit();
    testSequenceResetOnlyAffectsTheRequestedNonMasterPlayer();
    testSequenceResetQueuedWhileStoppedIsConsumedAtTransportStart();
    testSequenceResetTurnsOffAHeldTriggerBeforeRestartingIt();
    testRoutesAreGloballyFrameOrderedWithDeterministicSameFramePriority();
    std::cout << "All sequencer engine tests passed.\n";
    return 0;
}
