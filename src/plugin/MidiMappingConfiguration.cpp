#include "plugin/MidiMappingConfiguration.h"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace lps
{
namespace
{
struct CandidateMapping
{
    bool valid = true;
    bool enabled = false;
    bool idValid = false;
    bool sourceValid = false;
    std::string id;
    juce::String displayId;
    RuntimeMidiMapping runtime;
};

bool readInteger(const juce::var& value, int& result) noexcept
{
    if (!value.isInt() && !value.isInt64())
        return false;
    const auto number = static_cast<juce::int64>(value);
    if (number < std::numeric_limits<int>::min()
        || number > std::numeric_limits<int>::max())
        return false;
    result = static_cast<int>(number);
    return true;
}

bool sameSource(
    const RuntimeMidiMapping& left,
    const RuntimeMidiMapping& right) noexcept
{
    return left.sourceType == right.sourceType
        && left.channel == right.channel
        && left.control == right.control;
}

void diagnose(
    ParsedMidiMappingConfiguration& result,
    const juce::String& code,
    const CandidateMapping& candidate,
    const juce::String& message,
    const juce::StringArray& related = {})
{
    result.diagnostics.push_back({
        code, MidiDiagnosticSeverity::error,
        candidate.displayId, message, related
    });
}

bool addCondition(
    CandidateMapping& candidate,
    MidiConditionKind kind,
    std::uint8_t key,
    int value) noexcept
{
    auto& conditions = candidate.runtime.conditions;
    for (std::size_t index = 0; index < conditions.count; ++index)
    {
        const auto& existing = conditions.values[index];
        if (existing.kind == kind && existing.key == key)
            return existing.value == value;
    }
    if (conditions.count >= conditions.values.size())
        return false;
    conditions.values[conditions.count++] = {kind, key, value};
    return true;
}

std::optional<std::uint8_t> internModifier(
    ParsedMidiMappingConfiguration& result,
    const juce::String& name)
{
    const auto existing = result.modifierNames.indexOf(name);
    if (existing >= 0)
        return static_cast<std::uint8_t>(existing);
    if (result.modifierNames.size() >= 16)
        return std::nullopt;
    result.modifierNames.add(name);
    return static_cast<std::uint8_t>(result.modifierNames.size() - 1);
}

bool parseVersionTwoConditions(
    const juce::DynamicObject& object,
    CandidateMapping& candidate,
    ParsedMidiMappingConfiguration& result)
{
    const auto& properties = object.getProperties();
    for (int index = 0; index < properties.size(); ++index)
    {
        const auto name = properties.getName(index).toString();
        const auto& value = properties.getValueAt(index);
        if (name == "modifiers")
        {
            const auto* modifiers = value.getDynamicObject();
            if (modifiers == nullptr || modifiers->getProperties().size() > 16)
                return false;
            const auto& modifierProperties = modifiers->getProperties();
            for (int modifier = 0;
                 modifier < modifierProperties.size(); ++modifier)
            {
                const auto modifierName =
                    modifierProperties.getName(modifier).toString();
                const auto& modifierValue =
                    modifierProperties.getValueAt(modifier);
                const auto compact = internModifier(result, modifierName);
                if (!modifierValue.isBool() || !compact
                    || !addCondition(
                        candidate, MidiConditionKind::modifier, *compact,
                        static_cast<bool>(modifierValue) ? 1 : 0))
                    return false;
            }
        }
        else if (name == "notes")
        {
            const auto* notes = value.getArray();
            if (notes == nullptr || notes->size() > 16)
                return false;
            for (const auto& noteValue : *notes)
            {
                const auto* note = noteValue.getDynamicObject();
                int number = -1;
                if (note == nullptr || note->getProperties().size() != 2
                    || !readInteger(note->getProperty("note"), number)
                    || number < 0 || number > 127
                    || !note->getProperty("held").isBool()
                    || !addCondition(
                        candidate, MidiConditionKind::heldNote,
                        static_cast<std::uint8_t>(number),
                        static_cast<bool>(note->getProperty("held")) ? 1 : 0))
                    return false;
            }
        }
        else if (name == "bank")
        {
            int bank = -1;
            if (!readInteger(value, bank) || bank < 0 || bank > 127
                || !addCondition(
                    candidate, MidiConditionKind::bank, 0, bank))
                return false;
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool predicatesOverlap(
    const MidiMappingConditions& left,
    const MidiMappingConditions& right) noexcept
{
    for (std::size_t leftIndex = 0; leftIndex < left.count; ++leftIndex)
    {
        const auto& a = left.values[leftIndex];
        for (std::size_t rightIndex = 0; rightIndex < right.count; ++rightIndex)
        {
            const auto& b = right.values[rightIndex];
            if (a.kind == b.kind && a.key == b.key && a.value != b.value)
                return false;
        }
    }
    return true;
}
}

bool MidiTargetRegistry::registerTarget(
    const juce::String& stableId,
    MidiTargetHandle handle)
{
    if (stableId.isEmpty() || !handle.valid()
        || handle.value >= maximumMidiTargetCount
        || entries_.size() >= maximumMidiTargetCount
        || resolve(stableId).has_value())
        return false;
    entries_.push_back({stableId, handle});
    return true;
}

std::optional<MidiTargetHandle> MidiTargetRegistry::resolve(
    const juce::String& stableId) const noexcept
{
    for (const auto& entry : entries_)
        if (entry.stableId == stableId)
            return entry.handle;
    return std::nullopt;
}

std::optional<std::uint8_t> ParsedMidiMappingConfiguration::modifierIndex(
    const juce::String& name) const noexcept
{
    const auto index = modifierNames.indexOf(name);
    return index >= 0 ? std::optional<std::uint8_t> {
        static_cast<std::uint8_t>(index)} : std::nullopt;
}

juce::Result parseMidiMappingConfiguration(
    const juce::String& jsonText,
    const MidiTargetRegistry& targetRegistry,
    ParsedMidiMappingConfiguration& result)
{
    result = {};
    const auto parsed = juce::JSON::parse(jsonText);
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr)
    {
        result.diagnostics.push_back({
            "malformed-document", MidiDiagnosticSeverity::error, {},
            "Malformed MIDI mapping JSON", {}});
        return juce::Result::fail("Malformed MIDI mapping JSON");
    }

    if (!readInteger(root->getProperty("version"), result.version)
        || (result.version != 1 && result.version != 2))
    {
        result.diagnostics.push_back({
            "unsupported-version", MidiDiagnosticSeverity::error, {},
            "Unsupported MIDI mapping version", {}});
        return juce::Result::fail("Unsupported MIDI mapping version");
    }
    const auto* mappings = root->getProperty("mappings").getArray();
    if (mappings == nullptr)
    {
        result.diagnostics.push_back({
            "invalid-field-or-range", MidiDiagnosticSeverity::error, {},
            "MIDI mapping document requires a mappings array", {}});
        return juce::Result::fail(
            "MIDI mapping document requires a mappings array");
    }

    std::vector<CandidateMapping> candidates;
    candidates.reserve(static_cast<std::size_t>(mappings->size()));
    for (const auto& value : *mappings)
    {
        CandidateMapping candidate;
        const auto* object = value.getDynamicObject();
        if (object == nullptr)
        {
            candidate.valid = false;
            candidates.push_back(std::move(candidate));
            continue;
        }

        const auto& idValue = object->getProperty("id");
        candidate.idValid = idValue.isString()
            && idValue.toString().isNotEmpty();
        if (candidate.idValid)
        {
            candidate.displayId = idValue.toString();
            candidate.id = candidate.displayId.toStdString();
        }
        else
            candidate.valid = false;

        const auto& enabledValue = object->getProperty("enabled");
        if (!enabledValue.isBool())
            candidate.valid = false;
        else
            candidate.enabled = static_cast<bool>(enabledValue);

        const auto* source = object->getProperty("source").getDynamicObject();
        int channel = 0;
        int control = 0;
        if (source == nullptr
            || !source->getProperty("type").isString()
            || source->getProperty("type").toString() != "cc"
            || !readInteger(source->getProperty("channel"), channel)
            || channel < 1 || channel > 16
            || !readInteger(source->getProperty("control"), control)
            || control < 0 || control > 127
            || !source->getProperty("valueMode").isString()
            || source->getProperty("valueMode").toString() != "inc-dec")
        {
            candidate.valid = false;
        }
        else
        {
            const auto& controlId = source->getProperty("controlId");
            if (!controlId.isVoid() && !controlId.isString())
                candidate.valid = false;
            candidate.sourceValid = true;
            candidate.runtime.channel = static_cast<std::uint8_t>(channel);
            candidate.runtime.control = static_cast<std::uint8_t>(control);
        }

        const auto* conditions = object->getProperty("when").getDynamicObject();
        if (conditions == nullptr
            || (result.version == 1
                && conditions->getProperties().size() != 0)
            || (result.version == 2
                && !parseVersionTwoConditions(
                    *conditions, candidate, result)))
            candidate.valid = false;

        const auto* routes = object->getProperty("routes").getArray();
        const auto routeCount = routes != nullptr ? routes->size() : 0;
        if (routes == nullptr || routeCount < 1
            || routeCount > static_cast<int>(maximumMidiRoutesPerMapping)
            || (result.version == 1 && routeCount != 1))
        {
            candidate.valid = false;
        }
        else
        {
            for (int routeIndex = 0; routeIndex < routeCount; ++routeIndex)
            {
                const auto* route = routes->getReference(routeIndex)
                    .getDynamicObject();
                int scale = 0;
                if (route == nullptr
                    || !route->getProperty("operation").isString()
                    || route->getProperty("operation").toString()
                        != "increment"
                    || !readInteger(route->getProperty("scale"), scale)
                    || scale == 0 || scale < -127 || scale > 127)
                {
                    candidate.valid = false;
                    continue;
                }
                RuntimeMidiRoute runtimeRoute;
                runtimeRoute.scale = static_cast<std::int16_t>(scale);
                if (result.version == 1)
                {
                    if (!route->getProperty("target").isString())
                    {
                        candidate.valid = false;
                        continue;
                    }
                    const auto target = targetRegistry.resolve(
                        route->getProperty("target").toString());
                    if (!target)
                    {
                        candidate.valid = false;
                        diagnose(result, "unknown-direct-target", candidate,
                            "Unknown version 1 direct target");
                        continue;
                    }
                    runtimeRoute.target = *target;
                }
                else
                {
                    int controlSlot = 0;
                    if (!readInteger(
                            route->getProperty("controlSlot"), controlSlot)
                        || controlSlot < 1
                        || controlSlot
                            > static_cast<int>(maximumMidiControlSlots))
                    {
                        candidate.valid = false;
                        diagnose(result, "invalid-control-slot", candidate,
                            "Control slot must be in the range 1..64");
                        continue;
                    }
                    runtimeRoute.destination =
                        MidiRouteDestination::controlSlot;
                    runtimeRoute.controlSlot = static_cast<std::uint8_t>(
                        controlSlot - 1);
                }
                candidate.runtime.routes[static_cast<std::size_t>(routeIndex)] =
                    runtimeRoute;
                candidate.runtime.routeCount = static_cast<std::uint8_t>(
                    routeIndex + 1);
            }
        }
        if (!candidate.valid)
            diagnose(result, "invalid-field-or-range", candidate,
                "Mapping contains an invalid or unsupported field");
        candidates.push_back(std::move(candidate));
    }

    std::unordered_map<std::string, std::vector<std::size_t>> ids;
    for (std::size_t index = 0; index < candidates.size(); ++index)
        if (candidates[index].idValid)
            ids[candidates[index].id].push_back(index);
    for (const auto& entry : ids)
    {
        if (entry.second.size() <= 1)
            continue;
        juce::StringArray related;
        related.add(juce::String(entry.first));
        for (const auto index : entry.second)
        {
            candidates[index].valid = false;
            diagnose(result, "duplicate-mapping-id", candidates[index],
                "Duplicate MIDI mapping ID", related);
        }
    }

    for (std::size_t left = 0; left < candidates.size(); ++left)
    {
        if (!candidates[left].enabled || !candidates[left].sourceValid)
            continue;
        for (std::size_t right = left + 1; right < candidates.size(); ++right)
        {
            if (!candidates[right].enabled || !candidates[right].sourceValid
                || !sameSource(
                    candidates[left].runtime, candidates[right].runtime)
                || !predicatesOverlap(
                    candidates[left].runtime.conditions,
                    candidates[right].runtime.conditions))
                continue;
            candidates[left].valid = false;
            candidates[right].valid = false;
            const juce::StringArray related {
                candidates[left].displayId, candidates[right].displayId};
            diagnose(result,
                result.version == 1 ? "source-conflict"
                                    : "controller-context-ambiguity",
                candidates[left], "Overlapping mappings share one source",
                related);
        }
    }

    for (const auto& candidate : candidates)
    {
        if (!candidate.valid)
        {
            ++result.disabledMappingCount;
            continue;
        }
        if (!candidate.enabled)
            continue;
        if (result.runtimeTable.mappingCount
            >= result.runtimeTable.mappings.size())
        {
            ++result.disabledMappingCount;
            diagnose(result, "mapping-capacity-exhausted", candidate,
                "MIDI mapping capacity exhausted");
            continue;
        }
        result.runtimeTable.mappings[result.runtimeTable.mappingCount++] =
            candidate.runtime;
        ++result.activeMappingCount;
    }
    compileMidiSourceIndex(result.runtimeTable);
    return juce::Result::ok();
}

const juce::String& bundledMidiMappingConfiguration()
{
    static const juce::String configuration = R"json({
  "version": 2,
  "mappings": [
    {
      "id": "controller-encoder-1",
      "enabled": true,
      "source": {"controlId":"encoder-1","type":"cc","channel":15,"control":20,"valueMode":"inc-dec"},
      "when": {},
      "routes": [{"controlSlot":1,"operation":"increment","scale":1}]
    },
    {
      "id": "controller-encoder-2",
      "enabled": true,
      "source": {"controlId":"encoder-2","type":"cc","channel":15,"control":21,"valueMode":"inc-dec"},
      "when": {},
      "routes": [{"controlSlot":2,"operation":"increment","scale":1}]
    }
  ]
})json";
    return configuration;
}

const juce::String& emptyMidiMappingConfiguration(int version)
{
    static const juce::String versionOne =
        R"json({"version":1,"mappings":[]})json";
    static const juce::String versionTwo =
        R"json({"version":2,"mappings":[]})json";
    return version == 2 ? versionTwo : versionOne;
}

} // namespace lps
