#include "plugin/PatternLibraryFileStore.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

namespace
{
constexpr int schemaVersion = 3;
constexpr int oldestReadableSchemaVersion = 2;
constexpr juce::int64 maximumCatalogBytes = 16 * 1024 * 1024;
constexpr int catalogLockTimeoutMilliseconds = 2000;
constexpr auto catalogFormat = "live-pattern-sequencer-pattern-library";
constexpr auto catalogLockName = "com.hew.livepatternsequencer.pattern-library";
std::timed_mutex catalogProcessMutex;

[[nodiscard]] bool isLegacyBuiltIn(
    const lps::PatternLibraryEntry& entry) noexcept
{
    constexpr std::array<const char*, 10> names {{
        "Basic Kick", "All Steps", "Backbeat", "Offbeat Hats", "Tresillo",
        "3-Step Pulse", "5-Step Pulse", "7-Step Pulse", "9-Step Pulse",
        "Euclidean 5/12"
    }};
    const auto id = entry.id.value();
    if (id == 1 && entry.name == "None"
        && !entry.originVoiceName.has_value())
    {
        return true;
    }
    return id >= 1 && id <= names.size()
        && !entry.originVoiceName.has_value()
        && entry.name == names[static_cast<std::size_t>(id - 1)];
}

[[nodiscard]] bool catalogsEqual(
    const std::vector<lps::PatternLibraryEntry>& left,
    const std::vector<lps::PatternLibraryEntry>& right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index].id != right[index].id
            || left[index].name != right[index].name
            || left[index].originVoiceName != right[index].originVoiceName
            || !lps::patternsEqual(left[index].pattern, right[index].pattern))
        {
            return false;
        }
    }
    return true;
}

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
    const std::vector<lps::PatternLibraryEntry>& entries,
    lps::PatternId id) noexcept
{
    return std::any_of(entries.begin(), entries.end(), [id](const auto& entry)
    {
        return entry.id == id;
    });
}

[[nodiscard]] lps::PatternId nextAvailableId(
    const std::vector<lps::PatternLibraryEntry>& entries) noexcept
{
    for (std::uint64_t value = 1;
         value <= lps::PatternLibrary::maxEntryCount;
         ++value)
    {
        const auto candidate = lps::PatternId {value};
        if (!idIsUsed(entries, candidate))
            return candidate;
    }

    return {};
}

[[nodiscard]] const lps::PatternLibraryEntry* findEquivalent(
    const std::vector<lps::PatternLibraryEntry>& entries,
    const lps::Pattern& pattern) noexcept
{
    const auto found = std::find_if(
        entries.begin(), entries.end(), [&pattern](const auto& entry)
        {
            return lps::patternsEqual(entry.pattern, pattern);
        });
    return found != entries.end() ? &*found : nullptr;
}

[[nodiscard]] bool entriesAreValid(
    const std::vector<lps::PatternLibraryEntry>& entries) noexcept
{
    if (entries.empty()
        || entries.size() > lps::PatternLibrary::maxEntryCount)
    {
        return false;
    }

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        const auto& candidate = entries[index];
        if (!candidate.id.isValid()
            || candidate.id.value() > lps::PatternLibrary::maxEntryCount
            || !lps::patternIsValid(candidate.pattern))
        {
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (entries[previous].id == candidate.id
                || lps::patternsEqual(
                    entries[previous].pattern, candidate.pattern))
            {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] bool appendIfMissing(
    std::vector<lps::PatternLibraryEntry>& entries,
    lps::PatternLibraryEntry candidate,
    lps::PatternLibraryEntry* resolvedEntry = nullptr)
{
    if (const auto* existing = findEquivalent(entries, candidate.pattern))
    {
        if (resolvedEntry != nullptr)
            *resolvedEntry = *existing;
        return true;
    }

    if (entries.size() >= lps::PatternLibrary::maxEntryCount)
        return false;

    if (!candidate.id.isValid()
        || candidate.id.value() > lps::PatternLibrary::maxEntryCount
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
    std::vector<lps::PatternLibraryEntry>& entries,
    const lps::PatternLibraryEntry& candidate)
{
    if (const auto* existing = findEquivalent(entries, candidate.pattern))
        return existing->id == candidate.id;

    if (entries.size() >= lps::PatternLibrary::maxEntryCount
        || idIsUsed(entries, candidate.id))
    {
        return false;
    }

    entries.push_back(candidate);
    return true;
}

[[nodiscard]] juce::var makeCatalogJson(
    const std::vector<lps::PatternLibraryEntry>& entries)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", catalogFormat);
    root->setProperty("schemaVersion", schemaVersion);

    juce::Array<juce::var> serializedPatterns;
    serializedPatterns.ensureStorageAllocated(static_cast<int>(entries.size()));
    for (const auto& entry : entries)
    {
        auto* serialized = new juce::DynamicObject();
        serialized->setProperty(
            "id", static_cast<juce::int64>(entry.id.value()));
        if (!entry.name.empty())
            serialized->setProperty(
                "name", juce::String::fromUTF8(entry.name.c_str()));
        if (entry.originVoiceName && !entry.originVoiceName->empty())
            serialized->setProperty(
                "originVoiceName",
                juce::String::fromUTF8(entry.originVoiceName->c_str()));
        serialized->setProperty(
            "cycleLengthTicks",
            static_cast<juce::int64>(entry.pattern.cycleLengthTicks));
        juce::Array<juce::var> hits;
        hits.ensureStorageAllocated(static_cast<int>(entry.pattern.hitCount));
        for (std::size_t index = 0; index < entry.pattern.hitCount; ++index)
        {
            const auto& hit = entry.pattern.hits[index];
            auto* serializedHit = new juce::DynamicObject();
            serializedHit->setProperty(
                "startTick", static_cast<juce::int64>(hit.startTick));
            serializedHit->setProperty(
                "durationTicks", static_cast<juce::int64>(hit.durationTicks));
            hits.add(juce::var(serializedHit));
        }
        serialized->setProperty("hits", hits);
        serializedPatterns.add(juce::var(serialized));
    }

    root->setProperty("patterns", serializedPatterns);
    return juce::var(root);
}
} // namespace

PatternLibraryFileStore::PatternLibraryFileStore(juce::File catalogFile)
    : catalogFile_(std::move(catalogFile)),
      catalogLock_(catalogLockName)
{
}

juce::File PatternLibraryFileStore::defaultCatalogFile()
{
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("Hew")
        .getChildFile("LivePatternSequencer")
        .getChildFile("patterns.json");
}

bool PatternLibraryFileStore::loadOrCreate(lps::PatternLibrary& library)
{
    std::unique_lock<std::timed_mutex> processLock(
        catalogProcessMutex, std::defer_lock);
    if (!processLock.try_lock_for(
            std::chrono::milliseconds(catalogLockTimeoutMilliseconds)))
    {
        lastError_ =
            "Timed out waiting for another in-process pattern catalog operation:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    const CatalogLockGuard lock(catalogLock_);
    if (!lock.isLocked())
    {
        lastError_ =
            "Timed out waiting for access to the pattern catalog:\n"
            + catalogFile_.getFullPathName()
            + "\n\nClose other instances that may be saving, then reopen the plug-in.";
        return false;
    }

    if (!catalogFile_.existsAsFile())
    {
        std::vector<lps::PatternLibraryEntry> defaults;
        defaults.reserve(library.size());
        for (std::size_t index = 0; index < library.size(); ++index)
        {
            if (const auto* entry = library.recordAt(index))
                defaults.push_back(*entry);
        }

        if (!writeEntries(defaults))
        {
            lastError_ =
                "Could not create the pattern catalog:\n"
                + catalogFile_.getFullPathName()
                + "\n\nCheck that the folder is writable and there is free disk space.";
            return false;
        }

        lastError_.clear();
        return true;
    }

    std::vector<lps::PatternLibraryEntry> loadedEntries;
    if (!readEntries(loadedEntries))
    {
        lastError_ =
            "The pattern catalog is malformed, unsupported, or unreadable:\n"
            + catalogFile_.getFullPathName()
            + "\n\nThe file was left untouched. Correct it or move it aside, then reopen the plug-in.";
        return false;
    }

    std::vector<lps::PatternLibraryEntry> migratedEntries;
    migratedEntries.reserve(loadedEntries.size() + 1);
    if (const auto* none = library.recordAt(0))
        migratedEntries.push_back(*none);
    for (const auto& entry : loadedEntries)
    {
        if (isLegacyBuiltIn(entry))
            continue;
        if (!appendIfMissing(migratedEntries, entry))
        {
            lastError_ =
                "The pattern catalog could not be migrated to the current defaults:\n"
                + catalogFile_.getFullPathName();
            return false;
        }
    }
    const bool migrated = !catalogsEqual(loadedEntries, migratedEntries);
    if (!library.replaceEntriesForStartup(migratedEntries)
        || (migrated && !writeEntries(migratedEntries)))
    {
        lastError_ =
            "The pattern catalog is malformed, unsupported, or unreadable:\n"
            + catalogFile_.getFullPathName()
            + "\n\nThe catalog could not be migrated safely.";
        return false;
    }

    lastError_.clear();
    return true;
}

bool PatternLibraryFileStore::persistNewEntry(
    const lps::PatternLibrary& library,
    lps::PatternLibraryEntry& stagedEntry,
    const std::vector<lps::PatternId>& excludedIds)
{
    std::unique_lock<std::timed_mutex> processLock(
        catalogProcessMutex, std::defer_lock);
    if (!processLock.try_lock_for(
            std::chrono::milliseconds(catalogLockTimeoutMilliseconds)))
    {
        lastError_ =
            "Timed out waiting for another in-process pattern catalog operation:\n"
            + catalogFile_.getFullPathName()
            + "\n\nWait for the other save to finish and try again.";
        return false;
    }

    const CatalogLockGuard lock(catalogLock_);
    if (!lock.isLocked())
    {
        lastError_ =
            "Timed out waiting for access to the pattern catalog:\n"
            + catalogFile_.getFullPathName()
            + "\n\nWait for the other save to finish and try again.";
        return false;
    }

    std::vector<lps::PatternLibraryEntry> mergedEntries;
    if (catalogFile_.existsAsFile() && !readEntries(mergedEntries))
    {
        lastError_ =
            "The existing pattern catalog is malformed or unreadable:\n"
            + catalogFile_.getFullPathName()
            + "\n\nIt was not overwritten. Correct it or move it aside, then reopen the plug-in.";
        return false;
    }

    for (std::size_t index = 0; index < library.size(); ++index)
    {
        const auto* entry = library.recordAt(index);
        if (entry != nullptr
            && std::find(excludedIds.begin(), excludedIds.end(), entry->id)
                != excludedIds.end())
        {
            continue;
        }
        if (entry == nullptr || !mergePublishedEntry(mergedEntries, *entry))
        {
            lastError_ =
                "The pattern catalog is full or could not be merged safely:\n"
                + catalogFile_.getFullPathName();
            return false;
        }
    }

    lps::PatternLibraryEntry resolvedEntry;
    if (!appendIfMissing(mergedEntries, stagedEntry, &resolvedEntry)
        || !entriesAreValid(mergedEntries))
    {
        lastError_ =
            "The pattern catalog is full or contains conflicting entries:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    if (!writeEntries(mergedEntries))
    {
        lastError_ =
            "Could not write the pattern catalog:\n"
            + catalogFile_.getFullPathName()
            + "\n\nCheck that the folder is writable and there is free disk space.";
        return false;
    }

    stagedEntry = std::move(resolvedEntry);
    lastError_.clear();
    return true;
}

bool PatternLibraryFileStore::deleteEntry(lps::PatternId id)
{
    if (!id.isValid() || id.value() == 1)
        return false;

    std::unique_lock<std::timed_mutex> processLock(
        catalogProcessMutex, std::defer_lock);
    if (!processLock.try_lock_for(
            std::chrono::milliseconds(catalogLockTimeoutMilliseconds)))
    {
        lastError_ =
            "Timed out waiting for another in-process pattern catalog operation:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    const CatalogLockGuard lock(catalogLock_);
    if (!lock.isLocked())
    {
        lastError_ =
            "Timed out waiting for access to the pattern catalog:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    std::vector<lps::PatternLibraryEntry> entries;
    if (!readEntries(entries))
    {
        lastError_ =
            "The existing pattern catalog is malformed or unreadable:\n"
            + catalogFile_.getFullPathName()
            + "\n\nIt was not overwritten.";
        return false;
    }

    const auto found = std::find_if(
        entries.begin(), entries.end(), [id](const auto& entry)
        {
            return entry.id == id;
        });
    if (found == entries.end())
    {
        lastError_ = "The saved pattern could not be found in the catalog.";
        return false;
    }

    entries.erase(found);
    if (!entriesAreValid(entries) || !writeEntries(entries))
    {
        lastError_ =
            "Could not update the pattern catalog:\n"
            + catalogFile_.getFullPathName();
        return false;
    }

    lastError_.clear();
    return true;
}

bool PatternLibraryFileStore::readEntries(
    std::vector<lps::PatternLibraryEntry>& entries) const
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
    const auto& serializedPatterns = root->getProperty("patterns");
    const auto* patterns = serializedPatterns.getArray();
    if (!format.isString()
        || format.toString() != catalogFormat
        || !(version.isInt() || version.isInt64())
        || static_cast<juce::int64>(version) < oldestReadableSchemaVersion
        || static_cast<juce::int64>(version) > schemaVersion
        || patterns == nullptr
        || patterns->isEmpty()
        || patterns->size() > static_cast<int>(lps::PatternLibrary::maxEntryCount))
    {
        return false;
    }

    std::vector<lps::PatternLibraryEntry> parsedEntries;
    parsedEntries.reserve(static_cast<std::size_t>(patterns->size()));
    for (const auto& serializedValue : *patterns)
    {
        const auto* serialized = serializedValue.getDynamicObject();
        if (serialized == nullptr)
            return false;

        const auto& idValue = serialized->getProperty("id");
        const auto& nameValue = serialized->getProperty("name");
        const auto& originValue = serialized->getProperty("originVoiceName");
        const auto& cycleValue = serialized->getProperty("cycleLengthTicks");
        const auto* hits = serialized->getProperty("hits").getArray();
        if (!(idValue.isInt() || idValue.isInt64())
            || (!nameValue.isVoid() && !nameValue.isString())
            || (!originValue.isVoid() && !originValue.isString())
            || !(cycleValue.isInt() || cycleValue.isInt64())
            || hits == nullptr
            || hits->size() > static_cast<int>(lps::Pattern::maximumHitCount))
        {
            return false;
        }

        const auto id = static_cast<juce::int64>(idValue);
        const auto name = nameValue.toString();
        const auto cycleLength = static_cast<juce::int64>(cycleValue);
        if (id <= 0
            || id > static_cast<juce::int64>(lps::PatternLibrary::maxEntryCount)
            || cycleLength <= 0
            || cycleLength > lps::Pattern::maximumCycleLengthTicks)
        {
            return false;
        }

        lps::Pattern pattern;
        pattern.cycleLengthTicks = static_cast<lps::PatternTick>(cycleLength);
        pattern.hitCount = static_cast<std::uint16_t>(hits->size());
        for (int index = 0; index < hits->size(); ++index)
        {
            const auto* serializedHit = hits->getReference(index).getDynamicObject();
            if (serializedHit == nullptr)
                return false;
            const auto& startValue = serializedHit->getProperty("startTick");
            const auto& durationValue = serializedHit->getProperty("durationTicks");
            if (!(startValue.isInt() || startValue.isInt64())
                || !(durationValue.isInt() || durationValue.isInt64()))
            {
                return false;
            }
            const auto start = static_cast<juce::int64>(startValue);
            const auto duration = static_cast<juce::int64>(durationValue);
            if (start < 0
                || start > std::numeric_limits<lps::PatternTick>::max()
                || duration <= 0
                || duration > std::numeric_limits<lps::PatternTick>::max())
            {
                return false;
            }
            pattern.hits[static_cast<std::size_t>(index)] = {
                static_cast<lps::PatternTick>(start),
                static_cast<lps::PatternTick>(duration)
            };
        }

        if (!lps::patternIsValid(pattern))
            return false;

        parsedEntries.push_back({
            lps::PatternId {static_cast<std::uint64_t>(id)},
            name.toStdString(),
            pattern,
            originValue.isString()
                ? std::optional<std::string> {
                    originValue.toString().toStdString() }
                : std::nullopt
        });
    }

    if (!entriesAreValid(parsedEntries))
        return false;

    entries = std::move(parsedEntries);
    return true;
}

bool PatternLibraryFileStore::writeEntries(
    const std::vector<lps::PatternLibraryEntry>& entries) const
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
