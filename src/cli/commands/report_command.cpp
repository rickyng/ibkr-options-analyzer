#include "report_command.hpp"
#include "db/database.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "report/report_generator.hpp"
#include "report/csv_exporter.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <algorithm>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;
using analysis::StrategyDetector;
using analysis::RiskCalculator;
using report::ReportGenerator;
using report::CSVExporter;

Result<void> ReportCommand::execute(
    const config::Config& config,
    const std::string& output_path,
    const std::string& report_type,
    const std::string& /* account_filter */,
    const std::string& underlying_filter) {

    Logger::info("Starting report command: type={}", report_type);

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{
            "Failed to initialize database",
            init_result.error().message
        };
    }

    // Detect strategies
    auto strategies_result = StrategyDetector::detect_all_strategies(database, 0);
    if (!strategies_result) {
        return Error{
            "Failed to detect strategies",
            strategies_result.error().message
        };
    }

    auto strategies = *strategies_result;

    // Apply underlying filter
    if (!underlying_filter.empty()) {
        strategies.erase(
            std::remove_if(strategies.begin(), strategies.end(),
                [&](const auto& s) {
                    return s.underlying != underlying_filter;
                }),
            strategies.end()
        );
    }

    if (strategies.empty()) {
        std::cout << "No strategies found.\n";
        return Result<void>{};
    }

    // Calculate risk metrics
    std::vector<analysis::RiskMetrics> metrics;
    for (const auto& strategy : strategies) {
        metrics.push_back(RiskCalculator::calculate_risk(strategy));
    }

    // Build account names map from database
    std::map<int64_t, std::string> account_names;
    try {
        auto db_ptr = database.get_db();
        SQLite::Statement query(*db_ptr, "SELECT id, name FROM accounts");
        while (query.executeStep()) {
            int64_t id = query.getColumn(0).getInt64();
            std::string name = query.getColumn(1).getString();
            account_names[id] = name;
        }
    } catch (const std::exception& e) {
        Logger::warn("Failed to load account names: {}", e.what());
    }

    // Generate report or export CSV
    if (output_path.empty()) {
        // Generate text report to console
        std::string report = ReportGenerator::generate_full_report(strategies, metrics, account_names);
        std::cout << report;

    } else {
        // Export to CSV
        if (report_type == "positions") {
            auto result = CSVExporter::export_positions_csv(strategies, metrics, output_path);
            if (!result) {
                return result;
            }
            std::cout << "Positions exported to: " << output_path << "\n";

        } else if (report_type == "strategies") {
            auto result = CSVExporter::export_strategies_csv(strategies, metrics, output_path);
            if (!result) {
                return result;
            }
            std::cout << "Strategies exported to: " << output_path << "\n";

        } else if (report_type == "summary") {
            auto result = CSVExporter::export_summary_csv(strategies, metrics, output_path);
            if (!result) {
                return result;
            }
            std::cout << "Summary exported to: " << output_path << "\n";

        } else {
            // Default: export strategies
            auto result = CSVExporter::export_strategies_csv(strategies, metrics, output_path);
            if (!result) {
                return result;
            }
            std::cout << "Report exported to: " << output_path << "\n";
        }
    }

    return Result<void>{};
}

} // namespace ibkr::commands
