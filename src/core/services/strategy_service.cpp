#include "strategy_service.hpp"
#include "utils/logger.hpp"
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

    // Load positions from database
    auto positions_result = database_.get_all_positions(account_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    // Filter by underlying if specified
    if (!underlying_filter.empty()) {
        positions.erase(
            std::remove_if(positions.begin(), positions.end(),
                [&](const analysis::Position& pos) {
                    return pos.underlying != underlying_filter;
                }),
            positions.end()
        );
    }

    // Detect strategies
    auto strategies = analysis::StrategyDetector::detect_all_strategies(positions);

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