#pragma once

#include "utils/result.hpp"
#include "utils/http_client.hpp"
#include <string>
#include <map>
#include <memory>

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

private:
    std::string alpha_vantage_key_;
    std::unique_ptr<HttpClient> yahoo_client_;
    std::unique_ptr<HttpClient> alpha_vantage_client_;

    /**
     * Try fetching from Yahoo Finance
     */
    Result<StockPrice> fetch_from_yahoo(const std::string& symbol);

    /**
     * Try fetching from Alpha Vantage (fallback)
     */
    Result<StockPrice> fetch_from_alpha_vantage(const std::string& symbol);
};

} // namespace ibkr::utils
