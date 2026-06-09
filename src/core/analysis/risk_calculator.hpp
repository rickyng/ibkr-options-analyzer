#pragma once

#include "strategy_detector.hpp"
#include "utils/currency.hpp"
#include <limits>
#include <string>

namespace ibkr::analysis {

enum class RiskLevel {
    Low,
    Medium,
    High,
    Defined
};

inline std::string risk_level_to_string(RiskLevel level) {
    switch (level) {
        case RiskLevel::Low: return "LOW";
        case RiskLevel::Medium: return "MEDIUM";
        case RiskLevel::High: return "HIGH";
        case RiskLevel::Defined: return "DEFINED";
        default: return "UNKNOWN";
    }
}

namespace constants {
    constexpr int MAX_DTE_INVALID = std::numeric_limits<int>::max();
}

inline double premium_for(double quantity, double price_per_contract, double multiplier = 100.0) {
    return std::abs(quantity) * price_per_contract * multiplier;
}

/**
 * Risk metrics for a strategy.
 */
struct RiskMetrics {
    double breakeven_price{0.0};
    double breakeven_price_2{0.0};
    double max_profit{0.0};
    double max_loss{0.0};
    double max_loss_current{0.0};
    double max_loss_5pct{0.0};
    RiskLevel risk_level{RiskLevel::Low};
    double net_premium{0.0};
    int days_to_expiry{0};
    std::string currency;
};

/**
 * Risk calculator for option strategies.
 */
class RiskCalculator {
public:
    static RiskMetrics calculate_risk(const Strategy& strategy);
    static RiskMetrics calculate_naked_short_put_risk(const Position& pos);
    static RiskMetrics calculate_naked_short_call_risk(const Position& pos);
    static int calculate_days_to_expiry(const std::string& expiry_date);
};

} // namespace ibkr::analysis
