#include "analyze_command.hpp"
#include "db/database.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <algorithm>

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

    // Group by account first (for consolidated view)
    std::map<std::string, std::map<std::string, std::vector<Position>>> by_account_and_underlying;
    auto db_ptr = database.get_db();

    for (const auto& pos : positions) {
        SQLite::Statement query(*db_ptr, "SELECT name FROM accounts WHERE id = ?");
        query.bind(1, pos.account_id);
        std::string account_name = "Unknown";
        if (query.executeStep()) {
            account_name = query.getColumn(0).getString();
        }
        by_account_and_underlying[account_name][pos.underlying].push_back(pos);
    }

    // Display positions
    std::cout << "\nOpen Positions (" << positions.size() << " total";
    if (account_filter.empty()) {
        std::cout << " across " << by_account_and_underlying.size() << " accounts";
    }
    std::cout << "):\n";
    std::cout << std::string(80, '=') << "\n";

    // Show consolidated view by account
    for (const auto& [account_name, by_underlying] : by_account_and_underlying) {
        std::cout << "\n[" << account_name << "]\n";
        std::cout << std::string(80, '-') << "\n";

        for (const auto& [underlying, positions_list] : by_underlying) {
            std::cout << "\n  " << underlying << " (" << positions_list.size() << " position";
            if (positions_list.size() != 1) std::cout << "s";
            std::cout << "):\n";

            for (const auto& pos : positions_list) {
                int days_to_expiry = RiskCalculator::calculate_days_to_expiry(pos.expiry);

                std::cout << "    " << pos.expiry << " $" << std::fixed << std::setprecision(2)
                          << pos.strike << " " << pos.right << " × " << pos.quantity
                          << " @ $" << pos.entry_premium;

                if (pos.is_manual) {
                    std::cout << " [MANUAL]";
                }

                std::cout << " (" << days_to_expiry << " days)\n";
            }
        }
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
