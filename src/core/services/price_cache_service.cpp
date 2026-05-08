#include "price_cache_service.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <set>

namespace ibkr::services {

using utils::Logger;
using utils::Result;
using utils::Error;

// US market holidays for 2025-2027 (NYSE observed dates)
static const std::set<std::string> US_MARKET_HOLIDAYS = {
    // 2025
    "2025-01-01", "2025-01-20", "2025-02-17", "2025-04-18",
    "2025-05-26", "2025-06-19", "2025-07-04", "2025-09-01",
    "2025-11-27", "2025-12-25",
    // 2026
    "2026-01-01", "2026-01-19", "2026-02-16", "2026-04-03",
    "2026-05-25", "2026-06-19", "2026-07-03", "2026-09-07",
    "2026-11-26", "2026-12-25",
    // 2027
    "2027-01-01", "2027-01-18", "2027-02-15", "2027-03-26",
    "2027-05-31", "2027-06-18", "2027-07-05", "2027-09-06",
    "2027-11-25", "2027-12-24"
};

PriceCacheService::PriceCacheService(db::Database& db, utils::PriceFetcher& fetcher)
    : db_(db), fetcher_(fetcher) {}

std::string PriceCacheService::get_current_trading_date() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    // Copy tm struct immediately to avoid localtime static buffer issues
    struct tm local_tm = *std::localtime(&time_t_now);

    int year = local_tm.tm_year + 1900;
    int month = local_tm.tm_mon + 1;
    int day = local_tm.tm_mday;
    int wday = local_tm.tm_wday;  // 0=Sun, 6=Sat
    int hour = local_tm.tm_hour;
    int min = local_tm.tm_min;

    // If before 9:30 AM local time, roll back to previous trading day
    // Note: uses system local time, not Eastern Time. Works correctly
    // for users in US Eastern timezone. For other zones, the date library
    // would be needed for proper ET conversion.
    if (hour < 9 || (hour == 9 && min < 30)) {
        time_t_now -= 86400;
        local_tm = *std::localtime(&time_t_now);
        year = local_tm.tm_year + 1900;
        month = local_tm.tm_mon + 1;
        day = local_tm.tm_mday;
        wday = local_tm.tm_wday;
    }

    // Roll back over weekends
    while (wday == 0 || wday == 6) {
        time_t_now -= 86400;
        local_tm = *std::localtime(&time_t_now);
        year = local_tm.tm_year + 1900;
        month = local_tm.tm_mon + 1;
        day = local_tm.tm_mday;
        wday = local_tm.tm_wday;
    }

    // Format and roll back over holidays
    // TODO: Extend holiday list beyond 2027 before Jan 1, 2028
    std::ostringstream oss;
    oss << year << "-"
        << std::setfill('0') << std::setw(2) << month << "-"
        << std::setfill('0') << std::setw(2) << day;
    std::string date_str = oss.str();

    while (US_MARKET_HOLIDAYS.count(date_str)) {
        time_t_now -= 86400;
        local_tm = *std::localtime(&time_t_now);
        wday = local_tm.tm_wday;
        if (wday == 0 || wday == 6) continue;

        oss.str("");
        oss << (local_tm.tm_year + 1900) << "-"
            << std::setfill('0') << std::setw(2) << (local_tm.tm_mon + 1) << "-"
            << std::setfill('0') << std::setw(2) << local_tm.tm_mday;
        date_str = oss.str();
    }

    return date_str;
}

bool PriceCacheService::is_cache_valid(const std::string& trading_date) {
    return trading_date == get_current_trading_date();
}

// --- Price caching ---

Result<void> PriceCacheService::save_price(
    const utils::StockPrice& price, const std::string& trading_date) {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        SQLite::Statement upsert(*sqlite_db,
            "INSERT OR REPLACE INTO cached_prices (symbol, price, source, fetched_at, trading_date) "
            "VALUES (?, ?, ?, datetime('now'), ?)");
        upsert.bind(1, price.symbol);
        upsert.bind(2, price.price);
        upsert.bind(3, price.source);
        upsert.bind(4, trading_date);
        upsert.exec();
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to save cached price", std::string(e.what())};
    }
}

Result<std::optional<utils::StockPrice>> PriceCacheService::load_cached_price(
    const std::string& symbol) {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        SQLite::Statement q(*sqlite_db,
            "SELECT price, source, fetched_at, trading_date "
            "FROM cached_prices WHERE symbol = ?");
        q.bind(1, symbol);

        if (!q.executeStep()) return std::optional<utils::StockPrice>{std::nullopt};

        std::string trading_date = q.getColumn(3).getString();
        if (!is_cache_valid(trading_date)) {
            Logger::debug("Price cache stale for {} (cached={}, current={})",
                         symbol, trading_date, get_current_trading_date());
            return std::optional<utils::StockPrice>{std::nullopt};
        }

        utils::StockPrice price;
        price.symbol = symbol;
        price.price = q.getColumn(0).getDouble();
        price.source = q.getColumn(1).getString();
        price.timestamp = q.getColumn(2).getString();
        return std::optional<utils::StockPrice>{price};
    } catch (const std::exception& e) {
        return Error{"Failed to load cached price", std::string(e.what())};
    }
}

Result<utils::StockPrice> PriceCacheService::get_price(const std::string& symbol) {
    auto cached = load_cached_price(symbol);
    if (cached && *cached) {
        Logger::debug("Cache HIT: price for {}", symbol);
        return **cached;
    }

    Logger::debug("Cache MISS: price for {}", symbol);
    auto result = fetcher_.fetch_price(symbol);
    if (!result) return result;

    std::string trading_date = get_current_trading_date();
    auto save_result = save_price(*result, trading_date);
    if (!save_result) {
        Logger::warn("Failed to cache price for {}: {}", symbol, save_result.error().message);
    }

    return result;
}

Result<std::optional<utils::StockPrice>> PriceCacheService::get_price_cached_only(
    const std::string& symbol) {
    auto cached = load_cached_price(symbol);
    if (!cached) return cached;
    if (*cached) {
        Logger::debug("Cache HIT (cached-only): price for {}", symbol);
        return cached;
    }
    Logger::debug("Cache MISS (cached-only, skipping fetch): price for {}", symbol);
    return std::optional<utils::StockPrice>{std::nullopt};
}

std::map<std::string, utils::StockPrice> PriceCacheService::get_prices(
    const std::vector<std::string>& symbols) {
    std::map<std::string, utils::StockPrice> prices;

    for (const auto& symbol : symbols) {
        auto result = get_price(symbol);
        if (result) {
            prices[symbol] = *result;
        } else {
            Logger::warn("Failed to get price for {}: {}", symbol, result.error().message);
        }
    }

    return prices;
}

// --- Volatility caching ---

Result<void> PriceCacheService::save_volatility(
    const std::string& symbol, double vol,
    const std::string& source, const std::string& trading_date) {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        SQLite::Statement upsert(*sqlite_db,
            "INSERT OR REPLACE INTO cached_volatility (symbol, volatility, source, fetched_at, trading_date) "
            "VALUES (?, ?, ?, datetime('now'), ?)");
        upsert.bind(1, symbol);
        upsert.bind(2, vol);
        upsert.bind(3, source);
        upsert.bind(4, trading_date);
        upsert.exec();
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to save cached volatility", std::string(e.what())};
    }
}

Result<std::optional<double>> PriceCacheService::load_cached_volatility(
    const std::string& symbol) {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        SQLite::Statement q(*sqlite_db,
            "SELECT volatility, trading_date "
            "FROM cached_volatility WHERE symbol = ?");
        q.bind(1, symbol);

        if (!q.executeStep()) return std::optional<double>{std::nullopt};

        std::string trading_date = q.getColumn(1).getString();
        if (!is_cache_valid(trading_date)) return std::optional<double>{std::nullopt};

        return std::optional<double>{q.getColumn(0).getDouble()};
    } catch (const std::exception& e) {
        return Error{"Failed to load cached volatility", std::string(e.what())};
    }
}

Result<double> PriceCacheService::get_volatility(const std::string& symbol) {
    auto cached = load_cached_volatility(symbol);
    if (cached && *cached) {
        Logger::debug("Cache HIT: volatility for {}", symbol);
        return **cached;
    }

    Logger::debug("Cache MISS: volatility for {}", symbol);
    auto result = fetcher_.fetch_volatility(symbol);
    if (!result) return result;

    std::string trading_date = get_current_trading_date();
    auto save_result = save_volatility(symbol, *result, "alphavantage", trading_date);
    if (!save_result) {
        Logger::warn("Failed to cache volatility for {}: {}", symbol, save_result.error().message);
    }

    return result;
}

// --- Option chain caching ---

Result<void> PriceCacheService::save_option_chain(
    const utils::OptionChainData& chain,
    const std::string& source, const std::string& trading_date) {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        SQLite::Transaction transaction(*sqlite_db);

        SQLite::Statement del(*sqlite_db, "DELETE FROM cached_option_chains WHERE symbol = ?");
        del.bind(1, chain.symbol);
        del.exec();

        for (const auto& [expiry, strikes] : chain.puts_by_expiry) {
            nlohmann::json strikes_json = nlohmann::json::array();
            for (const auto& s : strikes) {
                strikes_json.push_back({
                    {"strike", s.strike},
                    {"bid", s.bid},
                    {"ask", s.ask},
                    {"last", s.last},
                    {"iv", s.iv}
                });
            }

            SQLite::Statement insert(*sqlite_db,
                "INSERT INTO cached_option_chains (symbol, expiry, chain_json, source, fetched_at, trading_date) "
                "VALUES (?, ?, ?, ?, datetime('now'), ?)");
            insert.bind(1, chain.symbol);
            insert.bind(2, expiry);
            insert.bind(3, strikes_json.dump());
            insert.bind(4, source);
            insert.bind(5, trading_date);
            insert.exec();
        }

        transaction.commit();
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to save cached option chain", std::string(e.what())};
    }
}

Result<std::optional<utils::OptionChainData>> PriceCacheService::load_cached_option_chain(
    const std::string& symbol) {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        std::string current_td = get_current_trading_date();

        SQLite::Statement check(*sqlite_db,
            "SELECT COUNT(*) FROM cached_option_chains "
            "WHERE symbol = ? AND trading_date = ?");
        check.bind(1, symbol);
        check.bind(2, current_td);

        if (!check.executeStep() || check.getColumn(0).getInt() == 0) {
            return std::optional<utils::OptionChainData>{std::nullopt};
        }

        utils::OptionChainData chain;
        chain.symbol = symbol;

        SQLite::Statement q(*sqlite_db,
            "SELECT expiry, chain_json FROM cached_option_chains "
            "WHERE symbol = ? AND trading_date = ? ORDER BY expiry");
        q.bind(1, symbol);
        q.bind(2, current_td);

        while (q.executeStep()) {
            std::string expiry = q.getColumn(0).getString();
            std::string json_str = q.getColumn(1).getString();

            auto strikes_json = nlohmann::json::parse(json_str);
            std::vector<utils::OptionStrike> strikes;
            for (const auto& s : strikes_json) {
                utils::OptionStrike strike;
                strike.strike = s.value("strike", 0.0);
                strike.bid = s.value("bid", 0.0);
                strike.ask = s.value("ask", 0.0);
                strike.last = s.value("last", 0.0);
                strike.iv = s.value("iv", 0.0);
                strikes.push_back(strike);
            }
            chain.puts_by_expiry[expiry] = strikes;
        }

        auto now = std::chrono::system_clock::now();
        auto now_time = std::chrono::system_clock::to_time_t(now);
        for (const auto& [expiry, _] : chain.puts_by_expiry) {
            struct tm tm_val = {};
            std::istringstream iss(expiry);
            iss >> std::get_time(&tm_val, "%Y-%m-%d");
            if (iss.fail()) continue;
            time_t exp_time = timegm(&tm_val);
            int dte = static_cast<int>((exp_time - now_time) / 86400);

            utils::OptionExpiry oe;
            oe.expiry_date = expiry;
            oe.dte = dte;
            chain.expirations.push_back(oe);
        }

        if (chain.puts_by_expiry.empty()) return std::optional<utils::OptionChainData>{std::nullopt};
        return std::optional<utils::OptionChainData>{chain};
    } catch (const std::exception& e) {
        return Error{"Failed to load cached option chain", std::string(e.what())};
    }
}

Result<utils::OptionChainData> PriceCacheService::get_option_chain(const std::string& symbol) {
    auto cached = load_cached_option_chain(symbol);
    if (cached && *cached) {
        Logger::debug("Cache HIT: option chain for {}", symbol);
        return **cached;
    }

    Logger::debug("Cache MISS: option chain for {}", symbol);
    auto result = fetcher_.fetch_option_chain(symbol);
    if (!result) return result;

    std::string trading_date = get_current_trading_date();
    std::string source = "yahoo";
    if (result->puts_by_expiry.empty()) {
        source = "empty";
    }

    auto save_result = save_option_chain(*result, source, trading_date);
    if (!save_result) {
        Logger::warn("Failed to cache option chain for {}: {}", symbol, save_result.error().message);
    }

    return result;
}

Result<std::optional<utils::OptionChainData>> PriceCacheService::get_option_chain_cached_only(
    const std::string& symbol) {
    auto cached = load_cached_option_chain(symbol);
    if (!cached) return cached;
    if (*cached) {
        Logger::debug("Cache HIT (cached-only): option chain for {}", symbol);
        return cached;
    }
    Logger::debug("Cache MISS (cached-only, skipping fetch): option chain for {}", symbol);
    return std::optional<utils::OptionChainData>{std::nullopt};
}

// --- Cache management ---

Result<void> PriceCacheService::clear_cache() {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        sqlite_db->exec("DELETE FROM cached_prices");
        sqlite_db->exec("DELETE FROM cached_volatility");
        sqlite_db->exec("DELETE FROM cached_option_chains");
        Logger::info("Price cache cleared");
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to clear cache", std::string(e.what())};
    }
}

Result<void> PriceCacheService::clear_expired() {
    auto* sqlite_db = db_.get_db();
    if (!sqlite_db) return Error{"Database not initialized"};

    try {
        std::string current_td = get_current_trading_date();

        SQLite::Statement del_prices(*sqlite_db,
            "DELETE FROM cached_prices WHERE trading_date != ?");
        del_prices.bind(1, current_td);
        int p = del_prices.exec();

        SQLite::Statement del_vol(*sqlite_db,
            "DELETE FROM cached_volatility WHERE trading_date != ?");
        del_vol.bind(1, current_td);
        int v = del_vol.exec();

        SQLite::Statement del_chains(*sqlite_db,
            "DELETE FROM cached_option_chains WHERE trading_date != ?");
        del_chains.bind(1, current_td);
        int c = del_chains.exec();

        Logger::info("Cleared expired cache entries: {} prices, {} volatilities, {} chains",
                     p, v, c);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to clear expired cache", std::string(e.what())};
    }
}

} // namespace ibkr::services
