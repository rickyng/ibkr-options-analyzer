#include "report_generator.hpp"
#include "analysis/strategy_detector.hpp"
#include "analysis/risk_calculator.hpp"
#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace ibkr::report {

using analysis::Strategy;
using analysis::RiskMetrics;
using analysis::RiskCalculator;
using analysis::StrategyDetector;

std::string ReportGenerator::generate_full_report(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics,
    const std::map<int64_t, std::string>& account_names) {

    std::ostringstream report;

    // Header
    report << std::string(80, '=') << "\n";
    report << "            IBKR Options Analyzer - Portfolio Report\n";
    report << std::string(80, '=') << "\n";
    report << "Generated: " << get_timestamp() << "\n\n";

    // Portfolio Summary
    report << generate_portfolio_summary(strategies, metrics);
    report << "\n";

    // Positions by Underlying
    report << generate_positions_by_underlying(strategies, metrics, account_names);
    report << "\n";

    // Strategies by Type
    report << generate_strategies_by_type(strategies, metrics);
    report << "\n";

    // Expiration Calendar
    report << generate_expiration_calendar(strategies, metrics);
    report << "\n";

    // Risk Analysis
    report << generate_risk_analysis(strategies, metrics);
    report << "\n";

    // Footer
    report << std::string(80, '=') << "\n";
    report << "                          End of Report\n";
    report << std::string(80, '=') << "\n";

    return report.str();
}

std::string ReportGenerator::generate_portfolio_summary(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics) {

    std::ostringstream section;

    section << "PORTFOLIO SUMMARY\n";
    section << std::string(80, '-') << "\n";

    // Calculate portfolio metrics
    auto portfolio_risk = RiskCalculator::calculate_portfolio_risk(strategies, metrics);

    section << std::left << std::setw(30) << "Total Positions:"
            << std::right << std::setw(20) << strategies.size() << "\n";

    section << std::left << std::setw(30) << "Total Strategies:"
            << std::right << std::setw(20) << portfolio_risk.total_strategies << "\n";

    section << std::left << std::setw(30) << "Total Max Profit:"
            << std::right << std::setw(20) << format_currency(portfolio_risk.total_max_profit) << "\n";

    section << std::left << std::setw(30) << "Total Max Loss:"
            << std::right << std::setw(20) << format_currency(portfolio_risk.total_max_loss) << "\n";

    section << std::left << std::setw(30) << "10% Loss:"
            << std::right << std::setw(20) << format_currency(portfolio_risk.total_loss_10pct) << "\n";

    section << std::left << std::setw(30) << "20% Loss:"
            << std::right << std::setw(20) << format_currency(portfolio_risk.total_loss_20pct) << "\n";

    section << std::left << std::setw(30) << "Expiring in 7 Days:"
            << std::right << std::setw(15) << portfolio_risk.positions_expiring_soon
            << " positions\n";

    return section.str();
}

std::string ReportGenerator::generate_positions_by_underlying(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics,
    const std::map<int64_t, std::string>& account_names) {

    std::ostringstream section;

    section << "POSITIONS BY UNDERLYING\n";
    section << std::string(80, '-') << "\n\n";

    // Group by account first, then by underlying
    std::map<int64_t, std::map<std::string, std::vector<std::pair<const Strategy*, const RiskMetrics*>>>> by_account_and_underlying;

    for (size_t i = 0; i < strategies.size(); ++i) {
        if (!strategies[i].legs.empty()) {
            int64_t account_id = strategies[i].legs[0].account_id;
            by_account_and_underlying[account_id][strategies[i].underlying].push_back({&strategies[i], &metrics[i]});
        }
    }

    // Display each account
    for (const auto& [account_id, by_underlying] : by_account_and_underlying) {
        auto it = account_names.find(account_id);
        std::string account_name = (it != account_names.end()) ? it->second : "Unknown Account";

        section << "[" << account_name << "]\n";
        section << std::string(80, '-') << "\n";

        // Display each underlying within this account
        for (const auto& [underlying, strats] : by_underlying) {
            section << "\n  " << underlying << " (" << strats.size() << " position";
            if (strats.size() != 1) section << "s";
            section << ")\n";

            for (const auto& [strategy, metric] : strats) {
                // Single-leg strategies
                if (strategy->legs.size() == 1) {
                    const auto& leg = strategy->legs[0];
                    section << "    " << leg.expiry << "  $" << std::fixed << std::setprecision(2)
                            << std::setw(7) << leg.strike << " " << leg.right
                            << "  x " << std::setw(5) << leg.quantity
                            << "  @ $" << std::setw(6) << leg.entry_premium;

                    if (leg.is_manual) {
                        section << "  [MANUAL]";
                    }

                    section << "  (" << metric->days_to_expiry << " days)";

                    if (metric->days_to_expiry <= 0) {
                        section << "  WARNING: EXPIRING";
                    }

                    section << "\n";
                } else {
                    // Multi-leg strategies
                    section << "    " << StrategyDetector::strategy_type_to_string(strategy->type) << "\n";
                    for (const auto& leg : strategy->legs) {
                        section << "      " << leg.expiry << "  $" << std::fixed << std::setprecision(2)
                                << std::setw(7) << leg.strike << " " << leg.right
                                << "  x " << std::setw(5) << leg.quantity
                                << "  @ $" << std::setw(6) << leg.entry_premium << "\n";
                    }
                    section << "      Breakeven: $" << metric->breakeven_price
                            << "  |  Max Profit: $" << metric->max_profit
                            << "  |  Max Loss: $" << metric->max_loss << "\n";
                }
            }
        }
        section << "\n";
    }

    return section.str();
}

std::string ReportGenerator::generate_strategies_by_type(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics) {

    std::ostringstream section;

    section << "STRATEGIES BY TYPE\n";
    section << std::string(80, '-') << "\n\n";

    // Group by strategy type
    std::map<Strategy::Type, std::vector<std::pair<const Strategy*, const RiskMetrics*>>> by_type;

    for (size_t i = 0; i < strategies.size(); ++i) {
        by_type[strategies[i].type].push_back({&strategies[i], &metrics[i]});
    }

    // Display each type
    for (const auto& [type, strats] : by_type) {
        section << StrategyDetector::strategy_type_to_string(type)
                << " (" << strats.size() << " strateg";
        if (strats.size() == 1) {
            section << "y";
        } else {
            section << "ies";
        }
        section << ")\n";

        // Calculate totals for this type
        double total_max_profit = 0.0;
        double total_max_loss = 0.0;
        int total_days = 0;
        int count_finite_loss = 0;

        for (const auto& [strategy, metric] : strats) {
            total_max_profit += metric->max_profit;
            if (std::isfinite(metric->max_loss)) {
                total_max_loss += metric->max_loss;
                count_finite_loss++;
            }
            total_days += metric->days_to_expiry;
        }

        section << "  Total Max Profit:       " << format_currency(total_max_profit) << "\n";

        if (count_finite_loss > 0) {
            section << "  Total Max Loss:         " << format_currency(total_max_loss) << "\n";
        } else {
            section << "  Total Max Loss:         UNLIMITED\n";
        }

        int avg_days = strats.empty() ? 0 : total_days / strats.size();
        section << "  Average Days to Expiry: " << avg_days << " days\n\n";
    }

    return section.str();
}

std::string ReportGenerator::generate_expiration_calendar(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics) {

    std::ostringstream section;

    section << "EXPIRATION CALENDAR\n";
    section << std::string(80, '-') << "\n\n";

    // Group by expiration timeframe
    std::vector<std::pair<const Strategy*, const RiskMetrics*>> this_week;
    std::vector<std::pair<const Strategy*, const RiskMetrics*>> this_month;
    std::vector<std::pair<const Strategy*, const RiskMetrics*>> later;

    for (size_t i = 0; i < strategies.size(); ++i) {
        if (metrics[i].days_to_expiry < 7) {
            this_week.push_back({&strategies[i], &metrics[i]});
        } else if (metrics[i].days_to_expiry < 30) {
            this_month.push_back({&strategies[i], &metrics[i]});
        } else {
            later.push_back({&strategies[i], &metrics[i]});
        }
    }

    // Sort by days to expiry
    auto sort_by_days = [](const auto& a, const auto& b) {
        return a.second->days_to_expiry < b.second->days_to_expiry;
    };

    std::sort(this_week.begin(), this_week.end(), sort_by_days);
    std::sort(this_month.begin(), this_month.end(), sort_by_days);
    std::sort(later.begin(), later.end(), sort_by_days);

    // Display this week
    section << "This Week (< 7 days): " << this_week.size() << " position";
    if (this_week.size() != 1) section << "s";
    section << "\n";

    for (const auto& [strategy, metric] : this_week) {
        if (strategy->legs.size() == 1) {
            const auto& leg = strategy->legs[0];
            section << "  " << leg.expiry << "  " << std::setw(6) << std::left << leg.underlying
                    << " $" << std::fixed << std::setprecision(2) << std::setw(7) << leg.strike
                    << " " << leg.right << "  x " << std::setw(5) << leg.quantity
                    << "  (" << metric->days_to_expiry << " days)";

            if (metric->days_to_expiry <= 0) {
                section << "  WARNING: EXPIRING";
            }
            section << "\n";
        } else {
            section << "  " << strategy->expiry << "  " << std::setw(6) << std::left
                    << strategy->underlying << " "
                    << StrategyDetector::strategy_type_to_string(strategy->type)
                    << "  (" << metric->days_to_expiry << " days)\n";
        }
    }
    section << "\n";

    // Display this month
    section << "This Month (7-30 days): " << this_month.size() << " position";
    if (this_month.size() != 1) section << "s";
    section << "\n";

    int shown = 0;
    for (const auto& [strategy, metric] : this_month) {
        if (shown >= 10) {
            section << "  ... and " << (this_month.size() - shown) << " more\n";
            break;
        }

        if (strategy->legs.size() == 1) {
            const auto& leg = strategy->legs[0];
            section << "  " << leg.expiry << "  " << std::setw(6) << std::left << leg.underlying
                    << " $" << std::fixed << std::setprecision(2) << std::setw(7) << leg.strike
                    << " " << leg.right << "  x " << std::setw(5) << leg.quantity
                    << "  (" << metric->days_to_expiry << " days)\n";
        } else {
            section << "  " << strategy->expiry << "  " << std::setw(6) << std::left
                    << strategy->underlying << " "
                    << StrategyDetector::strategy_type_to_string(strategy->type)
                    << "  (" << metric->days_to_expiry << " days)\n";
        }
        shown++;
    }
    section << "\n";

    // Display later
    section << "Later (> 30 days): " << later.size() << " position";
    if (later.size() != 1) section << "s";
    section << "\n";

    shown = 0;
    for (const auto& [strategy, metric] : later) {
        if (shown >= 5) {
            section << "  ... and " << (later.size() - shown) << " more\n";
            break;
        }

        if (strategy->legs.size() == 1) {
            const auto& leg = strategy->legs[0];
            section << "  " << leg.expiry << "  " << std::setw(6) << std::left << leg.underlying
                    << " $" << std::fixed << std::setprecision(2) << std::setw(7) << leg.strike
                    << " " << leg.right << "  x " << std::setw(5) << leg.quantity
                    << "  (" << metric->days_to_expiry << " days)\n";
        } else {
            section << "  " << strategy->expiry << "  " << std::setw(6) << std::left
                    << strategy->underlying << " "
                    << StrategyDetector::strategy_type_to_string(strategy->type)
                    << "  (" << metric->days_to_expiry << " days)\n";
        }
        shown++;
    }

    return section.str();
}

std::string ReportGenerator::generate_risk_analysis(
    const std::vector<Strategy>& strategies,
    const std::vector<RiskMetrics>& metrics) {

    std::ostringstream section;

    section << "RISK ANALYSIS\n";
    section << std::string(80, '-') << "\n\n";

    // Create pairs for sorting
    std::vector<std::pair<const Strategy*, const RiskMetrics*>> pairs;
    for (size_t i = 0; i < strategies.size(); ++i) {
        pairs.push_back({&strategies[i], &metrics[i]});
    }

    // Highest risk positions (by max loss)
    section << "Highest Risk Positions (Top 10 by Max Loss):\n";

    auto by_max_loss = pairs;
    std::sort(by_max_loss.begin(), by_max_loss.end(),
        [](const auto& a, const auto& b) {
            if (!std::isfinite(a.second->max_loss)) return true;
            if (!std::isfinite(b.second->max_loss)) return false;
            return a.second->max_loss > b.second->max_loss;
        });

    int rank = 1;
    for (const auto& [strategy, metric] : by_max_loss) {
        if (rank > 10) break;
        if (!std::isfinite(metric->max_loss)) continue;

        if (strategy->legs.size() == 1) {
            const auto& leg = strategy->legs[0];
            section << "  " << rank << ". " << std::setw(6) << std::left << leg.underlying
                    << " $" << std::fixed << std::setprecision(2) << std::setw(7) << leg.strike
                    << " " << leg.right << "  x " << std::setw(5) << leg.quantity
                    << "  Max Loss: " << format_currency(metric->max_loss)
                    << "  (" << metric->days_to_expiry << " days)";

            if (metric->days_to_expiry <= 3) {
                section << "  WARNING";
            }
            section << "\n";
        }
        rank++;
    }
    section << "\n";

    // Positions at risk (expiring soon)
    section << "Positions at Risk (Expiring in 3 days):\n";

    auto expiring_soon = pairs;
    std::sort(expiring_soon.begin(), expiring_soon.end(),
        [](const auto& a, const auto& b) {
            return a.second->days_to_expiry < b.second->days_to_expiry;
        });

    int count = 0;
    for (const auto& [strategy, metric] : expiring_soon) {
        if (metric->days_to_expiry > 3) break;
        if (count >= 10) {
            section << "  ... and more\n";
            break;
        }

        if (strategy->legs.size() == 1) {
            const auto& leg = strategy->legs[0];
            section << "  " << std::setw(6) << std::left << leg.underlying
                    << " $" << std::fixed << std::setprecision(2) << std::setw(7) << leg.strike
                    << " " << leg.right << "  x " << std::setw(5) << leg.quantity
                    << "  Breakeven: $" << std::setw(7) << metric->breakeven_price
                    << "  (" << metric->days_to_expiry << " days)";

            if (metric->days_to_expiry <= 0) {
                section << "  WARNING";
            }
            section << "\n";
        }
        count++;
    }

    return section.str();
}

std::string ReportGenerator::format_currency(double value, const std::string& currency) {
    std::ostringstream oss;
    oss << utils::get_currency_symbol(currency) << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

std::string ReportGenerator::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_time_t);

    std::ostringstream oss;
    oss << std::put_time(&now_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

} // namespace ibkr::report
