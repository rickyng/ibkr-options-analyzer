#include "download_command.hpp"
#include "flex/flex_downloader.hpp"
#include "utils/logger.hpp"
#include <iostream>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<void> DownloadCommand::execute(
    const config::Config& config,
    const std::string& token,
    const std::string& query_id,
    const std::string& account_name,
    bool force,
    const utils::OutputOptions& output_opts) {

    Logger::info("Starting download command for account: {}", account_name);

    if (force) {
        Logger::info("Force mode enabled (cache will be ignored)");
    }

    // Validate inputs
    if (token.empty()) {
        return Error{"Token is required", "Use --token to specify IBKR Flex token"};
    }

    if (query_id.empty()) {
        return Error{"Query ID is required", "Use --query-id to specify IBKR Flex query ID"};
    }

    if (account_name.empty()) {
        return Error{"Account name is required", "Use --account to specify account name"};
    }

    // Create temporary account config
    config::AccountConfig account;
    account.name = account_name;
    account.token = token;
    account.query_id = query_id;
    account.enabled = true;

    // Create Flex downloader
    flex::FlexDownloader downloader(config);

    // Download report
    auto result = downloader.download_report(account);
    if (!result) {
        return Error{
            "Download failed",
            result.error().format()
        };
    }

    Logger::info("Download complete: {}", *result);

    if (output_opts.json) {
        std::cout << utils::JsonOutput::download_result(*result, account_name) << "\n";
    } else if (!output_opts.quiet) {
        std::cout << "✓ Downloaded: " << *result << "\n";
    }

    return Result<void>{};
}

} // namespace ibkr::commands
