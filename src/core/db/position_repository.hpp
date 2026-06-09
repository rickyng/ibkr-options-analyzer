#pragma once

#include "utils/result.hpp"
#include "analysis/strategy_detector.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace SQLite {
class Database;
}

namespace ibkr::db {

/**
 * Position queries: open positions, risk summaries, exposure.
 */
class PositionRepository {
public:
    struct RiskSummary {
        std::string account_name;
        double total_max_loss{0.0};
        double total_max_profit{0.0};
        int strategy_count{0};
    };

    struct ExposureInfo {
        std::string underlying;
        double total_max_loss{0.0};
        int position_count{0};
    };

    explicit PositionRepository(SQLite::Database& db);

    [[nodiscard]] utils::Result<std::vector<analysis::Position>> get_all_positions(
        const std::string& account_name = "",
        int64_t account_id = 0);

    [[nodiscard]] utils::Result<std::vector<RiskSummary>> get_consolidated_risk();
    [[nodiscard]] utils::Result<std::vector<ExposureInfo>> get_underlying_exposure();

private:
    SQLite::Database& db_;
};

} // namespace ibkr::db
