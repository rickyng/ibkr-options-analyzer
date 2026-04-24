#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include <string>

namespace ibkr::commands {

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
        const utils::OutputOptions& output_opts = {});

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
};

} // namespace ibkr::commands
