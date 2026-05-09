#pragma once

#include "schema.hpp"
#include "utils/result.hpp"
#include "parsers/csv_parser.hpp"
#include "analysis/strategy_detector.hpp"
#include <string>
#include <memory>
#include <optional>
#include <SQLiteCpp/SQLiteCpp.h>

namespace ibkr::db {

/**
 * Database manager for IBKR options analyzer.
 *
 * Provides CRUD operations for:
 * - Accounts
 * - Trades
 * - Open options
 * - Detected strategies
 *
 * Usage:
 *   Database db("~/.ibkr-options-analyzer/data.db");
 *   auto init_result = db.initialize();
 *   if (init_result) {
 *       // Database ready
 *   }
 */
class Database {
public:
    /**
     * Constructor
     * @param db_path Path to SQLite database file
     */
    explicit Database(const std::string& db_path);

    /**
     * Destructor
     */
    ~Database();

    /**
     * Initialize database (create tables if not exist).
     * @return Result indicating success or error
     */
    [[nodiscard]] utils::Result<void> initialize();

    /**
     * Import trades from parsed CSV.
     * @param account_name Account name
     * @param trades Vector of trade records
     * @return Result indicating success or error
     */
    [[nodiscard]] utils::Result<void> import_trades(
        const std::string& account_name,
        const std::vector<parser::TradeRecord>& trades);

    /**
     * Import open positions from parsed CSV.
     * @param account_name Account name
     * @param positions Vector of open position records
     * @return Result indicating success or error
     */
    [[nodiscard]] utils::Result<void> import_open_positions(
        const std::string& account_name,
        const std::vector<parser::OpenPositionRecord>& positions);

    /**
     * Get or create account ID by name.
     * @param account_name Account name
     * @return Result containing account ID or Error
     */
    [[nodiscard]] utils::Result<int64_t> get_or_create_account(const std::string& account_name);

    /**
     * Clear all open positions for an account.
     * @param account_name Account name
     * @return Result indicating success or error
     */
    [[nodiscard]] utils::Result<void> clear_open_positions(const std::string& account_name);

    /**
     * Get count of open positions.
     * @return Result containing count or Error
     */
    [[nodiscard]] utils::Result<int> get_open_positions_count();

    /**
     * Get count of trades.
     * @return Result containing count or Error
     */
    [[nodiscard]] utils::Result<int> get_trades_count();

    // --- Account CRUD ---

    struct AccountInfo {
        int64_t id{0};
        std::string name;
        std::string token;
        std::string query_id;
        bool enabled{true};
        std::string created_at;
        std::string updated_at;
    };

    [[nodiscard]] utils::Result<std::vector<AccountInfo>> list_accounts();
    [[nodiscard]] utils::Result<AccountInfo> get_account(int64_t account_id);
    [[nodiscard]] utils::Result<void> update_account(int64_t account_id, const std::string& token,
                                       const std::string& query_id, bool enabled);
    [[nodiscard]] utils::Result<void> delete_account(int64_t account_id);

    // --- Consolidated Queries ---

    [[nodiscard]] utils::Result<std::vector<analysis::Position>> get_all_positions(
        const std::string& account_name = "",
        int64_t account_id = 0);

    struct RiskSummary {
        std::string account_name;
        double total_max_loss{0.0};
        double total_max_profit{0.0};
        int strategy_count{0};
    };

    [[nodiscard]] utils::Result<std::vector<RiskSummary>> get_consolidated_risk();

    struct ExposureInfo {
        std::string underlying;
        double total_max_loss{0.0};
        int position_count{0};
    };

    [[nodiscard]] utils::Result<std::vector<ExposureInfo>> get_underlying_exposure();

    // --- Round Trip Structures ---

    struct RoundTrip {
        int64_t id{0};
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
        std::string strategy_type;
        std::optional<int64_t> strategy_group_id;
    };

    struct RoundTripLeg {
        int64_t id{0};
        int64_t round_trip_id{0};
        int64_t trade_id{0};
        std::string role;
        int matched_quantity{0};
    };

    struct StrategyRoundTrip {
        int64_t id{0};
        int64_t account_id{0};
        std::string strategy_type;
        std::string underlying;
        std::string expiry;
        std::string open_date;
        std::string close_date;
        double net_premium{0.0};
        double realized_pnl{0.0};
        int leg_count{0};
    };

    struct PositionSnapshot {
        int64_t id{0};
        int64_t account_id{0};
        std::string snapshot_date;
        std::string symbol;
        std::string underlying;
        std::string expiry;
        double strike{0.0};
        char right{'P'};
        int quantity{0};
        double mark_price{0.0};
        double entry_price{0.0};
    };

    // --- Round Trip CRUD ---
    [[nodiscard]] utils::Result<int64_t> insert_round_trip(const RoundTrip& round_trip);
    [[nodiscard]] utils::Result<void> insert_round_trip_leg(const RoundTripLeg& leg);
    [[nodiscard]] utils::Result<std::vector<RoundTrip>> get_round_trips(
        int64_t account_id = 0,
        const std::string& date_from = "",
        const std::string& date_to = "",
        const std::string& strategy_type = "",
        const std::string& underlying = "");
    [[nodiscard]] utils::Result<int> get_round_trips_count(int64_t account_id = 0);
    [[nodiscard]] utils::Result<void> clear_round_trips(int64_t account_id = 0);

    // --- Strategy Round Trip CRUD ---
    [[nodiscard]] utils::Result<int64_t> insert_strategy_round_trip(const StrategyRoundTrip& srt);
    [[nodiscard]] utils::Result<void> link_round_trip_to_strategy(int64_t round_trip_id, int64_t strategy_group_id);

    // --- Position Snapshot CRUD ---
    [[nodiscard]] utils::Result<void> insert_position_snapshot(const PositionSnapshot& snapshot);
    [[nodiscard]] utils::Result<std::vector<PositionSnapshot>> get_position_snapshots(
        int64_t account_id,
        const std::string& snapshot_date);
    [[nodiscard]] utils::Result<std::vector<PositionSnapshot>> diff_snapshots(
        int64_t account_id,
        const std::string& date1,
        const std::string& date2);

    /**
     * Check if database is initialized.
     */
    bool is_initialized() const { return initialized_; }

    /**
     * Get raw database pointer (for manual queries).
     * @return Pointer to SQLite::Database or nullptr if not initialized
     */
    SQLite::Database* get_db() { return db_.get(); }

private:
    std::string db_path_;
    std::unique_ptr<SQLite::Database> db_;
    bool initialized_{false};

    /**
     * Expand tilde in path.
     */
    std::string expand_path(const std::string& path);

    /**
     * Execute schema statements.
     */
    utils::Result<void> execute_schema();

    /**
     * Check if schema is up to date.
     */
    bool check_schema_version();
};

} // namespace ibkr::db
