#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include <string>
#include <vector>

namespace ibkr::commands {

class ImportHistoryCommand {
public:
    static utils::Result<void> execute(
        const config::Config& config,
        const std::vector<std::string>& files,
        const std::string& account = "",
        const utils::OutputOptions& output_opts = {});
};

} // namespace ibkr::commands
