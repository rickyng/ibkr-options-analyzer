#include "database.hpp"
#include "utils/logger.hpp"

namespace ibkr::db {

using utils::Logger;

Database::Database(const std::string& db_path)
    : conn_(db_path) {
}

Database::~Database() = default;

utils::Result<void> Database::initialize() {
    auto result = conn_.initialize();
    if (!result) {
        return result;
    }

    // Create repositories
    adjustment_repo_.emplace(conn_.get());
    account_repo_.emplace(conn_.get());
    trade_repo_.emplace(conn_.get(), *adjustment_repo_);
    position_repo_.emplace(conn_.get());

    Logger::info("Database repositories initialized");
    return utils::Result<void>{};
}

bool Database::is_initialized() const {
    return conn_.is_initialized();
}

AccountRepository& Database::accounts() {
    return *account_repo_;
}

TradeRepository& Database::trades() {
    return *trade_repo_;
}

PositionRepository& Database::positions() {
    return *position_repo_;
}

AdjustmentRepository& Database::adjustments() {
    return *adjustment_repo_;
}

SQLite::Database& Database::connection() {
    return conn_.get();
}

} // namespace ibkr::db
