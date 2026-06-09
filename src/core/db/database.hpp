#pragma once

#include "database_connection.hpp"
#include "account_repository.hpp"
#include "adjustment_repository.hpp"
#include "trade_repository.hpp"
#include "position_repository.hpp"
#include "utils/result.hpp"
#include <optional>
#include <string>

namespace ibkr::db {

/**
 * Composition root for database access.
 *
 * Owns the connection and all repositories.
 * Repositories are available after initialize() succeeds.
 *
 * Usage:
 *   Database db("~/.ibkr-options-analyzer/data.db");
 *   db.initialize();
 *   db.trades().import_trades(account_id, trades);
 *   db.positions().get_all_positions();
 */
class Database {
public:
    explicit Database(const std::string& db_path);
    ~Database();

    [[nodiscard]] utils::Result<void> initialize();
    bool is_initialized() const;

    // --- Repository accessors (available after initialize()) ---

    AccountRepository& accounts();
    TradeRepository& trades();
    PositionRepository& positions();
    AdjustmentRepository& adjustments();

    /// Raw database reference for custom queries.
    SQLite::Database& connection();

private:
    DatabaseConnection conn_;

    // Repositories (created after initialize() succeeds)
    std::optional<AccountRepository> account_repo_;
    std::optional<AdjustmentRepository> adjustment_repo_;
    std::optional<TradeRepository> trade_repo_;
    std::optional<PositionRepository> position_repo_;
};

} // namespace ibkr::db
