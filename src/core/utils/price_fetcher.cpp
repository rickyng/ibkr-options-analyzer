#include "price_fetcher.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <map>
#include <thread>
#include <algorithm>
#include <cmath>

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

void PriceFetcher::set_alpha_vantage_config(
    const std::string& api_key,
    double default_volatility,
    double risk_free_rate,
    int volatility_lookback_days,
    int api_call_delay_ms) {
    alpha_vantage_key_ = api_key;
    default_volatility_ = default_volatility;
    risk_free_rate_ = risk_free_rate;
    volatility_lookback_days_ = volatility_lookback_days;
    api_call_delay_ms_ = api_call_delay_ms;
}

void PriceFetcher::set_allow_synthetic_options(bool allow) {
    allow_synthetic_options_ = allow;
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

Result<OptionChainData> PriceFetcher::fetch_option_chain(const std::string& symbol) {
    Logger::debug("Fetching option chain for {}", symbol);

    // Try Yahoo Finance first
    auto yahoo_result = fetch_option_chain_from_yahoo(symbol);
    if (yahoo_result) {
        return yahoo_result;
    }

    Logger::warn("Yahoo Finance option chain failed for {}: {}",
                symbol, yahoo_result.error().message);

    // Fallback to synthetic chain if allowed and Alpha Vantage key is set
    if (allow_synthetic_options_ && !alpha_vantage_key_.empty()) {
        Logger::info("Generating synthetic option chain for {} using Alpha Vantage", symbol);

        auto price_result = fetch_price(symbol);
        if (!price_result) {
            return Error{
                "Cannot generate synthetic option chain for " + symbol,
                "Failed to fetch price: " + price_result.error().message
            };
        }

        double current_price = price_result->price;
        auto vol_result = fetch_volatility(symbol);
        double volatility = vol_result ? *vol_result : default_volatility_;

        auto chain = generate_synthetic_option_chain(symbol, current_price, volatility);
        Logger::info("Using synthetic option chain for {} (price=${:.2f}, vol={:.1f}%)",
                    symbol, current_price, volatility * 100.0);
        return chain;
    }

    return Error{
        "Failed to fetch option chain for " + symbol,
        "Yahoo Finance blocked and synthetic options not enabled"
    };
}

Result<OptionChainData> PriceFetcher::fetch_option_chain_from_yahoo(const std::string& symbol) {
    std::string yahoo_symbol = map_symbol_to_yahoo(symbol);

    std::string path = "/v7/finance/options/" + yahoo_symbol;

    // Try v7 endpoint first
    auto response = yahoo_client_->get(path);
    if (response && response->status_code == 200) {
        try {
            auto json = nlohmann::json::parse(response->body);

            OptionChainData chain;
            chain.symbol = symbol;

            if (json.contains("optionChain") &&
                json["optionChain"].contains("result") &&
                !json["optionChain"]["result"].empty()) {

                auto result = json["optionChain"]["result"][0];

                if (result.contains("expirationDates")) {
                    auto now = std::chrono::system_clock::now();
                    auto now_time = std::chrono::system_clock::to_time_t(now);
                    for (const auto& ts : result["expirationDates"]) {
                        auto exp_time = static_cast<time_t>(ts.get<int64_t>());
                        int dte = static_cast<int>((exp_time - now_time) / 86400);
                        if (dte < 0) continue;

                        std::stringstream ss;
                        ss << std::put_time(std::gmtime(&exp_time), "%Y-%m-%d");

                        OptionExpiry oe;
                        oe.expiry_date = ss.str();
                        oe.dte = dte;
                        chain.expirations.push_back(oe);
                    }
                }

                if (result.contains("options") && !result["options"].empty()) {
                    for (const auto& opt_group : result["options"]) {
                        if (opt_group.contains("puts")) {
                            for (const auto& put : opt_group["puts"]) {
                                std::string expiry = put.value("expiration", "");
                                if (expiry.empty() && put.contains("expirationDate")) {
                                    auto exp_time = static_cast<time_t>(put["expirationDate"].get<int64_t>());
                                    std::stringstream ss;
                                    ss << std::put_time(std::gmtime(&exp_time), "%Y-%m-%d");
                                    expiry = ss.str();
                                }

                                OptionStrike strike;
                                strike.strike = put.value("strike", 0.0);
                                strike.bid = put.value("bid", 0.0);
                                strike.ask = put.value("ask", 0.0);
                                strike.last = put.value("lastPrice", 0.0);
                                strike.iv = put.value("impliedVolatility", 0.0);

                                chain.puts_by_expiry[expiry].push_back(strike);
                            }
                        }
                    }
                }

                if (!chain.expirations.empty() || !chain.puts_by_expiry.empty()) {
                    Logger::debug("Fetched option chain for {}: {} expirations, {} put strikes",
                                  symbol, chain.expirations.size(), chain.puts_by_expiry.size());
                    return chain;
                }
            }
        } catch (const nlohmann::json::exception&) {
            // Fall through to per-expiry approach
        }
    }

    // Fallback: try v7 with specific expiry dates to avoid 401 on the base endpoint
    Logger::debug("v7 base endpoint blocked for {}, trying per-expiry fetches", symbol);
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);

    OptionChainData chain;
    chain.symbol = symbol;

    // Generate standard option expirations (Fridays) for the next 2 months
    for (int days_out : {14, 21, 30, 45, 60}) {
        time_t target = now_time + static_cast<time_t>(days_out) * 86400;
        struct tm* t = std::gmtime(&target);
        int days_to_friday = (5 - t->tm_wday + 7) % 7;
        if (days_to_friday == 0 && t->tm_wday != 5) days_to_friday = 7;
        time_t expiry_time = target + static_cast<time_t>(days_to_friday) * 86400;

        int dte = static_cast<int>((expiry_time - now_time) / 86400);
        std::stringstream exp_ss;
        exp_ss << std::put_time(std::gmtime(&expiry_time), "%Y-%m-%d");

        OptionExpiry oe;
        oe.expiry_date = exp_ss.str();
        oe.dte = dte;
        chain.expirations.push_back(oe);
    }

    // Try fetching v7 option chain for each expiration with the date parameter
    for (const auto& exp : chain.expirations) {
        if (exp.dte < 14 || exp.dte > 60) continue;

        struct tm tm_val = {};
        std::istringstream iss(exp.expiry_date);
        iss >> std::get_time(&tm_val, "%Y-%m-%d");
        if (iss.fail()) continue;
        time_t exp_ts = timegm(&tm_val);

        std::string dated_path = "/v7/finance/options/" + yahoo_symbol +
            "?date=" + std::to_string(static_cast<int64_t>(exp_ts));
        auto exp_response = yahoo_client_->get(dated_path);
        if (!exp_response || exp_response->status_code != 200) continue;

        try {
            auto exp_json = nlohmann::json::parse(exp_response->body);
            if (!exp_json.contains("optionChain") ||
                !exp_json["optionChain"].contains("result") ||
                exp_json["optionChain"]["result"].empty()) continue;

            auto& v7_result = exp_json["optionChain"]["result"][0];
            if (v7_result.contains("options") && !v7_result["options"].empty()) {
                for (const auto& opt_group : v7_result["options"]) {
                    if (opt_group.contains("puts")) {
                        for (const auto& put : opt_group["puts"]) {
                            OptionStrike strike;
                            strike.strike = put.value("strike", 0.0);
                            strike.bid = put.value("bid", 0.0);
                            strike.ask = put.value("ask", 0.0);
                            strike.last = put.value("lastPrice", 0.0);
                            strike.iv = put.value("impliedVolatility", 0.0);
                            chain.puts_by_expiry[exp.expiry_date].push_back(strike);
                        }
                    }
                }
            }
        } catch (const nlohmann::json::exception&) {
            continue;
        }
    }

    if (chain.puts_by_expiry.empty()) {
        return Error{"Yahoo Finance blocked option chain access for " + symbol,
                     "v7 API returned 401 for all endpoints"};
    }

    Logger::debug("Fetched option chain for {}: {} expirations, {} put strike groups",
                  symbol, chain.expirations.size(), chain.puts_by_expiry.size());
    return chain;
}

// --- Black-Scholes helpers ---

double PriceFetcher::normal_cdf(double x) {
    return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

double PriceFetcher::black_scholes_put(double S, double K, double T, double r, double sigma) {
    if (T <= 0 || S <= 0 || K <= 0 || sigma <= 0) return 0.0;

    double sqrtT = std::sqrt(T);
    double d1 = (std::log(S / K) + (r + sigma * sigma / 2.0) * T) / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;

    double put_price = K * std::exp(-r * T) * normal_cdf(-d2) - S * normal_cdf(-d1);
    return std::max(0.0, put_price);
}

// --- Alpha Vantage historical data and volatility ---

void PriceFetcher::rate_limit_if_needed() {
    if (api_call_delay_ms_ > 0) {
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_av_call_).count();
        if (elapsed < api_call_delay_ms_) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(api_call_delay_ms_ - static_cast<long long>(elapsed)));
        }
        last_av_call_ = std::chrono::system_clock::now();
    }
}

Result<std::vector<double>> PriceFetcher::fetch_historical_prices_from_alpha_vantage(
    const std::string& symbol) {

    if (alpha_vantage_key_.empty()) {
        return Error{"Alpha Vantage API key not set"};
    }

    rate_limit_if_needed();

    std::string path = "/query";
    std::map<std::string, std::string> params = {
        {"function", "TIME_SERIES_DAILY"},
        {"symbol", symbol},
        {"outputsize", "compact"},
        {"apikey", alpha_vantage_key_}
    };

    auto response = alpha_vantage_client_->get(path, params);
    if (!response) {
        return Error{"Alpha Vantage TIME_SERIES_DAILY request failed",
                     response.error().message};
    }

    if (response->status_code != 200) {
        return Error{"Alpha Vantage returned error",
                     "HTTP " + std::to_string(response->status_code)};
    }

    try {
        auto json = nlohmann::json::parse(response->body);

        // Check for rate limit / informational messages
        if (json.contains("Note")) {
            return Error{"Alpha Vantage API rate limit", json["Note"].get<std::string>()};
        }
        if (json.contains("Information")) {
            return Error{"Alpha Vantage API limit", json["Information"].get<std::string>()};
        }

        if (!json.contains("Time Series (Daily)")) {
            return Error{"Invalid Alpha Vantage response", "Missing Time Series (Daily)"};
        }

        auto& time_series = json["Time Series (Daily)"];
        std::vector<std::pair<std::string, double>> dated_prices;
        for (auto& [date, data] : time_series.items()) {
            if (data.contains("4. close")) {
                double close = std::stod(data["4. close"].get<std::string>());
                dated_prices.push_back({date, close});
            }
        }

        // Sort by date ascending (oldest first)
        std::sort(dated_prices.begin(), dated_prices.end());

        // Take last N days
        std::vector<double> closes;
        size_t start = dated_prices.size() > static_cast<size_t>(volatility_lookback_days_)
            ? dated_prices.size() - volatility_lookback_days_
            : 0;
        for (size_t i = start; i < dated_prices.size(); ++i) {
            closes.push_back(dated_prices[i].second);
        }

        Logger::debug("Fetched {} historical prices for {}", closes.size(), symbol);
        return closes;

    } catch (const std::exception& e) {
        return Error{"Failed to parse Alpha Vantage TIME_SERIES_DAILY",
                     std::string(e.what())};
    }
}

double PriceFetcher::calculate_historical_volatility(const std::vector<double>& prices) {
    if (prices.size() < 2) return default_volatility_;

    std::vector<double> log_returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i - 1] > 0 && prices[i] > 0) {
            log_returns.push_back(std::log(prices[i] / prices[i - 1]));
        }
    }

    if (log_returns.empty()) return default_volatility_;

    double sum = 0.0;
    for (double r : log_returns) sum += r;
    double mean = sum / static_cast<double>(log_returns.size());

    double sq_sum = 0.0;
    for (double r : log_returns) {
        sq_sum += (r - mean) * (r - mean);
    }
    double stddev = std::sqrt(sq_sum / static_cast<double>(log_returns.size() - 1));

    // Annualize (252 trading days)
    return stddev * std::sqrt(252.0);
}

Result<double> PriceFetcher::fetch_volatility(const std::string& symbol) {
    auto now = std::chrono::system_clock::now();

    // Check cache (valid for 24 hours)
    auto cache_it = volatility_cache_.find(symbol);
    if (cache_it != volatility_cache_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::hours>(
            now - cache_it->second.cached_at).count();
        if (age < 24) {
            Logger::debug("Using cached volatility for {}: {:.2f}%",
                         symbol, cache_it->second.volatility * 100.0);
            return cache_it->second.volatility;
        }
    }

    // Try Alpha Vantage historical data
    if (!alpha_vantage_key_.empty()) {
        auto prices_result = fetch_historical_prices_from_alpha_vantage(symbol);
        if (prices_result && prices_result->size() >= 10) {
            double vol = calculate_historical_volatility(*prices_result);

            VolatilityCache cache;
            cache.volatility = vol;
            cache.cached_at = now;
            cache.source = "alphavantage";
            volatility_cache_[symbol] = cache;

            Logger::info("Calculated historical volatility for {}: {:.2f}%",
                        symbol, vol * 100.0);
            return vol;
        }
        if (prices_result) {
            Logger::warn("Insufficient historical data for {} ({}/10 min)", symbol, prices_result->size());
        } else {
            Logger::warn("Failed to fetch historical data for {}: {}",
                        symbol, prices_result.error().message);
        }
    }

    // Fallback to default volatility
    Logger::warn("Using default volatility {:.0f}% for {}",
                default_volatility_ * 100.0, symbol);

    VolatilityCache cache;
    cache.volatility = default_volatility_;
    cache.cached_at = now;
    cache.source = "default";
    volatility_cache_[symbol] = cache;

    return default_volatility_;
}

OptionChainData PriceFetcher::generate_synthetic_option_chain(
    const std::string& symbol,
    double current_price,
    double volatility) {

    OptionChainData chain;
    chain.symbol = symbol;

    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);

    // Generate expirations: standard Fridays for ~14-60 DTE
    for (int target_dte : {14, 21, 28, 35, 42, 49, 56, 60}) {
        time_t target = now_time + static_cast<time_t>(target_dte) * 86400;
        struct tm* t = std::gmtime(&target);

        // Find nearest Friday
        int days_to_friday = (5 - t->tm_wday + 7) % 7;
        if (days_to_friday == 0 && t->tm_wday != 5) days_to_friday = 7;
        time_t expiry_time = target + static_cast<time_t>(days_to_friday) * 86400;

        int actual_dte = static_cast<int>((expiry_time - now_time) / 86400);

        std::stringstream ss;
        ss << std::put_time(std::gmtime(&expiry_time), "%Y-%m-%d");

        OptionExpiry oe;
        oe.expiry_date = ss.str();
        oe.dte = actual_dte;
        chain.expirations.push_back(oe);
    }

    // Adaptive strike increment based on price
    double strike_increment;
    if (current_price < 25.0) strike_increment = 1.0;
    else if (current_price < 100.0) strike_increment = 2.5;
    else if (current_price < 250.0) strike_increment = 5.0;
    else strike_increment = 10.0;

    // Generate strikes from 80% to 110% of current price
    double min_strike = std::floor(current_price * 0.80 / strike_increment) * strike_increment;
    double max_strike = std::ceil(current_price * 1.10 / strike_increment) * strike_increment;

    for (const auto& exp : chain.expirations) {
        double T = static_cast<double>(exp.dte) / 365.0;

        std::vector<OptionStrike> strikes;
        for (double strike = min_strike; strike <= max_strike; strike += strike_increment) {
            double put_price = black_scholes_put(
                current_price, strike, T, risk_free_rate_, volatility);

            // Add volatility skew: OTM puts have higher IV
            // Typical skew: IV increases ~1% for each 1% OTM
            double otm_pct = (current_price - strike) / current_price;
            double skewed_iv = volatility * (1.0 + 0.1 * std::max(0.0, otm_pct));

            OptionStrike os;
            os.strike = strike;
            os.last = put_price;
            os.bid = put_price * 0.95;
            os.ask = put_price * 1.05;
            os.iv = skewed_iv;
            strikes.push_back(os);
        }

        chain.puts_by_expiry[exp.expiry_date] = strikes;
    }

    Logger::info("Generated synthetic option chain for {}: {} expirations, {} strikes per expiry",
                symbol, chain.expirations.size(),
                chain.puts_by_expiry.empty() ? 0 : chain.puts_by_expiry.begin()->second.size());

    return chain;
}

} // namespace ibkr::utils
