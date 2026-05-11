#pragma once

#include "db/database.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>

namespace ibkr::services {

struct TradeOverviewMetrics {
    int total_trades{0};
    int winning_trades{0};
    int losing_trades{0};
    double win_rate{0.0};
    double total_pnl{0.0};
    double avg_winner{0.0};
    double avg_loser{0.0};
    double profit_factor{0.0};
    double expectancy{0.0};
    double avg_holding_days{0.0};
    double avg_roc{0.0};
    double avg_annualized_return{0.0};
};

struct RoundTripDisplay {
    int64_t id{0};
    std::string account_name;
    std::string underlying;
    double strike{0.0};
    char right{'P'};
    std::string expiry;
    int quantity{0};
    std::string open_date;
    std::string close_date;
    int holding_days{0};
    double open_price{0.0};
    double net_premium{0.0};
    double realized_pnl{0.0};
    double commission{0.0};
    double roc{0.0};
    double annualized_return{0.0};
    std::string close_reason;
    std::string strategy_type;
};

struct StrategyPerformance {
    std::string strategy_type;
    int trade_count{0};
    int winning_trades{0};
    double win_rate{0.0};
    double total_pnl{0.0};
    double avg_pnl{0.0};
    double profit_factor{0.0};
    double avg_holding_days{0.0};
};

struct DTEBucketPerformance {
    std::string dte_bucket;
    int trade_count{0};
    int winning_trades{0};
    double win_rate{0.0};
    double total_pnl{0.0};
};

struct UnderlyingPerformance {
    std::string underlying;
    int trade_count{0};
    int winning_trades{0};
    double win_rate{0.0};
    double total_pnl{0.0};
    double avg_pnl{0.0};
};

struct LossCluster {
    std::string cluster_key;
    std::string underlying;
    std::string dte_bucket;
    int loss_count{0};
    double total_loss{0.0};
    std::vector<int64_t> trade_ids;
};

struct LossStreakInfo {
    int max_consecutive_losses{0};
    std::string streak_end_date;
    std::string recovery_date;
    int recovery_days{0};
    int current_streak{0};
};

class TradeAnalyticsService {
public:
    explicit TradeAnalyticsService(db::Database& database);

    utils::Result<TradeOverviewMetrics> get_overview_metrics(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "",
        const std::string& strategy_type = "",
        const std::string& underlying = "");

    utils::Result<std::vector<RoundTripDisplay>> get_round_trips(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "",
        const std::string& strategy_type = "",
        const std::string& underlying = "");

    utils::Result<std::vector<StrategyPerformance>> get_strategy_performance(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "");

    utils::Result<std::vector<DTEBucketPerformance>> get_dte_breakdown(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "");

    utils::Result<std::vector<UnderlyingPerformance>> get_underlying_breakdown(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "");

    utils::Result<std::vector<LossCluster>> get_loss_clusters(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "");

    utils::Result<LossStreakInfo> get_streak_info(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "");

private:
    static double calculate_roc(const db::Database::RoundTrip& rt);
    static std::string get_dte_bucket(int holding_days);

    db::Database& database_;
};

} // namespace ibkr::services