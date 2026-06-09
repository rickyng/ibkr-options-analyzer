#pragma once

#include "utils/result.hpp"
#include <string>
#include <vector>
#include <cstdint>

namespace SQLite {
class Database;
}

namespace ibkr::db {

/**
 * Account CRUD operations.
 *
 * Each method takes a SQLite::Database reference — the caller owns the connection.
 */
class AccountRepository {
public:
    struct AccountInfo {
        int64_t id{0};
        std::string name;
        std::string token;
        std::string query_id;
        bool enabled{true};
        std::string created_at;
        std::string updated_at;
    };

    explicit AccountRepository(SQLite::Database& db);

    [[nodiscard]] utils::Result<std::vector<AccountInfo>> list_accounts();
    [[nodiscard]] utils::Result<AccountInfo> get_account(int64_t account_id);
    [[nodiscard]] utils::Result<int64_t> get_or_create_account(const std::string& account_name);
    [[nodiscard]] utils::Result<void> update_account(int64_t account_id,
                                       const std::string& token,
                                       const std::string& query_id,
                                       bool enabled);
    [[nodiscard]] utils::Result<void> delete_account(int64_t account_id);

private:
    SQLite::Database& db_;
};

} // namespace ibkr::db
