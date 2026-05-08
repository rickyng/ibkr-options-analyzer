#pragma once

#include "strategy_detector.hpp"
#include "utils/currency.hpp"
#include "utils/result.hpp"
#include <limits>
#include <map>
#include <vector>
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
    constexpr double CONTRACT_MULTIPLIER = 100.0;
    constexpr int MAX_DTE_INVALID = std::numeric_limits<int>::max();
}

inline double premium_for(double quantity, double price_per_contract) {
    return std::abs(quantity) * price_per_contract * constants::CONTRACT_MULTIPLIER;
}

/**
 * Risk metrics for a strategy.
 */
struct RiskMetrics {
    double breakeven_price{0.0};
    double breakeven_price_2{0.0};  // For iron condors (two breakevens)
    double max_profit{0.0};
    double max_loss{0.0};
    double max_loss_10pct{0.0};
    double max_loss_20pct{0.0};
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
        double total_loss_10pct{0.0};
        double total_loss_20pct{0.0};
        int positions_expiring_soon{0};  // < 7 days
        int total_strategies{0};
        std::string base_currency;
        std::map<std::string, double> max_profit_by_currency;
        std::map<std::string, double> max_loss_by_currency;
        std::map<std::string, double> loss_10pct_by_currency;
        std::map<std::string, double> loss_20pct_by_currency;
    };

    static PortfolioRisk calculate_portfolio_risk(
        const std::vector<Strategy>& strategies,
        const std::vector<RiskMetrics>& metrics,
        const utils::CurrencyConverter& converter = utils::CurrencyConverter());

    /**
     * Per-account risk breakdown.
     */
    struct AccountRisk {
        std::string account_name;
        int64_t account_id{0};
        double total_max_profit{0.0};
        double total_max_loss{0.0};
        double total_loss_10pct{0.0};
        double total_loss_20pct{0.0};
        int strategy_count{0};
        int positions_expiring_soon{0};
        std::map<std::string, double> max_profit_by_currency;
        std::map<std::string, double> max_loss_by_currency;
        std::map<std::string, double> loss_10pct_by_currency;
        std::map<std::string, double> loss_20pct_by_currency;
    };

    /**
     * Break down risk by account.
     */
    static std::vector<AccountRisk> calculate_account_risks(
        const std::vector<Strategy>& strategies,
        const std::vector<RiskMetrics>& metrics,
        const utils::CurrencyConverter& converter = utils::CurrencyConverter());

    /**
     * Cross-account exposure per underlying.
     */
    struct UnderlyingExposure {
        std::string underlying;
        double total_max_loss{0.0};
        double total_max_profit{0.0};
        int position_count{0};
        std::map<std::string, double> by_account;  // account_name → max_loss
    };

    /**
     * Calculate exposure grouped by underlying across all accounts.
     */
    static std::vector<UnderlyingExposure> calculate_underlying_exposure(
        const std::vector<Strategy>& strategies,
        const std::vector<RiskMetrics>& metrics,
        const utils::CurrencyConverter& converter = utils::CurrencyConverter());
};

} // namespace ibkr::analysis
