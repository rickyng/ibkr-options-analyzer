#include "flex_service.hpp"
#include "utils/logger.hpp"

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

FlexService::FlexService(const config::Config& config)
    : config_(config) {}

Result<DownloadResult> FlexService::download_report(
    const std::string& token,
    const std::string& query_id,
    const std::string& account_name,
    bool force) {

    if (token.empty()) {
        return Error{"Token is required"};
    }
    if (query_id.empty()) {
        return Error{"Query ID is required"};
    }
    if (account_name.empty()) {
        return Error{"Account name is required"};
    }

    config::AccountConfig account;
    account.name = account_name;
    account.token = token;
    account.query_id = query_id;
    account.enabled = true;

    flex::FlexDownloader downloader(config_);
    auto result = downloader.download_report(account);
    if (!result) {
        return Error{"Download failed", result.error().format()};
    }

    Logger::info("Download complete: {}", *result);

    DownloadResult dr;
    dr.file_path = *result;
    dr.account_name = account_name;
    return dr;
}

} // namespace ibkr::services