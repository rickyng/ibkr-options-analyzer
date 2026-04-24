#pragma once

#include "strategy_detector.hpp"
#include "utils/result.hpp"

namespace ibkr::analysis {

/**
 * Risk metrics for a strategy.
 */
struct RiskMetrics {
    double breakeven_price{0.0};
    double breakeven_price_2{0.0};  // For iron condors (two breakevens)
    double max_profit{0.0};
    double max_loss{0.0};
    std::string risk_level;  // "LOW", "MEDIUM", "HIGH", "DEFINED"
    double net_premium{0.0};
    int days_to_expiry{0};
};

/**
 * Risk calculator for option strategies.
 */
class RiskCalculator {
public:
    /**
     * Calculate risk metrics for a strategy.
     * @param strategy Strategy to analyze
     * @return Risk metrics
     */
    static RiskMetrics calculate_risk(const Strategy& strategy);

    /**
     * Calculate risk for naked short put.
     */
    static RiskMetrics calculate_naked_short_put_risk(const Position& pos);

    /**
     * Calculate risk for naked short call.
     */
    static RiskMetrics calculate_naked_short_call_risk(const Position& pos);

    /**
     * Calculate risk for bull put spread.
     */
    static RiskMetrics calculate_bull_put_spread_risk(
        const Position& short_leg,
        const Position& long_leg);

    /**
     * Calculate risk for bear call spread.
     */
    static RiskMetrics calculate_bear_call_spread_risk(
        const Position& short_leg,
        const Position& long_leg);

    /**
     * Calculate risk for iron condor.
     */
    static RiskMetrics calculate_iron_condor_risk(const Strategy& condor);

    /**
     * Calculate days to expiry from expiry date string (YYYY-MM-DD).
     */
    static int calculate_days_to_expiry(const std::string& expiry_date);

    /**
     * Calculate portfolio-level risk summary.
     */
    struct PortfolioRisk {
        double total_max_profit{0.0};
        double total_max_loss{0.0};
        double total_capital_at_risk{0.0};
        int positions_expiring_soon{0};  // < 7 days
        int total_strategies{0};
    };

    static PortfolioRisk calculate_portfolio_risk(
        const std::vector<Strategy>& strategies,
        const std::vector<RiskMetrics>& metrics);
};

} // namespace ibkr::analysis
