#pragma once

#include "core/PatternLibrary.h"

#include <juce_core/juce_core.h>

#include <vector>

class PatternLibraryFileStore
{
public:
    explicit PatternLibraryFileStore(juce::File catalogFile);

    [[nodiscard]] static juce::File defaultCatalogFile();

    // Loads the complete catalog into the library. If the catalog does not
    // exist yet, the constructor defaults are written as its initial content.
    // A malformed existing file is left untouched and the library keeps its
    // defaults.
    [[nodiscard]] bool loadOrCreate(lps::PatternLibrary& library);

    // Merges the latest on-disk catalog with this instance's snapshot and the
    // staged entry, then atomically replaces the catalog. The staged ID may be
    // adjusted while the entry is still unpublished to avoid cross-instance
    // ID collisions.
    [[nodiscard]] bool persistNewEntry(
        const lps::PatternLibrary& library,
        lps::PatternLibraryEntry& stagedEntry);

    [[nodiscard]] const juce::File& catalogFile() const noexcept
    {
        return catalogFile_;
    }

    [[nodiscard]] juce::String lastError() const { return lastError_; }

private:
    [[nodiscard]] bool readEntries(
        std::vector<lps::PatternLibraryEntry>& entries) const;
    [[nodiscard]] bool writeEntries(
        const std::vector<lps::PatternLibraryEntry>& entries) const;

    juce::File catalogFile_;
    juce::InterProcessLock catalogLock_;
    juce::String lastError_;
};
