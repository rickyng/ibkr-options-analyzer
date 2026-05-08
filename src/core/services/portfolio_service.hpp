#pragma once

#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/price_fetcher.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>

namespace ibkr::services {

struct PortfolioPosition {
    analysis::Position position;
    std::string account_name;
    double current_price{0.0};
    bool has_price{false};
    double current_premium{0.0};
    double pnl{0.0};
    double pnl_percent{0.0};
    double otm_percent{0.0};
    double annualized_yield{0.0};
    std::string risk_alert;  // "ITM", "NEAR", "EXPIRING", ""
};

struct PortfolioView {
    std::vector<PortfolioPosition> positions;
    double total_premium_collected{0.0};
    double total_unrealized_pnl{0.0};
    int total_positions{0};
    int itm_count{0};
    int near_money_count{0};
    int expiring_soon_count{0};
    std::map<std::string, double> loss_10pct;  // account -> loss
    std::map<std::string, double> loss_20pct;  // account -> loss
    std::map<std::string, int> dte_buckets;    // label -> count
};

class PortfolioService {
public:
    PortfolioService() = default;

    PortfolioView build_portfolio_view(
        const std::vector<analysis::Position>& positions,
        const std::map<std::string, utils::StockPrice>& current_prices,
        const std::map<int64_t, std::string>& account_names);
};

} // namespace ibkr::services
