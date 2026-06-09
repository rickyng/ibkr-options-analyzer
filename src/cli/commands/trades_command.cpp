#include "trades_command.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include "utils/currency.hpp"
#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;
using json = nlohmann::json;

Result<void> TradesCommand::execute(
    const config::Config& config,
    const std::string& date_from,
    const std::string& date_to,
    const std::string& underlying,
    const std::string& account,
    const utils::OutputOptions& output_opts) {

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    int64_t account_id = 0;
    if (!account.empty()) {
        auto accounts_result = database.accounts().list_accounts();
        if (accounts_result) {
            for (const auto& acc : *accounts_result) {
                if (acc.name == account) {
                    account_id = acc.id;
                    break;
                }
            }
            if (account_id == 0) {
                return Error{"Account not found", account};
            }
        }
    }

    auto* db = &database.connection();
    if (!db) {
        return Error{"Database not available"};
    }

    // Build option trades query
    {
        std::string sql =
            "SELECT t.trade_date, t.symbol, t.underlying, t.right, t.strike, t.expiry, "
            "t.quantity, t.trade_price, t.proceeds, t.commission, t.net_cash, "
            "t.multiplier, t.notes_codes, COALESCE(a.name, '') "
            "FROM trades t LEFT JOIN accounts a ON a.id = t.account_id WHERE 1=1";
        if (account_id > 0) sql += " AND t.account_id = ?";
        if (!date_from.empty()) sql += " AND t.trade_date >= ?";
        if (!date_to.empty()) sql += " AND t.trade_date <= ?";
        if (!underlying.empty()) sql += " AND t.underlying = ?";
        sql += " ORDER BY t.trade_date DESC";

        SQLite::Statement q(*db, sql);
        int idx = 1;
        if (account_id > 0) q.bind(idx++, account_id);
        if (!date_from.empty()) q.bind(idx++, date_from);
        if (!date_to.empty()) q.bind(idx++, date_to);
        if (!underlying.empty()) q.bind(idx++, underlying);

        utils::CurrencyConverter converter;
        json opt_arr = json::array();
        double total_premium_in = 0;
        double total_premium_out = 0;
        double total_commissions = 0;
        int total_trades = 0;

        while (q.executeStep()) {
            total_trades++;
            std::string opt_underlying = q.getColumn(2).getString();
            std::string opt_currency = utils::deduce_currency(opt_underlying);
            double proceeds = converter.convert(q.getColumn(8).getDouble(), opt_currency);
            double commission = converter.convert(q.getColumn(9).getDouble(), opt_currency);
            double net_cash = converter.convert(q.getColumn(10).getDouble(), opt_currency);
            total_commissions += commission;
            if (net_cash > 0) total_premium_in += net_cash;
            else total_premium_out += std::abs(net_cash);

            opt_arr.push_back({
                {"date", q.getColumn(0).getString()},
                {"symbol", q.getColumn(1).getString()},
                {"underlying", opt_underlying},
                {"right", q.getColumn(3).getString()},
                {"strike", converter.convert(q.getColumn(4).getDouble(), opt_currency)},
                {"expiry", q.getColumn(5).getString()},
                {"quantity", q.getColumn(6).getDouble()},
                {"trade_price", converter.convert(q.getColumn(7).getDouble(), opt_currency)},
                {"proceeds", proceeds},
                {"commission", commission},
                {"net_cash", net_cash},
                {"multiplier", q.getColumn(11).getDouble()},
                {"notes_codes", q.getColumn(12).getString()},
                {"account", q.getColumn(13).getString()},
                {"currency", "USD"}
            });
        }

        // Build stock trades query
        std::string stock_sql =
            "SELECT st.trade_date, st.symbol, st.description, st.quantity, "
            "st.trade_price, st.proceeds, st.commission, st.net_cash, "
            "st.notes_codes, COALESCE(a.name, '') "
            "FROM stock_trades st LEFT JOIN accounts a ON a.id = st.account_id WHERE 1=1";
        if (account_id > 0) stock_sql += " AND st.account_id = ?";
        if (!date_from.empty()) stock_sql += " AND st.trade_date >= ?";
        if (!date_to.empty()) stock_sql += " AND st.trade_date <= ?";
        if (!underlying.empty()) stock_sql += " AND st.symbol = ?";
        stock_sql += " ORDER BY st.trade_date DESC";

        SQLite::Statement sq(*db, stock_sql);
        idx = 1;
        if (account_id > 0) sq.bind(idx++, account_id);
        if (!date_from.empty()) sq.bind(idx++, date_from);
        if (!date_to.empty()) sq.bind(idx++, date_to);
        if (!underlying.empty()) sq.bind(idx++, underlying);

        json stock_arr = json::array();
        while (sq.executeStep()) {
            std::string sym = sq.getColumn(1).getString();
            std::string stock_currency = utils::deduce_currency(sym);
            double proceeds_raw = sq.getColumn(5).getDouble();
            double comm_raw = sq.getColumn(6).getDouble();
            double net_raw = sq.getColumn(7).getDouble();
            double proceeds_usd = converter.convert(proceeds_raw, stock_currency);
            double comm_usd = converter.convert(comm_raw, stock_currency);
            double net_usd = converter.convert(net_raw, stock_currency);

            stock_arr.push_back({
                {"date", sq.getColumn(0).getString()},
                {"symbol", sym},
                {"description", sq.getColumn(2).getString()},
                {"quantity", sq.getColumn(3).getDouble()},
                {"trade_price", converter.convert(sq.getColumn(4).getDouble(), stock_currency)},
                {"proceeds", proceeds_usd},
                {"commission", comm_usd},
                {"net_cash", net_usd},
                {"notes_codes", sq.getColumn(8).getString()},
                {"account", sq.getColumn(9).getString()},
                {"currency", "USD"}
            });
        }

        json output;
        output["overview"] = {
            {"total_trades", total_trades},
            {"total_premium_in", total_premium_in},
            {"total_premium_out", total_premium_out},
            {"net_premium", total_premium_in - total_premium_out},
            {"total_commissions", total_commissions}
        };
        output["option_trades"] = opt_arr;
        output["stock_trades"] = stock_arr;

        if (output_opts.json) {
            std::cout << output.dump(2) << "\n";
        } else {
            const auto& ov = output["overview"];
            std::cout << "\n=== Trade Summary ===\n\n";
            std::cout << "Total Trades:      " << ov["total_trades"] << "\n";
            std::cout << "Premium In:        $" << std::fixed << std::setprecision(2) << total_premium_in << "\n";
            std::cout << "Premium Out:       $" << std::fixed << std::setprecision(2) << total_premium_out << "\n";
            std::cout << "Net Premium:       $" << std::fixed << std::setprecision(2) << (total_premium_in - total_premium_out) << "\n";
            std::cout << "Total Commissions: $" << std::fixed << std::setprecision(2) << total_commissions << "\n";
        }
    }

    return Result<void>{};
}

} // namespace ibkr::commands
