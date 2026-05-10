#include "trade_matcher.hpp"
#include "utils/logger.hpp"
#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <set>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TradeMatcher::TradeMatcher(db::Database& database, SnapshotService& snapshot_service)
    : database_(database)
    , snapshot_service_(snapshot_service) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<int> TradeMatcher::match_all_trades(bool rebuild) {
    if (rebuild) {
        auto clear_result = database_.clear_round_trips();
        if (!clear_result) {
            return Error{"Failed to clear round trips for rebuild",
                         clear_result.error().message};
        }
    }

    auto accounts_result = database_.list_accounts();
    if (!accounts_result) {
        return Error{"Failed to list accounts", accounts_result.error().message};
    }

    int total_matched = 0;

    for (const auto& account : *accounts_result) {
        auto match_result = match_account_trades(account.id, false);
        if (!match_result) {
            Logger::warn("Failed to match trades for account {}: {}",
                         account.name, match_result.error().message);
            continue;
        }
        total_matched += *match_result;
    }

    Logger::info("Matched {} total round trips across all accounts", total_matched);
    return total_matched;
}

Result<int> TradeMatcher::match_account_trades(int64_t account_id, bool rebuild) {
    if (rebuild) {
        auto clear_result = database_.clear_round_trips(account_id);
        if (!clear_result) {
            return Error{"Failed to clear round trips for rebuild",
                         clear_result.error().message};
        }
    }

    // Load trades grouped by contract
    auto grouped_result = load_trades_grouped(account_id);
    if (!grouped_result) {
        return Error{"Failed to load trades for matching",
                     grouped_result.error().message};
    }

    auto& groups = *grouped_result;
    if (groups.empty()) {
        Logger::info("No trades found for account_id={}", account_id);
        return 0;
    }

    std::vector<MatchedRoundTrip> all_round_trips;
    int single_trade_groups = 0;

    // FIFO match each contract group
    for (auto& [contract_key, trades] : groups) {
        if (trades.size() < 2) {
            // Single trade — create round-trip from closing BookTrade if possible
            auto rt = match_single_closing_trade(trades.front());
            if (rt) {
                rt->account_id = account_id;
                all_round_trips.push_back(std::move(*rt));
            } else {
                single_trade_groups++;
                Logger::debug("Skipping single-trade group: {} (qty={:.0f}, date={})",
                              contract_key, trades.front().quantity, trades.front().trade_date);
            }
            continue;
        }

        auto round_trips = match_contract_fifo(trades);
        for (auto& rt : round_trips) {
            rt.account_id = account_id;
            all_round_trips.push_back(std::move(rt));
        }
    }

    if (single_trade_groups > 0) {
        Logger::info("{} contract groups have only 1 trade (need open+close pair for match)",
                     single_trade_groups);
    }

    // Snapshot-based fallback for positions that disappeared
    auto snapshot_result = match_from_snapshots(account_id);
    if (snapshot_result) {
        for (auto& rt : *snapshot_result) {
            all_round_trips.push_back(std::move(rt));
        }
    }

    // Write to database
    int written = 0;
    if (!all_round_trips.empty()) {
        auto write_result = write_round_trips(all_round_trips, account_id);
        if (!write_result) {
            return Error{"Failed to write round trips",
                         write_result.error().message};
        }
        written = *write_result;
    }

    Logger::info("Matched {} round trips for account_id={}", written, account_id);
    return written;
}

Result<std::vector<MatchedRoundTrip>> TradeMatcher::match_contract_trades(
    int64_t account_id,
    const std::string& underlying,
    double strike,
    char right,
    const std::string& expiry) {

    auto grouped_result = load_trades_grouped(account_id);
    if (!grouped_result) {
        return Error{"Failed to load trades", grouped_result.error().message};
    }

    auto& groups = *grouped_result;
    std::string key = build_contract_key(underlying, strike, right, expiry);

    auto it = groups.find(key);
    if (it == groups.end()) {
        return std::vector<MatchedRoundTrip>{};
    }

    auto trades = it->second;
    if (trades.size() < 2) {
        return std::vector<MatchedRoundTrip>{};
    }

    auto round_trips = match_contract_fifo(trades);
    for (auto& rt : round_trips) {
        rt.account_id = account_id;
    }

    return round_trips;
}

// ---------------------------------------------------------------------------
// Trade loading
// ---------------------------------------------------------------------------

Result<std::map<std::string, std::vector<TradeForMatch>>>
TradeMatcher::load_trades_grouped(int64_t account_id) {
    auto* db = database_.get_db();
    if (!db) return Error{"Database not initialized"};

    try {
        std::string sql =
            "SELECT id, account_id, trade_date, underlying, expiry, "
            "strike, right, quantity, trade_price, "
            "COALESCE(commission, 0.0), "
            "COALESCE(multiplier, 100.0), "
            "COALESCE(fifo_pnl_realized, 0.0), "
            "COALESCE(close_price, 0.0), "
            "COALESCE(notes_codes, '') "
            "FROM trades WHERE underlying IS NOT NULL "
            "AND expiry IS NOT NULL AND strike IS NOT NULL";

        if (account_id > 0) {
            sql += " AND account_id = ?";
        }
        sql += " ORDER BY trade_date ASC, id ASC";

        SQLite::Statement q(*db, sql);
        if (account_id > 0) {
            q.bind(1, account_id);
        }

        std::map<std::string, std::vector<TradeForMatch>> groups;

        while (q.executeStep()) {
            TradeForMatch t;
            t.id = q.getColumn(0).getInt64();
            t.account_id = q.getColumn(1).getInt64();
            t.trade_date = q.getColumn(2).getString();
            t.underlying = q.getColumn(3).getString();
            t.expiry = q.getColumn(4).getString();
            t.strike = q.getColumn(5).getDouble();
            std::string right_str = q.getColumn(6).getString();
            t.right = right_str.empty() ? 'P' : right_str[0];
            t.quantity = q.getColumn(7).getDouble();
            t.trade_price = q.getColumn(8).getDouble();
            t.commission = q.getColumn(9).getDouble();
            t.multiplier = q.getColumn(10).getDouble();
            t.fifo_pnl_realized = q.getColumn(11).getDouble();
            t.close_price = q.getColumn(12).getDouble();
            std::string notes = q.getColumn(13).getString();
            t.assigned = (notes.find('A') != std::string::npos);

            std::string key = build_contract_key(t.underlying, t.strike, t.right, t.expiry);
            groups[key].push_back(t);
        }

        Logger::debug("Loaded {} contract groups from trades", groups.size());
        if (groups.empty()) {
            // Diagnose: are there ANY rows in the trades table?
            try {
                SQLite::Statement count_q(*db, "SELECT COUNT(*) FROM trades");
                if (count_q.executeStep()) {
                    int total = count_q.getColumn(0).getInt();
                    Logger::info("No trade groups formed (total trades in DB: {})", total);
                    if (total > 0) {
                        SQLite::Statement null_q(*db,
                            "SELECT COUNT(*) FROM trades WHERE underlying IS NULL OR expiry IS NULL OR strike IS NULL");
                        if (null_q.executeStep()) {
                            int nulls = null_q.getColumn(0).getInt();
                            if (nulls > 0) {
                                Logger::warn("{} trades skipped due to NULL underlying/expiry/strike", nulls);
                            }
                        }
                    }
                }
            } catch (...) {}
        }
        return groups;

    } catch (const std::exception& e) {
        return Error{"Failed to load trades for matching", std::string(e.what())};
    }
}

// ---------------------------------------------------------------------------
// Single closing trade (missing opening trade)
// ---------------------------------------------------------------------------

std::optional<MatchedRoundTrip> TradeMatcher::match_single_closing_trade(
    const TradeForMatch& trade) {
    // Only handle closing BookTrade entries (trade_price=0, positive qty = buy to close)
    if (trade.trade_price > 0.01 || trade.quantity < 0) {
        return std::nullopt;
    }

    // Only handle expired OTM trades with FifoPnlRealized (the premium received)
    // Assigned trades without opening trades can't be reconstructed (no premium info)
    if (trade.fifo_pnl_realized < 0.01) {
        return std::nullopt;
    }

    MatchedRoundTrip rt;
    rt.underlying = trade.underlying;
    rt.strike = trade.strike;
    rt.right = trade.right;
    rt.expiry = trade.expiry;
    rt.quantity = static_cast<int>(std::round(std::abs(trade.quantity)));
    if (rt.quantity == 0) rt.quantity = 1;
    rt.close_date = trade.trade_date;
    rt.open_date = trade.trade_date;  // Unknown, use close_date as placeholder
    rt.holding_days = 0;
    rt.close_price = 0;  // Expired worthless
    rt.commission = trade.commission;
    rt.match_method = "single_trade";
    rt.close_reason = "expired";

    // FifoPnlRealized = premium received for expired OTM
    rt.realized_pnl = trade.fifo_pnl_realized;
    // Back-calculate open price per share
    if (rt.quantity > 0 && trade.multiplier > 0) {
        rt.open_price = trade.fifo_pnl_realized / (rt.quantity * trade.multiplier);
    } else {
        rt.open_price = 0;
    }
    rt.net_premium = trade.fifo_pnl_realized;

    return rt;
}

// ---------------------------------------------------------------------------
// FIFO matching algorithm
// ---------------------------------------------------------------------------

std::vector<MatchedRoundTrip>
TradeMatcher::match_contract_fifo(std::vector<TradeForMatch>& trades) {
    std::vector<MatchedRoundTrip> results;

    if (trades.empty()) return results;

    // Determine position direction from the first trade.
    // Negative quantity = sell to open (short position)
    // Positive quantity = buy to open (long position)
    bool is_short = trades.front().quantity < 0;

    // Opening quantity sign: negative for shorts, positive for longs
    double open_sign = is_short ? -1.0 : 1.0;

    // FIFO queue of opening trades with remaining unmatched quantity
    struct OpenEntry {
        TradeForMatch trade;
        double remaining; // unsigned quantity remaining to match
    };
    std::deque<OpenEntry> open_queue;

    for (auto& trade : trades) {
        double qty = trade.quantity;

        if ((is_short && qty < 0) || (!is_short && qty > 0)) {
            // This is an opening trade (same direction as position)
            open_queue.push_back({trade, std::abs(qty)});
        } else {
            // This is a closing trade (opposite direction)
            double close_qty = std::abs(qty);

            while (close_qty > 0.01 && !open_queue.empty()) {
                auto& front = open_queue.front();
                double match_qty = std::min(close_qty, front.remaining);

                MatchedRoundTrip rt;
                rt.underlying = front.trade.underlying;
                rt.strike = front.trade.strike;
                rt.right = front.trade.right;
                rt.expiry = front.trade.expiry;
                rt.quantity = static_cast<int>(std::round(match_qty));
                if (rt.quantity == 0) rt.quantity = 1; // minimum 1 contract
                rt.open_date = front.trade.trade_date;
                rt.close_date = trade.trade_date;
                rt.holding_days = calculate_holding_days(rt.open_date, rt.close_date);
                rt.open_price = front.trade.trade_price;
                rt.close_price = trade.trade_price;
                rt.commission = front.trade.commission + trade.commission;
                rt.close_reason = detect_close_reason(rt.close_date, rt.expiry);
                rt.match_method = "trade_match";

                // Track legs
                rt.open_legs.emplace_back(front.trade.id, rt.quantity);
                rt.close_legs.emplace_back(trade.id, rt.quantity);

                // Use IBKR's FifoPnlRealized for expired options (Ep code)
                // which have trade_price=0 but accurate realized P&L.
                // For assigned options (A code), FifoPnlRealized=0 and P&L
                // went to the stock position — estimate from close_price.
                if (trade.fifo_pnl_realized > 0.01 && trade.trade_price < 0.001) {
                    // Expired OTM: IBKR's P&L is accurate
                    rt.realized_pnl = trade.fifo_pnl_realized;
                    double total_open_qty = std::abs(front.trade.quantity);
                    if (total_open_qty > match_qty + 0.01) {
                        rt.realized_pnl *= (match_qty / total_open_qty);
                    }
                    rt.net_premium = rt.realized_pnl;
                } else if (trade.assigned && trade.fifo_pnl_realized < 0.01) {
                    // Assigned: P&L = premium received - intrinsic value at assignment
                    rt.close_reason = "assigned";
                    double premium_received = rt.quantity * rt.open_price * front.trade.multiplier;
                    double intrinsic_cost = rt.quantity * trade.close_price * front.trade.multiplier;
                    rt.realized_pnl = premium_received - intrinsic_cost - rt.commission;
                    rt.net_premium = premium_received;
                } else {
                    calculate_pnl(rt, is_short, front.trade.multiplier);
                }
                results.push_back(rt);

                front.remaining -= match_qty;
                close_qty -= match_qty;

                if (front.remaining < 0.01) {
                    open_queue.pop_front();
                }
            }
        }
    }

    // Log unmatched opening trades still in the queue
    double unmatched = 0.0;
    for (const auto& entry : open_queue) {
        unmatched += entry.remaining;
    }
    if (unmatched > 0.01) {
        Logger::debug("Unmatched open quantity {:.1f} for {} {} {:.0f} {} exp {}",
                      unmatched,
                      trades.front().underlying,
                      std::string(1, trades.front().right),
                      trades.front().strike,
                      trades.front().expiry,
                      is_short ? "(short)" : "(long)");
    }

    return results;
}

// ---------------------------------------------------------------------------
// P&L calculation
// ---------------------------------------------------------------------------

void TradeMatcher::calculate_pnl(MatchedRoundTrip& rt, bool is_short, double multiplier) {
    int qty = rt.quantity;

    if (is_short) {
        // Short position: sold to open at open_price, bought to close at close_price
        double premium_received = qty * rt.open_price * multiplier;
        double cost_to_close = qty * rt.close_price * multiplier;
        double gross_pnl = premium_received - cost_to_close;
        rt.net_premium = premium_received;
        rt.realized_pnl = gross_pnl - rt.commission;
    } else {
        // Long position: bought to open at open_price, sold to close at close_price
        double cost_to_open = qty * rt.open_price * multiplier;
        double premium_received = qty * rt.close_price * multiplier;
        double gross_pnl = premium_received - cost_to_open;
        rt.net_premium = -cost_to_open;
        rt.realized_pnl = gross_pnl - rt.commission;
    }
}

// ---------------------------------------------------------------------------
// Close reason detection
// ---------------------------------------------------------------------------

std::string TradeMatcher::detect_close_reason(
    const std::string& close_date,
    const std::string& expiry) {
    // Normalize YYYYMMDD -> YYYY-MM-DD for comparison
    auto normalize = [](const std::string& d) -> std::string {
        if (d.size() == 8 && d[4] != '-') {
            return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2);
        }
        return d;
    };
    if (normalize(close_date) == normalize(expiry)) {
        return "expired";
    }
    return "closed";
}

// ---------------------------------------------------------------------------
// Date helpers
// ---------------------------------------------------------------------------

int TradeMatcher::calculate_holding_days(
    const std::string& open_date,
    const std::string& close_date) {
    // Parse YYYY-MM-DD format and compute difference in days
    auto parse_ymd = [](const std::string& date) -> int {
        // date format: "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS"
        if (date.size() < 10) return 0;
        int y = std::atoi(date.substr(0, 4).c_str());
        int m = std::atoi(date.substr(5, 2).c_str());
        int d = std::atoi(date.substr(8, 2).c_str());

        // Convert to days since epoch using simplified formula
        // Days from year 0 approximation: 365*y + y/4 - y/100 + y/400
        auto to_days = [](int year, int month, int day) -> int {
            // Adjust for March-based year (Feb 29 handled correctly)
            if (month <= 2) {
                year--;
                month += 12;
            }
            return 365 * year + year / 4 - year / 100 + year / 400
                   + (153 * (month - 3) + 2) / 5 + day;
        };

        return to_days(y, m, d);
    };

    int open_days = parse_ymd(open_date);
    int close_days = parse_ymd(close_date);

    int diff = close_days - open_days;
    return diff > 0 ? diff : 0;
}

// ---------------------------------------------------------------------------
// Write to database
// ---------------------------------------------------------------------------

Result<int> TradeMatcher::write_round_trips(
    const std::vector<MatchedRoundTrip>& round_trips,
    int64_t account_id) {
    int written = 0;

    auto db_ptr = database_.get_db();
    if (!db_ptr) return Error{"Database not initialized"};
    SQLite::Transaction transaction(*db_ptr);

    for (const auto& rt : round_trips) {
        // Convert to database RoundTrip struct
        db::Database::RoundTrip db_rt;
        db_rt.account_id = account_id;
        db_rt.underlying = rt.underlying;
        db_rt.strike = rt.strike;
        db_rt.right = rt.right;
        db_rt.expiry = rt.expiry;
        db_rt.quantity = rt.quantity;
        db_rt.open_date = rt.open_date;
        db_rt.close_date = rt.close_date;
        db_rt.holding_days = rt.holding_days;
        db_rt.open_price = rt.open_price;
        db_rt.close_price = rt.close_price;
        db_rt.net_premium = rt.net_premium;
        db_rt.commission = rt.commission;
        db_rt.realized_pnl = rt.realized_pnl;
        db_rt.close_reason = rt.close_reason;
        db_rt.match_method = rt.match_method;

        auto insert_result = database_.insert_round_trip(db_rt);
        if (!insert_result) {
            Logger::warn("Failed to insert round trip for {} {} {:.0f}: {}",
                         rt.underlying, std::string(1, rt.right), rt.strike,
                         insert_result.error().message);
            continue;
        }

        int64_t round_trip_id = *insert_result;

        // Insert open legs
        for (const auto& [trade_id, match_qty] : rt.open_legs) {
            db::Database::RoundTripLeg leg;
            leg.round_trip_id = round_trip_id;
            leg.trade_id = trade_id;
            leg.role = "open";
            leg.matched_quantity = match_qty;

            auto leg_result = database_.insert_round_trip_leg(leg);
            if (!leg_result) {
                Logger::warn("Failed to insert open leg for round_trip_id={}: {}",
                             round_trip_id, leg_result.error().message);
            }
        }

        // Insert close legs
        for (const auto& [trade_id, match_qty] : rt.close_legs) {
            db::Database::RoundTripLeg leg;
            leg.round_trip_id = round_trip_id;
            leg.trade_id = trade_id;
            leg.role = "close";
            leg.matched_quantity = match_qty;

            auto leg_result = database_.insert_round_trip_leg(leg);
            if (!leg_result) {
                Logger::warn("Failed to insert close leg for round_trip_id={}: {}",
                             round_trip_id, leg_result.error().message);
            }
        }

        written++;
    }

    transaction.commit();
    Logger::info("Wrote {} round trips to database for account_id={}", written, account_id);
    return written;
}

// ---------------------------------------------------------------------------
// Snapshot-based fallback matching
// ---------------------------------------------------------------------------

Result<std::vector<MatchedRoundTrip>> TradeMatcher::match_from_snapshots(int64_t account_id) {
    std::vector<MatchedRoundTrip> results;

    // Get the latest snapshot date
    auto latest_result = snapshot_service_.get_latest_snapshot_date(account_id);
    if (!latest_result) {
        // No snapshots available - not an error, just nothing to do
        Logger::debug("No snapshots found for account_id={}, skipping snapshot matching",
                      account_id);
        return results;
    }

    const std::string& latest_date = *latest_result;

    // Find positions present in the latest snapshot but no longer open
    // We compare against current positions (get_all_positions)
    auto snapshot_result = snapshot_service_.get_snapshot(account_id, latest_date);
    if (!snapshot_result) {
        return Error{"Failed to get snapshot", snapshot_result.error().message};
    }

    auto current_positions_result = database_.get_all_positions("", account_id);
    if (!current_positions_result) {
        return Error{"Failed to get current positions",
                     current_positions_result.error().message};
    }

    // Build a set of currently open contract keys
    std::set<std::string> open_contracts;
    for (const auto& pos : *current_positions_result) {
        std::string key = build_contract_key(pos.underlying, pos.strike, pos.right, pos.expiry);
        open_contracts.insert(key);
    }

    // For each snapshot position not in current positions, create an inferred round trip
    for (const auto& snap : *snapshot_result) {
        std::string key = build_contract_key(
            snap.underlying, snap.strike, snap.right, snap.expiry);

        if (open_contracts.count(key)) {
            continue; // Still open, not a closed round trip
        }

        // Position disappeared - create an inferred round trip from snapshot data
        bool is_short = snap.quantity < 0;

        MatchedRoundTrip rt;
        rt.account_id = account_id;
        rt.underlying = snap.underlying;
        rt.strike = snap.strike;
        rt.right = snap.right;
        rt.expiry = snap.expiry;
        rt.quantity = std::abs(snap.quantity);
        rt.open_date = ""; // Unknown from snapshot
        rt.close_date = latest_date; // Best estimate
        rt.holding_days = 0;
        rt.open_price = snap.entry_price;
        rt.close_price = snap.mark_price; // Last known mark
        rt.commission = 0.0;
        rt.close_reason = detect_close_reason(latest_date, snap.expiry);
        rt.match_method = "snapshot";

        calculate_pnl(rt, is_short);
        results.push_back(rt);
    }

    if (!results.empty()) {
        Logger::info("Inferred {} round trips from snapshot for account_id={}",
                     results.size(), account_id);
    }

    return results;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

std::string TradeMatcher::build_contract_key(
    const std::string& underlying,
    double strike,
    char right,
    const std::string& expiry) {
    return fmt::format("{}_{:.4f}_{}_{}", underlying, strike, right, expiry);
}

} // namespace ibkr::services