#pragma once

#include "db/database.hpp"
#include "snapshot_service.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace ibkr::services {

/**
 * Trade for matching - represents a single trade from the trades table.
 */
struct TradeForMatch {
    int64_t id{0};
    int64_t account_id{0};
    std::string trade_date;
    std::string underlying;
    std::string expiry;
    double strike{0.0};
    char right{'P'};
    double quantity{0.0};
    double trade_price{0.0};
    double commission{0.0};
    double multiplier{100.0};
    double fifo_pnl_realized{0.0};
    double close_price{0.0};
    bool assigned{false};
};

/**
 * Matched round trip - represents a completed open->close cycle.
 */
struct MatchedRoundTrip {
    int64_t account_id{0};
    std::string underlying;
    double strike{0.0};
    char right{'P'};
    std::string expiry;
    int quantity{0};
    std::string open_date;
    std::string close_date;
    int holding_days{0};
    double open_price{0.0};
    double close_price{0.0};
    double net_premium{0.0};
    double commission{0.0};
    double realized_pnl{0.0};
    std::string close_reason;
    std::string match_method;
    std::vector<std::pair<int64_t, int>> open_legs;   // (trade_id, matched_quantity)
    std::vector<std::pair<int64_t, int>> close_legs;  // (trade_id, matched_quantity)
};

/**
 * TradeMatcher - FIFO matching engine for pairing opening and closing trades.
 *
 * This service matches trades to compute realized P&L for completed position cycles.
 * It groups trades by contract (underlying, strike, right, expiry) and uses FIFO
 * (First In, First Out) matching within each group.
 *
 * For short positions (selling to open): opening trades have negative quantity.
 * For long positions (buying to open): opening trades have positive quantity.
 *
 * Matching flow:
 * 1. Load all trades for an account, grouped by contract
 * 2. For each contract group, apply FIFO matching
 * 3. If trades don't fully match, fall back to snapshot-based inference
 * 4. Write matched round trips to database
 */
class TradeMatcher {
public:
    /**
     * Constructor.
     * @param database Database instance for trade queries and round trip writes
     * @param snapshot_service Snapshot service for position inference fallback
     */
    TradeMatcher(db::Database& database, SnapshotService& snapshot_service);

    /**
     * Match all trades across all accounts.
     * @param rebuild If true, clear existing round trips and rebuild from scratch
     * @return Result containing count of matched round trips or error
     */
    utils::Result<int> match_all_trades(bool rebuild = false);

    /**
     * Match trades for a specific account.
     * @param account_id Account ID to match trades for
     * @param rebuild If true, clear existing round trips for this account first
     * @return Result containing count of matched round trips or error
     */
    utils::Result<int> match_account_trades(int64_t account_id, bool rebuild = false);

    /**
     * Match trades for a specific contract (for testing/debugging).
     * @param account_id Account ID
     * @param underlying Underlying symbol
     * @param strike Strike price
     * @param right Option right (C or P)
     * @param expiry Expiration date
     * @return Result containing vector of matched round trips or error
     */
    utils::Result<std::vector<MatchedRoundTrip>> match_contract_trades(
        int64_t account_id,
        const std::string& underlying,
        double strike,
        char right,
        const std::string& expiry);

private:
    /**
     * Load trades from database, grouped by contract key.
     * @param account_id Account ID (0 for all accounts)
     * @return Result containing map of contract_key -> trades or error
     */
    utils::Result<std::map<std::string, std::vector<TradeForMatch>>> load_trades_grouped(int64_t account_id);

    /**
     * Apply FIFO matching algorithm to a list of trades for one contract.
     * @param trades Vector of trades for a single contract (sorted by date)
     * @return Vector of matched round trips
     */
    std::vector<MatchedRoundTrip> match_contract_fifo(std::vector<TradeForMatch>& trades);

    /**
     * Create a round-trip from a single closing trade (missing opening trade).
     * Handles BookTrade entries for expired/assigned options where the opening
     * trade is not in the database.
     * @param trade The closing trade
     * @return Optional round-trip, or nullopt if not a closable single trade
     */
    std::optional<MatchedRoundTrip> match_single_closing_trade(const TradeForMatch& trade);

    /**
     * Detect close reason based on close date vs expiry.
     * @param close_date Date the position was closed
     * @param expiry Expiration date of the option
     * @return Close reason string ("closed", "expired")
     */
    std::string detect_close_reason(const std::string& close_date, const std::string& expiry);

    /**
     * Calculate P&L for a matched round trip.
     * @param rt Round trip to calculate P&L for
     * @param is_short True if short position (sell to open)
     * @param multiplier Option contract multiplier (default 100)
     */
    void calculate_pnl(MatchedRoundTrip& rt, bool is_short, double multiplier = 100.0);

    /**
     * Write matched round trips to database.
     * @param round_trips Vector of round trips to write
     * @param account_id Account ID for the round trips
     * @return Result containing count of written round trips or error
     */
    utils::Result<int> write_round_trips(const std::vector<MatchedRoundTrip>& round_trips, int64_t account_id);

    /**
     * Match round trips from snapshot differences (fallback for missing trades).
     * @param account_id Account ID to match
     * @return Result containing vector of inferred round trips or error
     */
    utils::Result<std::vector<MatchedRoundTrip>> match_from_snapshots(int64_t account_id);

    /**
     * Calculate holding days between two dates.
     * @param open_date Open date string (YYYY-MM-DD)
     * @param close_date Close date string (YYYY-MM-DD)
     * @return Number of days between dates
     */
    int calculate_holding_days(const std::string& open_date, const std::string& close_date);

    /**
     * Build contract key for grouping.
     * @param underlying Underlying symbol
     * @param strike Strike price
     * @param right Option right
     * @param expiry Expiration date
     * @return Contract key string
     */
    static std::string build_contract_key(
        const std::string& underlying,
        double strike,
        char right,
        const std::string& expiry);

    db::Database& database_;
    SnapshotService& snapshot_service_;
};

} // namespace ibkr::services