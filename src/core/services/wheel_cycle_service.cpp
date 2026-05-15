#include "wheel_cycle_service.hpp"
#include "utils/logger.hpp"
#include "utils/currency.hpp"
#include <algorithm>
#include <cctype>
#include <map>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

WheelCycleService::WheelCycleService(db::Database& database)
    : database_(database) {
}

int WheelCycleService::derive_multiplier(const std::string& underlying) {
    // JP stocks (.T suffix) have multiplier of 1
    if (underlying.size() > 2 && underlying.substr(underlying.size() - 2) == ".T") {
        return 1;
    }

    // HK stocks: symbol core is all digits (e.g., "0700.HK" or "0700")
    std::string core = underlying;
    if (core.size() > 3 && core.substr(core.size() - 3) == ".HK") {
        core = core.substr(0, core.size() - 3);
    }
    if (core.size() > 2 && core.substr(core.size() - 2) == ".T") {
        core = core.substr(0, core.size() - 2);
    }
    bool all_digits = !core.empty() && std::all_of(core.begin(), core.end(),
        [](unsigned char c) { return std::isdigit(c); });
    if (all_digits) {
        return 100;
    }

    // US stocks have multiplier of 100
    return 100;
}

Result<int> WheelCycleService::build_wheel_cycles(int64_t account_id) {
    Logger::info("Building wheel cycles for account_id={}", account_id);

    // Clear existing wheel cycles
    auto clear_result = database_.clear_wheel_cycles(account_id);
    if (!clear_result) {
        return Error{"Failed to clear wheel cycles", clear_result.error().message};
    }

    // Get all assigned puts
    auto put_result = database_.get_round_trips(account_id, "", "", "", "");
    if (!put_result) {
        return Error{"Failed to get round trips", put_result.error().message};
    }

    // Filter to assigned puts only
    std::vector<db::Database::RoundTrip> assigned_puts;
    for (const auto& rt : *put_result) {
        if (rt.right == 'P' && rt.close_reason == "assigned") {
            assigned_puts.push_back(rt);
        }
    }

    Logger::info("Found {} assigned puts to process", assigned_puts.size());

    // Group puts by account and underlying for matching
    struct PutKey {
        int64_t account_id;
        std::string underlying;

        bool operator<(const PutKey& other) const {
            if (account_id != other.account_id) return account_id < other.account_id;
            return underlying < other.underlying;
        }
    };

    std::map<PutKey, std::vector<db::Database::RoundTrip>> puts_by_key;
    for (const auto& put : assigned_puts) {
        PutKey key{put.account_id, put.underlying};
        puts_by_key[key].push_back(put);
    }

    // Get all calls for matching
    std::vector<db::Database::RoundTrip> all_calls;
    for (const auto& rt : *put_result) {
        if (rt.right == 'C') {
            all_calls.push_back(rt);
        }
    }

    int cycles_created = 0;

    // For each group of assigned puts, try to find matching calls
    for (auto& [key, puts] : puts_by_key) {
        // Sort puts by close_date (assignment date) ascending
        std::sort(puts.begin(), puts.end(), [](const auto& a, const auto& b) {
            return a.close_date < b.close_date;
        });

        // Get calls for this account/underlying
        std::vector<db::Database::RoundTrip> matching_calls;
        for (const auto& call : all_calls) {
            if (call.account_id == key.account_id && call.underlying == key.underlying) {
                matching_calls.push_back(call);
            }
        }

        // Sort calls by open_date ascending
        std::sort(matching_calls.begin(), matching_calls.end(), [](const auto& a, const auto& b) {
            return a.open_date < b.open_date;
        });

        // Track which calls have been used
        std::vector<bool> call_used(matching_calls.size(), false);

        // Match puts to calls using best-match scoring
        for (const auto& put : puts) {
            db::Database::WheelCycle cycle;
            cycle.account_id = put.account_id;
            cycle.underlying = put.underlying;
            cycle.put_round_trip_id = put.id;
            cycle.put_strike = put.strike;
            cycle.quantity = put.quantity;
            cycle.multiplier = derive_multiplier(put.underlying);
            cycle.put_premium = put.net_premium;
            cycle.put_assigned_date = put.close_date;

            // Find best matching call: prefer quantity match, then strike >= put_strike
            const db::Database::RoundTrip* best_call = nullptr;
            int best_call_idx = -1;
            int best_score = -1;

            for (size_t i = 0; i < matching_calls.size(); ++i) {
                if (call_used[i]) continue;

                const auto& call = matching_calls[i];

                // Call must open after put was assigned
                if (call.open_date < put.close_date) continue;

                // Score: prefer quantity match and strike above cost basis
                bool qty_match = (call.quantity == put.quantity);
                bool strike_ok = (call.strike >= put.strike);
                int score = (qty_match ? 0 : 100) + (strike_ok ? 0 : 50);

                if (!best_call || score < best_score) {
                    best_call = &call;
                    best_call_idx = static_cast<int>(i);
                    best_score = score;
                }
            }

            if (best_call) {
                call_used[best_call_idx] = true;
                cycle.call_round_trip_id = best_call->id;
                cycle.call_strike = best_call->strike;
                cycle.call_premium = best_call->net_premium;
                cycle.call_close_date = best_call->close_date;
                cycle.call_close_reason = best_call->close_reason;

                if (best_call->quantity != put.quantity) {
                    Logger::warn("Wheel cycle {}: quantity mismatch - put {} vs call {}",
                                 put.underlying, put.quantity, best_call->quantity);
                }

                // Calculate stock P&L: (call_strike - put_strike) * qty * multiplier
                cycle.stock_pnl = (best_call->strike - put.strike) * put.quantity * cycle.multiplier;

                // Calculate option P&L: put premium + call premium
                cycle.option_pnl = put.net_premium + best_call->net_premium;

                // Total wheel P&L
                cycle.total_pnl = *cycle.stock_pnl + cycle.option_pnl;

                // Determine cycle status
                if (best_call->close_reason == "assigned") {
                    cycle.cycle_status = "completed";  // Stock called away
                } else if (best_call->close_reason == "expired") {
                    cycle.cycle_status = "stock_held";  // Call expired, still hold stock
                } else {
                    cycle.cycle_status = "completed";
                }

                Logger::debug("Matched wheel cycle: {} put_strike={} call_strike={} stock_pnl={} option_pnl={}",
                              put.underlying, put.strike, best_call->strike,
                              *cycle.stock_pnl, cycle.option_pnl);
            } else {
                // No matching call - incomplete cycle
                // call_strike, call_premium, stock_pnl remain nullopt
                cycle.option_pnl = put.net_premium;
                cycle.total_pnl = put.net_premium;
                cycle.cycle_status = "incomplete";

                Logger::debug("Incomplete wheel cycle: {} put_strike={} (no call found)",
                              put.underlying, put.strike);
            }

            // Insert cycle
            auto insert_result = database_.insert_wheel_cycle(cycle);
            if (insert_result) {
                cycles_created++;
            } else {
                Logger::warn("Failed to insert wheel cycle: {}", insert_result.error().message);
            }
        }
    }

    Logger::info("Created {} wheel cycles", cycles_created);
    return cycles_created;
}

Result<WheelOverviewMetrics> WheelCycleService::get_wheel_overview(
    int64_t account_id,
    const std::string& underlying) {

    auto cycles_result = database_.get_wheel_cycles(account_id, underlying);
    if (!cycles_result) {
        return Error{"Failed to get wheel cycles", cycles_result.error().message};
    }

    WheelOverviewMetrics metrics;

    for (const auto& cycle : *cycles_result) {
        metrics.total_option_pnl += cycle.option_pnl;
        metrics.total_stock_pnl += cycle.stock_pnl.value_or(0.0);
        metrics.total_wheel_pnl += cycle.total_pnl;

        if (cycle.cycle_status == "completed") {
            metrics.completed_cycles++;
        } else if (cycle.cycle_status == "stock_held") {
            metrics.stock_held_cycles++;
        } else {
            metrics.incomplete_cycles++;
        }
    }

    return metrics;
}

Result<std::vector<WheelCycleDisplay>> WheelCycleService::get_wheel_cycles(
    int64_t account_id,
    const std::string& underlying) {

    auto cycles_result = database_.get_wheel_cycles(account_id, underlying);
    if (!cycles_result) {
        return Error{"Failed to get wheel cycles", cycles_result.error().message};
    }

    std::vector<WheelCycleDisplay> displays;
    displays.reserve(cycles_result->size());

    for (const auto& cycle : *cycles_result) {
        WheelCycleDisplay d;
        d.id = cycle.id;
        d.account_name = cycle.account_name;
        d.underlying = cycle.underlying;
        d.put_strike = cycle.put_strike;
        d.call_strike = cycle.call_strike;
        d.quantity = cycle.quantity;
        d.multiplier = cycle.multiplier;
        d.put_premium = cycle.put_premium;
        d.call_premium = cycle.call_premium;
        d.stock_pnl = cycle.stock_pnl;
        d.option_pnl = cycle.option_pnl;
        d.total_pnl = cycle.total_pnl;
        d.put_assigned_date = cycle.put_assigned_date;
        d.call_close_date = cycle.call_close_date;
        d.call_close_reason = cycle.call_close_reason;
        d.cycle_status = cycle.cycle_status;
        d.put_round_trip_id = cycle.put_round_trip_id;
        d.call_round_trip_id = cycle.call_round_trip_id;
        displays.push_back(d);
    }

    return displays;
}

} // namespace ibkr::services
