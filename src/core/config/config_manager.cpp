#include "config_manager.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace ibkr::config {

using utils::Result;
using utils::Error;

std::string ConfigManager::get_default_config_path() {
    return expand_path("~/.ibkr-options-analyzer/config.json");
}

std::string ConfigManager::expand_path(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }

    const char* home = std::getenv("HOME");
    if (!home) {
        home = std::getenv("USERPROFILE"); // Windows fallback
    }

    if (!home) {
        return path; // Can't expand, return as-is
    }

    return std::string(home) + path.substr(1);
}

Result<Config> ConfigManager::load(const std::string& config_path) {
    std::string path = config_path.empty() ? get_default_config_path() : config_path;
    path = expand_path(path);

    utils::Logger::debug("Loading config from: {}", path);

    // Check if file exists
    if (!std::filesystem::exists(path)) {
        return Error{
            "Config file not found",
            path + " (run with --help for setup instructions)"
        };
    }

    // Read file
    std::ifstream file(path);
    if (!file.is_open()) {
        return Error{"Failed to open config file", path};
    }

    // Parse JSON
    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        return Error{
            "Failed to parse config JSON",
            std::string(e.what()),
            e.id
        };
    }

    // Parse into Config struct
    auto config_result = parse_json(j);
    if (!config_result) {
        return config_result;
    }

    // Validate
    auto validation_result = validate(*config_result);
    if (!validation_result) {
        return Error{
            "Config validation failed",
            validation_result.error().message
        };
    }

    utils::Logger::info("Config loaded successfully: {} accounts configured",
                       config_result->accounts.size());

    return config_result;
}

Result<Config> ConfigManager::parse_json(const nlohmann::json& j) {
    Config config;

    // Parse accounts (optional - can be provided via command line)
    if (j.contains("accounts")) {
        if (!j["accounts"].is_array()) {
            return Error{"Field 'accounts' must be an array"};
        }

        for (const auto& account_json : j["accounts"]) {
            auto account_result = parse_account(account_json);
            if (!account_result) {
                return Error{
                    "Failed to parse account",
                    account_result.error().message
                };
            }
            config.accounts.push_back(*account_result);
        }
    }

    // Parse database (required)
    if (!j.contains("database")) {
        return Error{"Missing required field: database"};
    }

    if (!j["database"].contains("path")) {
        return Error{"Missing required field: database.path"};
    }

    config.database.path = expand_path(j["database"]["path"].get<std::string>());

    // Parse HTTP config (optional with defaults)
    if (j.contains("http")) {
        const auto& http = j["http"];
        if (http.contains("user_agent")) {
            config.http.user_agent = http["user_agent"].get<std::string>();
        }
        if (http.contains("timeout_seconds")) {
            config.http.timeout_seconds = http["timeout_seconds"].get<int>();
        }
        if (http.contains("max_retries")) {
            config.http.max_retries = http["max_retries"].get<int>();
        }
        if (http.contains("retry_delay_ms")) {
            config.http.retry_delay_ms = http["retry_delay_ms"].get<int>();
        }
    }

    // Default user agent if not specified
    if (config.http.user_agent.empty()) {
        config.http.user_agent = "IBKROptionsAnalyzer/1.0 (personal tool)";
    }

    // Parse Flex config (optional with defaults)
    if (j.contains("flex")) {
        const auto& flex = j["flex"];
        if (flex.contains("poll_interval_seconds")) {
            config.flex.poll_interval_seconds = flex["poll_interval_seconds"].get<int>();
        }
        if (flex.contains("max_poll_duration_seconds")) {
            config.flex.max_poll_duration_seconds = flex["max_poll_duration_seconds"].get<int>();
        }
    }

    // Parse logging config (optional with defaults)
    if (j.contains("logging")) {
        const auto& logging = j["logging"];
        if (logging.contains("level")) {
            config.logging.level = logging["level"].get<std::string>();
        }
        if (logging.contains("file")) {
            config.logging.file = expand_path(logging["file"].get<std::string>());
        }
        if (logging.contains("max_file_size_mb")) {
            config.logging.max_file_size_mb = logging["max_file_size_mb"].get<int>();
        }
        if (logging.contains("max_files")) {
            config.logging.max_files = logging["max_files"].get<int>();
        }
    }

    // Default log file if not specified
    if (config.logging.file.empty()) {
        config.logging.file = expand_path("~/.ibkr-options-analyzer/logs/app.log");
    }

    // Parse screener config (optional with defaults)
    if (j.contains("screener")) {
        const auto& scr = j["screener"];
        if (scr.contains("watchlist") && scr["watchlist"].is_array()) {
            for (const auto& sym : scr["watchlist"]) {
                config.screener.watchlist.push_back(sym.get<std::string>());
            }
        }
        if (scr.contains("min_iv_percentile")) {
            config.screener.min_iv_percentile = scr["min_iv_percentile"].get<double>();
        }
        if (scr.contains("min_premium_yield")) {
            config.screener.min_premium_yield = scr["min_premium_yield"].get<double>();
        }
        if (scr.contains("min_dte")) {
            config.screener.min_dte = scr["min_dte"].get<int>();
        }
        if (scr.contains("max_dte")) {
            config.screener.max_dte = scr["max_dte"].get<int>();
        }
        if (scr.contains("otm_buffer_percent")) {
            config.screener.otm_buffer_percent = scr["otm_buffer_percent"].get<double>();
        }
        if (scr.contains("allow_synthetic_options")) {
            config.screener.allow_synthetic_options = scr["allow_synthetic_options"].get<bool>();
        }
    }

    // Parse Alpha Vantage config (optional with defaults)
    if (j.contains("alpha_vantage")) {
        const auto& av = j["alpha_vantage"];
        if (av.contains("api_key")) {
            config.alpha_vantage.api_key = av["api_key"].get<std::string>();
        }
        if (av.contains("default_volatility")) {
            config.alpha_vantage.default_volatility = av["default_volatility"].get<double>();
        }
        if (av.contains("risk_free_rate")) {
            config.alpha_vantage.risk_free_rate = av["risk_free_rate"].get<double>();
        }
        if (av.contains("volatility_lookback_days")) {
            config.alpha_vantage.volatility_lookback_days = av["volatility_lookback_days"].get<int>();
        }
        if (av.contains("api_call_delay_ms")) {
            config.alpha_vantage.api_call_delay_ms = av["api_call_delay_ms"].get<int>();
        }
    }

    return config;
}

Result<AccountConfig> ConfigManager::parse_account(const nlohmann::json& j) {
    AccountConfig account;

    // Name (required)
    if (!j.contains("name")) {
        return Error{"Account missing required field: name"};
    }
    account.name = j["name"].get<std::string>();

    // Token (required)
    if (!j.contains("token")) {
        return Error{"Account '" + account.name + "' missing required field: token"};
    }
    account.token = j["token"].get<std::string>();

    // Query ID (required)
    if (!j.contains("query_id")) {
        return Error{"Account '" + account.name + "' missing required field: query_id"};
    }
    account.query_id = j["query_id"].get<std::string>();

    // Enabled (optional, default true)
    if (j.contains("enabled")) {
        account.enabled = j["enabled"].get<bool>();
    }

    return account;
}

Result<void> ConfigManager::validate(const Config& config) {
    // Accounts are optional (can be provided via command line)
    // If accounts are configured, validate them
    if (!config.accounts.empty()) {
        // Validate at least one enabled account
        bool has_enabled = false;
        for (const auto& account : config.accounts) {
            if (account.enabled) {
                has_enabled = true;
                break;
            }
        }

        if (!has_enabled) {
            return Error{"No enabled accounts found"};
        }

        // Validate account fields
        for (const auto& account : config.accounts) {
            if (account.name.empty()) {
                return Error{"Account has empty name"};
            }
            if (account.token.empty()) {
                return Error{"Account '" + account.name + "' has empty token"};
            }
            if (account.query_id.empty()) {
                return Error{"Account '" + account.name + "' has empty query_id"};
            }
        }
    }

    // Validate database path
    if (config.database.path.empty()) {
        return Error{"Database path is empty"};
    }

    // Validate HTTP config
    if (config.http.timeout_seconds <= 0) {
        return Error{"HTTP timeout must be positive"};
    }
    if (config.http.max_retries < 0) {
        return Error{"HTTP max_retries cannot be negative"};
    }
    if (config.http.retry_delay_ms <= 0) {
        return Error{"HTTP retry_delay_ms must be positive"};
    }

    // Validate Flex config
    if (config.flex.poll_interval_seconds <= 0) {
        return Error{"Flex poll_interval_seconds must be positive"};
    }
    if (config.flex.max_poll_duration_seconds <= 0) {
        return Error{"Flex max_poll_duration_seconds must be positive"};
    }

    // Validate logging config
    if (config.logging.max_file_size_mb <= 0) {
        return Error{"Logging max_file_size_mb must be positive"};
    }
    if (config.logging.max_files <= 0) {
        return Error{"Logging max_files must be positive"};
    }

    return Result<void>{};
}

} // namespace ibkr::config
