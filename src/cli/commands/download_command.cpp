#include "download_command.hpp"
#include "services/flex_service.hpp"
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

    services::FlexService flex_service(config);
    auto result = flex_service.download_report(token, query_id, account_name, force);
    if (!result) {
        return Error{"Download failed", result.error().message};
    }

    if (output_opts.json) {
        std::cout << utils::JsonOutput::download_result(result->file_path, result->account_name) << "\n";
    } else if (!output_opts.quiet) {
        std::cout << "✓ Downloaded: " << result->file_path << "\n";
    }

    return Result<void>{};
}

} // namespace ibkr::commands