#pragma once

#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>

namespace ibkr::report {

/**
 * CSV exporter for positions and strategies.
 */
class CSVExporter {
public:
    /**
     * Export positions to CSV file.
     * @param strategies Detected strategies
     * @param metrics Risk metrics for each strategy
     * @param output_path Output CSV file path
     * @return Result indicating success or error
     */
    static utils::Result<void> export_positions_csv(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics,
        const std::string& output_path);

    /**
     * Export strategies to CSV file.
     * @param strategies Detected strategies
     * @param metrics Risk metrics for each strategy
     * @param output_path Output CSV file path
     * @return Result indicating success or error
     */
    static utils::Result<void> export_strategies_csv(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics,
        const std::string& output_path);

    /**
     * Export summary to CSV file.
     * @param strategies Detected strategies
     * @param metrics Risk metrics for each strategy
     * @param output_path Output CSV file path
     * @return Result indicating success or error
     */
    static utils::Result<void> export_summary_csv(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics,
        const std::string& output_path);

private:
    /**
     * Escape CSV field (handle commas, quotes, newlines).
     */
    static std::string escape_csv_field(const std::string& field);
};

} // namespace ibkr::report
