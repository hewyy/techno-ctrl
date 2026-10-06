#pragma once

#include "core/MidiControlMapping.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace lps
{

class MidiTargetRegistry
{
public:
    [[nodiscard]] bool registerTarget(
        const juce::String& stableId, MidiTargetHandle handle);
    [[nodiscard]] std::optional<MidiTargetHandle> resolve(
        const juce::String& stableId) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    void clear() { entries_.clear(); }

private:
    struct Entry
    {
        juce::String stableId;
        MidiTargetHandle handle;
    };
    std::vector<Entry> entries_;
};

enum class MidiDiagnosticSeverity : std::uint8_t { warning, error };

struct MidiMappingDiagnostic
{
    juce::String code;
    MidiDiagnosticSeverity severity = MidiDiagnosticSeverity::error;
    juce::String mappingId;
    juce::String message;
    juce::StringArray relatedIds;
};

struct MidiPageBindingDiagnostic
{
    juce::String code;
    MidiDiagnosticSeverity severity = MidiDiagnosticSeverity::error;
    MidiPageProfileId profile;
    juce::String message;
};

struct ParsedMidiMappingConfiguration
{
    RuntimeMidiMappingTable runtimeTable;
    std::size_t activeMappingCount = 0;
    std::size_t disabledMappingCount = 0;
    int version = 0;
    juce::StringArray modifierNames;
    std::vector<MidiMappingDiagnostic> diagnostics;

    [[nodiscard]] std::optional<std::uint8_t> modifierIndex(
        const juce::String& name) const noexcept;
};

[[nodiscard]] juce::Result parseMidiMappingConfiguration(
    const juce::String& jsonText,
    const MidiTargetRegistry& targetRegistry,
    ParsedMidiMappingConfiguration& result);

[[nodiscard]] const juce::String& bundledMidiMappingConfiguration();
[[nodiscard]] const juce::String& emptyMidiMappingConfiguration(
    int version = 1);

} // namespace lps
