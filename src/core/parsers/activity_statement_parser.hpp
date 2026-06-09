#pragma once

#include "csv_parser.hpp"
#include "trade_types.hpp"
#include <map>
#include <string>
#include <vector>
#include <optional>

namespace ibkr::parser {

struct ActivityParseResult {
    std::vector<TradeRecord> trades;
    int option_rows = 0;

    std::vector<StockTradeRecord> stock_trades;
    int stock_rows = 0;

    std::vector<DividendRecord> dividends;
    int dividend_rows = 0;

    std::vector<InterestRecord> interests;
    int interest_rows = 0;

    int total_rows = 0;
    int skipped_rows = 0;
    int non_option_rows = 0;
};

class ActivityStatementParser {
public:
    utils::Result<ActivityParseResult> parse_file(const std::string& file_path);

    static std::string extract_account_from_filename(const std::string& filename);

private:
    utils::Result<TradeRecord> parse_trade_row(
        const std::map<std::string, std::string>& row);

    std::optional<StockTradeRecord> parse_stock_trade_row(
        const std::map<std::string, std::string>& row);

    std::optional<DividendRecord> parse_dividend_row(
        const std::map<std::string, std::string>& row);

    std::optional<InterestRecord> parse_interest_row(
        const std::map<std::string, std::string>& row);

    OptionDetails parse_activity_option_symbol(const std::string& symbol);

    std::string parse_trade_date(const std::string& datetime);

    double parse_double(const std::string& s) const;
};

} // namespace ibkr::parser
