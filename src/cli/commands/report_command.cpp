#include "report_command.hpp"
#include "services/report_service.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include "utils/subprocess.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;
using json = nlohmann::json;

Result<void> ReportCommand::execute(
    const config::Config& config,
    const std::string& output_path,
    const std::string& report_type,
    const std::string& account_filter,
    const std::string& underlying_filter,
    const utils::OutputOptions& output_opts) {

    Logger::info("Starting report command: type={}", report_type);

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::ReportService report_service(database);
    auto data_result = report_service.gather_report_data(account_filter, underlying_filter);
    if (!data_result) {
        return Error{"Failed to gather report data", data_result.error().message};
    }

    const auto& data = *data_result;

    if (output_opts.json) {
        std::cout << utils::JsonOutput::report(data.positions, data.risk_summaries, data.exposures) << "\n";
    }

    if (output_opts.google_sheet) {
        json report_json = utils::JsonOutput::report(data.positions, data.risk_summaries, data.exposures);

        auto script_result = utils::run_script(
            "scripts/sheets_exporter.py", {"report"}, report_json.dump());
        if (script_result) {
            std::cout << "\nGoogle Sheet created: " << *script_result << "\n";
        } else {
            Logger::error("Google Sheets export failed: {}", script_result.error().format());
        }
    }

    if (output_opts.quiet) return Result<void>{};

    if (!output_opts.json) {
        if (output_path.empty()) {
            std::cout << report_service.generate_text_report(data);
        } else {
            auto export_result = report_service.export_csv(data, output_path, report_type);
            if (!export_result) return export_result;
            std::cout << "Report exported to: " << output_path << "\n";
        }
    }

    return Result<void>{};
}

} // namespace ibkr::commands