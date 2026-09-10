#include "core/SequencerEngine.h"
#include "core/PatternLibrary.h"
#include "core/PatternPlayer.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
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
    void prepare(double rate) noexcept override { preparedRate = rate; }
    void reset() noexcept override { wasReset = true; }
    void process(const lps::ClockBlock& block, lps::SequencerEventBuffer& output) noexcept override
    {
        ++processCount;
        output.clear();
        (void) output.push({ block.ppqStart + eventOffset, 2, 64.0f, 0.75f, eventType });
    }
    [[nodiscard]] lps::PatternView get_pattern_view() const noexcept override
    {
        std::uint32_t mask = 0;
        for (std::size_t step = 0; step < pattern.length; ++step)
            if (pattern.hits[step])
                mask |= std::uint32_t { 1 } << step;
        return { static_cast<std::uint16_t>(pattern.length), mask };
    }
    [[nodiscard]] lps::PlaybackSnapshot snapshot() const noexcept override { return { 2, true }; }

    lps::Pattern pattern {};
    double preparedRate = 0.0;
    int processCount = 0;
    bool wasReset = false;
    double eventOffset = 0.25;
    lps::SequencerEventType eventType = lps::SequencerEventType::triggerOn;
};

class TestTransport final : public lps::ITransport
{
public:
    void send(const lps::SequencerEvent& event, const lps::ClockBlock& block) noexcept override
    {
        lastEvent = event;
        lastBlock = block;
        ++sendCount;
    }
    lps::SequencerEvent lastEvent {};
    lps::ClockBlock lastBlock {};
    int sendCount = 0;
};

class CollectingTransport final : public lps::ITransport
{
public:
    void send(const lps::SequencerEvent& event, const lps::ClockBlock&) noexcept override
    {
        events.push_back(event);
    }

    std::vector<lps::SequencerEvent> events;
};

[[nodiscard]] std::vector<double> triggerOnPositions(
    const CollectingTransport& transport)
{
    std::vector<double> positions;
    for (const auto& event : transport.events)
        if (event.type == lps::SequencerEventType::triggerOn)
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

[[nodiscard]] lps::ClockBlock playingBlock(
    double start,
    double end,
    bool discontinuity = false)
{
    lps::ClockBlock block;
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
    player.pattern.length = 4;
    player.pattern.hits[1] = true;
    engine.add_stuff(player, transport);

    engine.prepare(48'000.0);
    CHECK(player.preparedRate == 48'000.0);

    lps::ClockBlock block;
    block.ppqStart = 4.0;
    block.ppqEnd = 4.5;
    block.playing = true;
    engine.run(block);

    CHECK(player.processCount == 1);
    CHECK(transport.sendCount == 1);
    CHECK(transport.lastEvent.ppqPosition == 4.25);
    CHECK(transport.lastEvent.output == 2);
    CHECK(transport.lastEvent.pitchSemitones == 64.0f);
    CHECK(transport.lastEvent.type == lps::SequencerEventType::triggerOn);
    CHECK(transport.lastBlock.ppqStart == block.ppqStart);
    CHECK(engine.pattern().isHit(1));
    CHECK(engine.snapshot().currentStep == 2);

    engine.reset();
    CHECK(player.wasReset);
}

void testSimultaneousTriggerSuppression()
{
    lps::SequencerEngine engine;
    TestPlayer player1;
    TestPlayer player2;
    TestTransport transport1;
    TestTransport transport2;
    engine.add_stuff(player1, transport1);
    engine.add_stuff(player2, transport2);
    engine.setSuppression(0, 1, true);

    lps::ClockBlock block;
    block.ppqStart = 2.0;
    block.ppqEnd = 2.5;
    block.playing = true;
    engine.run(block);

    CHECK(transport1.sendCount == 1);
    CHECK(transport2.sendCount == 0);
    CHECK(engine.suppression(0, 1));
    CHECK(!engine.suppression(1, 0));
}

void testNonSimultaneousTriggersAreNotSuppressed()
{
    lps::SequencerEngine engine;
    TestPlayer player1;
    TestPlayer player2;
    TestTransport transport1;
    TestTransport transport2;
    player2.eventOffset = 0.30;
    engine.add_stuff(player1, transport1);
    engine.add_stuff(player2, transport2);
    engine.setSuppression(0, 1, true);
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
    player2.eventType = lps::SequencerEventType::triggerOff;
    engine.add_stuff(player1, transport1);
    engine.add_stuff(player2, transport2);
    engine.setSuppression(0, 1, true);
    engine.run({});

    CHECK(transport2.sendCount == 1);
    CHECK(transport2.lastEvent.type == lps::SequencerEventType::triggerOff);
}

void testPlayerMuteDefaultsAndBounds()
{
    lps::SequencerEngine engine;
    TestPlayer player;
    TestTransport transport;

    CHECK(!engine.playerMuted(0));
    engine.setPlayerMuted(0, true);
    CHECK(!engine.playerMuted(0));

    engine.add_stuff(player, transport);
    CHECK(!engine.playerMuted(0));
    CHECK(!engine.playerMuted(1));

    engine.setPlayerMuted(1, true);
    CHECK(!engine.playerMuted(0));
    CHECK(!engine.playerMuted(1));

    engine.setPlayerMuted(0, true);
    CHECK(engine.playerMuted(0));
    engine.setPlayerMuted(0, false);
    CHECK(!engine.playerMuted(0));
}

void testMutedPlayerStillRoutesTriggerOff()
{
    lps::SequencerEngine engine;
    TestPlayer player;
    TestTransport transport;
    player.eventType = lps::SequencerEventType::triggerOff;
    engine.add_stuff(player, transport);
    engine.setPlayerMuted(0, true);

    engine.run({});

    CHECK(player.processCount == 1);
    CHECK(transport.sendCount == 1);
    CHECK(transport.lastEvent.type == lps::SequencerEventType::triggerOff);
}

void testMutedPlayerStillParticipatesInSuppression()
{
    lps::SequencerEngine engine;
    TestPlayer mutedSuppressor;
    TestPlayer suppressedPlayer;
    TestTransport mutedTransport;
    TestTransport suppressedTransport;
    engine.add_stuff(mutedSuppressor, mutedTransport);
    engine.add_stuff(suppressedPlayer, suppressedTransport);
    engine.setPlayerMuted(0, true);
    engine.setSuppression(0, 1, true);

    engine.run({});

    CHECK(mutedSuppressor.processCount == 1);
    CHECK(suppressedPlayer.processCount == 1);
    CHECK(mutedTransport.sendCount == 0);
    CHECK(suppressedTransport.sendCount == 0);
}

void testMutedPlayerContinuesAndUnmutePreservesPhase()
{
    const lps::PatternLibrary library;
    lps::PatternPlayer player { library, 36 };
    CollectingTransport transport;
    lps::SequencerEngine engine;
    player.selectPattern(entryAt(library, 1).id);
    engine.add_stuff(player, transport);
    engine.prepare(48'000.0);
    engine.setPlayerMuted(0, true);

    engine.run(playingBlock(0.0, 0.1, true));
    engine.run(playingBlock(0.1, 0.3));
    engine.run(playingBlock(0.3, 0.35));

    CHECK(engine.snapshot(0).playing);
    CHECK(engine.snapshot(0).currentStep == 1);
    CHECK(triggerOnPositions(transport).empty());

    transport.events.clear();
    engine.setPlayerMuted(0, false);
    engine.run(playingBlock(0.35, 0.55));

    CHECK(!engine.playerMuted(0));
    checkPositions(triggerOnPositions(transport), {0.5});
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
        engine.add_stuff(*players.back(), *transports.back());
    }

    CHECK(engine.playerCount() == count);
    engine.setSuppression(18, 19, true);
    engine.run({});

    for (std::size_t index = 0; index < count - 1; ++index)
        CHECK(transports[index]->sendCount == 1);
    CHECK(transports.back()->sendCount == 0);
}

void testFixedNotePlayersUseRegularRoutingAndSuppression()
{
    const lps::PatternLibrary library;
    lps::SequencerEngine engine;
    lps::PatternPlayer bassDrum { library, 36 };
    lps::PatternPlayer snare { library, 39 };
    bassDrum.setPlaybackSpeed(0);
    snare.setPlaybackSpeed(2);
    CollectingTransport sharedTransport;
    engine.add_stuff(bassDrum, sharedTransport);
    engine.add_stuff(snare, sharedTransport);

    lps::ClockBlock block;
    block.ppqStart = 2.1;
    block.ppqEnd = 2.15;
    block.playing = true;
    block.transportDiscontinuity = true;
    engine.run(block);

    CHECK(sharedTransport.events.size() == 2);
    CHECK(sharedTransport.events[0].type == lps::SequencerEventType::triggerOn);
    CHECK(sharedTransport.events[0].pitchSemitones == 36.0f);
    CHECK(sharedTransport.events[0].ppqPosition == block.ppqStart);
    CHECK(sharedTransport.events[1].type == lps::SequencerEventType::triggerOn);
    CHECK(sharedTransport.events[1].pitchSemitones == 39.0f);
    CHECK(sharedTransport.events[1].ppqPosition == block.ppqStart);
    CHECK(engine.snapshot(0).currentStep == 0);
    CHECK(engine.snapshot(1).currentStep == 0);

    engine.reset();
    sharedTransport.events.clear();
    engine.setSuppression(0, 1, true);
    engine.run(block);

    CHECK(engine.suppression(0, 1));
    CHECK(sharedTransport.events.size() == 1);
    CHECK(sharedTransport.events[0].pitchSemitones == 36.0f);
}

void testSequenceResetUsesTheNextMasterBoundaryOnlyOnce()
{
    const lps::PatternLibrary library;
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library, 36 };
    lps::PatternPlayer target { library, 39 };
    target.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    engine.add_stuff(master, masterTransport);
    engine.add_stuff(target, targetTransport);
    CHECK(engine.setMasterPlayer(0));
    engine.prepare(48'000.0);

    CHECK(engine.masterPlayerIndex() == 0);
    engine.run(playingBlock(0.0, 0.3, true));
    masterTransport.events.clear();
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(1));
    CHECK(engine.resetToMasterPending(1));
    engine.run(playingBlock(0.3, 1.1));

    // The target first finishes the part of its old three-step cycle that is
    // before PPQ 1.0, then its first step is aligned with the master's next
    // four-step cycle. The event at 0.75 must not be discarded.
    checkPositions(triggerOnPositions(targetTransport), {0.75, 1.0});
    CHECK(!engine.resetToMasterPending(1));

    targetTransport.events.clear();
    engine.run(playingBlock(1.1, 2.1));

    // A reset is a one-shot operation. The target keeps its own three-step
    // cycle after alignment instead of restarting at every master boundary.
    checkPositions(triggerOnPositions(targetTransport), {1.75});
}

void testPatternSelectionWaitsForTheNextMasterBoundary()
{
    const lps::PatternLibrary library;
    const auto& allSteps = entryAt(library, 1);
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library, 36 };
    lps::PatternPlayer target { library, 39 };
    target.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    engine.add_stuff(master, masterTransport);
    engine.add_stuff(target, targetTransport);
    CHECK(engine.setMasterPlayer(0));
    engine.prepare(48'000.0);

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
    checkPositions(triggerOnPositions(targetTransport), {0.75});

    targetTransport.events.clear();
    engine.run(playingBlock(0.8, 1.0));
    CHECK(target.activePatternId() == threeStepPulse.id);
    CHECK(triggerOnPositions(targetTransport).empty());

    // Loop boundaries use half-open blocks, so a boundary exactly at the prior
    // block's end is applied once at the beginning of this block.
    engine.run(playingBlock(1.0, 1.1));

    CHECK(target.activePatternId() == allSteps.id);
    CHECK(target.get_pattern_view().playbackStart == 0);
    CHECK(target.get_pattern_view().playbackEnd == 3);
    checkPositions(triggerOnPositions(targetTransport), {1.0});
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
    lps::PatternPlayer master { library, 36 };
    lps::PatternPlayer target { library, 39 };
    master.selectPattern(nineStepPulse.id);
    target.selectPattern(inserted.entry->id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    engine.add_stuff(master, masterTransport);
    engine.add_stuff(target, targetTransport);
    CHECK(engine.setMasterPlayer(0));
    engine.prepare(48'000.0);

    // Master: four steps at 2x = 0.5 PPQ per cycle. Its only hit is outside
    // [2, 5], so the engine cannot infer this boundary from trigger output.
    master.setPlaybackSpeed(2);
    master.setPlaybackWindow(2, 5);
    // Target: three steps at 0.5x = 1.5 PPQ per cycle. Only its window start
    // is a hit, making a reset-produced trigger unambiguous.
    target.setPlaybackSpeed(0);
    target.setPlaybackWindow(1, 3);

    engine.run(playingBlock(0.0, 0.1, true));
    CHECK(triggerOnPositions(masterTransport).empty());
    checkPositions(triggerOnPositions(targetTransport), {0.0});
    masterTransport.events.clear();
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(1));
    engine.run(playingBlock(0.1, 0.6));

    CHECK(triggerOnPositions(masterTransport).empty());
    checkPositions(triggerOnPositions(targetTransport), {0.5});
    CHECK(target.activePatternId() == inserted.entry->id);
    CHECK(target.playbackSpeed() == 0);
    CHECK(target.requestedPlaybackStart() == 1);
    CHECK(target.requestedPlaybackEnd() == 3);

    targetTransport.events.clear();
    engine.run(playingBlock(0.6, 1.6));
    // Master boundaries at 1.0 and 1.5 must not continuously reset target.
    CHECK(triggerOnPositions(targetTransport).empty());
}

void testSequenceResetOnlyAffectsTheRequestedNonMasterPlayer()
{
    const lps::PatternLibrary library;
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library, 36 };
    lps::PatternPlayer requestedTarget { library, 39 };
    lps::PatternPlayer otherTarget { library, 40 };
    requestedTarget.selectPattern(threeStepPulse.id);
    otherTarget.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport requestedTransport;
    CollectingTransport otherTransport;
    engine.add_stuff(master, masterTransport);
    engine.add_stuff(requestedTarget, requestedTransport);
    engine.add_stuff(otherTarget, otherTransport);
    CHECK(engine.setMasterPlayer(0));
    engine.prepare(48'000.0);

    engine.run(playingBlock(0.0, 0.3, true));
    requestedTransport.events.clear();
    otherTransport.events.clear();

    // The master and invalid indices reject reset requests defensively.
    CHECK(!engine.requestResetToMaster(0));
    CHECK(!engine.requestResetToMaster(99));
    CHECK(!engine.resetToMasterPending(0));
    CHECK(!engine.resetToMasterPending(99));

    CHECK(engine.requestResetToMaster(1));
    CHECK(engine.resetToMasterPending(1));
    CHECK(!engine.resetToMasterPending(2));
    engine.run(playingBlock(0.3, 1.1));

    checkPositions(triggerOnPositions(requestedTransport), {0.75, 1.0});
    checkPositions(triggerOnPositions(otherTransport), {0.75});
    CHECK(!engine.resetToMasterPending(1));
    CHECK(!engine.resetToMasterPending(2));
}

void testSequenceResetQueuedWhileStoppedIsConsumedAtTransportStart()
{
    const lps::PatternLibrary library;
    const auto& threeStepPulse = entryAt(library, 5);
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library, 36 };
    lps::PatternPlayer target { library, 39 };
    target.selectPattern(threeStepPulse.id);
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    engine.add_stuff(master, masterTransport);
    engine.add_stuff(target, targetTransport);
    CHECK(engine.setMasterPlayer(0));
    engine.prepare(48'000.0);

    engine.run(playingBlock(0.0, 0.4, true));

    lps::ClockBlock stopped;
    stopped.ppqStart = 0.4;
    stopped.ppqEnd = 0.4;
    stopped.transportDiscontinuity = true;
    engine.run(stopped);
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(1));
    CHECK(engine.resetToMasterPending(1));

    constexpr double resumedAt = 6.125;
    engine.run(playingBlock(resumedAt, resumedAt + 0.1, true));
    checkPositions(triggerOnPositions(targetTransport), {resumedAt});
    CHECK(!engine.resetToMasterPending(1));

    targetTransport.events.clear();
    engine.run(playingBlock(resumedAt + 0.1, resumedAt + 1.075));
    // The target's own next three-step boundary is 0.75 PPQ after restart.
    // It must not also restart at the master's boundary one PPQ later.
    checkPositions(triggerOnPositions(targetTransport), {resumedAt + 0.75});
}

void testSequenceResetTurnsOffAHeldTriggerBeforeRestartingIt()
{
    const lps::PatternLibrary library;
    lps::SequencerEngine engine;
    lps::PatternPlayer master { library, 36 };
    lps::PatternPlayer target { library, 39 };
    CollectingTransport masterTransport;
    CollectingTransport targetTransport;
    engine.add_stuff(master, masterTransport);
    engine.add_stuff(target, targetTransport);
    CHECK(engine.setMasterPlayer(0));
    engine.prepare(48'000.0);

    // A one-step master at 2x cycles every 0.125 PPQ. The target's initial
    // normal-speed trigger is held until exactly that next master boundary.
    master.setPlaybackSpeed(2);
    master.setPlaybackWindow(0, 0);
    engine.run(playingBlock(0.0, 0.05, true));
    targetTransport.events.clear();

    CHECK(engine.requestResetToMaster(1));
    engine.run(playingBlock(0.05, 0.2));

    CHECK(targetTransport.events.size() == 2);
    CHECK(targetTransport.events[0].type == lps::SequencerEventType::triggerOff);
    CHECK(close(targetTransport.events[0].ppqPosition, 0.125));
    CHECK(targetTransport.events[1].type == lps::SequencerEventType::triggerOn);
    CHECK(close(targetTransport.events[1].ppqPosition, 0.125));
    CHECK(!engine.resetToMasterPending(1));
}

} // namespace

int main()
{
    testLifecycleAndRouting();
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
    std::cout << "All sequencer engine tests passed.\n";
    return 0;
}
