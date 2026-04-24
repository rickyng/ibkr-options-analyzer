#pragma once

#include "db/database.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>

namespace ibkr::services {

struct StrategyAnalysis {
    std::vector<analysis::Strategy> strategies;
    std::vector<analysis::RiskMetrics> metrics;
    analysis::RiskCalculator::PortfolioRisk portfolio_risk;
    std::vector<analysis::RiskCalculator::AccountRisk> account_risks;
    std::vector<analysis::RiskCalculator::UnderlyingExposure> underlying_exposure;
};

class StrategyService {
public:
    explicit StrategyService(db::Database& database);

    utils::Result<StrategyAnalysis> analyze_strategies(
        const std::string& account_filter = "",
        const std::string& underlying_filter = "");

    static std::string strategy_type_to_string(analysis::Strategy::Type type);

private:
    db::Database& database_;
};

} // namespace ibkr::services