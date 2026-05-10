#include "snapshot_service.hpp"
#include "utils/logger.hpp"

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

SnapshotService::SnapshotService(db::Database& database)
    : database_(database) {}

Result<int> SnapshotService::capture_snapshot(
    int64_t account_id,
    const std::string& snapshot_date) {

    auto positions_result = database_.get_all_positions("", account_id);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    const auto& positions = *positions_result;
    int captured = 0;

    for (const auto& pos : positions) {
        db::Database::PositionSnapshot snap;
        snap.account_id = account_id;
        snap.snapshot_date = snapshot_date;
        snap.symbol = pos.symbol;
        snap.underlying = pos.underlying;
        snap.expiry = pos.expiry;
        snap.strike = pos.strike;
        snap.right = pos.right;
        snap.quantity = static_cast<int>(pos.quantity);
        snap.mark_price = pos.mark_price;
        snap.entry_price = pos.entry_premium;

        auto insert_result = database_.insert_position_snapshot(snap);
        if (insert_result) {
            captured++;
        } else {
            Logger::warn("Failed to insert snapshot for {}: {}",
                        pos.symbol, insert_result.error().message);
        }
    }

    Logger::info("Captured {} position snapshots for account {} on {}",
                captured, account_id, snapshot_date);
    return captured;
}

Result<std::vector<db::Database::PositionSnapshot>> SnapshotService::get_snapshot(
    int64_t account_id,
    const std::string& snapshot_date) {
    return database_.get_position_snapshots(account_id, snapshot_date);
}

Result<std::vector<db::Database::PositionSnapshot>> SnapshotService::find_disappeared_positions(
    int64_t account_id,
    const std::string& date1,
    const std::string& date2) {
    return database_.diff_snapshots(account_id, date1, date2);
}

Result<std::string> SnapshotService::get_latest_snapshot_date(int64_t account_id) {
    auto db_ptr = database_.get_db();
    if (!db_ptr) return Error{"Database not initialized"};

    try {
        SQLite::Statement q(*db_ptr,
            "SELECT MAX(snapshot_date) FROM position_snapshots WHERE account_id = ?");
        q.bind(1, account_id);
        if (q.executeStep()) {
            return q.getColumn(0).getString();
        }
        return Error{"No snapshots found for account"};
    } catch (const std::exception& e) {
        return Error{"Failed to get latest snapshot date", std::string(e.what())};
    }
}

} // namespace ibkr::services