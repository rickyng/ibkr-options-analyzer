#include "position_service.hpp"
#include "utils/logger.hpp"
#include "config/config_manager.hpp"
#include "analysis/risk_calculator.hpp"
#include <algorithm>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

PositionService::PositionService(db::Database& database)
    : database_(database) {}

Result<std::vector<analysis::Position>> PositionService::load_positions(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    // Load positions using Database method
    auto positions_result = database_.get_all_positions(account_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    // Filter by underlying if specified
    if (!underlying_filter.empty()) {
        positions.erase(
            std::remove_if(positions.begin(), positions.end(),
                [&](const analysis::Position& pos) {
                    return pos.underlying != underlying_filter;
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

PortfolioSummary PositionService::calculate_portfolio_summary(
    const std::vector<analysis::Position>& positions) {

    PortfolioSummary summary;
    summary.total_positions = positions.size();

    for (const auto& pos : positions) {
        int dte = analysis::RiskCalculator::calculate_days_to_expiry(pos.expiry);
        if (dte <= 7) summary.expiring_7_days++;
        if (dte <= 30) summary.expiring_30_days++;

        if (pos.quantity < 0) {
            double prem = analysis::premium_for(pos.quantity, pos.entry_premium);
            summary.total_premium_collected += prem;
            if (pos.right == 'P') {
                summary.short_puts++;
                summary.total_max_loss += (pos.strike * analysis::constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity)) - prem;
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