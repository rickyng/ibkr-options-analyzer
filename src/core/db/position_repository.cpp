#include "position_repository.hpp"
#include "utils/logger.hpp"
#include "utils/currency.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

namespace ibkr::db {

using utils::Result;
using utils::Error;
using utils::Logger;

PositionRepository::PositionRepository(SQLite::Database& db)
    : db_(db) {}

Result<std::vector<analysis::Position>> PositionRepository::get_all_positions(
    const std::string& account_name,
    int64_t account_id) {
    try {
        std::string sql =
            "SELECT o.id, o.account_id, o.symbol, o.underlying, "
            "o.expiry, o.strike, o.right, o.quantity, o.mark_price, o.entry_premium, "
            "0 as is_manual, o.multiplier "
            "FROM open_options o";

        if (!account_name.empty()) {
            sql += " JOIN accounts a ON a.id = o.account_id WHERE a.name = ?";
        } else if (account_id > 0) {
            sql += " WHERE o.account_id = ?";
        }
        sql += " ORDER BY o.expiry, o.underlying";

        SQLite::Statement q(db_, sql);
        if (!account_name.empty()) {
            q.bind(1, account_name);
        } else if (account_id > 0) {
            q.bind(1, account_id);
        }

        std::vector<analysis::Position> result;
        while (q.executeStep()) {
            analysis::Position p;
            p.id = q.getColumn(0).getInt64();
            p.account_id = q.getColumn(1).getInt64();
            p.symbol = q.getColumn(2).getString();
            p.underlying = q.getColumn(3).getString();
            p.expiry = q.getColumn(4).getString();
            p.strike = q.getColumn(5).getDouble();
            std::string right_str = q.getColumn(6).getString();
            p.right = right_str.empty() ? ' ' : right_str[0];
            p.quantity = q.getColumn(7).getDouble();
            p.mark_price = q.getColumn(8).getDouble();
            p.entry_premium = q.getColumn(9).getDouble();
            p.is_manual = q.getColumn(10).getInt() == 1;
            p.multiplier = q.getColumn(11).getDouble();
            if (p.multiplier == 0.0) p.multiplier = 100.0;
            if (p.currency.empty()) p.currency = utils::deduce_currency(p.underlying);
            result.push_back(p);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get positions", std::string(e.what())};
    }
}

Result<std::vector<PositionRepository::RiskSummary>> PositionRepository::get_consolidated_risk() {
    try {
        SQLite::Statement q(db_,
            "SELECT a.name, COALESCE(SUM(s.max_loss), 0.0), "
            "COALESCE(SUM(s.max_profit), 0.0), COUNT(s.id) "
            "FROM accounts a LEFT JOIN detected_strategies s ON s.account_id = a.id "
            "GROUP BY a.id ORDER BY a.name");

        std::vector<RiskSummary> result;
        while (q.executeStep()) {
            RiskSummary r;
            r.account_name = q.getColumn(0).getString();
            r.total_max_loss = q.getColumn(1).getDouble();
            r.total_max_profit = q.getColumn(2).getDouble();
            r.strategy_count = q.getColumn(3).getInt();
            result.push_back(r);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get consolidated risk", std::string(e.what())};
    }
}

Result<std::vector<PositionRepository::ExposureInfo>> PositionRepository::get_underlying_exposure() {
    try {
        SQLite::Statement q(db_,
            "SELECT o.underlying, COALESCE(SUM("
            "  CASE WHEN o.quantity < 0 AND o.right = 'P' "
            "    THEN (o.strike * o.multiplier * ABS(o.quantity)) - "
            "         (ABS(o.quantity) * o.entry_premium * o.multiplier) "
            "    ELSE 0 END), 0.0), "
            "COUNT(*) "
            "FROM open_options o "
            "GROUP BY o.underlying ORDER BY o.underlying");

        std::vector<ExposureInfo> result;
        while (q.executeStep()) {
            ExposureInfo e;
            e.underlying = q.getColumn(0).getString();
            e.total_max_loss = q.getColumn(1).getDouble();
            e.position_count = q.getColumn(2).getInt();
            result.push_back(e);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get underlying exposure", std::string(e.what())};
    }
}

} // namespace ibkr::db
