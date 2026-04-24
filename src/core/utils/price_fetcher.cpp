#include "price_fetcher.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <map>

namespace ibkr::utils {

// Map IBKR symbols to Yahoo Finance symbols
static const std::map<std::string, std::string> SYMBOL_MAPPING = {
    {"BRKB", "BRK-B"},
    {"BRKA", "BRK-A"},
    {"BRK.B", "BRK-B"},
    {"BRK.A", "BRK-A"}
};

// Hong Kong stocks on IBKR are numeric-only (e.g., 1299, 388, 700)
// Yahoo Finance uses .HK suffix
static std::string map_symbol_to_yahoo(const std::string& symbol) {
    // Skip obvious header rows
    if (symbol == "UnderlyingSymbol" || symbol == "Symbol" || symbol.empty()) {
        return "";  // Invalid - will be filtered out
    }

    // Check explicit mapping first
    auto it = SYMBOL_MAPPING.find(symbol);
    if (it != SYMBOL_MAPPING.end()) {
        return it->second;
    }

    // Hong Kong stocks: numeric-only symbols need .HK suffix
    // (IBKR HK tickers are plain numbers like 1299, 388, 700)
    bool is_numeric = !symbol.empty();
    for (char c : symbol) {
        if (!std::isdigit(c)) {
            is_numeric = false;
            break;
        }
    }
    if (is_numeric && symbol.length() <= 5) {
        return symbol + ".HK";
    }

    return symbol;
}

PriceFetcher::PriceFetcher() {
    // Yahoo Finance API (unofficial, no key required)
    yahoo_client_ = std::make_unique<HttpClient>(
        "https://query1.finance.yahoo.com",
        "IBKROptionsAnalyzer/1.0",
        10,  // 10 second timeout
        3,   // 3 retries
        1000 // 1 second retry delay
    );

    // Alpha Vantage (requires API key)
    alpha_vantage_client_ = std::make_unique<HttpClient>(
        "https://www.alphavantage.co",
        "IBKROptionsAnalyzer/1.0",
        10,
        3,
        1000
    );
}

void PriceFetcher::set_alpha_vantage_key(const std::string& api_key) {
    alpha_vantage_key_ = api_key;
}

Result<StockPrice> PriceFetcher::fetch_price(const std::string& symbol) {
    Logger::debug("Fetching price for {}", symbol);

    // Try Yahoo Finance first (no API key required)
    auto yahoo_result = fetch_from_yahoo(symbol);
    if (yahoo_result) {
        Logger::debug("Got price from Yahoo: {} = ${}", symbol, yahoo_result->price);
        return yahoo_result;
    }

    Logger::debug("Yahoo fetch failed: {}", yahoo_result.error().message);

    // Fallback to Alpha Vantage if API key is set
    if (!alpha_vantage_key_.empty()) {
        auto av_result = fetch_from_alpha_vantage(symbol);
        if (av_result) {
            Logger::debug("Got price from Alpha Vantage: {} = ${}", symbol, av_result->price);
            return av_result;
        }
        Logger::debug("Alpha Vantage fetch failed: {}", av_result.error().message);
    }

    return Error{
        "Failed to fetch price for " + symbol,
        "All price sources failed"
    };
}

std::map<std::string, StockPrice> PriceFetcher::fetch_prices(const std::vector<std::string>& symbols) {
    std::map<std::string, StockPrice> prices;

    for (const auto& symbol : symbols) {
        auto yahoo_sym = map_symbol_to_yahoo(symbol);
        if (yahoo_sym.empty()) {
            Logger::debug("Skipping invalid symbol: {}", symbol);
            continue;
        }
        auto result = fetch_price(symbol);
        if (result) {
            prices[symbol] = *result;
        } else {
            Logger::warn("Failed to fetch price for {}: {}", symbol, result.error().message);
        }
    }

    return prices;
}

Result<StockPrice> PriceFetcher::fetch_from_yahoo(const std::string& symbol) {
    // Map symbol to Yahoo Finance format
    std::string yahoo_symbol = map_symbol_to_yahoo(symbol);

    Logger::debug("Fetching from Yahoo: {} (mapped from {})", yahoo_symbol, symbol);

    // Yahoo Finance v8 API endpoint
    std::string path = "/v8/finance/chart/" + yahoo_symbol;
    std::map<std::string, std::string> params = {
        {"interval", "1d"},
        {"range", "1d"}
    };

    auto response = yahoo_client_->get(path, params);
    if (!response) {
        return Error{
            "Yahoo Finance request failed",
            response.error().message
        };
    }

    if (response->status_code != 200) {
        return Error{
            "Yahoo Finance returned error",
            "HTTP " + std::to_string(response->status_code)
        };
    }

    // Parse JSON response
    try {
        auto json = nlohmann::json::parse(response->body);

        if (!json.contains("chart") || !json["chart"].contains("result") ||
            json["chart"]["result"].empty()) {
            return Error{"Invalid Yahoo Finance response", "Missing chart data"};
        }

        auto result = json["chart"]["result"][0];

        if (!result.contains("meta") || !result["meta"].contains("regularMarketPrice")) {
            return Error{"Invalid Yahoo Finance response", "Missing price data"};
        }

        double price = result["meta"]["regularMarketPrice"].get<double>();

        // Get timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

        StockPrice stock_price;
        stock_price.symbol = symbol;  // Use original symbol
        stock_price.price = price;
        stock_price.timestamp = ss.str();
        stock_price.source = "yahoo";

        return stock_price;

    } catch (const nlohmann::json::exception& e) {
        return Error{
            "Failed to parse Yahoo Finance response",
            std::string(e.what())
        };
    }
}

Result<StockPrice> PriceFetcher::fetch_from_alpha_vantage(const std::string& symbol) {
    if (alpha_vantage_key_.empty()) {
        return Error{"Alpha Vantage API key not set"};
    }

    std::string path = "/query";
    std::map<std::string, std::string> params = {
        {"function", "GLOBAL_QUOTE"},
        {"symbol", symbol},
        {"apikey", alpha_vantage_key_}
    };

    auto response = alpha_vantage_client_->get(path, params);
    if (!response) {
        return Error{
            "Alpha Vantage request failed",
            response.error().message
        };
    }

    if (response->status_code != 200) {
        return Error{
            "Alpha Vantage returned error",
            "HTTP " + std::to_string(response->status_code)
        };
    }

    // Parse JSON response
    try {
        auto json = nlohmann::json::parse(response->body);

        if (!json.contains("Global Quote")) {
            return Error{"Invalid Alpha Vantage response", "Missing Global Quote"};
        }

        auto quote = json["Global Quote"];

        if (!quote.contains("05. price")) {
            return Error{"Invalid Alpha Vantage response", "Missing price"};
        }

        double price = std::stod(quote["05. price"].get<std::string>());

        // Get timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");

        StockPrice stock_price;
        stock_price.symbol = symbol;
        stock_price.price = price;
        stock_price.timestamp = ss.str();
        stock_price.source = "alphavantage";

        return stock_price;

    } catch (const std::exception& e) {
        return Error{
            "Failed to parse Alpha Vantage response",
            std::string(e.what())
        };
    }
}

} // namespace ibkr::utils
