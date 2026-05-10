#include "trades_command.hpp"
#include "services/trade_matcher.hpp"
#include "services/trade_analytics_service.hpp"
#include "services/snapshot_service.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;
using json = nlohmann::json;

Result<void> TradesCommand::execute(
    const config::Config& config,
    bool rebuild,
    const std::string& date_from,
    const std::string& date_to,
    const std::string& strategy_type,
    const std::string& underlying,
    const std::string& account,
    const utils::OutputOptions& output_opts) {

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    int64_t account_id = 0;
    if (!account.empty()) {
        auto accounts_result = database.list_accounts();
        if (accounts_result) {
            for (const auto& acc : *accounts_result) {
                if (acc.name == account) {
                    account_id = acc.id;
                    break;
                }
            }
            if (account_id == 0) {
                return Error{"Account not found", account};
            }
        }
    }

    if (rebuild) {
        auto dedup_result = database.dedup_trades(account_id);
        if (!dedup_result) {
            return Error{"Failed to dedup trades", dedup_result.error().message};
        }

        services::SnapshotService snapshot_service(database);
        services::TradeMatcher matcher(database, snapshot_service);
        auto match_result = account_id > 0
            ? matcher.match_account_trades(account_id, true)
            : matcher.match_all_trades(true);
        if (!match_result) {
            return Error{"Failed to match trades", match_result.error().message};
        }
        if (!output_opts.json) {
            std::cout << "Matched " << *match_result << " round-trips\n";
        }
    }

    services::TradeAnalyticsService analytics(database);

    auto overview_result = analytics.get_overview_metrics(
        account_id, date_from, date_to, strategy_type, underlying);
    if (!overview_result) {
        return Error{"Failed to get overview metrics", overview_result.error().message};
    }

    const auto& overview = *overview_result;

    if (output_opts.json) {
        json output;
        output["overview"] = {
            {"total_trades", overview.total_trades},
            {"winning_trades", overview.winning_trades},
            {"losing_trades", overview.losing_trades},
            {"win_rate", overview.win_rate},
            {"total_pnl", overview.total_pnl},
            {"avg_winner", overview.avg_winner},
            {"avg_loser", overview.avg_loser},
            {"profit_factor", overview.profit_factor},
            {"expectancy", overview.expectancy},
            {"avg_holding_days", overview.avg_holding_days},
            {"avg_roc", overview.avg_roc},
            {"avg_annualized_return", overview.avg_annualized_return}
        };

        auto trips_result = analytics.get_round_trips(account_id, date_from, date_to, strategy_type, underlying);
        if (trips_result) {
            json trips_arr = json::array();
            for (const auto& rt : *trips_result) {
                trips_arr.push_back({
                    {"id", rt.id}, {"account", rt.account_name},
                    {"underlying", rt.underlying}, {"strike", rt.strike},
                    {"right", std::string(1, rt.right)}, {"expiry", rt.expiry},
                    {"quantity", rt.quantity}, {"open_date", rt.open_date},
                    {"close_date", rt.close_date}, {"holding_days", rt.holding_days},
                    {"net_premium", rt.net_premium}, {"realized_pnl", rt.realized_pnl},
                    {"roc", rt.roc}, {"annualized_return", rt.annualized_return},
                    {"close_reason", rt.close_reason}, {"strategy_type", rt.strategy_type}
                });
            }
            output["round_trips"] = trips_arr;
        }

        auto strategy_result = analytics.get_strategy_performance(account_id, date_from, date_to);
        if (strategy_result) {
            json strat_arr = json::array();
            for (const auto& sp : *strategy_result) {
                strat_arr.push_back({
                    {"strategy_type", sp.strategy_type}, {"trade_count", sp.trade_count},
                    {"winning_trades", sp.winning_trades}, {"win_rate", sp.win_rate},
                    {"total_pnl", sp.total_pnl}, {"avg_pnl", sp.avg_pnl},
                    {"profit_factor", sp.profit_factor}, {"avg_holding_days", sp.avg_holding_days}
                });
            }
            output["strategy_performance"] = strat_arr;
        }

        auto dte_result = analytics.get_dte_breakdown(account_id, date_from, date_to);
        if (dte_result) {
            json dte_arr = json::array();
            for (const auto& dp : *dte_result) {
                dte_arr.push_back({
                    {"dte_bucket", dp.dte_bucket}, {"trade_count", dp.trade_count},
                    {"winning_trades", dp.winning_trades}, {"win_rate", dp.win_rate},
                    {"total_pnl", dp.total_pnl}
                });
            }
            output["dte_breakdown"] = dte_arr;
        }

        auto underlying_result = analytics.get_underlying_breakdown(account_id, date_from, date_to);
        if (underlying_result) {
            json u_arr = json::array();
            for (const auto& up : *underlying_result) {
                u_arr.push_back({
                    {"underlying", up.underlying}, {"trade_count", up.trade_count},
                    {"winning_trades", up.winning_trades}, {"win_rate", up.win_rate},
                    {"total_pnl", up.total_pnl}, {"avg_pnl", up.avg_pnl}
                });
            }
            output["underlying_breakdown"] = u_arr;
        }

        auto clusters_result = analytics.get_loss_clusters(account_id, date_from, date_to);
        if (clusters_result) {
            json c_arr = json::array();
            for (const auto& lc : *clusters_result) {
                c_arr.push_back({
                    {"cluster_key", lc.cluster_key}, {"underlying", lc.underlying},
                    {"dte_bucket", lc.dte_bucket}, {"loss_count", lc.loss_count},
                    {"total_loss", lc.total_loss}
                });
            }
            output["loss_clusters"] = c_arr;
        }

        auto streak_result = analytics.get_streak_info(account_id, date_from, date_to);
        if (streak_result) {
            output["streak_info"] = {
                {"max_consecutive_losses", streak_result->max_consecutive_losses},
                {"streak_end_date", streak_result->streak_end_date},
                {"recovery_date", streak_result->recovery_date},
                {"recovery_days", streak_result->recovery_days},
                {"current_streak", streak_result->current_streak}
            };
        }

        std::cout << output.dump(2) << "\n";
    } else {
        std::cout << "\n=== Trade Review Summary ===\n\n";
        std::cout << "Total Trades:    " << overview.total_trades << "\n";
        std::cout << "Win Rate:        " << std::fixed << std::setprecision(1) << (overview.win_rate * 100) << "%\n";
        std::cout << "Total P&L:       $" << std::fixed << std::setprecision(2) << overview.total_pnl << "\n";
        std::cout << "Profit Factor:   " << std::fixed << std::setprecision(2) << overview.profit_factor << "\n";
        std::cout << "Expectancy:      $" << std::fixed << std::setprecision(2) << overview.expectancy << "\n";

        auto strategy_result = analytics.get_strategy_performance(account_id, date_from, date_to);
        if (strategy_result && !strategy_result->empty()) {
            std::cout << "\n=== Strategy Performance ===\n\n";
            std::cout << std::left << std::setw(20) << "Strategy" << std::right << std::setw(8) << "Trades" << std::setw(8) << "Win%" << std::setw(12) << "P&L" << "\n";
            std::cout << std::string(48, '-') << "\n";
            for (const auto& sp : *strategy_result) {
                std::cout << std::left << std::setw(20) << sp.strategy_type << std::right << std::setw(8) << sp.trade_count << std::setw(7) << std::fixed << std::setprecision(0) << (sp.win_rate * 100) << "%" << std::setw(12) << std::fixed << std::setprecision(2) << sp.total_pnl << "\n";
            }
        }
    }

    return Result<void>{};
}

} // namespace ibkr::commands
