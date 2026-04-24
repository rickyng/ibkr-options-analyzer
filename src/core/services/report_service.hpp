#pragma once

#include "db/database.hpp"
#include "report/report_generator.hpp"
#include "report/csv_exporter.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>

namespace ibkr::services {

struct ReportData {
    std::vector<db::Database::PositionInfo> positions;
    std::vector<db::Database::RiskSummary> risk_summaries;
    std::vector<db::Database::ExposureInfo> exposures;
    std::vector<analysis::Strategy> strategies;
    std::vector<analysis::RiskMetrics> metrics;
    std::map<int64_t, std::string> account_names;
};

class ReportService {
public:
    explicit ReportService(db::Database& database);

    utils::Result<ReportData> gather_report_data(
        const std::string& account_filter = "",
        const std::string& underlying_filter = "");

    utils::Result<void> export_csv(
        const ReportData& data,
        const std::string& output_path,
        const std::string& report_type = "full");

    std::string generate_text_report(
        const ReportData& data);

private:
    db::Database& database_;
};

} // namespace ibkr::services