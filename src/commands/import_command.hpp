#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Import command: Import downloaded CSV files into database.
 *
 * Usage:
 *   ibkr-options-analyzer import
 *   ibkr-options-analyzer import --file /path/to/flex_report.csv
 *   ibkr-options-analyzer import --account "Main Account"
 */
class ImportCommand {
public:
    /**
     * Execute import command.
     *
     * @param config Application configuration
     * @param file_path Optional specific file to import (empty = auto-discover)
     * @param account_filter Optional account name filter
     * @param options_only If true, only import option records
     * @param clear_existing If true, clear existing open positions before import
     * @return Result indicating success or error
     */
    static utils::Result<void> execute(
        const config::Config& config,
        const std::string& file_path = "",
        const std::string& account_filter = "",
        bool options_only = false,
        bool clear_existing = true);

private:
    /**
     * Auto-discover CSV files in downloads directory.
     */
    static utils::Result<std::vector<std::string>> discover_csv_files(
        const std::string& downloads_dir);

    /**
     * Extract account name from filename.
     * e.g., "flex_report_Main_Account_20260315_233006.csv" -> "Main Account"
     */
    static std::string extract_account_name(const std::string& filename);
};

} // namespace ibkr::commands
