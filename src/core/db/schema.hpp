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
 *
 * Note on monetary values:
 * REAL is used for strike prices, premiums, and P&L values.
 * This is acceptable for this use case as option premiums and P&L
 * calculations do not require sub-cent precision. If future requirements
 * demand exact monetary accuracy (e.g., tax reporting), a migration to
 * INTEGER storing cents would be needed.
 */

constexpr const char* SCHEMA_VERSION = "1.2.0";

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
    multiplier REAL DEFAULT 100,
    fifo_pnl_realized REAL DEFAULT 0,
    close_price REAL DEFAULT 0,
    notes_codes TEXT DEFAULT '',
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
    multiplier REAL NOT NULL DEFAULT 100.0,
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

INSERT OR IGNORE INTO metadata (key, value) VALUES ('schema_version', '1.2.0');
)";

// Cache table: cached stock prices
constexpr const char* CREATE_CACHED_PRICES_TABLE = R"(
CREATE TABLE IF NOT EXISTS cached_prices (
    symbol TEXT PRIMARY KEY,
    price REAL NOT NULL,
    source TEXT NOT NULL,
    fetched_at TEXT NOT NULL,
    trading_date TEXT NOT NULL
);
)";

// Cache table: cached volatility data
constexpr const char* CREATE_CACHED_VOLATILITY_TABLE = R"(
CREATE TABLE IF NOT EXISTS cached_volatility (
    symbol TEXT PRIMARY KEY,
    volatility REAL NOT NULL,
    source TEXT NOT NULL,
    fetched_at TEXT NOT NULL,
    trading_date TEXT NOT NULL
);
)";

// Cache table: cached option chains
constexpr const char* CREATE_CACHED_OPTION_CHAINS_TABLE = R"(
CREATE TABLE IF NOT EXISTS cached_option_chains (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    symbol TEXT NOT NULL,
    expiry TEXT NOT NULL,
    chain_json TEXT NOT NULL,
    source TEXT NOT NULL,
    fetched_at TEXT NOT NULL,
    trading_date TEXT NOT NULL,
    UNIQUE(symbol, expiry)
);

CREATE INDEX IF NOT EXISTS idx_option_chains_symbol ON cached_option_chains(symbol);
CREATE INDEX IF NOT EXISTS idx_option_chains_trading_date ON cached_option_chains(trading_date);
)";

// RoundTrips table: completed open→close cycles
constexpr const char* CREATE_ROUND_TRIPS_TABLE = R"(
CREATE TABLE IF NOT EXISTS round_trips (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    underlying TEXT NOT NULL,
    strike REAL NOT NULL,
    right TEXT NOT NULL CHECK(right IN ('C', 'P')),
    expiry TEXT NOT NULL,
    quantity INTEGER NOT NULL,
    open_date TEXT NOT NULL,
    close_date TEXT NOT NULL,
    holding_days INTEGER NOT NULL,
    open_price REAL NOT NULL,
    close_price REAL NOT NULL,
    net_premium REAL NOT NULL,
    commission REAL NOT NULL DEFAULT 0.0,
    realized_pnl REAL NOT NULL,
    close_reason TEXT NOT NULL CHECK(close_reason IN ('closed', 'expired', 'assigned', 'rolled', 'exercised')),
    match_method TEXT NOT NULL CHECK(match_method IN ('trade_match', 'snapshot', 'manual', 'single_trade')),
    strategy_type TEXT,
    strategy_group_id INTEGER,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
    FOREIGN KEY (strategy_group_id) REFERENCES strategy_round_trips(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_round_trips_account ON round_trips(account_id);
CREATE INDEX IF NOT EXISTS idx_round_trips_underlying ON round_trips(underlying);
CREATE INDEX IF NOT EXISTS idx_round_trips_close_date ON round_trips(close_date);
CREATE INDEX IF NOT EXISTS idx_round_trips_strategy_type ON round_trips(strategy_type);
)";

// RoundTripLegs table: links round_trips back to source trades
constexpr const char* CREATE_ROUND_TRIP_LEGS_TABLE = R"(
CREATE TABLE IF NOT EXISTS round_trip_legs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    round_trip_id INTEGER NOT NULL,
    trade_id INTEGER NOT NULL,
    role TEXT NOT NULL CHECK(role IN ('open', 'close', 'adjust', 'partial')),
    matched_quantity INTEGER NOT NULL,
    FOREIGN KEY (round_trip_id) REFERENCES round_trips(id) ON DELETE CASCADE,
    FOREIGN KEY (trade_id) REFERENCES trades(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_round_trip_legs_round_trip ON round_trip_legs(round_trip_id);
CREATE INDEX IF NOT EXISTS idx_round_trip_legs_trade ON round_trip_legs(trade_id);
)";

// StrategyRoundTrips table: groups multi-leg round-trips
constexpr const char* CREATE_STRATEGY_ROUND_TRIPS_TABLE = R"(
CREATE TABLE IF NOT EXISTS strategy_round_trips (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    strategy_type TEXT NOT NULL,
    underlying TEXT NOT NULL,
    expiry TEXT NOT NULL,
    open_date TEXT NOT NULL,
    close_date TEXT NOT NULL,
    net_premium REAL NOT NULL,
    realized_pnl REAL NOT NULL,
    leg_count INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_strategy_round_trips_account ON strategy_round_trips(account_id);
)";

// PositionSnapshots table: daily position state for snapshot matching
constexpr const char* CREATE_POSITION_SNAPSHOTS_TABLE = R"(
CREATE TABLE IF NOT EXISTS position_snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    snapshot_date TEXT NOT NULL,
    symbol TEXT NOT NULL,
    underlying TEXT NOT NULL,
    expiry TEXT NOT NULL,
    strike REAL NOT NULL,
    right TEXT NOT NULL CHECK(right IN ('C', 'P')),
    quantity INTEGER NOT NULL,
    mark_price REAL,
    entry_price REAL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
    UNIQUE(account_id, snapshot_date, symbol)
);

CREATE INDEX IF NOT EXISTS idx_position_snapshots_account_date ON position_snapshots(account_id, snapshot_date);
CREATE INDEX IF NOT EXISTS idx_position_snapshots_date ON position_snapshots(snapshot_date);
)";

// All schema statements in order
constexpr const char* ALL_SCHEMA_STATEMENTS[] = {
    CREATE_ACCOUNTS_TABLE,
    CREATE_TRADES_TABLE,
    CREATE_OPEN_OPTIONS_TABLE,
    CREATE_STRATEGIES_TABLE,
    CREATE_STRATEGY_LEGS_TABLE,
    CREATE_METADATA_TABLE,
    CREATE_CACHED_PRICES_TABLE,
    CREATE_CACHED_VOLATILITY_TABLE,
    CREATE_CACHED_OPTION_CHAINS_TABLE,
    CREATE_ROUND_TRIPS_TABLE,
    CREATE_ROUND_TRIP_LEGS_TABLE,
    CREATE_STRATEGY_ROUND_TRIPS_TABLE,
    CREATE_POSITION_SNAPSHOTS_TABLE
};

constexpr size_t SCHEMA_STATEMENT_COUNT = sizeof(ALL_SCHEMA_STATEMENTS) / sizeof(ALL_SCHEMA_STATEMENTS[0]);

} // namespace ibkr::db
