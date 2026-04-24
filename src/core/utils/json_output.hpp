#pragma once

#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "db/database.hpp"
#include "utils/price_fetcher.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>

namespace ibkr::utils {

/**
 * Output format options for CLI commands.
 */
struct OutputOptions {
    bool json{false};       // Output in JSON format
    bool quiet{false};      // Suppress human-readable output (only JSON)
};

/**
 * JSON serialization helpers for CLI commands.
 *
 * Provides consistent JSON output format for Python dashboard integration.
 */
class JsonOutput {
public:
    // --- Command-level JSON responses ---

    /**
     * Success response wrapper.
     */
    static nlohmann::json success(const std::string& message = "OK");

    /**
     * Error response wrapper.
     */
    static nlohmann::json error(const std::string& message, const std::string& detail = "");

    // --- Analyze command JSON ---

    /**
     * Serialize open positions analysis.
     */
    static nlohmann::json open_positions(
        const std::vector<analysis::Position>& positions,
        const std::map<std::string, StockPrice>& current_prices,
        const std::map<int64_t, std::string>& account_names);

    /**
     * Serialize strategy analysis.
     */
    static nlohmann::json strategies(
        const std::vector<analysis::Strategy>& strategies,
        const std::vector<analysis::RiskMetrics>& metrics,
        const std::map<int64_t, std::string>& account_names);

    /**
     * Serialize impact analysis for a single underlying.
     */
    static nlohmann::json impact_analysis(
        const std::string& underlying,
        const std::vector<analysis::Position>& positions,
        const std::vector<analysis::RiskMetrics>& metrics);

    // --- Report command JSON ---

    /**
     * Serialize comprehensive report.
     */
    static nlohmann::json report(
        const std::vector<db::Database::PositionInfo>& positions,
        const std::vector<db::Database::RiskSummary>& risk_summaries,
        const std::vector<db::Database::ExposureInfo>& exposures);

    // --- Import command JSON ---

    /**
     * Serialize import result.
     */
    static nlohmann::json import_result(
        int trades_imported,
        int positions_imported,
        const std::string& file_path);

    // --- Download command JSON ---

    /**
     * Serialize download result.
     */
    static nlohmann::json download_result(
        const std::string& file_path,
        const std::string& account_name);

    // --- Manual-add command JSON ---

    /**
     * Serialize manual position add result.
     */
    static nlohmann::json manual_add_result(
        int64_t position_id,
        const std::string& symbol,
        const std::string& underlying);

    // --- Database CRUD JSON ---

    /**
     * Serialize account info.
     */
    static nlohmann::json account(const db::Database::AccountInfo& info);

    /**
     * Serialize list of accounts.
     */
    static nlohmann::json accounts(const std::vector<db::Database::AccountInfo>& accounts);

    /**
     * Serialize position info.
     */
    static nlohmann::json position(const db::Database::PositionInfo& pos);

    /**
     * Serialize list of positions.
     */
    static nlohmann::json positions(const std::vector<db::Database::PositionInfo>& positions);

private:
    /**
     * Serialize single Position to JSON object.
     */
    static nlohmann::json position_to_json(
        const analysis::Position& pos,
        const std::string& account_name,
        const StockPrice* current_price = nullptr);

    /**
     * Serialize single Strategy to JSON object.
     */
    static nlohmann::json strategy_to_json(
        const analysis::Strategy& strategy,
        const analysis::RiskMetrics& metrics,
        const std::string& account_name);

    /**
     * Serialize RiskMetrics to JSON object.
     */
    static nlohmann::json risk_metrics_to_json(const analysis::RiskMetrics& metrics);

    /**
     * Categorize position risk level based on distance from strike.
     */
    static std::string categorize_risk_level(
        const analysis::Position& pos,
        double current_price);
};

} // namespace ibkr::utils