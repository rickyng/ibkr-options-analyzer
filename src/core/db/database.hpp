#pragma once

#include "schema.hpp"
#include "utils/result.hpp"
#include "parsers/csv_parser.hpp"
#include <string>
#include <memory>
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
    utils::Result<void> initialize();

    /**
     * Import trades from parsed CSV.
     * @param account_name Account name
     * @param trades Vector of trade records
     * @return Result indicating success or error
     */
    utils::Result<void> import_trades(
        const std::string& account_name,
        const std::vector<parser::TradeRecord>& trades);

    /**
     * Import open positions from parsed CSV.
     * @param account_name Account name
     * @param positions Vector of open position records
     * @return Result indicating success or error
     */
    utils::Result<void> import_open_positions(
        const std::string& account_name,
        const std::vector<parser::OpenPositionRecord>& positions);

    /**
     * Get or create account ID by name.
     * @param account_name Account name
     * @return Result containing account ID or Error
     */
    utils::Result<int64_t> get_or_create_account(const std::string& account_name);

    /**
     * Clear all open positions for an account.
     * @param account_name Account name
     * @return Result indicating success or error
     */
    utils::Result<void> clear_open_positions(const std::string& account_name);

    /**
     * Get count of open positions.
     * @return Result containing count or Error
     */
    utils::Result<int> get_open_positions_count();

    /**
     * Get count of trades.
     * @return Result containing count or Error
     */
    utils::Result<int> get_trades_count();

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
