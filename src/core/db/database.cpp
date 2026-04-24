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
            "right, quantity, mark_price, entry_premium, current_value, is_manual) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");

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
            insert.bind(9, pos.cost_basis_price);  // Use cost basis as entry premium
            insert.bind(10, pos.position_value);

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

} // namespace ibkr::db
