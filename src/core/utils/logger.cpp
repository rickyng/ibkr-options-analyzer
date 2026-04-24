#include "logger.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <filesystem>
#include <iostream>

namespace ibkr::utils {

bool Logger::initialized_ = false;

void Logger::init(const std::string& log_file_path,
                 const std::string& log_level,
                 size_t max_file_size_mb,
                 size_t max_files,
                 bool use_stderr) {
    if (initialized_) {
        return;
    }

    try {
        // Expand tilde in path
        std::string expanded_path = log_file_path;
        if (!expanded_path.empty() && expanded_path[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home) {
                expanded_path = std::string(home) + expanded_path.substr(1);
            }
        }

        // Create log directory if it doesn't exist
        std::filesystem::path log_path(expanded_path);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path());
        }

        // Create console sink with colors (stdout or stderr)
        spdlog::sink_ptr console_sink;
        if (use_stderr) {
            console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        } else {
            console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        }
        console_sink->set_level(spdlog::level::info);
        console_sink->set_pattern("[%^%l%$] %v");

        // Create rotating file sink
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            expanded_path,
            max_file_size_mb * 1024 * 1024,
            max_files
        );
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");

        // Create multi-sink logger
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("ibkr", sinks.begin(), sinks.end());

        // Set log level
        if (log_level == "trace") {
            logger->set_level(spdlog::level::trace);
        } else if (log_level == "debug") {
            logger->set_level(spdlog::level::debug);
        } else if (log_level == "info") {
            logger->set_level(spdlog::level::info);
        } else if (log_level == "warn") {
            logger->set_level(spdlog::level::warn);
        } else if (log_level == "error") {
            logger->set_level(spdlog::level::err);
        } else {
            logger->set_level(spdlog::level::info);
        }

        // Register as default logger
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::err);

        initialized_ = true;

        spdlog::info("Logger initialized: {}", expanded_path);

    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << "\n";
        // Fallback to console-only logging
        auto fallback_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("ibkr", fallback_sink);
        spdlog::set_default_logger(logger);
        initialized_ = true;
    }
}

void Logger::set_level(const std::string& level) {
    if (!initialized_) {
        return;
    }

    auto logger = spdlog::default_logger();
    if (level == "trace") {
        logger->set_level(spdlog::level::trace);
    } else if (level == "debug") {
        logger->set_level(spdlog::level::debug);
    } else if (level == "info") {
        logger->set_level(spdlog::level::info);
    } else if (level == "warn") {
        logger->set_level(spdlog::level::warn);
    } else if (level == "error") {
        logger->set_level(spdlog::level::err);
    }
}

void Logger::flush() {
    if (initialized_) {
        spdlog::default_logger()->flush();
    }
}

void Logger::shutdown() {
    if (initialized_) {
        spdlog::shutdown();
        initialized_ = false;
    }
}

} // namespace ibkr::utils
