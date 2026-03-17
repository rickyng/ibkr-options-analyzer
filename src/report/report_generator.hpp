#pragma once

#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include <string>
#include <vector>

namespace ibkr::report {

/**
 * Report generator for option positions and strategies.
 */
class ReportGenerator {
public:
    /**
     * Generate complete text report.
     * @param strategies Detected strategies
     * @param metrics Risk metrics for each strategy
     * @param account_names Map of account ID to account name
     * @return Formatted report string
     */
    static std::string generate_full_report(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics,
        const std::map<int64_t, std::string>& account_names);

    /**
     * Generate portfolio summary section.
     */
    static std::string generate_portfolio_summary(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics);

    /**
     * Generate positions by underlying section.
     */
    static std::string generate_positions_by_underlying(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics,
        const std::map<int64_t, std::string>& account_names);

    /**
     * Generate strategies by type section.
     */
    static std::string generate_strategies_by_type(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics);

    /**
     * Generate expiration calendar section.
     */
    static std::string generate_expiration_calendar(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics);

    /**
     * Generate risk analysis section.
     */
    static std::string generate_risk_analysis(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics);

private:
    /**
     * Format currency value.
     */
    static std::string format_currency(double value);

    /**
     * Get current timestamp string.
     */
    static std::string get_timestamp();
};

} // namespace ibkr::report
