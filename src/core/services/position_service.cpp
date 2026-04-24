#include "position_service.hpp"
#include "utils/logger.hpp"
#include "config/config_manager.hpp"
#include <SQLiteCpp/SQLiteCpp.h>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

PositionService::PositionService(db::Database& database)
    : database_(database) {}

Result<std::vector<analysis::Position>> PositionService::load_positions(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    auto positions_result = analysis::StrategyDetector::load_positions(database_, 0);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    if (!account_filter.empty() || !underlying_filter.empty()) {
        auto db_ptr = database_.get_db();
        if (!db_ptr) {
            return Error{"Database not initialized"};
        }

        positions.erase(
            std::remove_if(positions.begin(), positions.end(),
                [&](const analysis::Position& pos) {
                    if (!account_filter.empty()) {
                        SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
                        query.bind(1, pos.account_id);
                        if (query.executeStep()) {
                            std::string account_name = query.getColumn(0).getString();
                            if (account_name != account_filter) return true;
                        }
                    }
                    if (!underlying_filter.empty() && pos.underlying != underlying_filter) {
                        return true;
                    }
                    return false;
                }),
            positions.end()
        );
    }

    return positions;
}

Result<std::map<int64_t, std::string>> PositionService::load_account_names() {
    std::map<int64_t, std::string> names;

    auto db_ptr = database_.get_db();
    if (!db_ptr) return Error{"Database not initialized"};

    SQLite::Statement q(*db_ptr, "SELECT id, name FROM accounts");
    while (q.executeStep()) {
        names[q.getColumn(0).getInt64()] = q.getColumn(1).getString();
    }

    return names;
}

Result<int> PositionService::get_position_count() {
    return database_.get_open_positions_count();
}

Result<int> PositionService::get_trade_count() {
    return database_.get_trades_count();
}

Result<int64_t> PositionService::add_manual_position(
    const std::string& account_name,
    const std::string& underlying,
    const std::string& expiry_yyyymmdd,
    double strike,
    char right,
    double quantity,
    double premium,
    const std::string& notes) {

    auto account_result = database_.get_or_create_account(account_name);
    if (!account_result) {
        return Error{"Failed to get account", account_result.error().message};
    }
    int64_t account_id = *account_result;

    std::string expiry_formatted = expiry_yyyymmdd;
    if (expiry_yyyymmdd.length() == 8) {
        expiry_formatted = expiry_yyyymmdd.substr(0, 4) + "-" +
                          expiry_yyyymmdd.substr(4, 2) + "-" +
                          expiry_yyyymmdd.substr(6, 2);
    }

    std::string yy = expiry_yyyymmdd.substr(2, 2);
    std::string mmdd = expiry_yyyymmdd.substr(4, 4);
    std::ostringstream symbol_ss;
    symbol_ss << underlying << yy << mmdd << right;
    if (strike == static_cast<int>(strike)) {
        symbol_ss << static_cast<int>(strike);
    } else {
        symbol_ss << strike;
    }
    std::string symbol = symbol_ss.str();

    try {
        auto db_ptr = database_.get_db();
        if (!db_ptr) return Error{"Database not initialized"};

        SQLite::Statement insert(*db_ptr,
            "INSERT INTO open_options (account_id, symbol, underlying, expiry, strike, "
            "right, quantity, mark_price, entry_premium, current_value, is_manual, notes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?)");

        insert.bind(1, account_id);
        insert.bind(2, symbol);
        insert.bind(3, underlying);
        insert.bind(4, expiry_formatted);
        insert.bind(5, strike);
        insert.bind(6, std::string(1, right));
        insert.bind(7, quantity);
        insert.bind(8, premium);
        insert.bind(9, premium);
        insert.bind(10, quantity * premium * 100.0);
        insert.bind(11, notes);

        insert.exec();

        int64_t position_id = db_ptr->getLastInsertRowid();
        Logger::info("Manual position added: id={}, symbol={}", position_id, symbol);
        return position_id;

    } catch (const std::exception& e) {
        return Error{"Failed to insert position", std::string(e.what())};
    }
}

PortfolioSummary PositionService::calculate_portfolio_summary(
    const std::vector<analysis::Position>& positions) {

    PortfolioSummary summary;
    summary.total_positions = positions.size();

    for (const auto& pos : positions) {
        int dte = analysis::RiskCalculator::calculate_days_to_expiry(pos.expiry);
        if (dte <= 7) summary.expiring_7_days++;
        if (dte <= 30) summary.expiring_30_days++;

        if (pos.quantity < 0) {
            double prem = std::abs(pos.quantity) * pos.entry_premium * 100.0;
            summary.total_premium_collected += prem;
            if (pos.right == 'P') {
                summary.short_puts++;
                summary.total_max_loss += (pos.strike * 100.0 * std::abs(pos.quantity)) - prem;
            } else {
                summary.short_calls++;
            }
        } else {
            summary.long_positions++;
        }
    }

    return summary;
}

} // namespace ibkr::services