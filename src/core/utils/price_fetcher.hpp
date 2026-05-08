#pragma once

#include "utils/result.hpp"
#include "utils/http_client.hpp"
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <chrono>

namespace ibkr::utils {

/**
 * Stock price information
 */
struct StockPrice {
    std::string symbol;
    double price;
    std::string timestamp;
    std::string source;  // "yahoo", "alphavantage", etc.
};

struct OptionExpiry {
    std::string expiry_date;
    int dte{0};
};

struct OptionStrike {
    double strike{0.0};
    double bid{0.0};
    double ask{0.0};
    double last{0.0};
    double iv{0.0};
};

struct OptionChainData {
    std::string symbol;
    std::vector<OptionExpiry> expirations;
    std::map<std::string, std::vector<OptionStrike>> puts_by_expiry;  // expiry → strikes
};

struct VolatilityCache {
    double volatility{0.0};
    std::chrono::system_clock::time_point cached_at;
    std::string source;  // "alphavantage" or "default"
};

/**
 * Fetches real-time stock prices from free APIs
 *
 * Tries multiple sources in order:
 * 1. Yahoo Finance (no API key required)
 * 2. Alpha Vantage (requires API key)
 */
class PriceFetcher {
public:
    PriceFetcher();

    /**
     * Fetch current stock price for a symbol
     * @param symbol Stock symbol (e.g., "AAPL")
     * @return Result containing StockPrice or Error
     */
    Result<StockPrice> fetch_price(const std::string& symbol);

    /**
     * Fetch multiple stock prices (batched if possible)
     * @param symbols List of stock symbols
     * @return Map of symbol to StockPrice (missing symbols are skipped)
     */
    std::map<std::string, StockPrice> fetch_prices(const std::vector<std::string>& symbols);

    /**
     * Set Alpha Vantage API key (optional, for fallback)
     */
    void set_alpha_vantage_key(const std::string& api_key);

    /**
     * Set full Alpha Vantage configuration
     */
    void set_alpha_vantage_config(const std::string& api_key,
                                   double default_volatility,
                                   double risk_free_rate,
                                   int volatility_lookback_days,
                                   int api_call_delay_ms);

    /**
     * Enable or disable synthetic option chain generation
     */
    void set_allow_synthetic_options(bool allow);

    /**
     * Fetch option chain data for a symbol.
     */
    Result<OptionChainData> fetch_option_chain(const std::string& symbol);

    /**
     * Fetch historical volatility for a symbol.
     */
    Result<double> fetch_volatility(const std::string& symbol);

private:
    std::string alpha_vantage_key_;
    std::unique_ptr<HttpClient> yahoo_client_;
    std::unique_ptr<HttpClient> alpha_vantage_client_;

    double default_volatility_{0.30};
    double risk_free_rate_{0.05};
    int volatility_lookback_days_{100};
    int api_call_delay_ms_{1000};
    bool allow_synthetic_options_{false};

    std::map<std::string, VolatilityCache> volatility_cache_;
    std::chrono::system_clock::time_point last_av_call_;

    /**
     * Try fetching from Yahoo Finance
     */
    Result<StockPrice> fetch_from_yahoo(const std::string& symbol);

    /**
     * Try fetching from Alpha Vantage (fallback)
     */
    Result<StockPrice> fetch_from_alpha_vantage(const std::string& symbol);

    Result<OptionChainData> fetch_option_chain_from_yahoo(const std::string& symbol);

    // Black-Scholes helpers
    static double normal_cdf(double x);
    static double black_scholes_put(double S, double K, double T, double r, double sigma);

    // Historical volatility estimation
    Result<std::vector<double>> fetch_historical_prices_from_alpha_vantage(const std::string& symbol);
    double calculate_historical_volatility(const std::vector<double>& prices);

    // Synthetic option chain generation
    OptionChainData generate_synthetic_option_chain(
        const std::string& symbol,
        double current_price,
        double volatility);

    // Rate limiting
    void rate_limit_if_needed();
};

} // namespace ibkr::utils
