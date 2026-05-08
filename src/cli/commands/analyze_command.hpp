#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include "services/portfolio_service.hpp"
#include "services/screener_service.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Screener parameter overrides (from CLI flags).
 * Negative values mean "use config default".
 */
struct ScreenerOverrides {
    double min_iv_percentile{-1};
    double min_premium_yield{-1};
    int min_dte{-1};
    int max_dte{-1};
    double otm_buffer_percent{-1};

    bool has_overrides() const {
        return min_iv_percentile >= 0 || min_premium_yield >= 0 ||
               min_dte >= 0 || max_dte >= 0 || otm_buffer_percent >= 0;
    }

    void apply_to(config::ScreenerConfig& cfg) const {
        if (min_iv_percentile >= 0) cfg.min_iv_percentile = min_iv_percentile;
        if (min_premium_yield >= 0) cfg.min_premium_yield = min_premium_yield;
        if (min_dte >= 0) cfg.min_dte = min_dte;
        if (max_dte >= 0) cfg.max_dte = max_dte;
        if (otm_buffer_percent >= 0) cfg.otm_buffer_percent = otm_buffer_percent;
    }
};

/**
 * Analyze command: Analyze option positions and strategies.
 *
 * Usage:
 *   ibkr-options-analyzer analyze open [--account NAME] [--underlying SYM]
 *   ibkr-options-analyzer analyze impact --underlying SYM [--account NAME]
 *   ibkr-options-analyzer analyze strategy [--account NAME] [--underlying SYM]
 */
class AnalyzeCommand {
public:
    /**
     * Execute analyze command.
     *
     * @param config Application configuration
     * @param analysis_type Type of analysis (open|impact|strategy)
     * @param account_filter Optional account name filter
     * @param underlying_filter Optional underlying symbol filter
     * @param output_opts Output format options
     * @return Result indicating success or error
     */
    static utils::Result<void> execute(
        const config::Config& config,
        const std::string& analysis_type,
        const std::string& account_filter = "",
        const std::string& underlying_filter = "",
        const utils::OutputOptions& output_opts = {},
        bool cache_only = false,
        const ScreenerOverrides& screener_overrides = {});

private:
    /**
     * Analyze open positions.
     */
    static utils::Result<void> analyze_open(
        const config::Config& config,
        const std::string& account_filter,
        const std::string& underlying_filter,
        const utils::OutputOptions& output_opts);

    /**
     * Analyze impact of price changes.
     */
    static utils::Result<void> analyze_impact(
        const config::Config& config,
        const std::string& underlying_filter,
        const std::string& account_filter,
        const utils::OutputOptions& output_opts);

    /**
     * Analyze detected strategies.
     */
    static utils::Result<void> analyze_strategy(
        const config::Config& config,
        const std::string& account_filter,
        const std::string& underlying_filter,
        const utils::OutputOptions& output_opts);

    /**
     * Analyze portfolio review.
     */
    static utils::Result<void> analyze_portfolio(
        const config::Config& config,
        const std::string& account_filter,
        const std::string& underlying_filter,
        const utils::OutputOptions& output_opts);

    /**
     * Analyze screener opportunities.
     */
    static utils::Result<void> analyze_screener(
        const config::Config& config,
        const utils::OutputOptions& output_opts,
        bool cache_only = false,
        const ScreenerOverrides& overrides = {});
};

} // namespace ibkr::commands
