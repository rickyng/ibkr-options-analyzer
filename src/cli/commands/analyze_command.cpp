#include "analyze_command.hpp"
#include "services/position_service.hpp"
#include "services/strategy_service.hpp"
#include "services/price_service.hpp"
#include "services/portfolio_service.hpp"
#include "services/screener_service.hpp"
#include "db/database.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/logger.hpp"
#include "utils/json_output.hpp"
#include "utils/currency.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;
using analysis::RiskCalculator;
using analysis::Strategy;
using analysis::Position;
using analysis::RiskMetrics;

Result<void> AnalyzeCommand::execute(
    const config::Config& config,
    const std::string& analysis_type,
    const std::string& account_filter,
    const std::string& underlying_filter,
    const utils::OutputOptions& output_opts,
    bool cache_only,
    const ScreenerOverrides& screener_overrides) {

    Logger::info("Starting analyze command: type={}", analysis_type);

    if (analysis_type == "open") {
        return analyze_open(config, account_filter, underlying_filter, output_opts);
    } else if (analysis_type == "impact") {
        if (underlying_filter.empty()) {
            return Error{"Impact analysis requires --underlying option"};
        }
        return analyze_impact(config, underlying_filter, account_filter, output_opts);
    } else if (analysis_type == "strategy") {
        return analyze_strategy(config, account_filter, underlying_filter, output_opts);
    } else if (analysis_type == "portfolio") {
        return analyze_portfolio(config, account_filter, underlying_filter, output_opts);
    } else if (analysis_type == "screener") {
        return analyze_screener(config, output_opts, cache_only, screener_overrides);
    } else {
        return Error{"Invalid analysis type", "Must be one of: open, impact, strategy, portfolio, screener"};
    }
}

Result<void> AnalyzeCommand::analyze_open(
    const config::Config& config,
    const std::string& account_filter,
    const std::string& underlying_filter,
    const utils::OutputOptions& output_opts) {

    Logger::info("Analyzing open positions");

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::PositionService position_service(database);
    auto positions_result = position_service.load_positions(account_filter, underlying_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    if (positions.empty()) {
        if (output_opts.json) {
            std::cout << utils::JsonOutput::open_positions({}, {}, {}) << "\n";
        } else {
            std::cout << "No open positions found.\n";
        }
        return Result<void>{};
    }

    // Fetch prices via service
    services::PriceService price_service(database);
    std::vector<std::string> underlyings;
    for (const auto& pos : positions) underlyings.push_back(pos.underlying);
    auto current_prices = price_service.fetch_for_positions(underlyings);

    // Load account names via service
    auto account_names_result = position_service.load_account_names();
    std::map<int64_t, std::string> account_names = account_names_result
        ? *account_names_result : std::map<int64_t, std::string>{};

    // JSON output short-circuit
    if (output_opts.json) {
        std::cout << utils::JsonOutput::open_positions(positions, current_prices, account_names) << "\n";
        return Result<void>{};
    }
    if (output_opts.quiet) return Result<void>{};

    // --- Human-readable output ---

    // Duration bucket grouping
    struct Bucket { std::string label; int min_days; int max_days; };
    std::vector<Bucket> buckets = {
        {"< 7 days",      0,  6},
        {"7-14 days",     7, 14},
        {"14-21 days",   15, 21},
        {"21-28 days",   22, 28},
        {"> 28 days",    29, -1},
    };

    struct PosEntry { Position pos; int days; std::string account_name; };
    std::vector<std::vector<PosEntry>> bucket_entries(buckets.size());

    for (const auto& pos : positions) {
        int days = RiskCalculator::calculate_days_to_expiry(pos.expiry);
        std::string acct = "Unknown";
        auto it = account_names.find(pos.account_id);
        if (it != account_names.end()) acct = it->second;

        for (size_t b = 0; b < buckets.size(); ++b) {
            if (days >= buckets[b].min_days &&
                (buckets[b].max_days < 0 || days <= buckets[b].max_days)) {
                bucket_entries[b].push_back({pos, days, acct});
                break;
            }
        }
    }

    // Display by duration bucket
    for (size_t b = 0; b < buckets.size(); ++b) {
        const auto& entries = bucket_entries[b];
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << buckets[b].label << " — " << entries.size() << " position";
        if (entries.size() != 1) std::cout << "s";
        std::cout << "\n" << std::string(80, '-') << "\n";

        if (entries.empty()) { std::cout << "  (none)\n"; continue; }

        std::vector<PosEntry> sorted = entries;
        std::sort(sorted.begin(), sorted.end(),
            [](const PosEntry& a, const PosEntry& b) {
                if (a.account_name != b.account_name) return a.account_name < b.account_name;
                if (a.pos.underlying != b.pos.underlying) return a.pos.underlying < b.pos.underlying;
                return a.pos.expiry < b.pos.expiry;
            });

        std::string cur_account;
        for (const auto& e : sorted) {
            if (e.account_name != cur_account) {
                std::cout << "\n  [" << e.account_name << "]\n";
                cur_account = e.account_name;
            }
            std::cout << "    " << e.pos.underlying << "  "
                      << e.pos.expiry << "  $"
                      << std::fixed << std::setprecision(2) << e.pos.strike
                      << " " << e.pos.right << " × " << e.pos.quantity
                      << " @ $" << e.pos.entry_premium;
            if (e.pos.is_manual) std::cout << " [MANUAL]";
            if (current_prices.count(e.pos.underlying)) {
                std::cout << "  (now: $" << std::fixed << std::setprecision(2)
                          << current_prices[e.pos.underlying].price << ")";
            }
            if (current_prices.count(e.pos.underlying) && e.pos.quantity < 0) {
                double cp = current_prices[e.pos.underlying].price;
                double dist = e.pos.right == 'P'
                    ? ((cp - e.pos.strike) / cp) * 100.0
                    : ((e.pos.strike - cp) / cp) * 100.0;
                bool itm = (e.pos.right == 'P' && cp < e.pos.strike) ||
                           (e.pos.right == 'C' && cp > e.pos.strike);
                if (itm) std::cout << " ⚠️ ITM " << std::abs(dist) << "%";
                else if (dist < 5.0) std::cout << " ⚡ " << dist << "% OTM";
            }
            std::cout << "\n";
        }
    }

    // Portfolio summary via service
    auto summary = services::PositionService::calculate_portfolio_summary(positions);

    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "CONSOLIDATED PORTFOLIO SUMMARY\n";
    std::cout << std::string(80, '=') << "\n";

    // Risk level categorization
    struct PositionInfo { std::string label, expiry, account_name; double assignment_capital; };
    struct RiskLevel { int count = 0; double assignment_capital = 0.0; std::vector<PositionInfo> positions; };
    RiskLevel level1, level2, level3, level4;

    for (const auto& pos : positions) {
        std::string acct = "Unknown";
        auto it = account_names.find(pos.account_id);
        if (it != account_names.end()) acct = it->second;

        if (pos.quantity >= 0) continue;

        if (pos.right == 'P') {
            double assignment_capital = pos.strike * analysis::constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);
            PositionInfo info{pos.underlying + " $" + std::to_string(static_cast<int>(pos.strike)) + "P",
                              pos.expiry, acct, assignment_capital};
            if (current_prices.count(pos.underlying)) {
                double cp = current_prices[pos.underlying].price;
                double dist_pct = ((cp - pos.strike) / cp) * 100.0;
                if (cp <= pos.strike || dist_pct <= 1.0) { level1.count++; level1.assignment_capital += assignment_capital; level1.positions.push_back(info); }
                else if (dist_pct <= 5.0) { level2.count++; level2.assignment_capital += assignment_capital; level2.positions.push_back(info); }
                else if (dist_pct <= 10.0) { level3.count++; level3.assignment_capital += assignment_capital; level3.positions.push_back(info); }
                else { level4.count++; level4.assignment_capital += assignment_capital; level4.positions.push_back(info); }
            } else {
                level4.count++; level4.assignment_capital += assignment_capital; level4.positions.push_back(info);
            }
        } else if (pos.right == 'C') {
            PositionInfo info{pos.underlying + " $" + std::to_string(static_cast<int>(pos.strike)) + "C",
                              pos.expiry, acct, 0.0};
            if (current_prices.count(pos.underlying)) {
                double cp = current_prices[pos.underlying].price;
                double dist_pct = ((pos.strike - cp) / cp) * 100.0;
                if (cp >= pos.strike || dist_pct <= 1.0) { level1.count++; level1.positions.push_back(info); }
                else if (dist_pct <= 5.0) { level2.count++; level2.positions.push_back(info); }
                else if (dist_pct <= 10.0) { level3.count++; level3.positions.push_back(info); }
                else { level4.count++; level4.positions.push_back(info); }
            }
        }
    }

    std::cout << "\nPosition Breakdown:\n";
    std::cout << "  Total Positions: " << summary.total_positions << "\n";
    std::cout << "  Short Puts: " << summary.short_puts << "\n";
    std::cout << "  Short Calls: " << summary.short_calls << "\n";
    std::cout << "  Long Positions: " << summary.long_positions << "\n";

    std::cout << "\nExpiration Schedule:\n";
    std::cout << "  Expiring in 7 days: " << summary.expiring_7_days << " positions\n";
    std::cout << "  Expiring in 30 days: " << summary.expiring_30_days << " positions\n";

    std::cout << "\nRisk Summary:\n";
    std::cout << "  Total Premium Collected: $" << std::fixed << std::setprecision(2)
              << summary.total_premium_collected << "\n";
    std::cout << "  Total Max Loss (short puts): $" << summary.total_max_loss << "\n";
    if (summary.short_calls > 0)
        std::cout << "  Short Calls Max Loss: UNLIMITED (" << summary.short_calls << " positions)\n";

    // Risk levels display
    std::cout << "\n" << std::string(80, '-') << "\nASSIGNMENT RISK LEVELS\n"
              << std::string(80, '-') << "\n";

    auto sort_by_acct = [](std::vector<PositionInfo>& v) {
        std::sort(v.begin(), v.end(), [](const PositionInfo& a, const PositionInfo& b) {
            return a.account_name != b.account_name ? a.account_name < b.account_name : a.expiry < b.expiry;
        });
    };
    auto display = [](const std::vector<PositionInfo>& v) {
        std::string cur;
        for (const auto& p : v) {
            if (p.account_name != cur) { if (!cur.empty()) std::cout << "\n"; std::cout << "  [" << p.account_name << "]:\n"; cur = p.account_name; }
            std::cout << "    " << p.expiry << " - " << p.label;
            if (p.assignment_capital > 0) std::cout << " ($" << std::fixed << std::setprecision(0) << p.assignment_capital << ")";
            std::cout << "\n";
        }
    };

    sort_by_acct(level1.positions); sort_by_acct(level2.positions);
    sort_by_acct(level3.positions); sort_by_acct(level4.positions);

    std::cout << "\nLevel 1 - CRITICAL (ITM or ≤1% OTM):\n  Positions: " << level1.count << "\n";
    if (level1.count > 0) { std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2) << level1.assignment_capital << "\n"; display(level1.positions); }
    std::cout << "\nLevel 2 - HIGH (1-5% OTM):\n  Positions: " << level2.count << "\n";
    if (level2.count > 0) { std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2) << level2.assignment_capital << "\n"; display(level2.positions); }
    std::cout << "\nLevel 3 - MODERATE (5-10% OTM):\n  Positions: " << level3.count << "\n";
    if (level3.count > 0) { std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2) << level3.assignment_capital << "\n"; display(level3.positions); }
    std::cout << "\nLevel 4 - SAFE (>10% OTM):\n  Positions: " << level4.count << "\n";
    if (level4.count > 0) { std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2) << level4.assignment_capital << "\n"; }

    double critical = level1.assignment_capital + level2.assignment_capital;
    double total_all = critical + level3.assignment_capital + level4.assignment_capital;
    std::cout << "\n" << std::string(80, '-') << "\nCAPITAL REQUIREMENTS:\n"
              << "  Critical Risk (L1+L2): $" << std::fixed << std::setprecision(2) << critical
              << " (" << (level1.count + level2.count) << " positions)\n"
              << "  Total if All Assigned: $" << total_all
              << " (" << (level1.count + level2.count + level3.count + level4.count) << " positions)\n";

    if (current_prices.size() < underlyings.size()) {
        std::set<std::string> unique_underlyings(underlyings.begin(), underlyings.end());
        std::cout << "\n⚠️  Note: Could not fetch prices for "
                  << (unique_underlyings.size() - current_prices.size()) << " underlyings\n";
    }

    std::cout << "\n";
    return Result<void>{};
}

Result<void> AnalyzeCommand::analyze_impact(
    const config::Config& config,
    const std::string& underlying_filter,
    const std::string& /* account_filter */,
    const utils::OutputOptions& output_opts) {

    Logger::info("Analyzing impact for underlying: {}", underlying_filter);

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::PositionService position_service(database);
    auto positions_result = position_service.load_positions("", underlying_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    if (positions.empty()) {
        if (output_opts.json) {
            std::cout << utils::JsonOutput::impact_analysis(underlying_filter, {}, {}) << "\n";
        } else {
            std::cout << "No positions found for " << underlying_filter << "\n";
        }
        return Result<void>{};
    }

    std::vector<RiskMetrics> impact_metrics;
    utils::CurrencyConverter converter;
    for (const auto& pos : positions) {
        Strategy temp;
        temp.type = (pos.quantity < 0)
            ? (pos.right == 'P' ? Strategy::Type::NakedShortPut : Strategy::Type::NakedShortCall)
            : Strategy::Type::NakedShortPut;
        temp.legs.push_back(pos);
        temp.underlying = pos.underlying;
        temp.expiry = pos.expiry;
        temp.currency = pos.currency;
        impact_metrics.push_back(RiskCalculator::calculate_risk(temp));
    }

    if (output_opts.json) {
        std::cout << utils::JsonOutput::impact_analysis(underlying_filter, positions, impact_metrics) << "\n";
        return Result<void>{};
    }
    if (output_opts.quiet) return Result<void>{};

    std::cout << "\nImpact Analysis: " << underlying_filter << "\n"
              << std::string(80, '-') << "\n\nCurrent Positions:\n";
    for (const auto& pos : positions) {
        std::cout << "  " << pos.expiry << " " << utils::get_currency_symbol(pos.currency)
                  << std::fixed << std::setprecision(2)
                  << pos.strike << " " << pos.right << " × " << pos.quantity
                  << " @ " << utils::get_currency_symbol(pos.currency) << pos.entry_premium << "\n";
    }

    double total_max_profit = 0.0, total_max_loss = 0.0;
    double total_loss_10pct = 0.0, total_loss_20pct = 0.0;
    std::cout << "\nRisk Summary:\n";
    for (const auto& pos : positions) {
        if (pos.right == 'P' && pos.quantity < 0) {
            double premium = analysis::premium_for(pos.quantity, pos.entry_premium);
            double breakeven = pos.strike - pos.entry_premium;
            double max_loss = (pos.strike - pos.entry_premium) * analysis::constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);
            double loss_10pct = std::max(0.0, pos.strike * 0.10 - pos.entry_premium) * analysis::constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);
            double loss_20pct = std::max(0.0, pos.strike * 0.20 - pos.entry_premium) * analysis::constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);
            std::cout << "  Short Put " << utils::get_currency_symbol(pos.currency) << pos.strike
                      << ": Breakeven=" << utils::get_currency_symbol(pos.currency) << std::fixed
                      << std::setprecision(2) << breakeven << ", Max Profit="
                      << utils::get_currency_symbol(pos.currency) << premium
                      << ", Max Loss=" << utils::get_currency_symbol(pos.currency) << max_loss << "\n";
            total_max_profit += converter.convert(premium, pos.currency);
            total_max_loss += converter.convert(max_loss, pos.currency);
            total_loss_10pct += converter.convert(loss_10pct, pos.currency);
            total_loss_20pct += converter.convert(loss_20pct, pos.currency);
        }
    }

    std::cout << "\nPortfolio Total (in " << converter.get_base_currency() << "):\n"
              << "  Max Profit: " << utils::get_currency_symbol(converter.get_base_currency())
              << std::fixed << std::setprecision(2) << total_max_profit << "\n"
              << "  Max Loss: " << utils::get_currency_symbol(converter.get_base_currency())
              << total_max_loss << "\n"
              << "  10% Loss: " << utils::get_currency_symbol(converter.get_base_currency())
              << total_loss_10pct << "\n"
              << "  20% Loss: " << utils::get_currency_symbol(converter.get_base_currency())
              << total_loss_20pct << "\n\n";
    return Result<void>{};
}

Result<void> AnalyzeCommand::analyze_strategy(
    const config::Config& config,
    const std::string& account_filter,
    const std::string& underlying_filter,
    const utils::OutputOptions& output_opts) {

    Logger::info("Analyzing strategies");

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::StrategyService strategy_service(database);
    auto analysis_result = strategy_service.analyze_strategies(
        account_filter, underlying_filter);
    if (!analysis_result) {
        return Error{"Failed to analyze strategies", analysis_result.error().message};
    }

    const auto& analysis = *analysis_result;

    if (analysis.strategies.empty()) {
        if (output_opts.json) {
            std::cout << utils::JsonOutput::strategies({}, {}, {}) << "\n";
        } else {
            std::cout << "No strategies detected.\n";
        }
        return Result<void>{};
    }

    // Load account names
    services::PositionService position_service(database);
    auto account_names_result = position_service.load_account_names();
    std::map<int64_t, std::string> account_names = account_names_result
        ? *account_names_result : std::map<int64_t, std::string>{};

    if (output_opts.json) {
        std::cout << utils::JsonOutput::strategies(analysis.strategies, analysis.metrics, account_names) << "\n";
        return Result<void>{};
    }
    if (output_opts.quiet) return Result<void>{};

    // Group strategies by account
    std::map<std::string, std::vector<std::pair<const Strategy*, const RiskMetrics*>>> by_account;
    for (size_t i = 0; i < analysis.strategies.size(); ++i) {
        const auto& strategy = analysis.strategies[i];
        if (strategy.legs.empty()) continue;
        std::string acct = "Unknown";
        auto it = account_names.find(strategy.legs[0].account_id);
        if (it != account_names.end()) acct = it->second;
        by_account[acct].push_back({&strategy, &analysis.metrics[i]});
    }

    std::cout << "\nDetected Strategies (" << analysis.strategies.size() << " total";
    if (account_filter.empty()) std::cout << " across " << by_account.size() << " accounts";
    std::cout << "):\n" << std::string(80, '=') << "\n";

    for (const auto& [account_name, account_strategies] : by_account) {
        std::cout << "\n[" << account_name << "] - " << account_strategies.size() << " strateg"
                  << (account_strategies.size() == 1 ? "y" : "ies") << "\n"
                  << std::string(80, '-') << "\n";

        for (const auto& [strategy, metrics] : account_strategies) {
            std::cout << "\nStrategy: " << services::StrategyService::strategy_type_to_string(strategy->type) << "\n"
                      << "  Underlying: " << strategy->underlying << "\n"
                      << "  Expiry: " << strategy->expiry << " (" << metrics->days_to_expiry << " days)\n";

            if (strategy->legs.size() == 1) {
                const auto& leg = strategy->legs[0];
                std::cout << "  Strike: $" << std::fixed << std::setprecision(2) << leg.strike
                          << " " << leg.right << "\n"
                          << "  Quantity: " << leg.quantity << " (" << (leg.quantity < 0 ? "SHORT" : "LONG") << ")\n"
                          << "  Entry Premium: $" << leg.entry_premium << " per share\n";
            } else {
                std::cout << "  Legs:\n";
                for (const auto& leg : strategy->legs) {
                    std::cout << "    $" << std::fixed << std::setprecision(2) << leg.strike
                              << " " << leg.right << " × " << leg.quantity
                              << " @ $" << leg.entry_premium << "\n";
                }
            }

            std::cout << "\n  Risk Metrics:\n"
                      << "    Breakeven: $" << std::fixed << std::setprecision(2) << metrics->breakeven_price;
            if (metrics->breakeven_price_2 > 0) std::cout << " and $" << metrics->breakeven_price_2;
            std::cout << "\n    Max Profit: $" << metrics->max_profit << "\n";
            if (std::isinf(metrics->max_loss)) std::cout << "    Max Loss: UNLIMITED\n";
            else std::cout << "    Max Loss: $" << metrics->max_loss << "\n";
            std::cout << "    Risk Level: " << analysis::risk_level_to_string(metrics->risk_level) << "\n";
            if (metrics->net_premium > 0)
                std::cout << "    Net Premium: $" << metrics->net_premium << " (credit)\n";
            std::cout << "\n";
        }
    }

    const auto& pr = analysis.portfolio_risk;
    std::cout << std::string(80, '=') << "\nCONSOLIDATED PORTFOLIO SUMMARY";
    if (account_filter.empty()) std::cout << " (All Accounts)";
    std::cout << "\n" << std::string(80, '=') << "\n"
              << "  Total Strategies: " << pr.total_strategies << "\n"
              << "  Total Max Profit: " << utils::get_currency_symbol(pr.base_currency)
              << std::fixed << std::setprecision(2) << pr.total_max_profit << "\n"
              << "  Total Max Loss: " << utils::get_currency_symbol(pr.base_currency)
              << pr.total_max_loss << "\n"
              << "  10% Loss: " << utils::get_currency_symbol(pr.base_currency)
              << pr.total_loss_10pct << "\n"
              << "  20% Loss: " << utils::get_currency_symbol(pr.base_currency)
              << pr.total_loss_20pct << "\n"
              << "  Expiring in 7 days: " << pr.positions_expiring_soon << " positions\n";

    // Per-currency breakdown
    if (pr.max_profit_by_currency.size() > 1 || pr.max_loss_by_currency.size() > 1) {
        std::cout << "\n  Per-Currency Breakdown:\n";
        for (const auto& [ccy, profit] : pr.max_profit_by_currency) {
            std::cout << "    " << ccy << " Max Profit: " << utils::get_currency_symbol(ccy)
                      << std::fixed << std::setprecision(2) << profit << "\n";
        }
        for (const auto& [ccy, loss] : pr.max_loss_by_currency) {
            if (loss > 0) {
                std::cout << "    " << ccy << " Max Loss: " << utils::get_currency_symbol(ccy)
                          << std::fixed << std::setprecision(2) << loss << "\n";
            }
        }
    }
    std::cout << "\n";
    return Result<void>{};
}

Result<void> AnalyzeCommand::analyze_portfolio(
    const config::Config& config,
    const std::string& account_filter,
    const std::string& underlying_filter,
    const utils::OutputOptions& output_opts) {

    Logger::info("Analyzing portfolio review");

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::PositionService position_service(database);
    auto positions_result = position_service.load_positions(account_filter, underlying_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    if (positions.empty()) {
        if (output_opts.json) {
            std::cout << utils::JsonOutput::portfolio(services::PortfolioView{}) << "\n";
        } else {
            std::cout << "No open positions found.\n";
        }
        return Result<void>{};
    }

    // Fetch current prices
    services::PriceService price_service(database);
    std::vector<std::string> underlyings;
    for (const auto& pos : positions) underlyings.push_back(pos.underlying);
    auto current_prices = price_service.fetch_for_positions(underlyings);

    // Load account names
    auto account_names_result = position_service.load_account_names();
    std::map<int64_t, std::string> account_names = account_names_result
        ? *account_names_result : std::map<int64_t, std::string>{};

    // Build portfolio view
    services::PortfolioService portfolio_service;
    auto view = portfolio_service.build_portfolio_view(positions, current_prices, account_names);

    // JSON output
    if (output_opts.json) {
        std::cout << utils::JsonOutput::portfolio(view) << "\n";
        return Result<void>{};
    }
    if (output_opts.quiet) return Result<void>{};

    // --- Human-readable output ---

    // Section 1: Assignment Risk Alerts
    bool has_alerts = view.itm_count > 0 || view.near_money_count > 0 || view.expiring_soon_count > 0;
    if (has_alerts) {
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "ASSIGNMENT RISK ALERTS\n";
        std::cout << std::string(80, '=') << "\n";

        for (const auto& pp : view.positions) {
            if (pp.risk_alert.empty()) continue;
            std::cout << "  [" << pp.risk_alert << "] " << pp.account_name
                      << " | " << pp.position.underlying
                      << " $" << std::fixed << std::setprecision(2) << pp.position.strike
                      << "P exp " << pp.position.expiry;
            if (pp.has_price) {
                std::cout << " (now: $" << pp.current_price << ")";
            }
            std::cout << "\n";
        }
    }

    // Section 2: Positions Table
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "POSITIONS\n";
    std::cout << std::string(80, '=') << "\n";

    std::cout << std::left
              << std::setw(12) << "Account"
              << std::setw(8) << "Symbol"
              << std::setw(8) << "Strike"
              << std::setw(12) << "Expiry"
              << std::setw(5) << "DTE"
              << std::setw(10) << "Entry"
              << std::setw(10) << "P&L"
              << std::setw(8) << "OTM%"
              << std::setw(8) << "Yield%"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& pp : view.positions) {
        int dte = RiskCalculator::calculate_days_to_expiry(pp.position.expiry);
        std::cout << std::left << std::fixed << std::setprecision(2)
                  << std::setw(12) << pp.account_name
                  << std::setw(8) << pp.position.underlying
                  << std::setw(8) << pp.position.strike
                  << std::setw(12) << pp.position.expiry
                  << std::setw(5) << dte
                  << std::setw(10) << pp.position.entry_premium
                  << std::setw(10) << pp.pnl
                  << std::setw(8) << pp.otm_percent
                  << std::setw(8) << pp.annualized_yield;
        if (!pp.risk_alert.empty()) std::cout << " [" << pp.risk_alert << "]";
        std::cout << "\n";
    }

    // Section 3: Risk Aggregation
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "RISK SUMMARY\n";
    std::cout << std::string(80, '=') << "\n";
    std::cout << "  Total Positions: " << view.total_positions << "\n";
    std::cout << "  Premium Collected: $" << std::fixed << std::setprecision(2)
              << view.total_premium_collected << "\n";
    std::cout << "  Unrealized P&L: $" << view.total_unrealized_pnl << "\n";
    std::cout << "  ITM: " << view.itm_count
              << " | Near-money: " << view.near_money_count
              << " | Expiring Soon: " << view.expiring_soon_count << "\n";

    if (!view.loss_10pct.empty()) {
        std::cout << "\n  Loss Scenarios (per account):\n";
        for (const auto& [acct, loss10] : view.loss_10pct) {
            double loss20 = view.loss_20pct.count(acct) ? view.loss_20pct.at(acct) : 0.0;
            std::cout << "    [" << acct << "] 10% loss: $" << loss10
                      << " | 20% loss: $" << loss20 << "\n";
        }
    }

    // Section 4: Expiration Calendar
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "EXPIRATION CALENDAR\n";
    std::cout << std::string(80, '=') << "\n";

    std::vector<std::pair<std::string, std::string>> bucket_order = {
        {"<=7", "Expiring (<=7 days)"}, {"8-30", "Near-term (8-30 days)"},
        {"31-60", "Medium (31-60 days)"}, {"60+", "Far (60+ days)"}
    };
    for (const auto& [key, label] : bucket_order) {
        int count = view.dte_buckets.count(key) ? view.dte_buckets[key] : 0;
        std::cout << "  " << label << ": " << count << " positions\n";
    }

    std::cout << "\n";
    return Result<void>{};
}

Result<void> AnalyzeCommand::analyze_screener(
    const config::Config& config,
    const utils::OutputOptions& output_opts,
    bool cache_only,
    const ScreenerOverrides& overrides) {

    Logger::info("Running opportunity screener (cache_only={})", cache_only);

    if (config.screener.watchlist.empty()) {
        return Error{"No watchlist configured",
                     "Add a 'screener' section with a 'watchlist' array to config.json"};
    }

    // Apply overrides to a copy of screener config
    config::ScreenerConfig screener_cfg = config.screener;
    overrides.apply_to(screener_cfg);

    if (overrides.has_overrides()) {
        Logger::info("Screener overrides: iv_pctl={} yield={} dte={}-{} otm_buf={}",
                     screener_cfg.min_iv_percentile, screener_cfg.min_premium_yield,
                     screener_cfg.min_dte, screener_cfg.max_dte, screener_cfg.otm_buffer_percent);
    }

    // Load existing positions (to flag existing exposure)
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::PositionService position_service(database);
    auto positions_result = position_service.load_positions();
    std::vector<analysis::Position> existing;
    if (positions_result) existing = *positions_result;

    // Run screener with (possibly overridden) config
    services::ScreenerService screener_service(screener_cfg, config.alpha_vantage, database);
    auto output = screener_service.screen(existing, cache_only);

    // JSON output
    if (output_opts.json) {
        std::cout << utils::JsonOutput::screener(output) << "\n";
        return Result<void>{};
    }
    if (output_opts.quiet) return Result<void>{};

    // --- Human-readable output ---

    std::cout << "\n" << std::string(90, '=') << "\n";
    std::cout << "PUT SELLING OPPORTUNITIES\n";
    std::cout << std::string(90, '=') << "\n";
    std::cout << "Scanned: " << output.total_scanned
              << " | Passed filter: " << output.passed_filter << "\n\n";

    if (output.results.empty()) {
        std::cout << "No opportunities found matching criteria.\n";
        std::cout << "Try adjusting min_iv_percentile or min_premium_yield in config.\n\n";
        return Result<void>{};
    }

    std::cout << std::left << std::fixed << std::setprecision(2)
              << std::setw(8) << "Symbol"
              << std::setw(10) << "Price"
              << std::setw(8) << "IV"
              << std::setw(8) << "Strike"
              << std::setw(6) << "DTE"
              << std::setw(10) << "Premium"
              << std::setw(10) << "Yield%"
              << std::setw(8) << "IV Pctl"
              << std::setw(8) << "Grade"
              << std::setw(10) << "Existing"
              << "\n";
    std::cout << std::string(90, '-') << "\n";

    for (const auto& r : output.results) {
        std::cout << std::left << std::fixed << std::setprecision(2)
                  << std::setw(8) << r.symbol
                  << std::setw(10) << r.current_price
                  << std::setw(8) << (r.iv * 100.0)
                  << std::setw(8) << r.suggested_strike
                  << std::setw(6) << r.strike_dte
                  << std::setw(10) << r.premium
                  << std::setw(10) << r.annualized_yield
                  << std::setw(8) << r.iv_percentile
                  << std::setw(8) << std::string(1, r.risk_reward_grade)
                  << std::setw(10) << (r.has_existing_position ? "YES" : "");
        if (r.has_existing_position) {
            std::cout << " (" << r.existing_position_detail << ")";
        }
        std::cout << "\n";
    }

    if (!output.errors.empty()) {
        std::cout << "\nErrors: could not fetch data for: ";
        for (size_t i = 0; i < output.errors.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << output.errors[i];
        }
        std::cout << "\n";
    }

    std::cout << "\n";
    return Result<void>{};
}

} // namespace ibkr::commands