#pragma once

#include "utils/result.hpp"
#include <memory>
#include <string>

namespace SQLite {
class Database;
}

namespace ibkr::db {

/**
 * Manages SQLite connection lifecycle, schema creation, and migrations.
 */
class DatabaseConnection {
public:
    explicit DatabaseConnection(const std::string& db_path);
    ~DatabaseConnection();

    /// Open database, create tables, run migrations, seed adjustments.
    [[nodiscard]] utils::Result<void> initialize();

    bool is_initialized() const { return initialized_; }

    /// Raw database reference (available after initialize()).
    SQLite::Database& get() { return *db_; }

private:
    std::string db_path_;
    std::unique_ptr<SQLite::Database> db_;
    bool initialized_{false};

    std::string expand_path(const std::string& path);
    utils::Result<void> execute_schema();
    void seed_stock_adjustments();
};

} // namespace ibkr::db
