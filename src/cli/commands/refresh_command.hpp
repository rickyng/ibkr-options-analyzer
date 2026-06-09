#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Refresh command: Fetch fresh market prices and earnings dates for open positions.
 *
 * Usage:
 *   ibkr-options-analyzer refresh [--format json]
 *
 * Updates cached_prices and cached_earnings_dates tables in SQLite.
 */
class RefreshCommand {
public:
    /**
     * Execute refresh command.
     *
     * @param config Application configuration
     * @param output_opts Output format options
     * @return Result indicating success or error
     */
    static utils::Result<void> execute(
        const config::Config& config,
        const utils::OutputOptions& output_opts = {});
};

} // namespace ibkr::commands