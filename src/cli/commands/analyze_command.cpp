#include "analyze_command.hpp"
#include "db/database.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/logger.hpp"
#include "utils/price_fetcher.hpp"
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
using analysis::StrategyDetector;
using analysis::RiskCalculator;
using analysis::Strategy;
using analysis::Position;
using analysis::RiskMetrics;

Result<void> AnalyzeCommand::execute(
    const config::Config& config,
    const std::string& analysis_type,
    const std::string& account_filter,
    const std::string& underlying_filter) {

    Logger::info("Starting analyze command: type={}", analysis_type);

    if (analysis_type == "open") {
        return analyze_open(config, account_filter, underlying_filter);
    } else if (analysis_type == "impact") {
        if (underlying_filter.empty()) {
            return Error{"Impact analysis requires --underlying option"};
        }
        return analyze_impact(config, underlying_filter, account_filter);
    } else if (analysis_type == "strategy") {
        return analyze_strategy(config, account_filter, underlying_filter);
    } else {
        return Error{
            "Invalid analysis type",
            "Must be one of: open, impact, strategy"
        };
    }
}

Result<void> AnalyzeCommand::analyze_open(
    const config::Config& config,
    const std::string& account_filter,
    const std::string& underlying_filter) {

    Logger::info("Analyzing open positions");

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{
            "Failed to initialize database",
            init_result.error().message
        };
    }

    // Load positions
    auto positions_result = StrategyDetector::load_positions(database, 0);
    if (!positions_result) {
        return Error{
            "Failed to load positions",
            positions_result.error().message
        };
    }

    auto positions = *positions_result;

    // Apply filters
    if (!account_filter.empty() || !underlying_filter.empty()) {
        positions.erase(
            std::remove_if(positions.begin(), positions.end(),
                [&](const Position& pos) {
                    if (!account_filter.empty()) {
                        // Get account name from database
                        auto db_ptr = database.get_db();
                        SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
                        query.bind(1, pos.account_id);
                        if (query.executeStep()) {
                            std::string account_name = query.getColumn(0).getString();
                            if (account_name != account_filter) {
                                return true;
                            }
                        }
                    }
                    if (!underlying_filter.empty() && pos.underlying != underlying_filter) {
                        return true;
                    }
                    return false;
                }),
            positions.end()
        );
    }

    if (positions.empty()) {
        std::cout << "No open positions found.\n";
        return Result<void>{};
    }

    // Display positions
    std::cout << "\nOpen Positions (" << positions.size() << "):\n";
    std::cout << std::string(80, '=') << "\n";

    auto db_ptr = database.get_db();

    // Collect unique underlyings for price fetching
    std::set<std::string> unique_underlyings;
    for (const auto& pos : positions) {
        unique_underlyings.insert(pos.underlying);
    }

    // Fetch current prices
    Logger::info("Fetching current prices for {} underlyings", unique_underlyings.size());
    utils::PriceFetcher price_fetcher;
    std::vector<std::string> symbols(unique_underlyings.begin(), unique_underlyings.end());
    auto current_prices = price_fetcher.fetch_prices(symbols);

    // Build account name lookup
    std::map<int64_t, std::string> account_names;
    {
        SQLite::Statement q(*db_ptr, "SELECT id, name FROM accounts");
        while (q.executeStep()) {
            account_names[q.getColumn(0).getInt64()] = q.getColumn(1).getString();
        }
    }

    // Group positions by duration bucket
    struct Bucket {
        std::string label;
        int min_days;
        int max_days;  // inclusive, -1 = no upper bound
    };

    std::vector<Bucket> buckets = {
        {"Expiring within 1 week  (≤7 days)",   0,  7},
        {"Expiring within 2 weeks (8-14 days)", 8, 14},
        {"Expiring within 3 weeks (15-21 days)",15, 21},
        {"Expiring > 3 weeks     (>21 days)",   22, -1},
    };

    // Assign positions to buckets
    struct PosEntry {
        Position pos;
        int days;
        std::string account_name;
    };

    std::vector<std::vector<PosEntry>> bucket_entries(buckets.size());
    for (const auto& pos : positions) {
        int days = RiskCalculator::calculate_days_to_expiry(pos.expiry);
        std::string acct = "Unknown";
        auto it = account_names.find(pos.account_id);
        if (it != account_names.end()) acct = it->second;

        PosEntry entry{pos, days, acct};
        for (size_t b = 0; b < buckets.size(); ++b) {
            if (days >= buckets[b].min_days &&
                (buckets[b].max_days < 0 || days <= buckets[b].max_days)) {
                bucket_entries[b].push_back(entry);
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
        std::cout << "\n";
        std::cout << std::string(80, '-') << "\n";

        if (entries.empty()) {
            std::cout << "  (none)\n";
            continue;
        }

        // Sort entries: by account, then underlying, then expiry
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

            // Show current price
            if (current_prices.count(e.pos.underlying)) {
                std::cout << "  (now: $" << std::fixed << std::setprecision(2)
                          << current_prices[e.pos.underlying].price << ")";
            }

            // Assignment risk indicator
            if (current_prices.count(e.pos.underlying) && e.pos.quantity < 0) {
                double cp = current_prices[e.pos.underlying].price;
                double dist = e.pos.right == 'P'
                    ? ((cp - e.pos.strike) / cp) * 100.0
                    : ((e.pos.strike - cp) / cp) * 100.0;
                bool itm = (e.pos.right == 'P' && cp < e.pos.strike) ||
                           (e.pos.right == 'C' && cp > e.pos.strike);
                if (itm) {
                    std::cout << " ⚠️ ITM " << std::abs(dist) << "%";
                } else if (dist < 5.0) {
                    std::cout << " ⚡ " << dist << "% OTM";
                }
            }

            std::cout << "\n";
        }
    }

    // Add consolidated portfolio summary
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "CONSOLIDATED PORTFOLIO SUMMARY\n";
    std::cout << std::string(80, '=') << "\n";

    // Calculate totals
    int total_positions = positions.size();
    int short_puts = 0;
    int short_calls = 0;
    int long_positions = 0;
    int expiring_7_days = 0;
    int expiring_30_days = 0;
    double total_premium_collected = 0.0;
    double total_max_loss = 0.0;

    // Risk level categorization
    struct PositionInfo {
        std::string label;
        std::string expiry;
        std::string account_name;
        double assignment_capital;
    };

    struct RiskLevel {
        int count = 0;
        double assignment_capital = 0.0;  // Capital needed if assigned
        std::vector<PositionInfo> positions;
    };

    RiskLevel level1;  // ITM or within 1% OTM
    RiskLevel level2;  // 1-5% OTM
    RiskLevel level3;  // 5-10% OTM
    RiskLevel level4;  // >10% OTM (safe)

    for (const auto& pos : positions) {
        int days = RiskCalculator::calculate_days_to_expiry(pos.expiry);

        if (days <= 7) expiring_7_days++;
        if (days <= 30) expiring_30_days++;

        // Get account name for this position
        SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
        query.bind(1, pos.account_id);
        std::string account_name = "Unknown";
        if (query.executeStep()) {
            account_name = query.getColumn(0).getString();
        }

        if (pos.quantity < 0) {
            // Short position
            double premium = std::abs(pos.quantity) * pos.entry_premium * 100.0;
            total_premium_collected += premium;

            if (pos.right == 'P') {
                short_puts++;
                double max_loss = (pos.strike * 100.0 * std::abs(pos.quantity)) - premium;
                total_max_loss += max_loss;

                // Calculate assignment capital (strike * 100 * quantity)
                double assignment_capital = pos.strike * 100.0 * std::abs(pos.quantity);

                // Categorize by risk level
                if (current_prices.count(pos.underlying)) {
                    double current_price = current_prices[pos.underlying].price;
                    double distance_pct = ((current_price - pos.strike) / current_price) * 100.0;

                    PositionInfo info;
                    info.label = pos.underlying + " $" + std::to_string(static_cast<int>(pos.strike)) + "P";
                    info.expiry = pos.expiry;
                    info.account_name = account_name;
                    info.assignment_capital = assignment_capital;

                    if (current_price <= pos.strike || distance_pct <= 1.0) {
                        // Level 1: ITM or within 1% OTM
                        level1.count++;
                        level1.assignment_capital += assignment_capital;
                        level1.positions.push_back(info);
                    } else if (distance_pct <= 5.0) {
                        // Level 2: 1-5% OTM
                        level2.count++;
                        level2.assignment_capital += assignment_capital;
                        level2.positions.push_back(info);
                    } else if (distance_pct <= 10.0) {
                        // Level 3: 5-10% OTM
                        level3.count++;
                        level3.assignment_capital += assignment_capital;
                        level3.positions.push_back(info);
                    } else {
                        // Level 4: >10% OTM (safe)
                        level4.count++;
                        level4.assignment_capital += assignment_capital;
                        level4.positions.push_back(info);
                    }
                } else {
                    // No price available, assume safe
                    PositionInfo info;
                    info.label = pos.underlying + " $" + std::to_string(static_cast<int>(pos.strike)) + "P";
                    info.expiry = pos.expiry;
                    info.account_name = account_name;
                    info.assignment_capital = assignment_capital;
                    level4.count++;
                    level4.assignment_capital += assignment_capital;
                    level4.positions.push_back(info);
                }
            } else if (pos.right == 'C') {
                short_calls++;

                // For short calls, assignment means delivering shares
                // Capital impact is unlimited (or current price * 100 * quantity if assigned)
                if (current_prices.count(pos.underlying)) {
                    double current_price = current_prices[pos.underlying].price;
                    double distance_pct = ((pos.strike - current_price) / current_price) * 100.0;

                    PositionInfo info;
                    info.label = pos.underlying + " $" + std::to_string(static_cast<int>(pos.strike)) + "C";
                    info.expiry = pos.expiry;
                    info.account_name = account_name;
                    info.assignment_capital = 0.0;  // Calls don't require capital (deliver shares)

                    if (current_price >= pos.strike || distance_pct <= 1.0) {
                        // Level 1: ITM or within 1% OTM
                        level1.count++;
                        level1.positions.push_back(info);
                    } else if (distance_pct <= 5.0) {
                        // Level 2: 1-5% OTM
                        level2.count++;
                        level2.positions.push_back(info);
                    } else if (distance_pct <= 10.0) {
                        // Level 3: 5-10% OTM
                        level3.count++;
                        level3.positions.push_back(info);
                    } else {
                        // Level 4: >10% OTM (safe)
                        level4.count++;
                        level4.positions.push_back(info);
                    }
                }
            }
        } else {
            long_positions++;
        }
    }

    std::cout << "\nPosition Breakdown:\n";
    std::cout << "  Total Positions: " << total_positions << "\n";
    std::cout << "  Short Puts: " << short_puts << "\n";
    std::cout << "  Short Calls: " << short_calls << "\n";
    std::cout << "  Long Positions: " << long_positions << "\n";

    std::cout << "\nExpiration Schedule:\n";
    std::cout << "  Expiring in 7 days: " << expiring_7_days << " positions\n";
    std::cout << "  Expiring in 30 days: " << expiring_30_days << " positions\n";

    std::cout << "\nRisk Summary:\n";
    std::cout << "  Total Premium Collected: $" << std::fixed << std::setprecision(2)
              << total_premium_collected << "\n";
    std::cout << "  Total Max Loss (short puts): $" << total_max_loss << "\n";
    if (short_calls > 0) {
        std::cout << "  Short Calls Max Loss: UNLIMITED (" << short_calls << " positions)\n";
    }

    // Risk Level Categorization
    std::cout << "\n" << std::string(80, '-') << "\n";
    std::cout << "ASSIGNMENT RISK LEVELS\n";
    std::cout << std::string(80, '-') << "\n";

    // Sort positions by account, then by expiry date within each level
    auto sort_by_account_and_expiry = [](std::vector<PositionInfo>& positions) {
        std::sort(positions.begin(), positions.end(),
            [](const PositionInfo& a, const PositionInfo& b) {
                if (a.account_name != b.account_name) {
                    return a.account_name < b.account_name;
                }
                return a.expiry < b.expiry;
            });
    };

    sort_by_account_and_expiry(level1.positions);
    sort_by_account_and_expiry(level2.positions);
    sort_by_account_and_expiry(level3.positions);
    sort_by_account_and_expiry(level4.positions);

    // Helper function to display positions grouped by account
    auto display_positions = [](const std::vector<PositionInfo>& positions) {
        if (positions.empty()) return;

        std::string current_account = "";
        for (const auto& pos : positions) {
            if (pos.account_name != current_account) {
                if (!current_account.empty()) {
                    std::cout << "\n";
                }
                std::cout << "  [" << pos.account_name << "]:\n";
                current_account = pos.account_name;
            }
            std::cout << "    " << pos.expiry << " - " << pos.label;
            if (pos.assignment_capital > 0) {
                std::cout << " ($" << std::fixed << std::setprecision(0) << pos.assignment_capital << ")";
            }
            std::cout << "\n";
        }
    };

    std::cout << "\nLevel 1 - CRITICAL (ITM or ≤1% OTM):\n";
    std::cout << "  Positions: " << level1.count << "\n";
    if (level1.count > 0) {
        std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2)
                  << level1.assignment_capital << "\n";
        display_positions(level1.positions);
    }

    std::cout << "\nLevel 2 - HIGH (1-5% OTM):\n";
    std::cout << "  Positions: " << level2.count << "\n";
    if (level2.count > 0) {
        std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2)
                  << level2.assignment_capital << "\n";
        display_positions(level2.positions);
    }

    std::cout << "\nLevel 3 - MODERATE (5-10% OTM):\n";
    std::cout << "  Positions: " << level3.count << "\n";
    if (level3.count > 0) {
        std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2)
                  << level3.assignment_capital << "\n";
        display_positions(level3.positions);
    }

    std::cout << "\nLevel 4 - SAFE (>10% OTM):\n";
    std::cout << "  Positions: " << level4.count << "\n";
    if (level4.count > 0) {
        std::cout << "  Capital if Assigned: $" << std::fixed << std::setprecision(2)
                  << level4.assignment_capital << "\n";
    }

    // Total capital at risk summary
    double total_critical_capital = level1.assignment_capital + level2.assignment_capital;
    double total_all_capital = level1.assignment_capital + level2.assignment_capital +
                               level3.assignment_capital + level4.assignment_capital;

    std::cout << "\n" << std::string(80, '-') << "\n";
    std::cout << "CAPITAL REQUIREMENTS:\n";
    std::cout << "  Critical Risk (L1+L2): $" << std::fixed << std::setprecision(2)
              << total_critical_capital << " (" << (level1.count + level2.count) << " positions)\n";
    std::cout << "  Total if All Assigned: $" << total_all_capital
              << " (" << (level1.count + level2.count + level3.count + level4.count) << " positions)\n";

    if (current_prices.size() < unique_underlyings.size()) {
        std::cout << "\n⚠️  Note: Could not fetch prices for "
                  << (unique_underlyings.size() - current_prices.size())
                  << " underlyings\n";
    }

    std::cout << "\n";
    return Result<void>{};
}

Result<void> AnalyzeCommand::analyze_impact(
    const config::Config& config,
    const std::string& underlying_filter,
    const std::string& /* account_filter */) {

    Logger::info("Analyzing impact for underlying: {}", underlying_filter);

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{
            "Failed to initialize database",
            init_result.error().message
        };
    }

    // Load positions for this underlying
    auto positions_result = StrategyDetector::load_positions(database, 0);
    if (!positions_result) {
        return Error{
            "Failed to load positions",
            positions_result.error().message
        };
    }

    auto all_positions = *positions_result;

    // Filter by underlying
    std::vector<Position> positions;
    for (const auto& pos : all_positions) {
        if (pos.underlying == underlying_filter) {
            positions.push_back(pos);
        }
    }

    if (positions.empty()) {
        std::cout << "No positions found for " << underlying_filter << "\n";
        return Result<void>{};
    }

    std::cout << "\nImpact Analysis: " << underlying_filter << "\n";
    std::cout << std::string(80, '-') << "\n";

    std::cout << "\nCurrent Positions:\n";
    for (const auto& pos : positions) {
        std::cout << "  " << pos.expiry << " $" << std::fixed << std::setprecision(2)
                  << pos.strike << " " << pos.right << " × " << pos.quantity
                  << " @ $" << pos.entry_premium << "\n";
    }

    // Calculate total breakeven and risk
    double total_max_profit = 0.0;
    double total_max_loss = 0.0;

    std::cout << "\nRisk Summary:\n";
    for (const auto& pos : positions) {
        if (pos.right == 'P' && pos.quantity < 0) {
            // Short put
            double premium = std::abs(pos.quantity) * pos.entry_premium * 100.0;
            double breakeven = pos.strike - pos.entry_premium;
            double max_loss = (pos.strike - pos.entry_premium) * 100.0 * std::abs(pos.quantity);

            std::cout << "  Short Put $" << pos.strike << ": Breakeven=$" << std::fixed
                      << std::setprecision(2) << breakeven << ", Max Profit=$" << premium
                      << ", Max Loss=$" << max_loss << "\n";

            total_max_profit += premium;
            total_max_loss += max_loss;
        }
    }

    std::cout << "\nPortfolio Total:\n";
    std::cout << "  Max Profit: $" << std::fixed << std::setprecision(2) << total_max_profit << "\n";
    std::cout << "  Max Loss: $" << total_max_loss << "\n";
    std::cout << "  Capital at Risk: $" << total_max_loss << "\n";

    std::cout << "\n";
    return Result<void>{};
}

Result<void> AnalyzeCommand::analyze_strategy(
    const config::Config& config,
    const std::string& account_filter,
    const std::string& underlying_filter) {

    Logger::info("Analyzing strategies");

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{
            "Failed to initialize database",
            init_result.error().message
        };
    }

    // Detect strategies
    auto strategies_result = StrategyDetector::detect_all_strategies(database, 0);
    if (!strategies_result) {
        return Error{
            "Failed to detect strategies",
            strategies_result.error().message
        };
    }

    auto strategies = *strategies_result;

    // Apply filters
    if (!underlying_filter.empty()) {
        strategies.erase(
            std::remove_if(strategies.begin(), strategies.end(),
                [&](const Strategy& s) {
                    return s.underlying != underlying_filter;
                }),
            strategies.end()
        );
    }

    if (!account_filter.empty()) {
        auto db_ptr = database.get_db();
        strategies.erase(
            std::remove_if(strategies.begin(), strategies.end(),
                [&](const Strategy& s) {
                    if (s.legs.empty()) return true;
                    SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
                    query.bind(1, s.legs[0].account_id);
                    if (query.executeStep()) {
                        std::string account_name = query.getColumn(0).getString();
                        return account_name != account_filter;
                    }
                    return true;
                }),
            strategies.end()
        );
    }

    if (strategies.empty()) {
        std::cout << "No strategies detected.\n";
        return Result<void>{};
    }

    // Calculate risk metrics for each strategy
    std::vector<RiskMetrics> all_metrics;
    for (const auto& strategy : strategies) {
        all_metrics.push_back(RiskCalculator::calculate_risk(strategy));
    }

    // Group strategies by account for consolidated view
    std::map<std::string, std::vector<std::pair<const Strategy*, const RiskMetrics*>>> by_account;
    auto db_ptr = database.get_db();

    for (size_t i = 0; i < strategies.size(); ++i) {
        const auto& strategy = strategies[i];
        if (strategy.legs.empty()) continue;

        SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
        query.bind(1, strategy.legs[0].account_id);
        std::string account_name = "Unknown";
        if (query.executeStep()) {
            account_name = query.getColumn(0).getString();
        }
        by_account[account_name].push_back({&strategy, &all_metrics[i]});
    }

    // Display strategies grouped by account
    std::cout << "\nDetected Strategies (" << strategies.size() << " total";
    if (account_filter.empty()) {
        std::cout << " across " << by_account.size() << " accounts";
    }
    std::cout << "):\n";
    std::cout << std::string(80, '=') << "\n";

    for (const auto& [account_name, account_strategies] : by_account) {
        std::cout << "\n[" << account_name << "] - " << account_strategies.size() << " strateg";
        if (account_strategies.size() == 1) {
            std::cout << "y\n";
        } else {
            std::cout << "ies\n";
        }
        std::cout << std::string(80, '-') << "\n";

        for (const auto& [strategy, metrics] : account_strategies) {
            std::cout << "\nStrategy: " << StrategyDetector::strategy_type_to_string(strategy->type) << "\n";
            std::cout << "  Underlying: " << strategy->underlying << "\n";
            std::cout << "  Expiry: " << strategy->expiry << " (" << metrics->days_to_expiry << " days)\n";

            // Display legs
            if (strategy->legs.size() == 1) {
                const auto& leg = strategy->legs[0];
                std::cout << "  Strike: $" << std::fixed << std::setprecision(2) << leg.strike
                          << " " << leg.right << "\n";
                std::cout << "  Quantity: " << leg.quantity << " (";
                if (leg.quantity < 0) {
                    std::cout << "SHORT";
                } else {
                    std::cout << "LONG";
                }
                std::cout << ")\n";
                std::cout << "  Entry Premium: $" << leg.entry_premium << " per share\n";
            } else {
                std::cout << "  Legs:\n";
                for (const auto& leg : strategy->legs) {
                    std::cout << "    $" << std::fixed << std::setprecision(2) << leg.strike
                              << " " << leg.right << " × " << leg.quantity
                              << " @ $" << leg.entry_premium << "\n";
                }
            }

            // Display risk metrics
            std::cout << "\n  Risk Metrics:\n";
            std::cout << "    Breakeven: $" << std::fixed << std::setprecision(2)
                      << metrics->breakeven_price;
            if (metrics->breakeven_price_2 > 0) {
                std::cout << " and $" << metrics->breakeven_price_2;
            }
            std::cout << "\n";
            std::cout << "    Max Profit: $" << metrics->max_profit << "\n";

            if (std::isinf(metrics->max_loss)) {
                std::cout << "    Max Loss: UNLIMITED\n";
            } else {
                std::cout << "    Max Loss: $" << metrics->max_loss << "\n";
            }

            std::cout << "    Risk Level: " << metrics->risk_level << "\n";

            if (metrics->net_premium > 0) {
                std::cout << "    Net Premium: $" << metrics->net_premium << " (credit)\n";
            }

            std::cout << "\n";
        }
    }

    // Portfolio summary (consolidated across all accounts)
    auto portfolio_risk = RiskCalculator::calculate_portfolio_risk(strategies, all_metrics);

    std::cout << std::string(80, '=') << "\n";
    std::cout << "CONSOLIDATED PORTFOLIO SUMMARY";
    if (account_filter.empty()) {
        std::cout << " (All Accounts)";
    }
    std::cout << "\n";
    std::cout << std::string(80, '=') << "\n";
    std::cout << "  Total Strategies: " << portfolio_risk.total_strategies << "\n";
    std::cout << "  Total Max Profit: $" << std::fixed << std::setprecision(2)
              << portfolio_risk.total_max_profit << "\n";
    std::cout << "  Total Max Loss: $" << portfolio_risk.total_max_loss << "\n";
    std::cout << "  Capital at Risk: $" << portfolio_risk.total_capital_at_risk << "\n";
    std::cout << "  Expiring in 7 days: " << portfolio_risk.positions_expiring_soon << " positions\n";

    std::cout << "\n";
    return Result<void>{};
}

} // namespace ibkr::commands
