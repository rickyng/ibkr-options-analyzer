#pragma once

#include "config/config_manager.hpp"
#include "flex/flex_downloader.hpp"
#include "utils/result.hpp"
#include <string>

namespace ibkr::services {

struct DownloadResult {
    std::string file_path;
    std::string account_name;
};

class FlexService {
public:
    explicit FlexService(const config::Config& config);

    utils::Result<DownloadResult> download_report(
        const std::string& token,
        const std::string& query_id,
        const std::string& account_name,
        bool force = false);

private:
    const config::Config& config_;
};

} // namespace ibkr::services