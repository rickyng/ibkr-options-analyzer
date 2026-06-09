#include "account_repository.hpp"
#include "utils/logger.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

namespace ibkr::db {

using utils::Result;
using utils::Error;
using utils::Logger;

AccountRepository::AccountRepository(SQLite::Database& db)
    : db_(db) {}

Result<std::vector<AccountRepository::AccountInfo>> AccountRepository::list_accounts() {
    try {
        SQLite::Statement q(db_,
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

Result<AccountRepository::AccountInfo> AccountRepository::get_account(int64_t account_id) {
    try {
        SQLite::Statement q(db_,
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

Result<int64_t> AccountRepository::get_or_create_account(const std::string& account_name) {
    try {
        // Check if account exists by name
        SQLite::Statement by_name(db_, "SELECT id FROM accounts WHERE name = ?");
        by_name.bind(1, account_name);

        if (by_name.executeStep()) {
            int64_t account_id = by_name.getColumn(0).getInt64();
            Logger::debug("Found existing account: {} (id={})", account_name, account_id);
            return account_id;
        }

        // Check if account_name matches an ibkr_account_id — resolve to parent account
        SQLite::Statement by_ibkr(db_, "SELECT id, name FROM accounts WHERE ibkr_account_id = ?");
        by_ibkr.bind(1, account_name);

        if (by_ibkr.executeStep()) {
            int64_t account_id = by_ibkr.getColumn(0).getInt64();
            std::string parent_name = by_ibkr.getColumn(1).getString();
            Logger::debug("Resolved IBKR account {} -> {} (id={})", account_name, parent_name, account_id);
            return account_id;
        }

        // Create new account
        SQLite::Statement insert(db_,
            "INSERT INTO accounts (name, token, query_id, enabled) VALUES (?, '', '', 1)");
        insert.bind(1, account_name);
        insert.exec();

        int64_t account_id = db_.getLastInsertRowid();
        Logger::info("Created new account: {} (id={})", account_name, account_id);
        return account_id;

    } catch (const std::exception& e) {
        return Error{
            "Failed to get/create account",
            std::string(e.what())
        };
    }
}

Result<void> AccountRepository::update_account(int64_t account_id,
                                       const std::string& token,
                                       const std::string& query_id,
                                       bool enabled) {
    try {
        SQLite::Statement q(db_,
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

Result<void> AccountRepository::delete_account(int64_t account_id) {
    try {
        SQLite::Statement q(db_, "DELETE FROM accounts WHERE id = ?");
        q.bind(1, account_id);
        int rows = q.exec();
        if (rows == 0) return Error{"Account not found"};
        Logger::info("Deleted account id={} (cascading deletes)", account_id);
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to delete account", std::string(e.what())};
    }
}

} // namespace ibkr::db
