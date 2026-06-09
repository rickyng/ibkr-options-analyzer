#include "risk_calculator.hpp"
#include "utils/currency.hpp"
#include "utils/logger.hpp"
#include <date/date.h>
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace ibkr::analysis {

using utils::Logger;

RiskMetrics RiskCalculator::calculate_risk(const Strategy& strategy) {
    RiskMetrics metrics;
    switch (strategy.type) {
        case Strategy::Type::NakedShortPut:
            metrics = calculate_naked_short_put_risk(strategy.legs[0]);
            break;
        case Strategy::Type::NakedShortCall:
            metrics = calculate_naked_short_call_risk(strategy.legs[0]);
            break;
        default:
            Logger::warn("Unknown strategy type for risk calculation");
            break;
    }
    metrics.currency = strategy.currency;
    return metrics;
}

RiskMetrics RiskCalculator::calculate_naked_short_put_risk(const Position& pos) {
    RiskMetrics metrics;

    // Net premium received (negative quantity means short)
    metrics.net_premium = premium_for(pos.quantity, pos.entry_premium, pos.multiplier);

    // Breakeven: Strike - Premium
    metrics.breakeven_price = pos.strike - pos.entry_premium;

    // Max profit: Premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: (Strike - Premium) * multiplier * |Quantity| (if stock goes to $0)
    metrics.max_loss = (pos.strike - pos.entry_premium) * pos.multiplier * std::abs(pos.quantity);

    // Current loss (scenario placeholder; overridden when current price available)
    double loss_current_per_share = pos.strike * 0.10 - pos.entry_premium;
    metrics.max_loss_current = std::max(0.0, loss_current_per_share) * pos.multiplier * std::abs(pos.quantity);

    // 5% loss: estimate without live price (overridden in portfolio_service with live price)
    double loss_5pct_per_share = pos.strike * 0.05 - pos.entry_premium;
    metrics.max_loss_5pct = std::max(0.0, loss_5pct_per_share) * pos.multiplier * std::abs(pos.quantity);

    // Risk level: HIGH (undefined risk down to zero)
    metrics.risk_level = RiskLevel::High;

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(pos.expiry);

    return metrics;
}

RiskMetrics RiskCalculator::calculate_naked_short_call_risk(const Position& pos) {
    RiskMetrics metrics;

    // Net premium received
    metrics.net_premium = premium_for(pos.quantity, pos.entry_premium, pos.multiplier);

    // Breakeven: Strike + Premium
    metrics.breakeven_price = pos.strike + pos.entry_premium;

    // Max profit: Premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: UNLIMITED (stock can go to infinity)
    metrics.max_loss = std::numeric_limits<double>::infinity();

    // Current loss: always overridden by portfolio_service when real prices
    // are available. Leave at 0 (no meaningful placeholder for unlimited-risk calls).
    metrics.max_loss_current = 0.0;

    // 5% loss: $0 for short calls (5% market drop benefits call sellers)
    metrics.max_loss_5pct = 0.0;

    // Risk level: HIGH (unlimited risk)
    metrics.risk_level = RiskLevel::High;

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(pos.expiry);

    return metrics;
}

int RiskCalculator::calculate_days_to_expiry(const std::string& expiry_date) {
    using namespace date;

    // Parse expiry date (YYYY-MM-DD)
    std::istringstream ss(expiry_date);
    year_month_day ymd;
    ss >> parse("%F", ymd);

    if (ss.fail() || !ymd.ok()) {
        Logger::warn("Invalid expiry date format: '{}'", expiry_date);
        return constants::MAX_DTE_INVALID;
    }

    // Get current date
    auto today = floor<days>(std::chrono::system_clock::now());
    auto expiry = sys_days{ymd};

    // Calculate difference in days
    auto diff = expiry - today;
    return static_cast<int>(diff.count());
}

} // namespace ibkr::analysis
