#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace lps
{

enum class LogLevel : std::uint8_t
{
    debug,
    info,
    warning,
    error,
    off
};

struct LoggerConfig
{
    std::string filePath;
    LogLevel level = LogLevel::info;
    bool mirrorToStderr = false;
};

struct LoggerDiagnostics
{
    std::uint64_t written = 0;
    std::uint64_t dropped = 0;
    std::uint64_t writeFailures = 0;
};

// Process-wide asynchronous logger. Calls to write/logf never take a lock,
// allocate, or touch the filesystem, so they are safe to use from audio paths.
// A bounded queue deliberately drops records instead of blocking when a reader
// cannot keep up.
class Logger final
{
public:
    static bool configure(LoggerConfig config);
    static void shutdown() noexcept;

    static void setLevel(LogLevel level) noexcept;
    [[nodiscard]] static LogLevel level() noexcept;
    [[nodiscard]] static bool enabled(LogLevel level) noexcept;

    static void write(
        LogLevel level,
        std::string_view component,
        std::string_view event,
        std::string_view details = {}) noexcept;
    static void logf(
        LogLevel level,
        std::string_view component,
        std::string_view event,
        const char* format,
        ...) noexcept;

    // Intended for tests and orderly shutdown, never for the audio thread.
    [[nodiscard]] static bool flush(
        std::chrono::milliseconds timeout = std::chrono::milliseconds {1000}) noexcept;
    [[nodiscard]] static LoggerDiagnostics diagnostics() noexcept;

    [[nodiscard]] static LogLevel parseLevel(
        std::string_view value,
        LogLevel fallback = LogLevel::info) noexcept;
    [[nodiscard]] static const char* levelName(LogLevel level) noexcept;
};

} // namespace lps
