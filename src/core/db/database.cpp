#include "database.hpp"
#include "utils/logger.hpp"
#include "config/config_manager.hpp"
#include <filesystem>

namespace ibkr::db {

using utils::Result;
using utils::Error;
using utils::Logger;

Database::Database(const std::string& db_path)
    : db_path_(expand_path(db_path)) {
}

Database::~Database() = default;

Result<void> Database::initialize() {
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
            Logger::info("Running migration: adding multiplier column");
            try {
                db_->exec("ALTER TABLE open_options ADD COLUMN multiplier REAL NOT NULL DEFAULT 100.0");
            } catch (const std::exception& e) {
                return Error{"Failed to add multiplier column", std::string(e.what())};
            }
        }

        initialized_ = true;
        Logger::info("Database initialized successfully");

        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to initialize database",
            std::string(e.what())
        };
    }
}

Result<void> Database::execute_schema() {
    try {
        // Execute all schema statements
        for (size_t i = 0; i < SCHEMA_STATEMENT_COUNT; ++i) {
            Logger::debug("Executing schema statement {}/{}", i + 1, SCHEMA_STATEMENT_COUNT);
            db_->exec(ALL_SCHEMA_STATEMENTS[i]);
        }

        Logger::info("Schema created/verified successfully");
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to execute schema",
            std::string(e.what())
        };
    }
}

Result<int64_t> Database::get_or_create_account(const std::string& account_name) {
    if (!initialized_) {
        return Error{"Database not initialized"};
    }

    try {
        // Check if account exists
        SQLite::Statement query(*db_, "SELECT id FROM accounts WHERE name = ?");
        query.bind(1, account_name);

        if (query.executeStep()) {
            int64_t account_id = query.getColumn(0).getInt64();
            Logger::debug("Found existing account: {} (id={})", account_name, account_id);
            return account_id;
        }

        // Create new account
        SQLite::Statement insert(*db_,
            "INSERT INTO accounts (name, token, query_id, enabled) VALUES (?, '', '', 1)");
        insert.bind(1, account_name);
        insert.exec();

        int64_t account_id = db_->getLastInsertRowid();
        Logger::info("Created new account: {} (id={})", account_name, account_id);

        return account_id;

    } catch (const std::exception& e) {
        return Error{
            "Failed to get/create account",
            std::string(e.what())
        };
    }
}

Result<void> Database::import_trades(
    const std::string& account_name,
    const std::vector<parser::TradeRecord>& trades) {

    if (!initialized_) {
        return Error{"Database not initialized"};
    }

    if (trades.empty()) {
        Logger::info("No trades to import");
        return Result<void>{};
    }

    Logger::info("Importing {} trades for account: {}", trades.size(), account_name);

    try {
        // Get account ID
        auto account_result = get_or_create_account(account_name);
        if (!account_result) {
            return Error{
                "Failed to get account",
                account_result.error().message
            };
        }
        int64_t account_id = *account_result;

        // Begin transaction
        SQLite::Transaction transaction(*db_);

        SQLite::Statement insert(*db_,
            "INSERT INTO trades (account_id, trade_date, symbol, underlying, expiry, "
            "strike, right, quantity, trade_price, proceeds, commission, net_cash) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        int imported = 0;
        for (const auto& trade : trades) {
            // Skip non-option trades (no option details)
            if (!trade.option_details) {
                Logger::debug("Skipping non-option trade: {}", trade.symbol);
                continue;
            }

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, trade.trade_date);
            insert.bind(3, trade.symbol);
            insert.bind(4, trade.option_details->underlying);
            insert.bind(5, trade.option_details->expiry);
            insert.bind(6, trade.option_details->strike);
            insert.bind(7, std::string(1, trade.option_details->right));
            insert.bind(8, trade.quantity);
            insert.bind(9, trade.trade_price);
            insert.bind(10, trade.proceeds);
            insert.bind(11, trade.commission);
            insert.bind(12, trade.net_cash);

            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} trades successfully", imported);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to import trades",
            std::string(e.what())
        };
    }
}

Result<void> Database::import_open_positions(
    const std::string& account_name,
    const std::vector<parser::OpenPositionRecord>& positions) {

    if (!initialized_) {
        return Error{"Database not initialized"};
    }

    if (positions.empty()) {
        Logger::info("No open positions to import");
        return Result<void>{};
    }

    Logger::info("Importing {} open positions for account: {}", positions.size(), account_name);

    try {
        // Get account ID
        auto account_result = get_or_create_account(account_name);
        if (!account_result) {
            return Error{
                "Failed to get account",
                account_result.error().message
            };
        }
        int64_t account_id = *account_result;

        // Begin transaction
        SQLite::Transaction transaction(*db_);

        SQLite::Statement insert(*db_,
            "INSERT INTO open_options (account_id, symbol, underlying, expiry, strike, "
            "right, quantity, mark_price, entry_premium, current_value, multiplier, is_manual) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");

        int imported = 0;
        for (const auto& pos : positions) {
            // Skip non-option positions (no option details)
            if (!pos.option_details) {
                Logger::debug("Skipping non-option position: {}", pos.symbol);
                continue;
            }

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, pos.symbol);
            insert.bind(3, pos.option_details->underlying);
            insert.bind(4, pos.option_details->expiry);
            insert.bind(5, pos.option_details->strike);
            insert.bind(6, std::string(1, pos.option_details->right));
            insert.bind(7, pos.quantity);
            insert.bind(8, pos.mark_price);
            insert.bind(9, pos.cost_basis_price);
            insert.bind(10, pos.position_value);
            insert.bind(11, pos.multiplier);

            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} open positions successfully", imported);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to import open positions",
            std::string(e.what())
        };
    }
}

Result<void> Database::clear_open_positions(const std::string& account_name) {
    if (!initialized_) {
        return Error{"Database not initialized"};
    }

    try {
        // Get account ID
        auto account_result = get_or_create_account(account_name);
        if (!account_result) {
            return Error{
                "Failed to get account",
                account_result.error().message
            };
        }
        int64_t account_id = *account_result;

        SQLite::Statement del(*db_, "DELETE FROM open_options WHERE account_id = ?");
        del.bind(1, account_id);
        int deleted = del.exec();

        Logger::info("Cleared {} open positions for account: {}", deleted, account_name);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to clear open positions",
            std::string(e.what())
        };
    }
}

Result<int> Database::get_open_positions_count() {
    if (!initialized_) {
        return Error{"Database not initialized"};
    }

    try {
        SQLite::Statement query(*db_, "SELECT COUNT(*) FROM open_options");
        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        }
        return 0;

    } catch (const std::exception& e) {
        return Error{
            "Failed to get open positions count",
            std::string(e.what())
        };
    }
}

Result<int> Database::get_trades_count() {
    if (!initialized_) {
        return Error{"Database not initialized"};
    }

    try {
        SQLite::Statement query(*db_, "SELECT COUNT(*) FROM trades");
        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        }
        return 0;

    } catch (const std::exception& e) {
        return Error{
            "Failed to get trades count",
            std::string(e.what())
        };
    }
}

std::string Database::expand_path(const std::string& path) {
    return config::ConfigManager::expand_path(path);
}

// --- Account CRUD ---

Result<std::vector<Database::AccountInfo>> Database::list_accounts() {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "SELECT id, name, token, query_id, enabled, created_at, updated_at "
            "FROM accounts ORDER BY name");
        std::vector<AccountInfo> result;
        while (q.executeStep()) {
            AccountInfo a;
            a.id = q.getColumn(0).getInt64();
            a.name = q.getColumn(1).getString();
            a.token = q.getColumn(2).getString();
            a.query_id = q.getColumn(3).getString();
            a.enabled = q.getColumn(4).getInt() != 0;
            a.created_at = q.getColumn(5).getString();
            a.updated_at = q.getColumn(6).getString();
            result.push_back(a);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to list accounts", std::string(e.what())};
    }
}

Result<Database::AccountInfo> Database::get_account(int64_t account_id) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "SELECT id, name, token, query_id, enabled, created_at, updated_at "
            "FROM accounts WHERE id = ?");
        q.bind(1, account_id);
        if (!q.executeStep()) return Error{"Account not found"};

        AccountInfo a;
        a.id = q.getColumn(0).getInt64();
        a.name = q.getColumn(1).getString();
        a.token = q.getColumn(2).getString();
        a.query_id = q.getColumn(3).getString();
        a.enabled = q.getColumn(4).getInt() != 0;
        a.created_at = q.getColumn(5).getString();
        a.updated_at = q.getColumn(6).getString();
        return a;
    } catch (const std::exception& e) {
        return Error{"Failed to get account", std::string(e.what())};
    }
}

Result<void> Database::update_account(int64_t account_id, const std::string& token,
                                       const std::string& query_id, bool enabled) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "UPDATE accounts SET token = ?, query_id = ?, enabled = ?, "
            "updated_at = datetime('now') WHERE id = ?");
        q.bind(1, token);
        q.bind(2, query_id);
        q.bind(3, enabled ? 1 : 0);
        q.bind(4, account_id);
        int rows = q.exec();
        if (rows == 0) return Error{"Account not found"};
        Logger::info("Updated account id={}", account_id);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to update account", std::string(e.what())};
    }
}

Result<void> Database::delete_account(int64_t account_id) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_, "DELETE FROM accounts WHERE id = ?");
        q.bind(1, account_id);
        int rows = q.exec();
        if (rows == 0) return Error{"Account not found"};
        Logger::info("Deleted account id={} (cascading deletes)", account_id);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to delete account", std::string(e.what())};
    }
}

// --- Consolidated Queries ---

Result<std::vector<analysis::Position>> Database::get_all_positions(
    const std::string& account_name,
    int64_t account_id) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        std::string sql =
            "SELECT o.id, o.account_id, o.symbol, o.underlying, "
            "o.expiry, o.strike, o.right, o.quantity, o.mark_price, o.entry_premium, "
            "0 as is_manual, o.multiplier "
            "FROM open_options o";

        if (!account_name.empty()) {
            sql += " JOIN accounts a ON a.id = o.account_id WHERE a.name = ?";
        } else if (account_id > 0) {
            sql += " WHERE o.account_id = ?";
        }
        sql += " ORDER BY o.expiry, o.underlying";

        SQLite::Statement q(*db_, sql);
        if (!account_name.empty()) {
            q.bind(1, account_name);
        } else if (account_id > 0) {
            q.bind(1, account_id);
        }

        std::vector<analysis::Position> result;
        while (q.executeStep()) {
            analysis::Position p;
            p.id = q.getColumn(0).getInt64();
            p.account_id = q.getColumn(1).getInt64();
            p.symbol = q.getColumn(2).getString();
            p.underlying = q.getColumn(3).getString();
            p.expiry = q.getColumn(4).getString();
            p.strike = q.getColumn(5).getDouble();
            std::string right_str = q.getColumn(6).getString();
            p.right = right_str.empty() ? ' ' : right_str[0];
            p.quantity = q.getColumn(7).getDouble();
            p.mark_price = q.getColumn(8).getDouble();
            p.entry_premium = q.getColumn(9).getDouble();
            p.is_manual = q.getColumn(10).getInt() == 1;
            p.multiplier = q.getColumn(11).getDouble();
            if (p.multiplier == 0.0) p.multiplier = 100.0;
            result.push_back(p);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get positions", std::string(e.what())};
    }
}

Result<std::vector<Database::RiskSummary>> Database::get_consolidated_risk() {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "SELECT a.name, COALESCE(SUM(s.max_loss), 0.0), "
            "COALESCE(SUM(s.max_profit), 0.0), COUNT(s.id) "
            "FROM accounts a LEFT JOIN detected_strategies s ON s.account_id = a.id "
            "GROUP BY a.id ORDER BY a.name");

        std::vector<RiskSummary> result;
        while (q.executeStep()) {
            RiskSummary r;
            r.account_name = q.getColumn(0).getString();
            r.total_max_loss = q.getColumn(1).getDouble();
            r.total_max_profit = q.getColumn(2).getDouble();
            r.strategy_count = q.getColumn(3).getInt();
            result.push_back(r);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get consolidated risk", std::string(e.what())};
    }
}

Result<std::vector<Database::ExposureInfo>> Database::get_underlying_exposure() {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "SELECT o.underlying, COALESCE(SUM("
            "  CASE WHEN o.quantity < 0 AND o.right = 'P' "
            "    THEN (o.strike * 100.0 * ABS(o.quantity)) - "
            "         (ABS(o.quantity) * o.entry_premium * 100.0) "
            "    ELSE 0 END), 0.0), "
            "COUNT(*) "
            "FROM open_options o "
            "GROUP BY o.underlying ORDER BY o.underlying");

        std::vector<ExposureInfo> result;
        while (q.executeStep()) {
            ExposureInfo e;
            e.underlying = q.getColumn(0).getString();
            e.total_max_loss = q.getColumn(1).getDouble();
            e.position_count = q.getColumn(2).getInt();
            result.push_back(e);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get underlying exposure", std::string(e.what())};
    }
}

// --- Round Trip CRUD ---

Result<int64_t> Database::insert_round_trip(const RoundTrip& round_trip) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "INSERT INTO round_trips "
            "(account_id, underlying, strike, right, expiry, quantity, "
            "open_date, close_date, holding_days, open_price, close_price, "
            "net_premium, commission, realized_pnl, close_reason, match_method, "
            "strategy_type, strategy_group_id) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        q.bind(1, round_trip.account_id);
        q.bind(2, round_trip.underlying);
        q.bind(3, round_trip.strike);
        q.bind(4, std::string(1, round_trip.right));
        q.bind(5, round_trip.expiry);
        q.bind(6, round_trip.quantity);
        q.bind(7, round_trip.open_date);
        q.bind(8, round_trip.close_date);
        q.bind(9, round_trip.holding_days);
        q.bind(10, round_trip.open_price);
        q.bind(11, round_trip.close_price);
        q.bind(12, round_trip.net_premium);
        q.bind(13, round_trip.commission);
        q.bind(14, round_trip.realized_pnl);
        q.bind(15, round_trip.close_reason);
        q.bind(16, round_trip.match_method);
        if (round_trip.strategy_type.empty()) {
            q.bind(17);
        } else {
            q.bind(17, round_trip.strategy_type);
        }
        if (round_trip.strategy_group_id.has_value()) {
            q.bind(18, *round_trip.strategy_group_id);
        } else {
            q.bind(18);
        }
        q.exec();
        int64_t id = db_->getLastInsertRowid();
        Logger::debug("Inserted round_trip id={} for account_id={}", id, round_trip.account_id);
        return id;
    } catch (const std::exception& e) {
        return Error{"Failed to insert round trip", std::string(e.what())};
    }
}

Result<void> Database::insert_round_trip_leg(const RoundTripLeg& leg) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "INSERT INTO round_trip_legs (round_trip_id, trade_id, role, matched_quantity) "
            "VALUES (?, ?, ?, ?)");
        q.bind(1, leg.round_trip_id);
        q.bind(2, leg.trade_id);
        q.bind(3, leg.role);
        q.bind(4, leg.matched_quantity);
        q.exec();
        Logger::debug("Inserted round_trip_leg for round_trip_id={}", leg.round_trip_id);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to insert round trip leg", std::string(e.what())};
    }
}

Result<std::vector<Database::RoundTrip>> Database::get_round_trips(
    int64_t account_id,
    const std::string& date_from,
    const std::string& date_to,
    const std::string& strategy_type,
    const std::string& underlying) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        std::string sql =
            "SELECT id, account_id, underlying, strike, right, expiry, quantity, "
            "open_date, close_date, holding_days, open_price, close_price, "
            "net_premium, commission, realized_pnl, close_reason, match_method, "
            "strategy_type, strategy_group_id "
            "FROM round_trips WHERE 1=1";

        if (account_id > 0) sql += " AND account_id = ?";
        if (!date_from.empty()) sql += " AND close_date >= ?";
        if (!date_to.empty()) sql += " AND close_date <= ?";
        if (!strategy_type.empty()) sql += " AND strategy_type = ?";
        if (!underlying.empty()) sql += " AND underlying = ?";
        sql += " ORDER BY close_date DESC";

        SQLite::Statement q(*db_, sql);
        int idx = 1;
        if (account_id > 0) q.bind(idx++, account_id);
        if (!date_from.empty()) q.bind(idx++, date_from);
        if (!date_to.empty()) q.bind(idx++, date_to);
        if (!strategy_type.empty()) q.bind(idx++, strategy_type);
        if (!underlying.empty()) q.bind(idx++, underlying);

        std::vector<RoundTrip> result;
        while (q.executeStep()) {
            RoundTrip rt;
            rt.id = q.getColumn(0).getInt64();
            rt.account_id = q.getColumn(1).getInt64();
            rt.underlying = q.getColumn(2).getString();
            rt.strike = q.getColumn(3).getDouble();
            std::string right_str = q.getColumn(4).getString();
            rt.right = right_str.empty() ? ' ' : right_str[0];
            rt.expiry = q.getColumn(5).getString();
            rt.quantity = q.getColumn(6).getInt();
            rt.open_date = q.getColumn(7).getString();
            rt.close_date = q.getColumn(8).getString();
            rt.holding_days = q.getColumn(9).getInt();
            rt.open_price = q.getColumn(10).getDouble();
            rt.close_price = q.getColumn(11).getDouble();
            rt.net_premium = q.getColumn(12).getDouble();
            rt.commission = q.getColumn(13).getDouble();
            rt.realized_pnl = q.getColumn(14).getDouble();
            rt.close_reason = q.getColumn(15).getString();
            rt.match_method = q.getColumn(16).getString();
            auto strategy_type_col = q.getColumn(17);
            if (!strategy_type_col.isNull()) {
                rt.strategy_type = strategy_type_col.getString();
            }
            auto sg_col = q.getColumn(18);
            if (!sg_col.isNull()) {
                rt.strategy_group_id = sg_col.getInt64();
            }
            result.push_back(rt);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get round trips", std::string(e.what())};
    }
}

Result<int> Database::get_round_trips_count(int64_t account_id) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        std::string sql = "SELECT COUNT(*) FROM round_trips";
        if (account_id > 0) sql += " WHERE account_id = ?";
        SQLite::Statement q(*db_, sql);
        if (account_id > 0) q.bind(1, account_id);
        if (q.executeStep()) {
            return q.getColumn(0).getInt();
        }
        return 0;
    } catch (const std::exception& e) {
        return Error{"Failed to get round trips count", std::string(e.what())};
    }
}

Result<void> Database::clear_round_trips(int64_t account_id) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Transaction transaction(*db_);

        // Delete legs first (referential integrity)
        if (account_id > 0) {
            SQLite::Statement del_legs(*db_,
                "DELETE FROM round_trip_legs WHERE round_trip_id IN "
                "(SELECT id FROM round_trips WHERE account_id = ?)");
            del_legs.bind(1, account_id);
            del_legs.exec();

            SQLite::Statement del_rts(*db_,
                "DELETE FROM round_trips WHERE account_id = ?");
            del_rts.bind(1, account_id);
            del_rts.exec();

            Logger::info("Cleared round trips for account_id={}", account_id);
        } else {
            db_->exec("DELETE FROM round_trip_legs");
            db_->exec("DELETE FROM round_trips");
            Logger::info("Cleared all round trips");
        }

        transaction.commit();
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to clear round trips", std::string(e.what())};
    }
}

// --- Strategy Round Trip CRUD ---

Result<int64_t> Database::insert_strategy_round_trip(const StrategyRoundTrip& srt) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "INSERT INTO strategy_round_trips "
            "(account_id, strategy_type, underlying, expiry, open_date, close_date, "
            "net_premium, realized_pnl, leg_count) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        q.bind(1, srt.account_id);
        q.bind(2, srt.strategy_type);
        q.bind(3, srt.underlying);
        q.bind(4, srt.expiry);
        q.bind(5, srt.open_date);
        q.bind(6, srt.close_date);
        q.bind(7, srt.net_premium);
        q.bind(8, srt.realized_pnl);
        q.bind(9, srt.leg_count);
        q.exec();
        int64_t id = db_->getLastInsertRowid();
        Logger::debug("Inserted strategy_round_trip id={} for account_id={}", id, srt.account_id);
        return id;
    } catch (const std::exception& e) {
        return Error{"Failed to insert strategy round trip", std::string(e.what())};
    }
}

Result<void> Database::link_round_trip_to_strategy(int64_t round_trip_id, int64_t strategy_group_id) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "UPDATE round_trips SET strategy_group_id = ? WHERE id = ?");
        q.bind(1, strategy_group_id);
        q.bind(2, round_trip_id);
        int rows = q.exec();
        if (rows == 0) return Error{"Round trip not found"};
        Logger::debug("Linked round_trip id={} to strategy_group_id={}", round_trip_id, strategy_group_id);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to link round trip to strategy", std::string(e.what())};
    }
}

// --- Position Snapshot CRUD ---

Result<void> Database::insert_position_snapshot(const PositionSnapshot& snapshot) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "INSERT OR REPLACE INTO position_snapshots "
            "(account_id, snapshot_date, symbol, underlying, expiry, strike, "
            "right, quantity, mark_price, entry_price) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        q.bind(1, snapshot.account_id);
        q.bind(2, snapshot.snapshot_date);
        q.bind(3, snapshot.symbol);
        q.bind(4, snapshot.underlying);
        q.bind(5, snapshot.expiry);
        q.bind(6, snapshot.strike);
        q.bind(7, std::string(1, snapshot.right));
        q.bind(8, snapshot.quantity);
        q.bind(9, snapshot.mark_price);
        q.bind(10, snapshot.entry_price);
        q.exec();
        Logger::debug("Inserted position snapshot for account_id={} date={} symbol={}",
                       snapshot.account_id, snapshot.snapshot_date, snapshot.symbol);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to insert position snapshot", std::string(e.what())};
    }
}

Result<std::vector<Database::PositionSnapshot>> Database::get_position_snapshots(
    int64_t account_id,
    const std::string& snapshot_date) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        SQLite::Statement q(*db_,
            "SELECT id, account_id, snapshot_date, symbol, underlying, expiry, "
            "strike, right, quantity, mark_price, entry_price "
            "FROM position_snapshots "
            "WHERE account_id = ? AND snapshot_date = ? "
            "ORDER BY underlying, expiry, strike");
        q.bind(1, account_id);
        q.bind(2, snapshot_date);

        std::vector<PositionSnapshot> result;
        while (q.executeStep()) {
            PositionSnapshot s;
            s.id = q.getColumn(0).getInt64();
            s.account_id = q.getColumn(1).getInt64();
            s.snapshot_date = q.getColumn(2).getString();
            s.symbol = q.getColumn(3).getString();
            s.underlying = q.getColumn(4).getString();
            s.expiry = q.getColumn(5).getString();
            s.strike = q.getColumn(6).getDouble();
            std::string right_str = q.getColumn(7).getString();
            s.right = right_str.empty() ? ' ' : right_str[0];
            s.quantity = q.getColumn(8).getInt();
            s.mark_price = q.getColumn(9).getDouble();
            s.entry_price = q.getColumn(10).getDouble();
            result.push_back(s);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get position snapshots", std::string(e.what())};
    }
}

Result<std::vector<Database::PositionSnapshot>> Database::diff_snapshots(
    int64_t account_id,
    const std::string& date1,
    const std::string& date2) {
    if (!initialized_) return Error{"Database not initialized"};
    try {
        // Find positions present in date1 but not in date2 (closed between snapshots)
        SQLite::Statement q(*db_,
            "SELECT id, account_id, snapshot_date, symbol, underlying, expiry, "
            "strike, right, quantity, mark_price, entry_price "
            "FROM position_snapshots "
            "WHERE account_id = ? AND snapshot_date = ? "
            "AND NOT EXISTS ("
            "  SELECT 1 FROM position_snapshots ps2 "
            "  WHERE ps2.account_id = ? AND ps2.snapshot_date = ? "
            "  AND ps2.symbol = position_snapshots.symbol"
            ") "
            "ORDER BY underlying, expiry, strike");
        q.bind(1, account_id);
        q.bind(2, date1);
        q.bind(3, account_id);
        q.bind(4, date2);

        std::vector<PositionSnapshot> result;
        while (q.executeStep()) {
            PositionSnapshot s;
            s.id = q.getColumn(0).getInt64();
            s.account_id = q.getColumn(1).getInt64();
            s.snapshot_date = q.getColumn(2).getString();
            s.symbol = q.getColumn(3).getString();
            s.underlying = q.getColumn(4).getString();
            s.expiry = q.getColumn(5).getString();
            s.strike = q.getColumn(6).getDouble();
            std::string right_str = q.getColumn(7).getString();
            s.right = right_str.empty() ? ' ' : right_str[0];
            s.quantity = q.getColumn(8).getInt();
            s.mark_price = q.getColumn(9).getDouble();
            s.entry_price = q.getColumn(10).getDouble();
            result.push_back(s);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to diff snapshots", std::string(e.what())};
    }
}

} // namespace ibkr::db
