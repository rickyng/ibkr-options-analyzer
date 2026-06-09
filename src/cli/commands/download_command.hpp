#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include <string>

namespace ibkr::commands {

/**
 * Download command: Download Flex reports from IBKR.
 *
 * Usage:
 *   ibkr-options-analyzer download --account "Account Name"
 *   ibkr-options-analyzer download --account "Account Name" --force
 *   ibkr-options-analyzer download --token TOKEN --query-id QID --account "Account Name"
 */
class DownloadCommand {
public:
    static utils::Result<void> execute(
        const config::Config& config,
        const std::string& token,
        const std::string& query_id,
        const std::string& account_name,
        bool force = false,
        const utils::OutputOptions& output_opts = {});

    /**
     * Look up an account by name in config. Returns nullptr if not found.
     */
    static const config::AccountConfig* find_account(
        const config::Config& config,
        const std::string& account_name);
};

} // namespace ibkr::commands
