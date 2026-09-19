#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace lps
{
namespace
{
constexpr std::size_t queueCapacity = 2048;
constexpr std::size_t componentCapacity = 32;
constexpr std::size_t eventCapacity = 48;
constexpr std::size_t detailsCapacity = 256;
constexpr std::uintmax_t maximumLogBytes = 10u * 1024u * 1024u;

struct LogRecord
{
    LogLevel level = LogLevel::info;
    std::int64_t timestampMicroseconds = 0;
    std::array<char, componentCapacity> component {};
    std::array<char, eventCapacity> event {};
    std::array<char, detailsCapacity> details {};
};

struct QueueSlot
{
    std::atomic<std::size_t> sequence {0};
    LogRecord record;
};

template <std::size_t Size>
void copyText(std::array<char, Size>& destination, std::string_view source) noexcept
{
    const auto count = std::min(source.size(), Size - 1);
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto character = source[index];
        destination[index] = character == '\n' || character == '\r'
            || character == '\t' ? ' ' : character;
    }
    destination[count] = '\0';
}

class LoggerState final
{
public:
    LoggerState()
    {
        for (std::size_t index = 0; index < slots_.size(); ++index)
            slots_[index].sequence.store(index, std::memory_order_relaxed);
    }

    ~LoggerState()
    {
        shutdown();
    }

    bool configure(LoggerConfig config)
    {
        std::lock_guard<std::mutex> lock(configurationMutex_);
        minimumLevel_.store(config.level, std::memory_order_release);
        mirrorToStderr_.store(config.mirrorToStderr, std::memory_order_release);
        if (running_.load(std::memory_order_acquire))
            return config.filePath == filePath_;
        if (config.filePath.empty())
            return false;

        std::error_code fileError;
        const std::filesystem::path path {config.filePath};
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), fileError);
        fileError.clear();
        const bool existing = std::filesystem::exists(path, fileError);
        fileError.clear();
        const auto existingSize = existing
            ? std::filesystem::file_size(path, fileError) : 0;
        if (existing && !fileError && existingSize >= maximumLogBytes)
        {
            auto previous = path;
            previous += ".1";
            std::filesystem::remove(previous, fileError);
            fileError.clear();
            std::filesystem::rename(path, previous, fileError);
        }

        auto file = std::make_unique<std::ofstream>(
            config.filePath, std::ios::out | std::ios::app);
        if (!file->is_open())
            return false;

        filePath_ = std::move(config.filePath);
        file_ = std::move(file);
        running_.store(true, std::memory_order_release);
        worker_ = std::thread([this] { run(); });
        return true;
    }

    void shutdown() noexcept
    {
        std::lock_guard<std::mutex> lock(configurationMutex_);
        if (!running_.exchange(false, std::memory_order_acq_rel))
            return;
        if (worker_.joinable())
            worker_.join();
        drain();
        if (file_ != nullptr)
            file_->flush();
        flushedPosition_.store(
            dequeuePosition_.load(std::memory_order_acquire),
            std::memory_order_release);
        file_.reset();
    }

    bool enqueue(LogRecord record) noexcept
    {
        auto position = enqueuePosition_.load(std::memory_order_relaxed);
        QueueSlot* slot = nullptr;
        for (;;)
        {
            slot = &slots_[position % slots_.size()];
            const auto sequence = slot->sequence.load(std::memory_order_acquire);
            const auto difference = static_cast<std::intptr_t>(sequence)
                - static_cast<std::intptr_t>(position);
            if (difference == 0)
            {
                if (enqueuePosition_.compare_exchange_weak(
                        position, position + 1, std::memory_order_relaxed))
                    break;
            }
            else if (difference < 0)
            {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            else
            {
                position = enqueuePosition_.load(std::memory_order_relaxed);
            }
        }
        slot->record = record;
        slot->sequence.store(position + 1, std::memory_order_release);
        return true;
    }

    void setLevel(LogLevel level) noexcept
    {
        minimumLevel_.store(level, std::memory_order_release);
    }

    LogLevel level() const noexcept
    {
        return minimumLevel_.load(std::memory_order_acquire);
    }

    bool enabled(LogLevel candidate) const noexcept
    {
        return running_.load(std::memory_order_acquire)
            && candidate >= level() && candidate < LogLevel::off;
    }

    bool flush(std::chrono::milliseconds timeout) noexcept
    {
        const auto target = enqueuePosition_.load(std::memory_order_acquire);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (flushedPosition_.load(std::memory_order_acquire) < target
            && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds {2});
        }
        return flushedPosition_.load(std::memory_order_acquire) >= target;
    }

    LoggerDiagnostics diagnostics() const noexcept
    {
        return {
            written_.load(std::memory_order_acquire),
            dropped_.load(std::memory_order_acquire),
            writeFailures_.load(std::memory_order_acquire)
        };
    }

private:
    bool tryDequeue(LogRecord& record) noexcept
    {
        const auto position = dequeuePosition_.load(std::memory_order_relaxed);
        auto& slot = slots_[position % slots_.size()];
        const auto sequence = slot.sequence.load(std::memory_order_acquire);
        if (static_cast<std::intptr_t>(sequence)
                - static_cast<std::intptr_t>(position + 1) != 0)
            return false;
        record = slot.record;
        slot.sequence.store(position + slots_.size(), std::memory_order_release);
        dequeuePosition_.store(position + 1, std::memory_order_release);
        return true;
    }

    void writeRecord(const LogRecord& record)
    {
        const auto seconds = static_cast<std::time_t>(
            record.timestampMicroseconds / 1'000'000);
        const auto micros = static_cast<long long>(
            record.timestampMicroseconds % 1'000'000);
        std::tm utc {};
#if defined(_WIN32)
        gmtime_s(&utc, &seconds);
#else
        gmtime_r(&seconds, &utc);
#endif
        char timestamp[40] {};
        std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &utc);
        char line[512] {};
        const auto length = std::snprintf(
            line,
            sizeof(line),
            "%s.%06lldZ level=%s component=%s event=%s%s%s\n",
            timestamp,
            micros,
            Logger::levelName(record.level),
            record.component.data(),
            record.event.data(),
            record.details[0] == '\0' ? "" : " ",
            record.details.data());
        const auto safeLength = static_cast<std::size_t>(
            std::clamp(length, 0, static_cast<int>(sizeof(line) - 1)));
        bool fileWritten = false;
        if (file_ != nullptr)
        {
            file_->write(line, static_cast<std::streamsize>(safeLength));
            fileWritten = file_->good();
        }
        if (mirrorToStderr_.load(std::memory_order_acquire) || !fileWritten)
            std::fwrite(line, 1, safeLength, stderr);
        if (fileWritten)
            written_.fetch_add(1, std::memory_order_relaxed);
        else
            writeFailures_.fetch_add(1, std::memory_order_relaxed);
    }

    bool drain()
    {
        bool consumed = false;
        LogRecord record;
        while (tryDequeue(record))
        {
            writeRecord(record);
            consumed = true;
        }
        return consumed;
    }

    void run()
    {
        while (running_.load(std::memory_order_acquire))
        {
            if (drain())
            {
                if (file_ != nullptr)
                    file_->flush();
                flushedPosition_.store(
                    dequeuePosition_.load(std::memory_order_acquire),
                    std::memory_order_release);
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds {20});
            }
        }
    }

    std::array<QueueSlot, queueCapacity> slots_ {};
    std::atomic<std::size_t> enqueuePosition_ {0};
    std::atomic<std::size_t> dequeuePosition_ {0};
    std::atomic<std::size_t> flushedPosition_ {0};
    std::atomic<std::uint64_t> written_ {0};
    std::atomic<std::uint64_t> dropped_ {0};
    std::atomic<std::uint64_t> writeFailures_ {0};
    std::atomic<LogLevel> minimumLevel_ {LogLevel::off};
    std::atomic_bool mirrorToStderr_ {false};
    std::atomic_bool running_ {false};
    std::mutex configurationMutex_;
    std::string filePath_;
    std::unique_ptr<std::ofstream> file_;
    std::thread worker_;
};

LoggerState& state()
{
    static LoggerState instance;
    return instance;
}
} // namespace

bool Logger::configure(LoggerConfig config)
{
    return state().configure(std::move(config));
}

void Logger::shutdown() noexcept
{
    state().shutdown();
}

void Logger::setLevel(LogLevel level) noexcept
{
    state().setLevel(level);
}

LogLevel Logger::level() noexcept
{
    return state().level();
}

bool Logger::enabled(LogLevel level) noexcept
{
    return state().enabled(level);
}

void Logger::write(
    LogLevel level,
    std::string_view component,
    std::string_view event,
    std::string_view details) noexcept
{
    if (!enabled(level))
        return;
    LogRecord record;
    record.level = level;
    record.timestampMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    copyText(record.component, component);
    copyText(record.event, event);
    copyText(record.details, details);
    (void) state().enqueue(record);
}

void Logger::logf(
    LogLevel level,
    std::string_view component,
    std::string_view event,
    const char* format,
    ...) noexcept
{
    if (!enabled(level))
        return;
    std::array<char, detailsCapacity> details {};
    va_list arguments;
    va_start(arguments, format);
    (void) std::vsnprintf(details.data(), details.size(), format, arguments);
    va_end(arguments);
    write(level, component, event, details.data());
}

bool Logger::flush(std::chrono::milliseconds timeout) noexcept
{
    return state().flush(timeout);
}

LoggerDiagnostics Logger::diagnostics() noexcept
{
    return state().diagnostics();
}

LogLevel Logger::parseLevel(std::string_view value, LogLevel fallback) noexcept
{
    if (value == "debug") return LogLevel::debug;
    if (value == "info") return LogLevel::info;
    if (value == "warn" || value == "warning") return LogLevel::warning;
    if (value == "error") return LogLevel::error;
    if (value == "off") return LogLevel::off;
    return fallback;
}

const char* Logger::levelName(LogLevel level) noexcept
{
    switch (level)
    {
        case LogLevel::debug: return "DEBUG";
        case LogLevel::info: return "INFO";
        case LogLevel::warning: return "WARN";
        case LogLevel::error: return "ERROR";
        case LogLevel::off: return "OFF";
    }
    return "UNKNOWN";
}

} // namespace lps
