#include "core/Logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
int failures = 0;

void check(bool condition, const char* expression, int line)
{
    if (!condition)
    {
        std::cerr << __FILE__ << ':' << line << ": test assertion failed: "
                  << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)
}

int main()
{
    const auto suffix = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("lps-logger-test-" + std::to_string(suffix) + ".log");

    CHECK(lps::Logger::configure({path.string(), lps::LogLevel::debug, false}));
    lps::Logger::write(
        lps::LogLevel::info, "logger_test", "plain_record", "answer=42");
    lps::Logger::logf(
        lps::LogLevel::warning, "logger_test", "formatted_record",
        "voice_id=%u reason=%s", 7u, "test");
    CHECK(lps::Logger::flush(std::chrono::milliseconds {2000}));
    lps::Logger::shutdown();

    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    const auto text = contents.str();
    CHECK(text.find("level=INFO component=logger_test event=plain_record answer=42")
        != std::string::npos);
    CHECK(text.find("level=WARN component=logger_test event=formatted_record "
                    "voice_id=7 reason=test") != std::string::npos);
    CHECK(lps::Logger::diagnostics().written >= 2);
    CHECK(lps::Logger::diagnostics().dropped == 0);
    CHECK(lps::Logger::diagnostics().writeFailures == 0);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    if (failures == 0)
        std::cout << "Logger tests passed\n";
    return failures == 0 ? 0 : 1;
}
