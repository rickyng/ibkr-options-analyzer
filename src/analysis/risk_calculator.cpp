#include "risk_calculator.hpp"
#include "utils/logger.hpp"
#include <cmath>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace ibkr::analysis {

using utils::Logger;

RiskMetrics RiskCalculator::calculate_risk(const Strategy& strategy) {
    switch (strategy.type) {
        case Strategy::Type::NakedShortPut:
            return calculate_naked_short_put_risk(strategy.legs[0]);

        case Strategy::Type::NakedShortCall:
            return calculate_naked_short_call_risk(strategy.legs[0]);

        case Strategy::Type::BullPutSpread:
            return calculate_bull_put_spread_risk(strategy.legs[0], strategy.legs[1]);

        case Strategy::Type::BearCallSpread:
            return calculate_bear_call_spread_risk(strategy.legs[0], strategy.legs[1]);

        case Strategy::Type::IronCondor:
            return calculate_iron_condor_risk(strategy);

        default:
            Logger::warn("Unknown strategy type for risk calculation");
            return RiskMetrics{};
    }
}

RiskMetrics RiskCalculator::calculate_naked_short_put_risk(const Position& pos) {
    RiskMetrics metrics;

    // Net premium received (negative quantity means short)
    metrics.net_premium = std::abs(pos.quantity) * pos.entry_premium * 100.0;

    // Breakeven: Strike - Premium
    metrics.breakeven_price = pos.strike - pos.entry_premium;

    // Max profit: Premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: (Strike - Premium) * 100 * |Quantity| (if stock goes to $0)
    metrics.max_loss = (pos.strike - pos.entry_premium) * 100.0 * std::abs(pos.quantity);

    // Risk level: HIGH (undefined risk down to zero)
    metrics.risk_level = "HIGH";

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(pos.expiry);

    return metrics;
}

RiskMetrics RiskCalculator::calculate_naked_short_call_risk(const Position& pos) {
    RiskMetrics metrics;

    // Net premium received
    metrics.net_premium = std::abs(pos.quantity) * pos.entry_premium * 100.0;

    // Breakeven: Strike + Premium
    metrics.breakeven_price = pos.strike + pos.entry_premium;

    // Max profit: Premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: UNLIMITED (stock can go to infinity)
    metrics.max_loss = std::numeric_limits<double>::infinity();

    // Risk level: HIGH (unlimited risk)
    metrics.risk_level = "HIGH";

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(pos.expiry);

    return metrics;
}

RiskMetrics RiskCalculator::calculate_bull_put_spread_risk(
    const Position& short_leg,
    const Position& long_leg) {

    RiskMetrics metrics;

    // Net premium: Short premium - Long premium
    double short_premium = std::abs(short_leg.quantity) * short_leg.entry_premium * 100.0;
    double long_premium = std::abs(long_leg.quantity) * long_leg.entry_premium * 100.0;
    metrics.net_premium = short_premium - long_premium;

    // Breakeven: Short Strike - Net Premium per share
    metrics.breakeven_price = short_leg.strike - (metrics.net_premium / 100.0);

    // Max profit: Net premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: (Strike Difference - Net Premium)
    double strike_diff = short_leg.strike - long_leg.strike;
    metrics.max_loss = (strike_diff * 100.0 * std::abs(short_leg.quantity)) - metrics.net_premium;

    // Risk level: DEFINED (limited risk)
    metrics.risk_level = "DEFINED";

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(short_leg.expiry);

    return metrics;
}

RiskMetrics RiskCalculator::calculate_bear_call_spread_risk(
    const Position& short_leg,
    const Position& long_leg) {

    RiskMetrics metrics;

    // Net premium: Short premium - Long premium
    double short_premium = std::abs(short_leg.quantity) * short_leg.entry_premium * 100.0;
    double long_premium = std::abs(long_leg.quantity) * long_leg.entry_premium * 100.0;
    metrics.net_premium = short_premium - long_premium;

    // Breakeven: Short Strike + Net Premium per share
    metrics.breakeven_price = short_leg.strike + (metrics.net_premium / 100.0);

    // Max profit: Net premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: (Strike Difference - Net Premium)
    double strike_diff = long_leg.strike - short_leg.strike;
    metrics.max_loss = (strike_diff * 100.0 * std::abs(short_leg.quantity)) - metrics.net_premium;

    // Risk level: DEFINED (limited risk)
    metrics.risk_level = "DEFINED";

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(short_leg.expiry);

    return metrics;
}

RiskMetrics RiskCalculator::calculate_iron_condor_risk(const Strategy& condor) {
    RiskMetrics metrics;

    // Iron condor has 4 legs: 2 puts (bull put spread) + 2 calls (bear call spread)
    if (condor.legs.size() != 4) {
        Logger::error("Iron condor must have 4 legs");
        return metrics;
    }

    // Separate puts and calls
    std::vector<Position> puts, calls;
    for (const auto& leg : condor.legs) {
        if (leg.right == 'P') {
            puts.push_back(leg);
        } else {
            calls.push_back(leg);
        }
    }

    if (puts.size() != 2 || calls.size() != 2) {
        Logger::error("Iron condor must have 2 puts and 2 calls");
        return metrics;
    }

    // Sort by strike
    std::sort(puts.begin(), puts.end(), [](const Position& a, const Position& b) {
        return a.strike < b.strike;
    });
    std::sort(calls.begin(), calls.end(), [](const Position& a, const Position& b) {
        return a.strike < b.strike;
    });

    // Calculate net premium from all legs
    double total_premium = 0.0;
    for (const auto& leg : condor.legs) {
        if (leg.quantity < 0) {
            // Short leg: receive premium
            total_premium += std::abs(leg.quantity) * leg.entry_premium * 100.0;
        } else {
            // Long leg: pay premium
            total_premium -= std::abs(leg.quantity) * leg.entry_premium * 100.0;
        }
    }
    metrics.net_premium = total_premium;

    // Breakeven prices (two breakevens)
    // Put side breakeven: Short put strike - Net premium per share
    // Call side breakeven: Short call strike + Net premium per share
    double net_premium_per_share = metrics.net_premium / 100.0;

    // Find short strikes
    double short_put_strike = 0.0, short_call_strike = 0.0;
    for (const auto& put : puts) {
        if (put.quantity < 0) short_put_strike = put.strike;
    }
    for (const auto& call : calls) {
        if (call.quantity < 0) short_call_strike = call.strike;
    }

    metrics.breakeven_price = short_put_strike - net_premium_per_share;
    metrics.breakeven_price_2 = short_call_strike + net_premium_per_share;

    // Max profit: Net premium received
    metrics.max_profit = metrics.net_premium;

    // Max loss: Wider spread width - Net premium
    double put_spread_width = puts[1].strike - puts[0].strike;
    double call_spread_width = calls[1].strike - calls[0].strike;
    double max_spread_width = std::max(put_spread_width, call_spread_width);
    metrics.max_loss = (max_spread_width * 100.0) - metrics.net_premium;

    // Risk level: DEFINED (limited risk)
    metrics.risk_level = "DEFINED";

    // Days to expiry
    metrics.days_to_expiry = calculate_days_to_expiry(condor.expiry);

    return metrics;
}

int RiskCalculator::calculate_days_to_expiry(const std::string& expiry_date) {
    // Parse expiry date (YYYY-MM-DD)
    std::istringstream ss(expiry_date);
    int year, month, day;
    char dash1, dash2;
    ss >> year >> dash1 >> month >> dash2 >> day;

    // Get current date
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);

    // Create expiry date
    std::tm expiry_tm = {};
    expiry_tm.tm_year = year - 1900;
    expiry_tm.tm_mon = month - 1;
    expiry_tm.tm_mday = day;

    // Calculate difference in days
    auto expiry_time_t = std::mktime(&expiry_tm);
    auto diff_seconds = std::difftime(expiry_time_t, now_time_t);
    int days = static_cast<int>(diff_seconds / (60 * 60 * 24));

    return days;
}

RiskCalculator::PortfolioRisk RiskCalculator::calculate_portfolio_risk(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics) {

    PortfolioRisk portfolio;
    portfolio.total_strategies = strategies.size();

    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];

        portfolio.total_max_profit += m.max_profit;

        // Only add finite max loss values
        if (std::isfinite(m.max_loss)) {
            portfolio.total_max_loss += m.max_loss;
            portfolio.total_capital_at_risk += m.max_loss;
        }

        // Count positions expiring soon (< 7 days)
        if (m.days_to_expiry < 7 && m.days_to_expiry >= 0) {
            portfolio.positions_expiring_soon++;
        }
    }

    return portfolio;
}

} // namespace ibkr::analysis
