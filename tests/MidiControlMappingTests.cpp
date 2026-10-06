#include "core/MidiControlMapping.h"

#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace
{
[[noreturn]] void failTest(const char* expression, const char* file, int line)
{
    std::cerr << file << ':' << line << ": test assertion failed: "
              << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define CHECK(condition) \
    ((condition) ? static_cast<void>(0) : failTest(#condition, __FILE__, __LINE__))

lps::RuntimeMidiMappingTable oneMapping()
{
    lps::RuntimeMidiMappingTable table;
    auto& mapping = table.mappings[0];
    mapping.sourceType = lps::MidiControlMessageType::controlChange;
    mapping.channel = 15;
    mapping.control = 20;
    mapping.valueMode = lps::MidiValueMode::incDec;
    mapping.routes[0] = {
        lps::MidiTargetHandle {3},
        lps::MidiRouteOperation::increment,
        1
    };
    mapping.routeCount = 1;
    table.mappingCount = 1;
    return table;
}

void testIncDecDecoderPreservesRawInput()
{
    lps::DecodedMidiControlEvent decoded;
    lps::MidiControlEvent event;
    event.type = lps::MidiControlMessageType::controlChange;
    event.channel = 15;
    event.control = 20;
    event.samplePosition = 41;
    event.eventOrder = 7;

    event.rawValue = 1;
    CHECK(lps::decodeMidiControlEvent(
        event, lps::MidiValueMode::incDec, decoded));
    CHECK(decoded.logicalMovement == 1);
    CHECK(decoded.source.rawValue == 1);
    CHECK(decoded.source.samplePosition == 41);
    CHECK(decoded.source.eventOrder == 7);

    event.rawValue = 127;
    CHECK(lps::decodeMidiControlEvent(
        event, lps::MidiValueMode::incDec, decoded));
    CHECK(decoded.logicalMovement == -1);
    CHECK(decoded.source.rawValue == 127);

    event.rawValue = 64;
    CHECK(!lps::decodeMidiControlEvent(
        event, lps::MidiValueMode::incDec, decoded));
}

void testResolverMatchesWireIdentityAndConsumesIgnoredValues()
{
    const auto table = oneMapping();
    const lps::MidiMappingContext context;
    lps::MidiControlEvent event;
    event.type = lps::MidiControlMessageType::controlChange;
    event.channel = 15;
    event.control = 20;
    event.rawValue = 1;

    auto result = lps::resolveMidiControlEvent(event, context, table);
    CHECK(result.consumed);
    CHECK(result.targetCommands.count == 1);
    CHECK(result.targetCommands.commands[0].target.value == 3);
    CHECK(result.targetCommands.commands[0].value == 1);

    event.rawValue = 2;
    result = lps::resolveMidiControlEvent(event, context, table);
    CHECK(result.consumed);
    CHECK(result.targetCommands.count == 0);

    event.rawValue = 127;
    result = lps::resolveMidiControlEvent(event, context, table);
    CHECK(result.targetCommands.count == 1);
    CHECK(result.targetCommands.commands[0].value == -1);

    event.channel = 14;
    CHECK(!lps::resolveMidiControlEvent(event, context, table).consumed);
    event.channel = 15;
    event.control = 21;
    CHECK(!lps::resolveMidiControlEvent(event, context, table).consumed);
    event.control = 20;
    event.type = lps::MidiControlMessageType::noteOn;
    CHECK(!lps::resolveMidiControlEvent(event, context, table).consumed);
}

void testRuntimeTypesSupportStructuredConditionsAndFanOut()
{
    auto table = oneMapping();
    auto& mapping = table.mappings[0];
    mapping.conditions.values[0] = {
        lps::MidiConditionKind::modifier, 2, 1 };
    mapping.conditions.values[1] = {
        lps::MidiConditionKind::bank, 0, 4 };
    mapping.conditions.count = 2;
    mapping.routes[1] = {
        lps::MidiTargetHandle {4},
        lps::MidiRouteOperation::increment,
        -2
    };
    mapping.routeCount = 2;

    lps::MidiControlEvent event;
    event.type = lps::MidiControlMessageType::controlChange;
    event.channel = 15;
    event.control = 20;
    event.rawValue = 1;
    lps::MidiMappingContext context;
    CHECK(!lps::resolveMidiControlEvent(event, context, table).consumed);

    context.modifiers[2] = true;
    context.bank = 4;
    const auto result = lps::resolveMidiControlEvent(event, context, table);
    CHECK(result.consumed);
    CHECK(result.targetCommands.count == 2);
    CHECK(result.targetCommands.commands[0].value == 1);
    CHECK(result.targetCommands.commands[1].value == -2);
}

void testMailboxCoalescesAndSaturatesWithoutWrapping()
{
    lps::MidiTargetMailbox mailbox;
    const lps::MidiTargetHandle target {2};
    for (int index = 0; index < 1000; ++index)
        mailbox.accumulate(target, 127);
    CHECK(mailbox.take(target) == lps::maximumPendingMidiIncrement);
    CHECK(mailbox.take(target) == 0);

    mailbox.accumulate(target, 100);
    mailbox.accumulate(target, -40);
    CHECK(mailbox.take(target) == 60);

    for (int index = 0; index < 1000; ++index)
        mailbox.accumulate(target, -127);
    CHECK(mailbox.take(target) == -lps::maximumPendingMidiIncrement);
}

void testDirectSourceIndexAndPageRelativeResolution()
{
    lps::RuntimeMidiMappingTable table;
    for (std::size_t index = 0; index < 1000; ++index)
    {
        auto& mapping = table.mappings[index];
        mapping.channel = static_cast<std::uint8_t>(index / 128 + 1);
        mapping.control = static_cast<std::uint8_t>(index % 128);
        mapping.routes[0].destination =
            lps::MidiRouteDestination::controlSlot;
        mapping.routes[0].controlSlot = 4;
        mapping.routeCount = 1;
    }
    table.mappingCount = 1000;
    lps::compileMidiSourceIndex(table);

    for (std::uint8_t channel = 1; channel <= 16; ++channel)
        for (std::uint8_t control = 0; control < 128; ++control)
            CHECK(lps::midiCcSourceIndex(channel, control)
                == (static_cast<std::size_t>(channel) - 1) * 128 + control);

    lps::MidiPageBindingSnapshot page;
    page.valid = true;
    page.controlTargets[4] = {73};
    lps::MidiControlEvent event;
    event.type = lps::MidiControlMessageType::controlChange;
    event.channel = 7;
    event.control = 99;
    event.rawValue = 1;
    const auto result = lps::resolveMidiControlEvent(
        event, {}, table, page);
    CHECK(result.consumed);
    CHECK(result.mappingsInspected == 1);
    CHECK(result.targetCommands.count == 1);
    CHECK(result.targetCommands.commands[0].target.value == 73);

    page.controlTargets[4] = {};
    const auto unbound = lps::resolveMidiControlEvent(
        event, {}, table, page);
    CHECK(unbound.consumed);
    CHECK(unbound.targetCommands.count == 0);
}

void testRuntimeAmbiguityFailsClosed()
{
    auto table = oneMapping();
    table.mappings[1] = table.mappings[0];
    table.mappings[1].routes[0].target = {9};
    table.mappingCount = 2;
    lps::compileMidiSourceIndex(table);
    lps::MidiControlEvent event;
    event.type = lps::MidiControlMessageType::controlChange;
    event.channel = 15;
    event.control = 20;
    event.rawValue = 1;
    const auto result = lps::resolveMidiControlEvent(event, {}, table);
    CHECK(result.consumed);
    CHECK(result.ambiguous);
    CHECK(result.targetCommands.count == 0);
}

void testDirtyDrainVisitsOnlyChangedTargets()
{
    lps::MidiTargetMailbox mailbox;
    mailbox.accumulate({1}, 2);
    mailbox.accumulate({7000}, -3);
    std::size_t visited = 0;
    int total = 0;
    mailbox.drainDirty([&](lps::MidiTargetHandle target, int movement)
    {
        CHECK(target.value == 1 || target.value == 7000);
        ++visited;
        total += movement;
    });
    CHECK(visited == 2);
    CHECK(total == -1);
    visited = 0;
    mailbox.drainDirty([&](lps::MidiTargetHandle, int) { ++visited; });
    CHECK(visited == 0);
}
}

int main()
{
    static_assert(lps::maximumMidiMappings >= 32);
    static_assert(lps::maximumMidiRoutesPerMapping >= 8);
    static_assert(std::is_trivially_copyable_v<lps::RuntimeMidiMappingTable>);
    testIncDecDecoderPreservesRawInput();
    testResolverMatchesWireIdentityAndConsumesIgnoredValues();
    testRuntimeTypesSupportStructuredConditionsAndFanOut();
    testMailboxCoalescesAndSaturatesWithoutWrapping();
    testDirectSourceIndexAndPageRelativeResolution();
    testRuntimeAmbiguityFailsClosed();
    testDirtyDrainVisitsOnlyChangedTargets();
    return EXIT_SUCCESS;
}
