#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Download command: Download Flex reports from IBKR.
 *
 * Usage:
 *   ibkr-options-analyzer download --token TOKEN --query-id QUERY_ID --account "Account Name"
 *   ibkr-options-analyzer download --token TOKEN --query-id QUERY_ID --account "Account Name" --force
 */
class DownloadCommand {
public:
    /**
     * Execute download command.
     *
     * @param config Application configuration
     * @param token IBKR Flex Web Service token
     * @param query_id IBKR Flex query ID
     * @param account_name Account name for this download
     * @param force Force re-download (skip cache)
     * @return Result indicating success or error
     */
    static utils::Result<void> execute(
        const config::Config& config,
        const std::string& token,
        const std::string& query_id,
        const std::string& account_name,
        bool force = false);
};

} // namespace ibkr::commands
