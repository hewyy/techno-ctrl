#include "plugin/ModulationLibraryFileStore.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

namespace
{
constexpr int schemaVersion = 1;
constexpr juce::int64 maximumCatalogBytes = 1024 * 1024;
constexpr int catalogLockTimeoutMilliseconds = 2000;
constexpr auto catalogFormat =
    "live-pattern-sequencer-modulation-library";
constexpr auto catalogLockName =
    "com.hew.livepatternsequencer.modulation-library";
std::timed_mutex modulationCatalogProcessMutex;

class CatalogLockGuard
{
public:
    explicit CatalogLockGuard(juce::InterProcessLock& lock)
        : lock_(lock),
          locked_(lock_.enter(catalogLockTimeoutMilliseconds))
    {
    }

    ~CatalogLockGuard()
    {
        if (locked_)
            lock_.exit();
    }

    [[nodiscard]] bool isLocked() const noexcept { return locked_; }

private:
    juce::InterProcessLock& lock_;
    bool locked_ = false;
};

[[nodiscard]] bool idIsUsed(
    const std::vector<lps::ModulationLibraryEntry>& entries,
    lps::ModulationId id) noexcept
{
    return std::any_of(entries.begin(), entries.end(), [id](const auto& entry)
    {
        return entry.id == id;
    });
}

[[nodiscard]] lps::ModulationId nextAvailableId(
    const std::vector<lps::ModulationLibraryEntry>& entries) noexcept
{
    for (std::uint64_t value = 1;
         value <= lps::ModulationLibrary::maxEntryCount;
         ++value)
    {
        const auto candidate = lps::ModulationId {value};
        if (!idIsUsed(entries, candidate))
            return candidate;
    }

    return {};
}

[[nodiscard]] const lps::ModulationLibraryEntry* findEquivalent(
    const std::vector<lps::ModulationLibraryEntry>& entries,
    const lps::Modulation& modulation) noexcept
{
    const auto found = std::find_if(
        entries.begin(), entries.end(), [&modulation](const auto& entry)
        {
            return lps::modulationsEqual(entry.modulation, modulation);
        });
    return found != entries.end() ? &*found : nullptr;
}

[[nodiscard]] bool entriesAreValid(
    const std::vector<lps::ModulationLibraryEntry>& entries) noexcept
{
    if (entries.empty()
        || entries.size() > lps::ModulationLibrary::maxEntryCount)
    {
        return false;
    }

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& candidate = entries[index];
        if (!candidate.id.isValid()
            || candidate.id.value()
                > lps::ModulationLibrary::maxEntryCount
            || candidate.name.empty()
            || candidate.modulation.length == 0
            || candidate.modulation.length
                > lps::Modulation::maxLength)
        {
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (entries[previous].id == candidate.id
                || lps::modulationsEqual(
                    entries[previous].modulation, candidate.modulation))
            {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] bool appendIfMissing(
    std::vector<lps::ModulationLibraryEntry>& entries,
    lps::ModulationLibraryEntry candidate,
    lps::ModulationLibraryEntry* resolvedEntry = nullptr)
{
    if (const auto* existing = findEquivalent(entries, candidate.modulation))
    {
        if (resolvedEntry != nullptr)
            *resolvedEntry = *existing;
        return true;
    }

    if (entries.size() >= lps::ModulationLibrary::maxEntryCount)
        return false;

    if (!candidate.id.isValid()
        || candidate.id.value()
            > lps::ModulationLibrary::maxEntryCount
        || idIsUsed(entries, candidate.id))
    {
        candidate.id = nextAvailableId(entries);
    }

    if (!candidate.id.isValid())
        return false;

    entries.push_back(std::move(candidate));
    if (resolvedEntry != nullptr)
        *resolvedEntry = entries.back();
    return true;
}

[[nodiscard]] bool mergePublishedEntry(
    std::vector<lps::ModulationLibraryEntry>& entries,
    const lps::ModulationLibraryEntry& candidate)
{
    if (const auto* existing = findEquivalent(entries, candidate.modulation))
        return existing->id == candidate.id;

    if (entries.size() >= lps::ModulationLibrary::maxEntryCount
        || idIsUsed(entries, candidate.id))
    {
        return false;
    }

    entries.push_back(candidate);
    return true;
}

[[nodiscard]] juce::var makeCatalogJson(
    const std::vector<lps::ModulationLibraryEntry>& entries)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", catalogFormat);
    root->setProperty("schemaVersion", schemaVersion);

    juce::Array<juce::var> serializedModulations;
    serializedModulations.ensureStorageAllocated(
        static_cast<int>(entries.size()));
    for (const auto& entry : entries)
    {
        auto* serialized = new juce::DynamicObject();
        serialized->setProperty(
            "id", static_cast<juce::int64>(entry.id.value()));
        serialized->setProperty(
            "name", juce::String::fromUTF8(entry.name.c_str()));

        juce::Array<juce::var> serializedValues;
        serializedValues.ensureStorageAllocated(
            static_cast<int>(entry.modulation.length));
        for (std::size_t step = 0; step < entry.modulation.length; ++step)
        {
            serializedValues.add(
                static_cast<int>(entry.modulation.values[step].raw));
        }

        serialized->setProperty("values", serializedValues);
        serializedModulations.add(juce::var(serialized));
    }

    root->setProperty("modulations", serializedModulations);
    return juce::var(root);
}
} // namespace

ModulationLibraryFileStore::ModulationLibraryFileStore(
    juce::File catalogFile)
    : catalogFile_(std::move(catalogFile)),
      catalogLock_(catalogLockName)
{
}

juce::File ModulationLibraryFileStore::defaultCatalogFile()
{
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("Hew")
        .getChildFile("LivePatternSequencer")
        .getChildFile("modulations.json");
}

bool ModulationLibraryFileStore::loadOrCreate(
    lps::ModulationLibrary& library)
{
    std::unique_lock<std::timed_mutex> processLock(
        modulationCatalogProcessMutex, std::defer_lock);
    if (!processLock.try_lock_for(
            std::chrono::milliseconds(catalogLockTimeoutMilliseconds)))
    {
        lastError_ =
            "Timed out waiting for another in-process modulation "
            "catalog operation:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    const CatalogLockGuard lock(catalogLock_);
    if (!lock.isLocked())
    {
        lastError_ =
            "Timed out waiting for access to the modulation catalog:\n"
            + catalogFile_.getFullPathName()
            + "\n\nClose other instances that may be saving, then reopen the plug-in.";
        return false;
    }

    if (!catalogFile_.existsAsFile())
    {
        std::vector<lps::ModulationLibraryEntry> defaults;
        defaults.reserve(library.size());
        for (std::size_t index = 0; index < library.size(); ++index)
        {
            if (const auto* entry = library.recordAt(index))
                defaults.push_back(*entry);
        }

        if (!writeEntries(defaults))
        {
            lastError_ =
                "Could not create the modulation catalog:\n"
                + catalogFile_.getFullPathName()
                + "\n\nCheck that the folder is writable and there is free disk space.";
            return false;
        }

        lastError_.clear();
        return true;
    }

    std::vector<lps::ModulationLibraryEntry> loadedEntries;
    if (!readEntries(loadedEntries)
        || !library.replaceEntriesForStartup(loadedEntries))
    {
        lastError_ =
            "The modulation catalog is malformed, unsupported, or "
            "unreadable:\n"
            + catalogFile_.getFullPathName()
            + "\n\nThe file was left untouched. Correct it or move it aside, "
              "then reopen the plug-in.";
        return false;
    }

    lastError_.clear();
    return true;
}

bool ModulationLibraryFileStore::persistNewEntry(
    const lps::ModulationLibrary& library,
    lps::ModulationLibraryEntry& stagedEntry)
{
    std::unique_lock<std::timed_mutex> processLock(
        modulationCatalogProcessMutex, std::defer_lock);
    if (!processLock.try_lock_for(
            std::chrono::milliseconds(catalogLockTimeoutMilliseconds)))
    {
        lastError_ =
            "Timed out waiting for another in-process modulation "
            "catalog operation:\n"
            + catalogFile_.getFullPathName()
            + "\n\nWait for the other save to finish and try again.";
        return false;
    }

    const CatalogLockGuard lock(catalogLock_);
    if (!lock.isLocked())
    {
        lastError_ =
            "Timed out waiting for access to the modulation catalog:\n"
            + catalogFile_.getFullPathName()
            + "\n\nWait for the other save to finish and try again.";
        return false;
    }

    std::vector<lps::ModulationLibraryEntry> mergedEntries;
    if (catalogFile_.existsAsFile() && !readEntries(mergedEntries))
    {
        lastError_ =
            "The existing modulation catalog is malformed or "
            "unreadable:\n"
            + catalogFile_.getFullPathName()
            + "\n\nIt was not overwritten. Correct it or move it aside, then "
              "reopen the plug-in.";
        return false;
    }

    for (std::size_t index = 0; index < library.size(); ++index)
    {
        const auto* entry = library.recordAt(index);
        if (entry == nullptr || !mergePublishedEntry(mergedEntries, *entry))
        {
            lastError_ =
                "The modulation catalog is full or could not be "
                "merged safely:\n"
                + catalogFile_.getFullPathName();
            return false;
        }
    }

    lps::ModulationLibraryEntry resolvedEntry;
    if (!appendIfMissing(mergedEntries, stagedEntry, &resolvedEntry)
        || !entriesAreValid(mergedEntries))
    {
        lastError_ =
            "The modulation catalog is full or contains conflicting "
            "entries:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    if (!writeEntries(mergedEntries))
    {
        lastError_ =
            "Could not write the modulation catalog:\n"
            + catalogFile_.getFullPathName()
            + "\n\nCheck that the folder is writable and there is free disk space.";
        return false;
    }

    stagedEntry = std::move(resolvedEntry);
    lastError_.clear();
    return true;
}

bool ModulationLibraryFileStore::readEntries(
    std::vector<lps::ModulationLibraryEntry>& entries) const
{
    if (!catalogFile_.existsAsFile()
        || catalogFile_.getSize() <= 0
        || catalogFile_.getSize() > maximumCatalogBytes)
    {
        return false;
    }

    juce::var rootValue;
    const auto parseResult = juce::JSON::parse(
        catalogFile_.loadFileAsString(), rootValue);
    const auto* root = rootValue.getDynamicObject();
    if (parseResult.failed() || root == nullptr)
        return false;

    const auto& format = root->getProperty("format");
    const auto& version = root->getProperty("schemaVersion");
    const auto& serializedModulations = root->getProperty("modulations");
    const auto* modulations = serializedModulations.getArray();
    if (!format.isString()
        || format.toString() != catalogFormat
        || !(version.isInt() || version.isInt64())
        || static_cast<juce::int64>(version) != schemaVersion
        || modulations == nullptr
        || modulations->isEmpty()
        || modulations->size()
            > static_cast<int>(lps::ModulationLibrary::maxEntryCount))
    {
        return false;
    }

    std::vector<lps::ModulationLibraryEntry> parsedEntries;
    parsedEntries.reserve(static_cast<std::size_t>(modulations->size()));
    for (const auto& serializedValue : *modulations)
    {
        const auto* serialized = serializedValue.getDynamicObject();
        if (serialized == nullptr)
            return false;

        const auto& idValue = serialized->getProperty("id");
        const auto& nameValue = serialized->getProperty("name");
        const auto& valuesValue = serialized->getProperty("values");
        const auto* values = valuesValue.getArray();
        if (!(idValue.isInt() || idValue.isInt64())
            || !nameValue.isString()
            || values == nullptr)
        {
            return false;
        }

        const auto id = static_cast<juce::int64>(idValue);
        const auto name = nameValue.toString();
        if (id <= 0
            || id > static_cast<juce::int64>(
                lps::ModulationLibrary::maxEntryCount)
            || name.isEmpty()
            || name.trim().isEmpty()
            || values->isEmpty()
            || values->size()
                > static_cast<int>(lps::Modulation::maxLength))
        {
            return false;
        }

        lps::Modulation modulation;
        modulation.length = static_cast<std::uint8_t>(values->size());
        for (int step = 0; step < values->size(); ++step)
        {
            const auto& value = values->getReference(step);
            if (!(value.isInt() || value.isInt64()))
                return false;

            const auto integerValue = static_cast<juce::int64>(value);
            if (integerValue < 0
                || integerValue
                    > static_cast<juce::int64>(
                        lps::NormalizedValue::maximum))
            {
                return false;
            }

            modulation.values[static_cast<std::size_t>(step)].raw =
                static_cast<std::uint16_t>(integerValue);
        }

        parsedEntries.push_back({
            lps::ModulationId {static_cast<std::uint64_t>(id)},
            name.toStdString(),
            modulation
        });
    }

    if (!entriesAreValid(parsedEntries))
        return false;

    entries = std::move(parsedEntries);
    return true;
}

bool ModulationLibraryFileStore::writeEntries(
    const std::vector<lps::ModulationLibraryEntry>& entries) const
{
    if (!entriesAreValid(entries))
        return false;

    auto serializedCatalog = juce::JSON::toString(
        makeCatalogJson(entries),
        juce::JSON::FormatOptions {}
            .withSpacing(juce::JSON::Spacing::multiLine));
    serializedCatalog << '\n';
    if (serializedCatalog.getNumBytesAsUTF8()
        > static_cast<std::size_t>(maximumCatalogBytes))
    {
        return false;
    }

    const auto parentDirectory = catalogFile_.getParentDirectory();
    if (!parentDirectory.isDirectory()
        && parentDirectory.createDirectory().failed())
    {
        return false;
    }

    juce::TemporaryFile temporaryFile(catalogFile_);
    auto output = temporaryFile.getFile().createOutputStream();
    if (output == nullptr || output->failedToOpen())
        return false;

    output->writeText(serializedCatalog, false, false, "\n");
    output->flush();
    const bool writeSucceeded = output->getStatus().wasOk();
    output.reset();

    return writeSucceeded
        && temporaryFile.overwriteTargetFileWithTemporary();
}
