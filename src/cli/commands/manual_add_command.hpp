#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Manual-add command: Manually add an option position for what-if analysis.
 *
 * Usage:
 *   ibkr-options-analyzer manual-add \
 *     --account "Main Account" \
 *     --underlying AAPL \
 *     --expiry 20250321 \
 *     --strike 150.0 \
 *     --right P \
 *     --quantity -1 \
 *     --premium 2.50 \
 *     --notes "What-if scenario"
 */
class ManualAddCommand {
public:
    /**
     * Execute manual-add command.
     *
     * @param config Application configuration
     * @param account_name Account name
     * @param underlying Underlying symbol (e.g., "AAPL")
     * @param expiry Expiry date in YYYYMMDD format (e.g., "20250321")
     * @param strike Strike price (e.g., 150.0)
     * @param right Option right ('C' or 'P')
     * @param quantity Quantity (negative=short, positive=long)
     * @param premium Entry premium per share (e.g., 2.50)
     * @param notes Optional notes
     * @param output_opts Output format options
     * @return Result indicating success or error
     */
    static utils::Result<void> execute(
        const config::Config& config,
        const std::string& account_name,
        const std::string& underlying,
        const std::string& expiry,
        double strike,
        const std::string& right,
        double quantity,
        double premium,
        const std::string& notes = "",
        const utils::OutputOptions& output_opts = {});

private:
    /**
     * Validate input parameters.
     */
    static utils::Result<void> validate_input(
        const std::string& underlying,
        const std::string& expiry,
        double strike,
        const std::string& right,
        double quantity,
        double premium);

    /**
     * Convert YYYYMMDD to YYYY-MM-DD format.
     */
    static utils::Result<std::string> convert_expiry_format(const std::string& yyyymmdd);

    /**
     * Generate option symbol from components.
     * Format: "AAPL250321P150"
     */
    static std::string generate_symbol(
        const std::string& underlying,
        const std::string& expiry_yyyymmdd,
        double strike,
        char right);
};

} // namespace ibkr::commands
