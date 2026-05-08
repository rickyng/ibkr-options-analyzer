#pragma once

#include "db/database.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>

namespace ibkr::services {

struct PositionWithPrice {
    analysis::Position position;
    std::string account_name;
    double current_price{0.0};
    bool has_price{false};
    double distance_from_strike_pct{0.0};
    bool in_the_money{false};
    std::string risk_category;
};

struct PortfolioSummary {
    int total_positions{0};
    int short_puts{0};
    int short_calls{0};
    int long_positions{0};
    double total_premium_collected{0.0};
    double total_max_loss{0.0};
    int expiring_7_days{0};
    int expiring_30_days{0};
};

class PositionService {
public:
    explicit PositionService(db::Database& database);

    utils::Result<std::vector<analysis::Position>> load_positions(
        const std::string& account_filter = "",
        const std::string& underlying_filter = "");

    utils::Result<std::map<int64_t, std::string>> load_account_names();

    utils::Result<int> get_position_count();
    utils::Result<int> get_trade_count();

    static PortfolioSummary calculate_portfolio_summary(
        const std::vector<analysis::Position>& positions);

private:
    db::Database& database_;
};

} // namespace ibkr::services