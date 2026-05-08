#pragma once

#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace ibkr::config {

/**
 * Account configuration for IBKR Flex Web Service.
 */
struct AccountConfig {
    std::string name;
    std::string token;
    std::string query_id;
    bool enabled{true};
};

/**
 * Database configuration.
 */
struct DatabaseConfig {
    std::string path;
};

/**
 * HTTP client configuration.
 */
struct HttpConfig {
    std::string user_agent;
    int timeout_seconds{30};
    int max_retries{5};
    int retry_delay_ms{2000};
};

/**
 * Flex Web Service configuration.
 */
struct FlexConfig {
    int poll_interval_seconds{5};
    int max_poll_duration_seconds{300};
};

/**
 * Logging configuration.
 */
struct LoggingConfig {
    std::string level{"info"};
    std::string file;
    int max_file_size_mb{10};
    int max_files{5};
};

/**
 * Screener configuration for option opportunity analysis.
 */
struct ScreenerConfig {
    std::vector<std::string> watchlist;
    double min_iv_percentile{30.0};
    double min_premium_yield{0.5};
    int min_dte{14};
    int max_dte{60};
    double otm_buffer_percent{5.0};
    bool allow_synthetic_options{true};
};

/**
 * Alpha Vantage API configuration for price/volatility fallback.
 */
struct AlphaVantageConfig {
    std::string api_key;
    double default_volatility{0.30};
    double risk_free_rate{0.05};
    int volatility_lookback_days{100};
    int api_call_delay_ms{12000};  // 12s for free tier (5 calls/min)
};

/**
 * Main application configuration.
 */
struct Config {
    std::vector<AccountConfig> accounts;
    DatabaseConfig database;
    HttpConfig http;
    FlexConfig flex;
    LoggingConfig logging;
    ScreenerConfig screener;
    AlphaVantageConfig alpha_vantage;
};

/**
 * Configuration manager for loading and validating config.json.
 *
 * Loads configuration from ~/.ibkr-options-analyzer/config.json by default.
 * Validates all required fields and provides helpful error messages.
 *
 * Usage:
 *   auto config_result = ConfigManager::load();
 *   if (!config_result) {
 *       std::cerr << "Config error: " << config_result.error().format() << "\n";
 *       return 1;
 *   }
 *   const auto& config = *config_result;
 */
class ConfigManager {
public:
    /**
     * Load configuration from file.
     * @param config_path Path to config.json (default: ~/.ibkr-options-analyzer/config.json)
     * @return Result containing Config or Error
     */
    static utils::Result<Config> load(const std::string& config_path = "");

    /**
     * Get default config path (~/.ibkr-options-analyzer/config.json)
     */
    static std::string get_default_config_path();

    /**
     * Expand tilde (~) in file paths to home directory
     */
    static std::string expand_path(const std::string& path);

    /**
     * Validate configuration (called automatically by load())
     */
    static utils::Result<void> validate(const Config& config);

private:
    /**
     * Parse JSON into Config struct
     */
    static utils::Result<Config> parse_json(const nlohmann::json& j);

    /**
     * Parse account configuration
     */
    static utils::Result<AccountConfig> parse_account(const nlohmann::json& j);
};

} // namespace ibkr::config
