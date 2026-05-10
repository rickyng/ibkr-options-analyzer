#pragma once

#include "config/config_manager.hpp"
#include "utils/result.hpp"
#include "utils/json_output.hpp"
#include <string>

namespace ibkr::commands {

class TradesCommand {
public:
    static utils::Result<void> execute(
        const config::Config& config,
        bool rebuild = false,
        const std::string& date_from = "",
        const std::string& date_to = "",
        const std::string& strategy_type = "",
        const std::string& underlying = "",
        const std::string& account = "",
        const utils::OutputOptions& output_opts = {});
};

} // namespace ibkr::commands
