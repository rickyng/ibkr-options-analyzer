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

} // namespace ibkr::db
