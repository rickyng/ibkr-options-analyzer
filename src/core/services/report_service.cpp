#include "report_service.hpp"
#include "utils/logger.hpp"
#include <algorithm>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

ReportService::ReportService(db::Database& database)
    : database_(database) {}

Result<ReportData> ReportService::gather_report_data(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    ReportData data;

    // Load positions from database
    auto pos_result = database_.get_all_positions(account_filter);
    if (!pos_result) {
        return Error{"Failed to load positions", pos_result.error().message};
    }
    data.positions = *pos_result;

    // Load risk summaries
    auto risk_result = database_.get_consolidated_risk();
    if (!risk_result) {
        return Error{"Failed to load risk summaries", risk_result.error().message};
    }
    data.risk_summaries = *risk_result;

    // Load exposures
    auto exp_result = database_.get_underlying_exposure();
    if (!exp_result) {
        return Error{"Failed to load exposures", exp_result.error().message};
    }
    data.exposures = *exp_result;

    // Detect strategies and calculate risk
    auto strategies_result = analysis::StrategyDetector::detect_all_strategies(database_, 0);
    if (strategies_result) {
        auto strategies = *strategies_result;

        if (!underlying_filter.empty()) {
            strategies.erase(
                std::remove_if(strategies.begin(), strategies.end(),
                    [&](const auto& s) { return s.underlying != underlying_filter; }),
                strategies.end()
            );
        }

        for (const auto& strategy : strategies) {
            data.metrics.push_back(analysis::RiskCalculator::calculate_risk(strategy));
        }
        data.strategies = std::move(strategies);
    }

    // Load account names
    try {
        auto db_ptr = database_.get_db();
        if (db_ptr) {
            SQLite::Statement q(*db_ptr, "SELECT id, name FROM accounts");
            while (q.executeStep()) {
                data.account_names[q.getColumn(0).getInt64()] = q.getColumn(1).getString();
            }
        }
    } catch (const std::exception& e) {
        Logger::warn("Failed to load account names: {}", e.what());
    }

    return data;
}

Result<void> ReportService::export_csv(
    const ReportData& data,
    const std::string& output_path,
    const std::string& report_type) {

    if (report_type == "positions") {
        return report::CSVExporter::export_positions_csv(
            data.strategies, data.metrics, output_path);
    } else if (report_type == "strategies") {
        return report::CSVExporter::export_strategies_csv(
            data.strategies, data.metrics, output_path);
    } else if (report_type == "summary") {
        return report::CSVExporter::export_summary_csv(
            data.strategies, data.metrics, output_path);
    } else {
        return report::CSVExporter::export_strategies_csv(
            data.strategies, data.metrics, output_path);
    }
}

std::string ReportService::generate_text_report(const ReportData& data) {
    return report::ReportGenerator::generate_full_report(
        data.strategies, data.metrics, data.account_names);
}

} // namespace ibkr::services