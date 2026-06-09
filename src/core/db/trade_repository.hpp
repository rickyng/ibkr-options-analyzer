#pragma once

#include "adjustment_repository.hpp"
#include "utils/result.hpp"
#include "parsers/csv_parser.hpp"
#include "parsers/activity_statement_parser.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace ibkr::db {

/**
 * Trade import, dedup, and count operations.
 *
 * Import methods take account_id (caller resolves account_name via AccountRepository).
 * Uses AdjustmentRepository for split/rename handling during imports.
 */
class TradeRepository {
public:
    explicit TradeRepository(SQLite::Database& db, AdjustmentRepository& adjustments);

    [[nodiscard]] utils::Result<void> import_trades(
        int64_t account_id,
        const std::vector<parser::TradeRecord>& trades);

    [[nodiscard]] utils::Result<void> import_open_positions(
        int64_t account_id,
        const std::vector<parser::OpenPositionRecord>& positions);

    [[nodiscard]] utils::Result<void> import_stock_trades(
        int64_t account_id,
        const std::vector<parser::StockTradeRecord>& trades);

    [[nodiscard]] utils::Result<void> import_dividends(
        int64_t account_id,
        const std::vector<parser::DividendRecord>& dividends);

    [[nodiscard]] utils::Result<void> import_interest_expenses(
        int64_t account_id,
        const std::vector<parser::InterestRecord>& interests);

    [[nodiscard]] utils::Result<void> clear_open_positions(int64_t account_id);

    [[nodiscard]] utils::Result<int> dedup_trades(int64_t account_id = 0);
    [[nodiscard]] utils::Result<int> get_trades_count();
    [[nodiscard]] utils::Result<int> get_open_positions_count();

    /// Return symbols with a net positive stock position (shares held).
    [[nodiscard]] utils::Result<std::vector<std::string>> get_stock_holding_symbols();

private:
    SQLite::Database& db_;
    AdjustmentRepository& adjustments_;
};

} // namespace ibkr::db
