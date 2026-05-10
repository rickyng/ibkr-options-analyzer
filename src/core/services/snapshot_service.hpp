#pragma once

#include "db/database.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>

namespace ibkr::services {

class SnapshotService {
public:
    explicit SnapshotService(db::Database& database);

    utils::Result<int> capture_snapshot(
        int64_t account_id,
        const std::string& snapshot_date);

    utils::Result<std::vector<db::Database::PositionSnapshot>> get_snapshot(
        int64_t account_id,
        const std::string& snapshot_date);

    utils::Result<std::vector<db::Database::PositionSnapshot>> find_disappeared_positions(
        int64_t account_id,
        const std::string& date1,
        const std::string& date2);

    utils::Result<std::string> get_latest_snapshot_date(int64_t account_id);

private:
    db::Database& database_;
};

} // namespace ibkr::services