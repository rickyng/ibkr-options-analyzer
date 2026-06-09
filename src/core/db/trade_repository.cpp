#include "trade_repository.hpp"
#include "utils/logger.hpp"
#include "utils/currency.hpp"
#include <SQLiteCpp/SQLiteCpp.h>
#include <algorithm>

namespace ibkr::db {

using utils::Result;
using utils::Error;
using utils::Logger;

TradeRepository::TradeRepository(SQLite::Database& db, AdjustmentRepository& adjustments)
    : db_(db), adjustments_(adjustments) {}

Result<void> TradeRepository::import_trades(
    int64_t account_id,
    const std::vector<parser::TradeRecord>& trades) {

    if (trades.empty()) {
        Logger::info("No trades to import");
        return Result<void>{};
    }

    Logger::info("Importing {} trades for account_id: {}", trades.size(), account_id);

    try {
        // Load split/rename adjustments once for the entire batch.
        // For option trades, only apply renames to the underlying field.
        auto adj_result = adjustments_.load_cache();
        if (!adj_result) {
            Logger::warn("Could not load stock adjustments for option import: {}",
                         adj_result.error().message);
        }

        // Convert all monetary values to USD at import time (consistent with
        // dividends and interest). The dashboard reads directly from the DB
        // and must not perform its own currency conversion.
        utils::CurrencyConverter converter;

        // Begin transaction
        SQLite::Transaction transaction(db_);

        // Dedup on identity keys only — NOT on price/commission, which are
        // USD-converted and would change if FX rates change between imports.
        SQLite::Statement check(db_,
            "SELECT 1 FROM trades WHERE account_id = ? AND trade_date = ? "
            "AND symbol = ? AND quantity = ? "
            "LIMIT 1");

        SQLite::Statement insert(db_,
            "INSERT INTO trades (account_id, trade_date, symbol, underlying, expiry, "
            "strike, right, quantity, trade_price, proceeds, commission, net_cash, "
            "multiplier, fifo_pnl_realized, close_price, notes_codes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        int imported = 0;
        int skipped = 0;
        for (const auto& trade : trades) {
            // Skip non-option trades (no option details)
            if (!trade.option_details) {
                Logger::debug("Skipping non-option trade: {}", trade.symbol);
                continue;
            }

            // Apply rename-only to the underlying symbol (FB->META, SQ->XYZ, etc.)
            auto adjusted = adjustments_.adjust(trade.option_details->underlying,
                                                trade.trade_date,
                                                1.0, 1.0);
            std::string effective_underlying = adjusted.symbol;
            std::string currency = utils::deduce_currency(effective_underlying);

            // Convert monetary fields to USD for consistent DB storage
            double trade_price_usd = converter.convert(trade.trade_price, currency);
            double proceeds_usd = converter.convert(trade.proceeds, currency);
            double commission_usd = converter.convert(trade.commission, currency);
            double net_cash_usd = converter.convert(trade.net_cash, currency);
            double strike_usd = converter.convert(trade.option_details->strike, currency);
            double fifo_pnl_usd = converter.convert(trade.fifo_pnl_realized, currency);
            double close_price_usd = converter.convert(trade.close_price, currency);

            // Check for existing duplicate
            check.reset();
            check.bind(1, account_id);
            check.bind(2, trade.trade_date);
            check.bind(3, trade.symbol);
            check.bind(4, trade.quantity);
            if (check.executeStep()) {
                skipped++;
                continue;
            }

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, trade.trade_date);
            insert.bind(3, trade.symbol);
            insert.bind(4, effective_underlying);
            insert.bind(5, trade.option_details->expiry);
            insert.bind(6, strike_usd);
            insert.bind(7, std::string(1, trade.option_details->right));
            insert.bind(8, trade.quantity);
            insert.bind(9, trade_price_usd);
            insert.bind(10, proceeds_usd);
            insert.bind(11, commission_usd);
            insert.bind(12, net_cash_usd);
            insert.bind(13, trade.multiplier);
            insert.bind(14, fifo_pnl_usd);
            insert.bind(15, close_price_usd);
            insert.bind(16, trade.notes_codes);

            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} trades ({} duplicates skipped) for account_id: {}",
            imported, skipped, account_id);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to import trades", std::string(e.what())};
    }
}

Result<void> TradeRepository::import_open_positions(
    int64_t account_id,
    const std::vector<parser::OpenPositionRecord>& positions) {

    if (positions.empty()) {
        Logger::info("No open positions to import");
        return Result<void>{};
    }

    Logger::info("Importing {} open positions for account_id: {}", positions.size(), account_id);

    try {
        // Convert monetary values to USD at import time
        utils::CurrencyConverter converter;

        // Begin transaction
        SQLite::Transaction transaction(db_);

        SQLite::Statement insert(db_,
            "INSERT INTO open_options (account_id, symbol, underlying, expiry, strike, "
            "right, quantity, mark_price, entry_premium, current_value, multiplier, is_manual) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)");

        int imported = 0;
        for (const auto& pos : positions) {
            // Skip non-option positions (no option details)
            if (!pos.option_details) {
                Logger::debug("Skipping non-option position: {}", pos.symbol);
                continue;
            }

            std::string currency = utils::deduce_currency(pos.option_details->underlying);

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, pos.symbol);
            insert.bind(3, pos.option_details->underlying);
            insert.bind(4, pos.option_details->expiry);
            insert.bind(5, converter.convert(pos.option_details->strike, currency));
            insert.bind(6, std::string(1, pos.option_details->right));
            insert.bind(7, pos.quantity);
            insert.bind(8, converter.convert(pos.mark_price, currency));
            insert.bind(9, converter.convert(pos.cost_basis_price, currency));
            insert.bind(10, converter.convert(pos.position_value, currency));
            insert.bind(11, pos.multiplier);

            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} open positions successfully", imported);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to import open positions", std::string(e.what())};
    }
}

Result<void> TradeRepository::import_stock_trades(
    int64_t account_id,
    const std::vector<parser::StockTradeRecord>& trades) {

    if (trades.empty()) {
        Logger::info("No stock trades to import");
        return Result<void>{};
    }

    Logger::info("Importing {} stock trades for account_id: {}", trades.size(), account_id);

    try {
        // Load split/rename adjustments once for the entire batch
        auto adj_result = adjustments_.load_cache();
        if (!adj_result) {
            Logger::warn("Could not load stock adjustments, importing raw data: {}",
                         adj_result.error().message);
        }

        // Convert monetary values to USD at import time
        utils::CurrencyConverter converter;

        SQLite::Transaction transaction(db_);

        SQLite::Statement check(db_,
            "SELECT 1 FROM stock_trades WHERE account_id = ? AND trade_date = ? "
            "AND symbol = ? AND quantity = ? "
            "LIMIT 1");

        SQLite::Statement insert(db_,
            "INSERT INTO stock_trades (account_id, symbol, description, trade_date, "
            "quantity, trade_price, proceeds, commission, net_cash, notes_codes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        int imported = 0;
        int skipped = 0;
        for (const auto& trade : trades) {
            // Apply split/rename adjustments
            auto adjusted = adjustments_.adjust(trade.symbol, trade.trade_date,
                                                trade.quantity, trade.trade_price);

            // Convert to USD
            std::string currency = utils::deduce_currency(adjusted.symbol);
            double trade_price_usd = converter.convert(adjusted.trade_price, currency);
            double proceeds_usd = converter.convert(trade.proceeds, currency);
            double commission_usd = converter.convert(trade.commission, currency);
            double net_cash_usd = converter.convert(trade.net_cash, currency);

            check.reset();
            check.bind(1, account_id);
            check.bind(2, trade.trade_date);
            check.bind(3, adjusted.symbol);
            check.bind(4, adjusted.quantity);
            if (check.executeStep()) {
                skipped++;
                continue;
            }

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, adjusted.symbol);
            insert.bind(3, trade.description);
            insert.bind(4, trade.trade_date);
            insert.bind(5, adjusted.quantity);
            insert.bind(6, trade_price_usd);
            insert.bind(7, proceeds_usd);
            insert.bind(8, commission_usd);
            insert.bind(9, net_cash_usd);
            insert.bind(10, trade.notes_codes);
            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} stock trades ({} duplicates skipped) for account_id: {}",
            imported, skipped, account_id);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to import stock trades", std::string(e.what())};
    }
}

Result<void> TradeRepository::import_dividends(
    int64_t account_id,
    const std::vector<parser::DividendRecord>& dividends) {

    if (dividends.empty()) {
        Logger::info("No dividends to import");
        return Result<void>{};
    }

    Logger::info("Importing {} dividends for account_id: {}", dividends.size(), account_id);

    try {
        utils::CurrencyConverter converter;
        std::string base = converter.get_base_currency();

        SQLite::Transaction transaction(db_);

        // Dedup on identity keys only — amount is USD-converted and would
        // change if FX rates change between imports.
        SQLite::Statement check(db_,
            "SELECT 1 FROM dividends WHERE account_id = ? AND symbol = ? "
            "AND pay_date = ? "
            "LIMIT 1");

        SQLite::Statement insert(db_,
            "INSERT INTO dividends (account_id, symbol, description, ex_date, "
            "pay_date, amount, tax_withheld, currency) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

        int imported = 0;
        int skipped = 0;
        for (auto div : dividends) {
            // Convert to base currency (USD) at import time
            double amount_usd = converter.convert(div.amount, div.currency);
            double tax_usd = converter.convert(div.tax_withheld, div.currency);

            check.reset();
            check.bind(1, account_id);
            check.bind(2, div.symbol);
            check.bind(3, div.pay_date);
            if (check.executeStep()) {
                skipped++;
                continue;
            }

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, div.symbol);
            insert.bind(3, div.description);
            if (div.ex_date.empty()) {
                insert.bind(4);
            } else {
                insert.bind(4, div.ex_date);
            }
            insert.bind(5, div.pay_date);
            insert.bind(6, amount_usd);
            insert.bind(7, tax_usd);
            insert.bind(8, base);
            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} dividends ({} duplicates skipped) for account_id: {}",
            imported, skipped, account_id);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to import dividends", std::string(e.what())};
    }
}

Result<void> TradeRepository::import_interest_expenses(
    int64_t account_id,
    const std::vector<parser::InterestRecord>& interests) {

    if (interests.empty()) {
        Logger::info("No interest records to import");
        return Result<void>{};
    }

    Logger::info("Importing {} interest records for account_id: {}", interests.size(), account_id);

    try {
        utils::CurrencyConverter converter;
        std::string base = converter.get_base_currency();

        SQLite::Transaction transaction(db_);

        // Dedup on identity keys only — amount is USD-converted.
        SQLite::Statement check(db_,
            "SELECT 1 FROM interest_expenses WHERE account_id = ? AND date = ? "
            "AND description = ? "
            "LIMIT 1");

        SQLite::Statement insert(db_,
            "INSERT INTO interest_expenses (account_id, currency, date, "
            "description, amount) "
            "VALUES (?, ?, ?, ?, ?)");

        int imported = 0;
        int skipped = 0;
        for (const auto& rec : interests) {
            double amount_usd = converter.convert(rec.amount, rec.currency);

            check.reset();
            check.bind(1, account_id);
            check.bind(2, rec.date);
            check.bind(3, rec.description);
            if (check.executeStep()) {
                skipped++;
                continue;
            }

            insert.reset();
            insert.bind(1, account_id);
            insert.bind(2, base);
            insert.bind(3, rec.date);
            insert.bind(4, rec.description);
            insert.bind(5, amount_usd);
            insert.exec();
            imported++;
        }

        transaction.commit();

        Logger::info("Imported {} interest records ({} duplicates skipped) for account_id: {}",
            imported, skipped, account_id);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to import interest records", std::string(e.what())};
    }
}

Result<void> TradeRepository::clear_open_positions(int64_t account_id) {
    try {
        SQLite::Statement del(db_, "DELETE FROM open_options WHERE account_id = ?");
        del.bind(1, account_id);
        int deleted = del.exec();

        Logger::info("Cleared {} open positions for account_id: {}", deleted, account_id);
        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{"Failed to clear open positions", std::string(e.what())};
    }
}

Result<int> TradeRepository::dedup_trades(int64_t account_id) {
    try {
        int deleted;
        if (account_id > 0) {
            SQLite::Statement q(db_,
                "DELETE FROM trades WHERE account_id = ? AND id NOT IN ("
                "SELECT MIN(id) FROM trades WHERE account_id = ? "
                "GROUP BY trade_date, symbol, quantity)");
            q.bind(1, account_id);
            q.bind(2, account_id);
            q.exec();
            deleted = db_.getChanges();
        } else {
            deleted = db_.exec(
                "DELETE FROM trades WHERE id NOT IN ("
                "SELECT MIN(id) FROM trades "
                "GROUP BY account_id, trade_date, symbol, quantity)");
        }
        if (deleted > 0) {
            Logger::info("Deduplicated trades: removed {} duplicates", deleted);
        }
        return deleted;
    } catch (const std::exception& e) {
        return Error{"Failed to dedup trades", std::string(e.what())};
    }
}

Result<int> TradeRepository::get_trades_count() {
    try {
        SQLite::Statement query(db_, "SELECT COUNT(*) FROM trades");
        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        }
        return 0;
    } catch (const std::exception& e) {
        return Error{"Failed to get trades count", std::string(e.what())};
    }
}

Result<int> TradeRepository::get_open_positions_count() {
    try {
        SQLite::Statement query(db_, "SELECT COUNT(*) FROM open_options");
        if (query.executeStep()) {
            return query.getColumn(0).getInt();
        }
        return 0;
    } catch (const std::exception& e) {
        return Error{"Failed to get open positions count", std::string(e.what())};
    }
}

Result<std::vector<std::string>> TradeRepository::get_stock_holding_symbols() {
    try {
        SQLite::Statement query(db_,
            "SELECT symbol FROM stock_trades "
            "GROUP BY symbol "
            "HAVING SUM(quantity) > 0");
        std::vector<std::string> symbols;
        while (query.executeStep()) {
            symbols.push_back(query.getColumn(0).getString());
        }
        return symbols;
    } catch (const std::exception& e) {
        return Error{"Failed to get stock holding symbols", std::string(e.what())};
    }
}

} // namespace ibkr::db
