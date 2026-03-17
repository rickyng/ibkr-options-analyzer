#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Report command: Generate comprehensive reports.
 *
 * Usage:
 *   ibkr-options-analyzer report
 *   ibkr-options-analyzer report --output report.csv
 *   ibkr-options-analyzer report --output positions.csv --type positions
 *   ibkr-options-analyzer report --output strategies.csv --type strategies
 *   ibkr-options-analyzer report --account "Main Account"
 *   ibkr-options-analyzer report --underlying AAPL
 */
class ReportCommand {
public:
    /**
     * Execute report command.
     *
     * @param config Application configuration
     * @param output_path Optional output CSV file path
     * @param report_type Type of report (full|positions|strategies|summary)
     * @param account_filter Optional account name filter
     * @param underlying_filter Optional underlying symbol filter
     * @return Result indicating success or error
     */
    static utils::Result<void> execute(
        const config::Config& config,
        const std::string& output_path = "",
        const std::string& report_type = "full",
        const std::string& account_filter = "",
        const std::string& underlying_filter = "");
};

} // namespace ibkr::commands
