#include "adjustment_repository.hpp"
#include "utils/logger.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

namespace ibkr::db {

using utils::Result;
using utils::Error;
using utils::Logger;

AdjustmentRepository::AdjustmentRepository(SQLite::Database& db)
    : db_(db) {}

Result<int64_t> AdjustmentRepository::insert(const StockAdjustment& adj) {
    try {
        SQLite::Statement q(db_,
            "INSERT INTO stock_adjustments (symbol, effective_date, split_ratio, new_symbol) "
            "VALUES (?, ?, ?, ?)");
        q.bind(1, adj.symbol);
        q.bind(2, adj.effective_date);
        q.bind(3, adj.split_ratio);
        if (adj.new_symbol.empty()) {
            q.bind(4);
        } else {
            q.bind(4, adj.new_symbol);
        }
        q.exec();
        return db_.getLastInsertRowid();
    } catch (const std::exception& e) {
        return Error{"Failed to insert stock adjustment", std::string(e.what())};
    }
}

Result<std::vector<AdjustmentRepository::StockAdjustment>> AdjustmentRepository::get(
    const std::string& symbol) {
    try {
        std::string sql = "SELECT id, symbol, effective_date, split_ratio, COALESCE(new_symbol, '') "
                          "FROM stock_adjustments";
        if (!symbol.empty()) {
            sql += " WHERE symbol = ? ORDER BY effective_date";
        } else {
            sql += " ORDER BY symbol, effective_date";
        }
        SQLite::Statement q(db_, sql);
        if (!symbol.empty()) {
            q.bind(1, symbol);
        }

        std::vector<StockAdjustment> results;
        while (q.executeStep()) {
            StockAdjustment adj;
            adj.id = q.getColumn(0).getInt64();
            adj.symbol = q.getColumn(1).getString();
            adj.effective_date = q.getColumn(2).getString();
            adj.split_ratio = q.getColumn(3).getDouble();
            adj.new_symbol = q.getColumn(4).getString();
            results.push_back(adj);
        }
        return results;
    } catch (const std::exception& e) {
        return Error{"Failed to get stock adjustments", std::string(e.what())};
    }
}

Result<void> AdjustmentRepository::load_cache() {
    try {
        splits_.clear();
        renames_.clear();

        SQLite::Statement q(db_,
            "SELECT symbol, effective_date, split_ratio, COALESCE(new_symbol, '') "
            "FROM stock_adjustments ORDER BY effective_date");
        while (q.executeStep()) {
            std::string symbol = q.getColumn(0).getString();
            std::string date = q.getColumn(1).getString();
            double ratio = q.getColumn(2).getDouble();
            std::string new_sym = q.getColumn(3).getString();

            // All entries go into splits (ratio=1.0 is a no-op multiply)
            splits_[symbol].emplace_back(date, ratio);

            // Only renames go into the rename map
            if (!new_sym.empty()) {
                renames_[symbol] = new_sym;
            }
        }
        return Result<void>{};
    } catch (const std::exception& e) {
        return Error{"Failed to load stock adjustments", std::string(e.what())};
    }
}

AdjustmentRepository::Adjusted AdjustmentRepository::adjust(
    const std::string& raw_symbol,
    const std::string& trade_date,
    double quantity,
    double trade_price) const
{
    // Fast path: no adjustments for this symbol
    auto sit = splits_.find(raw_symbol);
    auto rit = renames_.find(raw_symbol);
    if (sit == splits_.end() && rit == renames_.end()) {
        return {raw_symbol, quantity, trade_price};
    }

    // Resolve rename
    std::string resolved = (rit != renames_.end()) ? rit->second : raw_symbol;

    // Apply cumulative split ratio for all splits where trade_date < effective_date
    double cum_ratio = 1.0;
    if (sit != splits_.end()) {
        for (const auto& [eff_date, ratio] : sit->second) {
            if (trade_date < eff_date) {
                cum_ratio *= ratio;
            }
        }
    }

    if (cum_ratio == 1.0) {
        return {resolved, quantity, trade_price};
    }

    return {resolved, quantity * cum_ratio, trade_price / cum_ratio};
}

} // namespace ibkr::db
