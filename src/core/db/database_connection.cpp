#include "database_connection.hpp"
#include "schema.hpp"
#include "utils/logger.hpp"
#include "utils/currency.hpp"
#include "config/config_manager.hpp"
#include <filesystem>
#include <sstream>
#include <tuple>
#include <SQLiteCpp/SQLiteCpp.h>

namespace ibkr::db {

using utils::Result;
using utils::Error;
using utils::Logger;

DatabaseConnection::DatabaseConnection(const std::string& db_path)
    : db_path_(expand_path(db_path)) {
}

DatabaseConnection::~DatabaseConnection() = default;

Result<void> DatabaseConnection::initialize() {
    if (initialized_) {
        return Result<void>{};
    }

    Logger::info("Initializing database: {}", db_path_);

    try {
        // Create directory if it doesn't exist
        std::filesystem::path path(db_path_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        // Open database (creates if doesn't exist)
        db_ = std::make_unique<SQLite::Database>(
            db_path_,
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE
        );

        Logger::debug("Database opened: {}", db_path_);

        // Enable foreign key enforcement (SQLite default is off)
        db_->exec("PRAGMA foreign_keys = ON");

        // Execute schema
        auto schema_result = execute_schema();
        if (!schema_result) {
            return schema_result;
        }

        // Run migrations for existing databases
        try {
            SQLite::Statement check(*db_, "SELECT multiplier FROM open_options LIMIT 1");
            try { check.executeStep(); } catch (const SQLite::Exception&) {
                // Table empty — column exists, no migration needed
            }
        } catch (const SQLite::Exception&) {
            // Column doesn't exist — Statement constructor threw
            Logger::info("Running migration: adding multiplier column to open_options");
            try {
                db_->exec("ALTER TABLE open_options ADD COLUMN multiplier REAL NOT NULL DEFAULT 100.0");
            } catch (const std::exception& e) {
                return Error{"Failed to add multiplier column", std::string(e.what())};
            }
        }

        // Migration: add multiplier and fifo_pnl_realized to trades
        try {
            SQLite::Statement check(*db_, "SELECT multiplier FROM trades LIMIT 1");
            try { check.executeStep(); } catch (const SQLite::Exception&) {}
        } catch (const SQLite::Exception&) {
            Logger::info("Running migration: adding multiplier/fifo_pnl_realized to trades");
            try {
                db_->exec("ALTER TABLE trades ADD COLUMN multiplier REAL NOT NULL DEFAULT 100.0");
                db_->exec("ALTER TABLE trades ADD COLUMN fifo_pnl_realized REAL NOT NULL DEFAULT 0.0");
            } catch (const std::exception& e) {
                return Error{"Failed to add trade columns", std::string(e.what())};
            }
        }

        // Migration: add close_price and notes_codes to trades
        try {
            SQLite::Statement check(*db_, "SELECT close_price FROM trades LIMIT 1");
            try { check.executeStep(); } catch (const SQLite::Exception&) {}
        } catch (const SQLite::Exception&) {
            Logger::info("Running migration: adding close_price/notes_codes to trades");
            try {
                db_->exec("ALTER TABLE trades ADD COLUMN close_price REAL NOT NULL DEFAULT 0.0");
                db_->exec("ALTER TABLE trades ADD COLUMN notes_codes TEXT NOT NULL DEFAULT ''");
            } catch (const std::exception& e) {
                return Error{"Failed to add close_price/notes_codes", std::string(e.what())};
            }
        }

        // Migration 3.0.0: add columns to detected_strategies
        try {
            SQLite::Statement check(*db_, "SELECT risk_level FROM detected_strategies LIMIT 1");
            try { check.executeStep(); } catch (const SQLite::Exception&) {}
        } catch (const SQLite::Exception&) {
            Logger::info("Running migration: adding columns to detected_strategies for schema 3.0.0");
            try {
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN risk_level TEXT");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN net_premium_usd REAL");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN max_profit_usd REAL");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN max_loss_usd REAL");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN breakeven_price_usd REAL");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN account_name TEXT");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN currency TEXT DEFAULT 'USD'");
                db_->exec("ALTER TABLE detected_strategies ADD COLUMN analysis_timestamp TEXT");
                // Update schema version
                db_->exec("UPDATE metadata SET value = '3.0.0', updated_at = CURRENT_TIMESTAMP WHERE key = 'schema_version'");
            } catch (const std::exception& e) {
                return Error{"Failed to add detected_strategies columns", std::string(e.what())};
            }
        }

        // Seed known stock adjustments (splits and renames)
        seed_stock_adjustments();

        // Migration 3.1.0: add ibkr_account_id to accounts
        try {
            SQLite::Statement check(*db_, "SELECT ibkr_account_id FROM accounts LIMIT 1");
            try { check.executeStep(); } catch (const SQLite::Exception&) {}
        } catch (const SQLite::Exception&) {
            Logger::info("Running migration: adding ibkr_account_id to accounts (schema 3.1.0)");
            try {
                db_->exec("ALTER TABLE accounts ADD COLUMN ibkr_account_id TEXT DEFAULT ''");
                db_->exec("UPDATE metadata SET value = '3.1.0', updated_at = CURRENT_TIMESTAMP WHERE key = 'schema_version'");
            } catch (const std::exception& e) {
                return Error{"Failed to add ibkr_account_id column", std::string(e.what())};
            }
        }

        // Migration 3.2.0: convert existing native-currency data to USD.
        // New imports now convert at write time (consistent with dividends/interest).
        // This one-time migration brings existing rows up to the same standard.
        {
            std::string current_version;
            try {
                SQLite::Statement q(*db_, "SELECT value FROM metadata WHERE key = 'schema_version'");
                if (q.executeStep()) current_version = q.getColumn(0).getString();
            } catch (const std::exception&) {}

            auto parse_version = [](const std::string& v) -> std::tuple<int,int,int> {
                int major = 0, minor = 0, patch = 0;
                char dot1, dot2;
                std::istringstream ss(v);
                ss >> major >> dot1 >> minor >> dot2 >> patch;
                return {major, minor, patch};
            };

            auto [cur_major, cur_minor, cur_patch] = parse_version(current_version);
            auto [tgt_major, tgt_minor, tgt_patch] = parse_version("3.2.0");
            bool needs_migration = std::tie(cur_major, cur_minor, cur_patch)
                                 < std::tie(tgt_major, tgt_minor, tgt_patch);

            if (needs_migration) {
                Logger::info("Running migration: converting native-currency data to USD (schema 3.2.0)");
                utils::CurrencyConverter converter;
                int migrated = 0;
                try {
                    db_->exec("BEGIN TRANSACTION");
                    // --- trades ---
                    {
                        SQLite::Statement sel(*db_,
                            "SELECT id, underlying, trade_price, proceeds, commission, "
                            "net_cash, strike, fifo_pnl_realized, close_price FROM trades");
                        SQLite::Statement upd(*db_,
                            "UPDATE trades SET trade_price=?, proceeds=?, commission=?, "
                            "net_cash=?, strike=?, fifo_pnl_realized=?, close_price=? WHERE id=?");
                        while (sel.executeStep()) {
                            std::string ul = sel.getColumn(1).getString();
                            std::string ccy = utils::deduce_currency(ul);
                            if (ccy == "USD") continue;
                            int64_t id = sel.getColumn(0).getInt64();
                            upd.reset();
                            upd.bind(1, converter.convert(sel.getColumn(2).getDouble(), ccy));
                            upd.bind(2, converter.convert(sel.getColumn(3).getDouble(), ccy));
                            upd.bind(3, converter.convert(sel.getColumn(4).getDouble(), ccy));
                            upd.bind(4, converter.convert(sel.getColumn(5).getDouble(), ccy));
                            upd.bind(5, converter.convert(sel.getColumn(6).getDouble(), ccy));
                            upd.bind(6, converter.convert(sel.getColumn(7).getDouble(), ccy));
                            upd.bind(7, converter.convert(sel.getColumn(8).getDouble(), ccy));
                            upd.bind(8, id);
                            upd.exec();
                            ++migrated;
                        }
                    }
                    // --- stock_trades ---
                    {
                        SQLite::Statement sel(*db_,
                            "SELECT id, symbol, trade_price, proceeds, commission, net_cash FROM stock_trades");
                        SQLite::Statement upd(*db_,
                            "UPDATE stock_trades SET trade_price=?, proceeds=?, commission=?, net_cash=? WHERE id=?");
                        while (sel.executeStep()) {
                            std::string sym = sel.getColumn(1).getString();
                            std::string ccy = utils::deduce_currency(sym);
                            if (ccy == "USD") continue;
                            int64_t id = sel.getColumn(0).getInt64();
                            upd.reset();
                            upd.bind(1, converter.convert(sel.getColumn(2).getDouble(), ccy));
                            upd.bind(2, converter.convert(sel.getColumn(3).getDouble(), ccy));
                            upd.bind(3, converter.convert(sel.getColumn(4).getDouble(), ccy));
                            upd.bind(4, converter.convert(sel.getColumn(5).getDouble(), ccy));
                            upd.bind(5, id);
                            upd.exec();
                            ++migrated;
                        }
                    }
                    // --- open_options ---
                    {
                        SQLite::Statement sel(*db_,
                            "SELECT id, underlying, strike, mark_price, entry_premium, current_value FROM open_options");
                        SQLite::Statement upd(*db_,
                            "UPDATE open_options SET strike=?, mark_price=?, entry_premium=?, current_value=? WHERE id=?");
                        while (sel.executeStep()) {
                            std::string ul = sel.getColumn(1).getString();
                            std::string ccy = utils::deduce_currency(ul);
                            if (ccy == "USD") continue;
                            int64_t id = sel.getColumn(0).getInt64();
                            upd.reset();
                            upd.bind(1, converter.convert(sel.getColumn(2).getDouble(), ccy));
                            upd.bind(2, converter.convert(sel.getColumn(3).getDouble(), ccy));
                            upd.bind(3, converter.convert(sel.getColumn(4).getDouble(), ccy));
                            upd.bind(4, converter.convert(sel.getColumn(5).getDouble(), ccy));
                            upd.bind(5, id);
                            upd.exec();
                            ++migrated;
                        }
                    }
                    // --- cached_prices ---
                    {
                        SQLite::Statement sel(*db_, "SELECT symbol, price FROM cached_prices");
                        SQLite::Statement upd(*db_, "UPDATE cached_prices SET price=? WHERE symbol=?");
                        while (sel.executeStep()) {
                            std::string sym = sel.getColumn(0).getString();
                            std::string ccy = utils::deduce_currency(sym);
                            if (ccy == "USD") continue;
                            double price = sel.getColumn(1).getDouble();
                            upd.reset();
                            upd.bind(1, converter.convert(price, ccy));
                            upd.bind(2, sym);
                            upd.exec();
                            ++migrated;
                        }
                    }
                    db_->exec("UPDATE metadata SET value = '3.2.0', updated_at = CURRENT_TIMESTAMP WHERE key = 'schema_version'");
                    db_->exec("COMMIT");
                    Logger::info("Currency migration complete: {} rows converted to USD", migrated);
                } catch (const std::exception& e) {
                    db_->exec("ROLLBACK");
                    Logger::warn("Currency migration failed (rolled back): {}", e.what());
                    // Non-fatal: new imports will still write USD, stale rows will
                    // be overwritten on next import.
                }
            }
        }

        initialized_ = true;
        Logger::info("Database initialized successfully");

        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to initialize database", std::string(e.what())};
    }
}

Result<void> DatabaseConnection::execute_schema() {
    try {
        for (size_t i = 0; i < SCHEMA_STATEMENT_COUNT; ++i) {
            Logger::debug("Executing schema statement {}/{}", i + 1, SCHEMA_STATEMENT_COUNT);
            db_->exec(ALL_SCHEMA_STATEMENTS[i]);
        }

        Logger::info("Schema created/verified successfully");
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to execute schema", std::string(e.what())};
    }
}

void DatabaseConnection::seed_stock_adjustments() {
    try {
        // Ensure uniqueness to prevent duplicate accumulation
        try {
            db_->exec(
                "CREATE UNIQUE INDEX IF NOT EXISTS idx_stock_adj_uniq "
                "ON stock_adjustments(symbol, effective_date)");
        } catch (const SQLite::Exception&) {
            // Index may already exist or table state varies — non-fatal
        }

        SQLite::Statement insert(*db_,
            "INSERT OR REPLACE INTO stock_adjustments "
            "(symbol, effective_date, split_ratio, new_symbol) VALUES (?, ?, ?, ?)");

        // Known splits and renames.
        struct Seed { const char* symbol; const char* date; double ratio; const char* new_sym; };
        static constexpr Seed seeds[] = {
            {"NVDA",  "2021-07-20", 4.0,  ""},
            {"NVDA",  "2024-06-10", 10.0, ""},
            {"TSLA",  "2020-08-31", 5.0,  ""},
            {"TSLA",  "2022-08-25", 3.0,  ""},
            {"AAPL",  "2020-08-31", 4.0,  ""},
            {"AMZN",  "2022-06-06", 20.0, ""},
            {"GOOG",  "2022-07-15", 20.0, ""},
            {"NFLX",  "2025-10-15", 10.0, ""},
            {"FB",    "2022-06-09", 1.0,  "META"},
            {"SQ",    "2025-01-27", 1.0,  "XYZ"},
            {"BIGC",  "2025-01-28", 1.0,  "CMRC"},
        };

        int seeded = 0;
        for (const auto& s : seeds) {
            insert.reset();
            insert.bind(1, s.symbol);
            insert.bind(2, s.date);
            insert.bind(3, s.ratio);
            insert.bind(4, s.new_sym);
            if (insert.exec() > 0) {
                seeded++;
            }
        }
        if (seeded > 0) {
            Logger::info("Seeded {} stock adjustment(s)", seeded);
        }
    } catch (const std::exception& e) {
        Logger::warn("Failed to seed stock adjustments: {}", e.what());
    }
}

std::string DatabaseConnection::expand_path(const std::string& path) {
    return config::ConfigManager::expand_path(path);
}

} // namespace ibkr::db
