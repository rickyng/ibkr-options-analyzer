#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

namespace ibkr::utils {

/**
 * Logger wrapper around spdlog with colored console output and file rotation.
 *
 * Provides a centralized logging interface for the entire application.
 * Supports multiple log levels, colored console output, and rotating file logs.
 *
 * Usage:
 *   Logger::init("~/.ibkr-options-analyzer/logs/app.log", "info");
 *   Logger::info("Processing account: {}", account_name);
 *   Logger::error("Failed to download: {}", error.format());
 */
class Logger {
public:
    // Initialize logger with file path and log level
    static void init(const std::string& log_file_path,
                    const std::string& log_level = "info",
                    size_t max_file_size_mb = 10,
                    size_t max_files = 5,
                    bool use_stderr = false);

    // Set log level at runtime
    static void set_level(const std::string& level);

    // Logging methods
    template<typename... Args>
    static void trace(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::trace(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void debug(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void info(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void critical(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::critical(fmt, std::forward<Args>(args)...);
    }

    // Flush logs to disk
    static void flush();

    // Shutdown logger (call before exit)
    static void shutdown();

private:
    static bool initialized_;
};

} // namespace ibkr::utils
