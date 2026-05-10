#include "trade_analytics_service.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;
using RoundTrip = db::Database::RoundTrip;

TradeAnalyticsService::TradeAnalyticsService(db::Database& database)
    : database_(database) {}

// ---------------------------------------------------------------------------
// Overview metrics
// ---------------------------------------------------------------------------

Result<TradeOverviewMetrics> TradeAnalyticsService::get_overview_metrics(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to,
    const std::string& strategy_type,
    const std::string& underlying) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to,
                                                   strategy_type, underlying);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    const auto& trips = *trips_result;

    TradeOverviewMetrics m;
    m.total_trades = static_cast<int>(trips.size());

    if (trips.empty()) {
        return m;
    }

    double sum_winners = 0.0;
    double sum_losers = 0.0;
    double sum_roc = 0.0;
    double sum_annualized = 0.0;
    double sum_holding = 0.0;
    int annualized_count = 0;

    for (const auto& rt : trips) {
        m.total_pnl += rt.realized_pnl;
        sum_holding += rt.holding_days;

        if (rt.realized_pnl > 0.0) {
            ++m.winning_trades;
            sum_winners += rt.realized_pnl;
        } else {
            ++m.losing_trades;
            sum_losers += rt.realized_pnl;  // negative
        }

        double roc = calculate_roc(rt);
        sum_roc += roc;

        if (rt.holding_days > 0) {
            sum_annualized += (roc * 365.0 / static_cast<double>(rt.holding_days));
            ++annualized_count;
        }
    }

    m.win_rate = static_cast<double>(m.winning_trades) / static_cast<double>(m.total_trades);
    m.avg_winner = m.winning_trades > 0 ? sum_winners / m.winning_trades : 0.0;
    m.avg_loser = m.losing_trades > 0 ? std::abs(sum_losers) / m.losing_trades : 0.0;

    // profit_factor: gross wins / gross losses. Use 999.9 sentinel when no losses.
    m.profit_factor = (sum_losers < 0.0) ? sum_winners / std::abs(sum_losers) : 999.9;

    double loss_rate = 1.0 - m.win_rate;
    m.expectancy = (m.win_rate * m.avg_winner) - (loss_rate * m.avg_loser);

    m.avg_holding_days = sum_holding / static_cast<double>(m.total_trades);
    m.avg_roc = sum_roc / static_cast<double>(m.total_trades);
    m.avg_annualized_return = annualized_count > 0
        ? sum_annualized / static_cast<double>(annualized_count) : 0.0;

    return m;
}

// ---------------------------------------------------------------------------
// Round trip display list
// ---------------------------------------------------------------------------

Result<std::vector<RoundTripDisplay>> TradeAnalyticsService::get_round_trips(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to,
    const std::string& strategy_type,
    const std::string& underlying) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to,
                                                   strategy_type, underlying);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    const auto& trips = *trips_result;
    std::vector<RoundTripDisplay> display;
    display.reserve(trips.size());

    // Resolve account names in batch to avoid per-row lookups
    auto accounts_result = database_.list_accounts();
    std::unordered_map<int64_t, std::string> account_names;
    if (accounts_result) {
        for (const auto& acct : *accounts_result) {
            account_names[acct.id] = acct.name;
        }
    }

    for (const auto& rt : trips) {
        RoundTripDisplay d;
        d.id = rt.id;
        d.account_name = account_names.count(rt.account_id)
                             ? account_names[rt.account_id]
                             : std::to_string(rt.account_id);
        d.underlying = rt.underlying;
        d.strike = rt.strike;
        d.right = rt.right;
        d.expiry = rt.expiry;
        d.quantity = rt.quantity;
        d.open_date = rt.open_date;
        d.close_date = rt.close_date;
        d.holding_days = rt.holding_days;
        d.net_premium = rt.net_premium;
        d.realized_pnl = rt.realized_pnl;
        d.commission = rt.commission;
        d.roc = calculate_roc(rt);
        d.annualized_return = rt.holding_days > 0
                                  ? (d.roc * 365.0 / static_cast<double>(rt.holding_days))
                                  : 0.0;
        d.close_reason = rt.close_reason;
        d.strategy_type = rt.strategy_type;
        display.push_back(std::move(d));
    }

    return display;
}

// ---------------------------------------------------------------------------
// Strategy performance breakdown
// ---------------------------------------------------------------------------

Result<std::vector<StrategyPerformance>> TradeAnalyticsService::get_strategy_performance(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    const auto& trips = *trips_result;

    // Group by strategy_type
    struct Accum {
        int count{0};
        int winners{0};
        double total_pnl{0.0};
        double sum_wins{0.0};
        double sum_losses{0.0};
        double sum_holding{0.0};
    };

    std::map<std::string, Accum> groups;
    for (const auto& rt : trips) {
        auto& acc = groups[rt.strategy_type];
        ++acc.count;
        acc.total_pnl += rt.realized_pnl;
        acc.sum_holding += rt.holding_days;
        if (rt.realized_pnl > 0.0) {
            ++acc.winners;
            acc.sum_wins += rt.realized_pnl;
        } else {
            acc.sum_losses += rt.realized_pnl;
        }
    }

    std::vector<StrategyPerformance> result;
    result.reserve(groups.size());
    for (auto& [stype, acc] : groups) {
        StrategyPerformance sp;
        sp.strategy_type = stype;
        sp.trade_count = acc.count;
        sp.winning_trades = acc.winners;
        sp.win_rate = acc.count > 0 ? static_cast<double>(acc.winners) / acc.count : 0.0;
        sp.total_pnl = acc.total_pnl;
        sp.avg_pnl = acc.count > 0 ? acc.total_pnl / acc.count : 0.0;
        sp.profit_factor = acc.sum_losses < 0.0
                               ? acc.sum_wins / std::abs(acc.sum_losses)
                               : 999.9;
        sp.avg_holding_days = acc.count > 0 ? acc.sum_holding / acc.count : 0.0;
        result.push_back(std::move(sp));
    }

    // Sort by total_pnl descending
    std::sort(result.begin(), result.end(),
              [](const StrategyPerformance& a, const StrategyPerformance& b) {
                  return a.total_pnl > b.total_pnl;
              });

    return result;
}

// ---------------------------------------------------------------------------
// DTE bucket breakdown
// ---------------------------------------------------------------------------

Result<std::vector<DTEBucketPerformance>> TradeAnalyticsService::get_dte_breakdown(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    const auto& trips = *trips_result;

    struct Accum {
        int count{0};
        int winners{0};
        double total_pnl{0.0};
    };

    // Pre-create ordered buckets
    std::vector<std::string> bucket_order = {
        "0-7", "8-14", "15-30", "31-45", "46-60", "60+"
    };

    std::map<std::string, Accum> groups;
    for (const auto& b : bucket_order) {
        groups[b] = {};
    }

    for (const auto& rt : trips) {
        std::string bucket = get_dte_bucket(rt.holding_days);
        auto& acc = groups[bucket];
        ++acc.count;
        acc.total_pnl += rt.realized_pnl;
        if (rt.realized_pnl > 0.0) {
            ++acc.winners;
        }
    }

    std::vector<DTEBucketPerformance> result;
    for (const auto& b : bucket_order) {
        const auto& acc = groups[b];
        DTEBucketPerformance perf;
        perf.dte_bucket = b;
        perf.trade_count = acc.count;
        perf.winning_trades = acc.winners;
        perf.win_rate = acc.count > 0 ? static_cast<double>(acc.winners) / acc.count : 0.0;
        perf.total_pnl = acc.total_pnl;
        result.push_back(perf);
    }

    return result;
}

// ---------------------------------------------------------------------------
// Underlying breakdown
// ---------------------------------------------------------------------------

Result<std::vector<UnderlyingPerformance>> TradeAnalyticsService::get_underlying_breakdown(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    const auto& trips = *trips_result;

    struct Accum {
        int count{0};
        int winners{0};
        double total_pnl{0.0};
    };

    std::map<std::string, Accum> groups;
    for (const auto& rt : trips) {
        auto& acc = groups[rt.underlying];
        ++acc.count;
        acc.total_pnl += rt.realized_pnl;
        if (rt.realized_pnl > 0.0) {
            ++acc.winners;
        }
    }

    std::vector<UnderlyingPerformance> result;
    result.reserve(groups.size());
    for (auto& [sym, acc] : groups) {
        UnderlyingPerformance perf;
        perf.underlying = sym;
        perf.trade_count = acc.count;
        perf.winning_trades = acc.winners;
        perf.win_rate = acc.count > 0 ? static_cast<double>(acc.winners) / acc.count : 0.0;
        perf.total_pnl = acc.total_pnl;
        perf.avg_pnl = acc.count > 0 ? acc.total_pnl / acc.count : 0.0;
        result.push_back(std::move(perf));
    }

    // Sort by total_pnl descending
    std::sort(result.begin(), result.end(),
              [](const UnderlyingPerformance& a, const UnderlyingPerformance& b) {
                  return a.total_pnl > b.total_pnl;
              });

    return result;
}

// ---------------------------------------------------------------------------
// Loss clusters
// ---------------------------------------------------------------------------

Result<std::vector<LossCluster>> TradeAnalyticsService::get_loss_clusters(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    const auto& trips = *trips_result;

    struct Accum {
        std::string underlying;
        std::string dte_bucket;
        int loss_count{0};
        double total_loss{0.0};
        std::vector<int64_t> trade_ids;
    };

    std::map<std::string, Accum> clusters;
    for (const auto& rt : trips) {
        if (rt.realized_pnl >= 0.0) {
            continue;  // Only consider losing trades
        }

        std::string bucket = get_dte_bucket(rt.holding_days);
        std::string key = rt.underlying + "_" + bucket;
        auto& acc = clusters[key];
        acc.underlying = rt.underlying;
        acc.dte_bucket = bucket;
        ++acc.loss_count;
        acc.total_loss += rt.realized_pnl;
        acc.trade_ids.push_back(rt.id);
    }

    std::vector<LossCluster> result;
    result.reserve(clusters.size());
    for (auto& [key, acc] : clusters) {
        LossCluster lc;
        lc.cluster_key = key;
        lc.underlying = acc.underlying;
        lc.dte_bucket = acc.dte_bucket;
        lc.loss_count = acc.loss_count;
        lc.total_loss = acc.total_loss;
        lc.trade_ids = std::move(acc.trade_ids);
        result.push_back(std::move(lc));
    }

    // Sort by total_loss descending (most negative first)
    std::sort(result.begin(), result.end(),
              [](const LossCluster& a, const LossCluster& b) {
                  return a.total_loss < b.total_loss;
              });

    return result;
}

// ---------------------------------------------------------------------------
// Loss streak info
// ---------------------------------------------------------------------------

Result<LossStreakInfo> TradeAnalyticsService::get_streak_info(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to) {

    auto trips_result = database_.get_round_trips(account_id, date_from, date_to);
    if (!trips_result) {
        return Error{"Failed to load round trips", trips_result.error().message};
    }

    auto trips = *trips_result;

    // Sort by close_date ascending
    std::sort(trips.begin(), trips.end(),
              [](const RoundTrip& a, const RoundTrip& b) {
                  return a.close_date < b.close_date;
              });

    LossStreakInfo info;

    int current_streak = 0;
    int max_streak = 0;
    std::string streak_end;
    std::string last_loss_date;

    // For tracking recovery: the first winner after the worst streak ends
    bool in_max_streak = false;
    int max_streak_end_idx = -1;

    for (int i = 0; i < static_cast<int>(trips.size()); ++i) {
        const auto& rt = trips[i];

        if (rt.realized_pnl <= 0.0) {
            ++current_streak;
            if (current_streak > max_streak) {
                max_streak = current_streak;
                streak_end = rt.close_date;
                max_streak_end_idx = i;
                in_max_streak = true;
            }
        } else {
            // Winner: if we were in the max streak, this is a potential recovery
            if (in_max_streak) {
                info.recovery_date = rt.close_date;
                in_max_streak = false;
            }
            current_streak = 0;
        }
    }

    info.max_consecutive_losses = max_streak;
    info.streak_end_date = streak_end;
    info.current_streak = current_streak;

    // Compute recovery days between streak end and recovery date
    if (!info.recovery_date.empty() && !info.streak_end_date.empty()) {
        // Simple YYYY-MM-DD difference using day-of-year approach
        auto parse_ymd = [](const std::string& s) -> long {
            if (s.size() < 10) return 0;
            int y = std::stoi(s.substr(0, 4));
            int m = std::stoi(s.substr(5, 2));
            int d = std::stoi(s.substr(8, 2));
            // Days from epoch (approximation for date diffs)
            static const int mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
            long days = y * 365L + y / 4 - y / 100 + y / 400 + mdays[m - 1] + d;
            // Leap year adjustment for this year
            if (m > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) {
                ++days;
            }
            return days;
        };
        long end_days = parse_ymd(info.streak_end_date);
        long rec_days = parse_ymd(info.recovery_date);
        info.recovery_days = static_cast<int>(rec_days - end_days);
    }

    return info;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

double TradeAnalyticsService::calculate_roc(const db::Database::RoundTrip& rt) {
    double collateral = rt.strike * static_cast<double>(rt.quantity) * 100.0;
    if (collateral == 0.0) {
        return 0.0;
    }
    return (rt.realized_pnl / collateral) * 100.0;
}

std::string TradeAnalyticsService::get_dte_bucket(int holding_days) {
    if (holding_days <= 7)  return "0-7";
    if (holding_days <= 14) return "8-14";
    if (holding_days <= 30) return "15-30";
    if (holding_days <= 45) return "31-45";
    if (holding_days <= 60) return "46-60";
    return "60+";
}

} // namespace ibkr::services