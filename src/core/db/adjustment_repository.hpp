#pragma once

#include "utils/result.hpp"
#include <map>
#include <string>
#include <vector>
#include <cstdint>

namespace SQLite {
class Database;
}

namespace ibkr::db {

/**
 * Stock split/rename adjustment CRUD and cache.
 */
class AdjustmentRepository {
public:
    struct StockAdjustment {
        int64_t id{0};
        std::string symbol;
        std::string effective_date;
        double split_ratio{1.0};
        std::string new_symbol;  // empty if no rename
    };

    /// Result of adjusting a single trade record.
    struct Adjusted {
        std::string symbol;  // resolved (post-rename)
        double quantity;     // split-adjusted
        double trade_price;  // split-adjusted
    };

    explicit AdjustmentRepository(SQLite::Database& db);

    [[nodiscard]] utils::Result<int64_t> insert(const StockAdjustment& adj);
    [[nodiscard]] utils::Result<std::vector<StockAdjustment>> get(const std::string& symbol = "");

    /// Load all adjustments into an in-memory cache for batch processing.
    [[nodiscard]] utils::Result<void> load_cache();

    /// Adjust a single trade: apply cumulative splits and resolve renames.
    /// Dollar amounts (proceeds, net_cash) are unchanged by splits.
    Adjusted adjust(const std::string& raw_symbol,
                    const std::string& trade_date,
                    double quantity,
                    double trade_price) const;

private:
    SQLite::Database& db_;

    // raw_symbol -> [(effective_date, split_ratio)], sorted by date
    std::map<std::string, std::vector<std::pair<std::string, double>>> splits_;
    // raw_symbol -> new_symbol
    std::map<std::string, std::string> renames_;
};

} // namespace ibkr::db
