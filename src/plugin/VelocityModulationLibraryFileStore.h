#pragma once

#include "core/VelocityModulationLibrary.h"

#include <juce_core/juce_core.h>

#include <vector>

class VelocityModulationLibraryFileStore
{
public:
    explicit VelocityModulationLibraryFileStore(juce::File catalogFile);

    [[nodiscard]] static juce::File defaultCatalogFile();

    // Loads the complete catalog into the library. If the catalog does not
    // exist yet, the constructor defaults are written as its initial content.
    // A malformed existing file is left untouched and the library keeps its
    // defaults.
    [[nodiscard]] bool loadOrCreate(lps::VelocityModulationLibrary& library);

    // Merges the latest on-disk catalog with this instance's snapshot and the
    // staged entry, then atomically replaces the catalog. The staged ID may be
    // adjusted while the entry is still unpublished to avoid cross-instance
    // ID collisions.
    [[nodiscard]] bool persistNewEntry(
        const lps::VelocityModulationLibrary& library,
        lps::VelocityModulationLibraryEntry& stagedEntry);

    [[nodiscard]] const juce::File& catalogFile() const noexcept
    {
        return catalogFile_;
    }

    [[nodiscard]] juce::String lastError() const { return lastError_; }

private:
    [[nodiscard]] bool readEntries(
        std::vector<lps::VelocityModulationLibraryEntry>& entries) const;
    [[nodiscard]] bool writeEntries(
        const std::vector<lps::VelocityModulationLibraryEntry>& entries) const;

    juce::File catalogFile_;
    juce::InterProcessLock catalogLock_;
    juce::String lastError_;
};
