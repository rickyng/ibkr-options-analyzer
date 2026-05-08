#include "csv_exporter.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace ibkr::report {

using utils::Result;
using utils::Error;
using utils::Logger;
using analysis::Strategy;
using analysis::RiskMetrics;
using analysis::RiskCalculator;
using analysis::StrategyDetector;

utils::Result<void> CSVExporter::export_positions_csv(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics,
    const std::string& output_path) {

    Logger::info("Exporting positions to CSV: {}", output_path);

    try {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            return Error{"Failed to open output file", output_path};
        }

        // Write header
        file << "Underlying,Expiry,Strike,Right,Quantity,Entry Premium,Mark Price,"
             << "Days to Expiry,Is Manual,Breakeven,Max Profit,Max Loss,Risk Level\n";

        // Write positions (single-leg strategies only)
        for (size_t i = 0; i < strategies.size(); ++i) {
            const auto& strategy = strategies[i];
            const auto& metric = metrics[i];

            if (strategy.legs.size() == 1) {
                const auto& leg = strategy.legs[0];

                file << escape_csv_field(leg.underlying) << ","
                     << escape_csv_field(leg.expiry) << ","
                     << std::fixed << std::setprecision(2) << leg.strike << ","
                     << leg.right << ","
                     << leg.quantity << ","
                     << leg.entry_premium << ","
                     << leg.mark_price << ","
                     << metric.days_to_expiry << ","
                     << (leg.is_manual ? "1" : "0") << ","
                     << metric.breakeven_price << ","
                     << metric.max_profit << ",";

                if (std::isinf(metric.max_loss)) {
                    file << "UNLIMITED";
                } else {
                    file << metric.max_loss;
                }

                file << "," << escape_csv_field(analysis::risk_level_to_string(metric.risk_level)) << "\n";
            }
        }

        file.close();
        Logger::info("Positions exported successfully");
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to export positions CSV",
            std::string(e.what())
        };
    }
}

utils::Result<void> CSVExporter::export_strategies_csv(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics,
    const std::string& output_path) {

    Logger::info("Exporting strategies to CSV: {}", output_path);

    try {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            return Error{"Failed to open output file", output_path};
        }

        // Write header
        file << "Strategy Type,Underlying,Expiry,Legs,Breakeven,Breakeven 2,"
             << "Max Profit,Max Loss,Risk Level,Net Premium,Days to Expiry\n";

        // Write strategies
        for (size_t i = 0; i < strategies.size(); ++i) {
            const auto& strategy = strategies[i];
            const auto& metric = metrics[i];

            file << escape_csv_field(StrategyDetector::strategy_type_to_string(strategy.type)) << ","
                 << escape_csv_field(strategy.underlying) << ","
                 << escape_csv_field(strategy.expiry) << ",";

            // Format legs
            std::ostringstream legs_str;
            for (size_t j = 0; j < strategy.legs.size(); ++j) {
                const auto& leg = strategy.legs[j];
                if (j > 0) legs_str << "; ";
                legs_str << "$" << std::fixed << std::setprecision(2) << leg.strike
                         << " " << leg.right << " x " << leg.quantity
                         << " @ $" << leg.entry_premium;
            }
            file << escape_csv_field(legs_str.str()) << ",";

            // Breakevens
            file << std::fixed << std::setprecision(2) << metric.breakeven_price << ",";
            if (metric.breakeven_price_2 > 0) {
                file << metric.breakeven_price_2;
            }
            file << ",";

            // Max profit/loss
            file << metric.max_profit << ",";
            if (std::isinf(metric.max_loss)) {
                file << "UNLIMITED";
            } else {
                file << metric.max_loss;
            }
            file << ",";

            // Risk level, net premium, days to expiry
            file << escape_csv_field(analysis::risk_level_to_string(metric.risk_level)) << ","
                 << metric.net_premium << ","
                 << metric.days_to_expiry << "\n";
        }

        file.close();
        Logger::info("Strategies exported successfully");
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to export strategies CSV",
            std::string(e.what())
        };
    }
}

utils::Result<void> CSVExporter::export_summary_csv(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics,
    const std::string& output_path) {

    Logger::info("Exporting summary to CSV: {}", output_path);

    try {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            return Error{"Failed to open output file", output_path};
        }

        // Calculate portfolio metrics
        auto portfolio_risk = RiskCalculator::calculate_portfolio_risk(strategies, metrics);

        // Write header
        file << "Metric,Value\n";

        // Write metrics
        file << "Total Positions," << strategies.size() << "\n";
        file << "Total Strategies," << portfolio_risk.total_strategies << "\n";
        file << "Total Max Profit," << std::fixed << std::setprecision(2)
             << portfolio_risk.total_max_profit << "\n";
        file << "Total Max Loss," << portfolio_risk.total_max_loss << "\n";
        file << "10% Loss," << portfolio_risk.total_loss_10pct << "\n";
        file << "20% Loss," << portfolio_risk.total_loss_20pct << "\n";
        file << "Expiring in 7 Days," << portfolio_risk.positions_expiring_soon << "\n";

        file.close();
        Logger::info("Summary exported successfully");
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to export summary CSV",
            std::string(e.what())
        };
    }
}

std::string CSVExporter::escape_csv_field(const std::string& field) {
    // Check if field needs escaping
    bool needs_escape = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_escape = true;
            break;
        }
    }

    if (!needs_escape) {
        return field;
    }

    // Escape quotes by doubling them
    std::string escaped;
    escaped.reserve(field.size() + 10);
    escaped += '"';

    for (char c : field) {
        if (c == '"') {
            escaped += "\"\"";
        } else {
            escaped += c;
        }
    }

    escaped += '"';
    return escaped;
}

} // namespace ibkr::report
