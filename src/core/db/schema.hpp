#pragma once

#include <string>

namespace ibkr::db {

/**
 * SQLite database schema for IBKR options analyzer.
 *
 * Design principles:
 * - Normalized structure (accounts, trades, open_options, strategies)
 * - Indexes on frequently queried columns (account_id, underlying, expiry)
 * - Foreign key constraints for referential integrity
 * - Timestamps for audit trail
 */

constexpr const char* SCHEMA_VERSION = "1.0.0";

// Accounts table: stores IBKR account information
constexpr const char* CREATE_ACCOUNTS_TABLE = R"(
CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    token TEXT NOT NULL,
    query_id TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)";

// Trades table: full trade history from Flex reports
constexpr const char* CREATE_TRADES_TABLE = R"(
CREATE TABLE IF NOT EXISTS trades (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    trade_date TEXT NOT NULL,
    symbol TEXT NOT NULL,
    underlying TEXT,
    expiry TEXT,
    strike REAL,
    right TEXT,
    quantity REAL NOT NULL,
    trade_price REAL,
    proceeds REAL,
    commission REAL,
    net_cash REAL,
    imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_trades_account ON trades(account_id);
CREATE INDEX IF NOT EXISTS idx_trades_symbol ON trades(symbol);
CREATE INDEX IF NOT EXISTS idx_trades_underlying ON trades(underlying);
CREATE INDEX IF NOT EXISTS idx_trades_date ON trades(trade_date);
)";

// OpenOptions table: current non-expired option positions
constexpr const char* CREATE_OPEN_OPTIONS_TABLE = R"(
CREATE TABLE IF NOT EXISTS open_options (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    symbol TEXT NOT NULL,
    underlying TEXT NOT NULL,
    expiry TEXT NOT NULL,
    strike REAL NOT NULL,
    right TEXT NOT NULL CHECK(right IN ('C', 'P')),
    quantity REAL NOT NULL,
    mark_price REAL,
    entry_premium REAL,
    current_value REAL,
    is_manual INTEGER NOT NULL DEFAULT 0,
    notes TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_open_options_account ON open_options(account_id);
CREATE INDEX IF NOT EXISTS idx_open_options_underlying ON open_options(underlying);
CREATE INDEX IF NOT EXISTS idx_open_options_expiry ON open_options(expiry);
CREATE INDEX IF NOT EXISTS idx_open_options_symbol ON open_options(symbol);
)";

// DetectedStrategies table: auto-detected option strategies
constexpr const char* CREATE_STRATEGIES_TABLE = R"(
CREATE TABLE IF NOT EXISTS detected_strategies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    strategy_type TEXT NOT NULL,
    underlying TEXT NOT NULL,
    expiry TEXT NOT NULL,
    leg_count INTEGER NOT NULL,
    net_premium REAL,
    max_profit REAL,
    max_loss REAL,
    breakeven_price REAL,
    confidence REAL,
    detected_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_strategies_account ON detected_strategies(account_id);
CREATE INDEX IF NOT EXISTS idx_strategies_underlying ON detected_strategies(underlying);
CREATE INDEX IF NOT EXISTS idx_strategies_type ON detected_strategies(strategy_type);
)";

// StrategyLegs table: links options to detected strategies
constexpr const char* CREATE_STRATEGY_LEGS_TABLE = R"(
CREATE TABLE IF NOT EXISTS strategy_legs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    strategy_id INTEGER NOT NULL,
    option_id INTEGER NOT NULL,
    leg_role TEXT,
    FOREIGN KEY (strategy_id) REFERENCES detected_strategies(id) ON DELETE CASCADE,
    FOREIGN KEY (option_id) REFERENCES open_options(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_strategy_legs_strategy ON strategy_legs(strategy_id);
CREATE INDEX IF NOT EXISTS idx_strategy_legs_option ON strategy_legs(option_id);
)";

// Metadata table: schema version and migration tracking
constexpr const char* CREATE_METADATA_TABLE = R"(
CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT OR IGNORE INTO metadata (key, value) VALUES ('schema_version', '1.0.0');
)";

// All schema statements in order
constexpr const char* ALL_SCHEMA_STATEMENTS[] = {
    CREATE_ACCOUNTS_TABLE,
    CREATE_TRADES_TABLE,
    CREATE_OPEN_OPTIONS_TABLE,
    CREATE_STRATEGIES_TABLE,
    CREATE_STRATEGY_LEGS_TABLE,
    CREATE_METADATA_TABLE
};

constexpr size_t SCHEMA_STATEMENT_COUNT = sizeof(ALL_SCHEMA_STATEMENTS) / sizeof(ALL_SCHEMA_STATEMENTS[0]);

} // namespace ibkr::db
