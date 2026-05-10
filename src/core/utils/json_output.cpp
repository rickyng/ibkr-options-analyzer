#include "json_output.hpp"
#include "services/portfolio_service.hpp"
#include "services/screener_service.hpp"
#include <date/date.h>
#include <chrono>
#include <cmath>
#include <sstream>

namespace ibkr::utils {

using json = nlohmann::json;

// Calendar week offset from current week (0 = this week, 1 = next week, etc.)
static int calendar_week_offset(const std::string& expiry_str) {
    using namespace date;
    std::istringstream ss(expiry_str);
    year_month_day ymd;
    ss >> parse("%F", ymd);
    if (ss.fail() || !ymd.ok()) return -1;

    auto today = floor<std::chrono::days>(std::chrono::system_clock::now());
    auto expiry_day = sys_days{ymd};
    auto today_monday = sys_days{today} - (weekday{today} - Monday);
    auto expiry_monday = expiry_day - (weekday{expiry_day} - Monday);
    return static_cast<int>((expiry_monday - today_monday).count() / 7);
}

static std::string week_bucket(int offset) {
    if (offset <= 0) return "W1";
    if (offset == 1) return "W2";
    if (offset == 2) return "W3";
    if (offset == 3) return "W4";
    return "W5+";
}

// --- Command-level responses ---

json JsonOutput::success(const std::string& message) {
    return json{{"status", "ok"}, {"message", message}};
}

json JsonOutput::error(const std::string& message, const std::string& detail) {
    auto j = json{{"status", "error"}, {"error", message}};
    if (!detail.empty()) j["detail"] = detail;
    return j;
}

// --- Private serialization helpers ---

json JsonOutput::risk_metrics_to_json(const analysis::RiskMetrics& m) {
    json j;
    j["breakeven_price"] = m.breakeven_price;
    if (m.breakeven_price_2 > 0) j["breakeven_price_2"] = m.breakeven_price_2;
    j["max_profit"] = m.max_profit;
    if (std::isinf(m.max_loss)) {
        j["max_loss"] = nullptr;
        j["max_loss_display"] = "UNLIMITED";
    } else {
        j["max_loss"] = m.max_loss;
    }
    j["risk_level"] = analysis::risk_level_to_string(m.risk_level);
    j["net_premium"] = m.net_premium;
    j["days_to_expiry"] = m.days_to_expiry;
    j["currency"] = m.currency;
    return j;
}

std::string JsonOutput::categorize_risk_level(
    const analysis::Position& pos,
    double current_price) {

    if (pos.quantity >= 0) return "LONG";

    double dist;
    bool itm;
    if (pos.right == 'P') {
        dist = ((current_price - pos.strike) / current_price) * 100.0;
        itm = current_price < pos.strike;
    } else {
        dist = ((pos.strike - current_price) / current_price) * 100.0;
        itm = current_price > pos.strike;
    }

    if (itm || dist <= 1.0) return "CRITICAL";
    if (dist <= 5.0) return "HIGH";
    if (dist <= 10.0) return "MODERATE";
    return "SAFE";
}

json JsonOutput::position_to_json(
    const analysis::Position& pos,
    const std::string& account_name,
    const StockPrice* current_price) {

    json j;
    j["account"] = account_name;
    j["underlying"] = pos.underlying;
    j["expiry"] = pos.expiry;
    j["strike"] = pos.strike;
    j["right"] = std::string(1, pos.right);
    j["quantity"] = pos.quantity;
    j["entry_premium"] = pos.entry_premium;
    j["multiplier"] = pos.multiplier;
    j["is_manual"] = pos.is_manual;
    j["currency"] = pos.currency;
    j["days_to_expiry"] = analysis::RiskCalculator::calculate_days_to_expiry(pos.expiry);

    // Duration bucket (calendar weeks)
    j["duration_bucket"] = week_bucket(calendar_week_offset(pos.expiry));

    if (current_price) {
        j["current_price"] = current_price->price;
        j["risk_category"] = categorize_risk_level(pos, current_price->price);

        // Distance from strike
        if (pos.quantity < 0) {
            double dist;
            bool itm;
            if (pos.right == 'P') {
                dist = ((current_price->price - pos.strike) / current_price->price) * 100.0;
                itm = current_price->price < pos.strike;
            } else {
                dist = ((pos.strike - current_price->price) / current_price->price) * 100.0;
                itm = current_price->price > pos.strike;
            }
            j["distance_from_strike_pct"] = std::round(dist * 100.0) / 100.0;
            j["in_the_money"] = itm;
        }
    }

    return j;
}

json JsonOutput::strategy_to_json(
    const analysis::Strategy& strategy,
    const analysis::RiskMetrics& metrics,
    const std::string& account_name) {

    json j;
    j["account"] = account_name;
    j["type"] = analysis::StrategyDetector::strategy_type_to_string(strategy.type);
    j["underlying"] = strategy.underlying;
    j["expiry"] = strategy.expiry;
    j["currency"] = strategy.currency;

    json legs_arr = json::array();
    for (const auto& leg : strategy.legs) {
        json leg_j;
        leg_j["strike"] = leg.strike;
        leg_j["right"] = std::string(1, leg.right);
        leg_j["quantity"] = leg.quantity;
        leg_j["entry_premium"] = leg.entry_premium;
        leg_j["currency"] = leg.currency;
        legs_arr.push_back(leg_j);
    }
    j["legs"] = legs_arr;
    j["risk"] = risk_metrics_to_json(metrics);

    return j;
}

// --- Analyze command ---

json JsonOutput::open_positions(
    const std::vector<analysis::Position>& positions,
    const std::map<std::string, StockPrice>& current_prices,
    const std::map<int64_t, std::string>& account_names) {

    json j;
    j["status"] = "ok";
    j["count"] = positions.size();

    // Summary
    int short_puts = 0, short_calls = 0, long_pos = 0;
    double total_premium = 0.0, total_max_loss = 0.0;
    int expiring_7 = 0, expiring_30 = 0;

    for (const auto& pos : positions) {
        int dte = analysis::RiskCalculator::calculate_days_to_expiry(pos.expiry);
        if (dte <= 7) expiring_7++;
        if (dte <= 30) expiring_30++;
        if (pos.quantity < 0) {
            double prem = analysis::premium_for(pos.quantity, pos.entry_premium);
            total_premium += prem;
            if (pos.right == 'P') {
                short_puts++;
                total_max_loss += (pos.strike * analysis::constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity)) - prem;
            } else {
                short_calls++;
            }
        } else {
            long_pos++;
        }
    }

    json summary;
    summary["total"] = positions.size();
    summary["short_puts"] = short_puts;
    summary["short_calls"] = short_calls;
    summary["long_positions"] = long_pos;
    summary["premium_collected"] = std::round(total_premium * 100.0) / 100.0;
    summary["max_loss_short_puts"] = std::round(total_max_loss * 100.0) / 100.0;
    if (short_calls > 0) summary["short_calls_unlimited_risk"] = short_calls;
    summary["expiring_7_days"] = expiring_7;
    summary["expiring_30_days"] = expiring_30;
    j["summary"] = summary;

    // Positions grouped by duration bucket
    json buckets = json::object();
    std::map<std::string, std::vector<json>> bucket_map = {
        {"W1", {}}, {"W2", {}}, {"W3", {}}, {"W4", {}}, {"W5+", {}}
    };

    for (const auto& pos : positions) {
        std::string acct = "Unknown";
        auto it = account_names.find(pos.account_id);
        if (it != account_names.end()) acct = it->second;

        const StockPrice* price = nullptr;
        auto pit = current_prices.find(pos.underlying);
        if (pit != current_prices.end()) price = &pit->second;

        auto pos_j = position_to_json(pos, acct, price);

        bucket_map[week_bucket(calendar_week_offset(pos.expiry))].push_back(pos_j);
    }

    for (auto& [key, arr] : bucket_map) {
        buckets[key] = arr;
    }
    j["positions"] = buckets;

    return j;
}

json JsonOutput::strategies(
    const std::vector<analysis::Strategy>& strategies,
    const std::vector<analysis::RiskMetrics>& metrics,
    const std::map<int64_t, std::string>& account_names) {

    json j;
    j["status"] = "ok";
    j["count"] = strategies.size();

    json arr = json::array();
    for (size_t i = 0; i < strategies.size() && i < metrics.size(); ++i) {
        std::string acct = "Unknown";
        if (!strategies[i].legs.empty()) {
            auto it = account_names.find(strategies[i].legs[0].account_id);
            if (it != account_names.end()) acct = it->second;
        }
        arr.push_back(strategy_to_json(strategies[i], metrics[i], acct));
    }
    j["strategies"] = arr;

    // Portfolio summary
    auto portfolio = analysis::RiskCalculator::calculate_portfolio_risk(strategies, metrics);
    json pjson;
    pjson["total_strategies"] = portfolio.total_strategies;
    pjson["total_max_profit"] = std::round(portfolio.total_max_profit * 100.0) / 100.0;
    pjson["total_max_loss"] = std::round(portfolio.total_max_loss * 100.0) / 100.0;
    pjson["total_loss_10pct"] = std::round(portfolio.total_loss_10pct * 100.0) / 100.0;
    pjson["total_loss_20pct"] = std::round(portfolio.total_loss_20pct * 100.0) / 100.0;
    pjson["positions_expiring_soon"] = portfolio.positions_expiring_soon;
    j["portfolio"] = pjson;

    // Account-level breakdown
    auto account_risks = analysis::RiskCalculator::calculate_account_risks(strategies, metrics);
    json accounts_arr = json::array();
    for (const auto& ar : account_risks) {
        json arj;
        arj["account_id"] = ar.account_id;
        // Try to resolve account name
        auto it = account_names.find(ar.account_id);
        arj["account_name"] = (it != account_names.end()) ? it->second : "Unknown";
        arj["strategy_count"] = ar.strategy_count;
        arj["total_max_profit"] = std::round(ar.total_max_profit * 100.0) / 100.0;
        arj["total_max_loss"] = std::round(ar.total_max_loss * 100.0) / 100.0;
        arj["positions_expiring_soon"] = ar.positions_expiring_soon;
        accounts_arr.push_back(arj);
    }
    j["accounts"] = accounts_arr;

    // Underlying exposure
    auto exposures = analysis::RiskCalculator::calculate_underlying_exposure(strategies, metrics);
    json exp_arr = json::array();
    for (const auto& ue : exposures) {
        json uej;
        uej["underlying"] = ue.underlying;
        uej["total_max_loss"] = std::round(ue.total_max_loss * 100.0) / 100.0;
        uej["total_max_profit"] = std::round(ue.total_max_profit * 100.0) / 100.0;
        uej["position_count"] = ue.position_count;
        exp_arr.push_back(uej);
    }
    j["underlying_exposure"] = exp_arr;

    return j;
}

json JsonOutput::impact_analysis(
    const std::string& underlying,
    const std::vector<analysis::Position>& positions,
    const std::vector<analysis::RiskMetrics>& metrics) {

    json j;
    j["status"] = "ok";
    j["underlying"] = underlying;
    j["count"] = positions.size();

    json arr = json::array();
    double total_profit = 0.0, total_loss = 0.0;

    for (size_t i = 0; i < positions.size() && i < metrics.size(); ++i) {
        json pj;
        pj["expiry"] = positions[i].expiry;
        pj["strike"] = positions[i].strike;
        pj["right"] = std::string(1, positions[i].right);
        pj["quantity"] = positions[i].quantity;
        pj["entry_premium"] = positions[i].entry_premium;
        pj["risk"] = risk_metrics_to_json(metrics[i]);
        arr.push_back(pj);

        total_profit += metrics[i].max_profit;
        if (std::isfinite(metrics[i].max_loss)) total_loss += metrics[i].max_loss;
    }
    j["positions"] = arr;

    json totals;
    totals["max_profit"] = std::round(total_profit * 100.0) / 100.0;
    totals["max_loss"] = std::round(total_loss * 100.0) / 100.0;
    j["totals"] = totals;

    return j;
}

// --- Report command ---

json JsonOutput::report(
    const std::vector<analysis::Position>& positions,
    const std::vector<db::Database::RiskSummary>& risk_summaries,
    const std::vector<db::Database::ExposureInfo>& exposures) {

    json j;
    j["status"] = "ok";

    // Positions
    json pos_arr = json::array();
    for (const auto& p : positions) {
        json pj;
        pj["id"] = p.id;
        pj["account_id"] = p.account_id;
        pj["underlying"] = p.underlying;
        pj["expiry"] = p.expiry;
        pj["strike"] = p.strike;
        pj["right"] = std::string(1, p.right);
        pj["quantity"] = p.quantity;
        pj["mark_price"] = p.mark_price;
        pj["entry_premium"] = p.entry_premium;
        pos_arr.push_back(pj);
    }
    j["positions"] = pos_arr;
    j["position_count"] = positions.size();

    // Risk summaries by account
    json risk_arr = json::array();
    for (const auto& r : risk_summaries) {
        json rj;
        rj["account"] = r.account_name;
        rj["total_max_loss"] = std::round(r.total_max_loss * 100.0) / 100.0;
        rj["total_max_profit"] = std::round(r.total_max_profit * 100.0) / 100.0;
        rj["strategy_count"] = r.strategy_count;
        risk_arr.push_back(rj);
    }
    j["risk_summaries"] = risk_arr;

    // Exposure by underlying
    json exp_arr = json::array();
    for (const auto& e : exposures) {
        json ej;
        ej["underlying"] = e.underlying;
        ej["total_max_loss"] = std::round(e.total_max_loss * 100.0) / 100.0;
        ej["position_count"] = e.position_count;
        exp_arr.push_back(ej);
    }
    j["underlying_exposure"] = exp_arr;

    return j;
}

// --- Import command ---

json JsonOutput::import_result(
    int trades_imported,
    int positions_imported,
    const std::string& file_path) {

    json j;
    j["status"] = "ok";
    j["trades_imported"] = trades_imported;
    j["positions_imported"] = positions_imported;
    if (!file_path.empty()) j["file"] = file_path;
    j["message"] = "Import completed";
    return j;
}

// --- Download command ---

json JsonOutput::download_result(
    const std::string& file_path,
    const std::string& account_name) {

    json j;
    j["status"] = "ok";
    j["file"] = file_path;
    j["account"] = account_name;
    j["message"] = "Download completed";
    return j;
}

// --- Database CRUD ---

json JsonOutput::account(const db::Database::AccountInfo& info) {
    json j;
    j["id"] = info.id;
    j["name"] = info.name;
    j["token"] = info.token;
    j["query_id"] = info.query_id;
    j["enabled"] = info.enabled;
    j["created_at"] = info.created_at;
    j["updated_at"] = info.updated_at;
    return j;
}

json JsonOutput::accounts(const std::vector<db::Database::AccountInfo>& accounts) {
    json j;
    j["status"] = "ok";
    json arr = json::array();
    for (const auto& a : accounts) arr.push_back(account(a));
    j["accounts"] = arr;
    j["count"] = accounts.size();
    return j;
}

json JsonOutput::position(const analysis::Position& pos) {
    json j;
    j["id"] = pos.id;
    j["account_id"] = pos.account_id;
    j["underlying"] = pos.underlying;
    j["expiry"] = pos.expiry;
    j["strike"] = pos.strike;
    j["right"] = std::string(1, pos.right);
    j["quantity"] = pos.quantity;
    j["mark_price"] = pos.mark_price;
    j["entry_premium"] = pos.entry_premium;
    return j;
}

json JsonOutput::positions(const std::vector<analysis::Position>& positions) {
    json j;
    j["status"] = "ok";
    json arr = json::array();
    for (const auto& p : positions) arr.push_back(position(p));
    j["positions"] = arr;
    j["count"] = positions.size();
    return j;
}

nlohmann::json JsonOutput::portfolio(const services::PortfolioView& view) {
    auto j = success();
    auto& data = j["data"];

    data["total_positions"] = view.total_positions;
    data["total_premium_collected"] = view.total_premium_collected;
    data["total_unrealized_pnl"] = view.total_unrealized_pnl;
    data["itm_count"] = view.itm_count;
    data["near_money_count"] = view.near_money_count;
    data["expiring_soon_count"] = view.expiring_soon_count;

    auto j_positions = nlohmann::json::array();
    for (const auto& pp : view.positions) {
        nlohmann::json jp;
        jp["account"] = pp.account_name;
        jp["underlying"] = pp.position.underlying;
        jp["strike"] = pp.position.strike;
        jp["right"] = std::string(1, pp.position.right);
        jp["quantity"] = pp.position.quantity;
        jp["expiry"] = pp.position.expiry;
        jp["entry_premium"] = pp.position.entry_premium;
        if (pp.has_price) {
            jp["current_price"] = pp.current_price;
        } else {
            jp["current_price"] = nlohmann::json{};
        }
        jp["pnl"] = pp.pnl;
        jp["pnl_percent"] = pp.pnl_percent;
        jp["otm_percent"] = pp.otm_percent;
        jp["annualized_yield"] = pp.annualized_yield;
        jp["multiplier"] = pp.position.multiplier;
        jp["currency"] = pp.position.currency;
        if (pp.risk_alert.empty()) {
            jp["risk_alert"] = nlohmann::json{};
        } else {
            jp["risk_alert"] = pp.risk_alert;
        }
        j_positions.push_back(jp);
    }
    data["positions"] = j_positions;

    data["loss_10pct"] = view.loss_10pct;
    data["loss_20pct"] = view.loss_20pct;
    data["dte_buckets"] = view.dte_buckets;

    return j;
}

nlohmann::json JsonOutput::screener(const services::ScreenerOutput& output) {
    auto j = success();
    auto& data = j["data"];

    data["total_scanned"] = output.total_scanned;
    data["passed_filter"] = output.passed_filter;
    data["errors"] = output.errors;

    auto j_results = nlohmann::json::array();
    for (const auto& r : output.results) {
        nlohmann::json jr;
        jr["symbol"] = r.symbol;
        jr["current_price"] = r.current_price;
        jr["iv"] = r.iv;
        jr["iv_percentile"] = r.iv_percentile;
        jr["suggested_strike"] = r.suggested_strike;
        jr["strike_dte"] = r.strike_dte;
        jr["premium"] = r.premium;
        jr["annualized_yield"] = r.annualized_yield;
        jr["grade"] = std::string(1, r.risk_reward_grade);
        jr["has_existing_position"] = r.has_existing_position;
        if (r.existing_position_detail.empty()) {
            jr["existing_position_detail"] = nlohmann::json{};
        } else {
            jr["existing_position_detail"] = r.existing_position_detail;
        }
        j_results.push_back(jr);
    }
    data["results"] = j_results;

    return j;
}

} // namespace ibkr::utils