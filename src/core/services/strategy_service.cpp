#include "strategy_service.hpp"
#include "utils/logger.hpp"
#include <SQLiteCpp/SQLiteCpp.h>
#include <algorithm>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

StrategyService::StrategyService(db::Database& database)
    : database_(database) {}

Result<StrategyAnalysis> StrategyService::analyze_strategies(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    auto strategies_result = analysis::StrategyDetector::detect_all_strategies(database_, 0);
    if (!strategies_result) {
        return Error{"Failed to detect strategies", strategies_result.error().message};
    }

    auto strategies = *strategies_result;

    // Apply underlying filter
    if (!underlying_filter.empty()) {
        strategies.erase(
            std::remove_if(strategies.begin(), strategies.end(),
                [&](const analysis::Strategy& s) {
                    return s.underlying != underlying_filter;
                }),
            strategies.end()
        );
    }

    // Apply account filter
    if (!account_filter.empty()) {
        auto db_ptr = database_.get_db();
        strategies.erase(
            std::remove_if(strategies.begin(), strategies.end(),
                [&](const analysis::Strategy& s) {
                    if (s.legs.empty()) return true;
                    SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
                    query.bind(1, s.legs[0].account_id);
                    if (query.executeStep()) {
                        std::string name = query.getColumn(0).getString();
                        return name != account_filter;
                    }
                    return true;
                }),
            strategies.end()
        );
    }

    // Calculate risk metrics
    std::vector<analysis::RiskMetrics> all_metrics;
    for (const auto& strategy : strategies) {
        all_metrics.push_back(analysis::RiskCalculator::calculate_risk(strategy));
    }

    StrategyAnalysis result;
    result.strategies = std::move(strategies);
    result.metrics = std::move(all_metrics);
    result.portfolio_risk = analysis::RiskCalculator::calculate_portfolio_risk(
        result.strategies, result.metrics);
    result.account_risks = analysis::RiskCalculator::calculate_account_risks(
        result.strategies, result.metrics);
    result.underlying_exposure = analysis::RiskCalculator::calculate_underlying_exposure(
        result.strategies, result.metrics);

    return result;
}

std::string StrategyService::strategy_type_to_string(analysis::Strategy::Type type) {
    return analysis::StrategyDetector::strategy_type_to_string(type);
}

} // namespace ibkr::services