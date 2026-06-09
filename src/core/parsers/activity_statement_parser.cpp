#include "activity_statement_parser.hpp"
#include "utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <regex>
#include <algorithm>
#include <filesystem>

namespace ibkr::parser {

using utils::Result;
using utils::Error;
using utils::Logger;

namespace {

std::string csv_unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> result;
    std::string field;
    bool in_quotes = false;

    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
            field += c;
        } else if (c == ',' && !in_quotes) {
            result.push_back(csv_unquote(field));
            field.clear();
        } else {
            field += c;
        }
    }
    result.push_back(csv_unquote(field));
    return result;
}

std::string get_val(const std::map<std::string, std::string>& row,
                    const std::string& key,
                    const std::string& def = "") {
    auto it = row.find(key);
    return it != row.end() ? it->second : def;
}

} // namespace

Result<ActivityParseResult> ActivityStatementParser::parse_file(
    const std::string& file_path) {

    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return Error{"Cannot open file", file_path};
    }

    ActivityParseResult result;
    std::string line;

    std::vector<std::string> trade_headers;
    bool in_trades = false;

    std::vector<std::string> dividend_headers;
    bool in_dividends = false;

    std::vector<std::string> interest_headers;
    bool in_interest = false;

    while (std::getline(ifs, line)) {
        if (line.empty()) continue;

        auto cols = split_csv_line(line);
        if (cols.empty()) continue;

        const std::string& section = cols[0];

        // --- Trades section ---
        if (section == "Trades" && cols.size() >= 2 && cols[1] == "Header") {
            in_trades = true;
            in_dividends = false;
            trade_headers = cols;
            continue;
        }

        if (in_trades && section != "Trades") {
            in_trades = false;
            trade_headers.clear();
        }

        if (in_trades && cols.size() >= 2 && cols[1] == "Data") {
            std::map<std::string, std::string> row;
            for (size_t i = 0; i < trade_headers.size() && i < cols.size(); ++i) {
                row[trade_headers[i]] = cols[i];
            }

            // Skip "Order" rows — keep only "Trade" rows
            if (get_val(row, "DataDiscriminator") == "Order") continue;

            const std::string asset_cat = get_val(row, "Asset Category");

            if (asset_cat == "Equity and Index Options") {
                result.total_rows++;
                result.option_rows++;

                auto trade_res = parse_trade_row(row);
                if (trade_res) {
                    result.trades.push_back(std::move(*trade_res));
                } else {
                    result.skipped_rows++;
                    Logger::debug("Skipped trade row: {}", trade_res.error().message);
                }
            } else if (asset_cat == "Stocks") {
                result.total_rows++;
                result.stock_rows++;

                auto stock_res = parse_stock_trade_row(row);
                if (stock_res) {
                    result.stock_trades.push_back(std::move(*stock_res));
                } else {
                    result.skipped_rows++;
                    Logger::debug("Skipped stock trade row: empty symbol");
                }
            } else {
                result.non_option_rows++;
            }
            continue;
        }

        // --- Dividends section ---
        if (section == "Dividends" && cols.size() >= 2 && cols[1] == "Header") {
            in_dividends = true;
            in_trades = false;
            in_interest = false;
            dividend_headers = cols;
            continue;
        }

        if (in_dividends && section != "Dividends") {
            in_dividends = false;
            dividend_headers.clear();
        }

        if (in_dividends && cols.size() >= 2 && cols[1] == "Data") {
            std::map<std::string, std::string> row;
            for (size_t i = 0; i < dividend_headers.size() && i < cols.size(); ++i) {
                row[dividend_headers[i]] = cols[i];
            }

            result.total_rows++;
            result.dividend_rows++;

            auto div_res = parse_dividend_row(row);
            if (div_res) {
                result.dividends.push_back(std::move(*div_res));
            } else {
                result.skipped_rows++;
                Logger::debug("Skipped dividend row: missing symbol or date");
            }
            continue;
        }

        // --- Interest section ---
        if (section == "Interest" && cols.size() >= 2 && cols[1] == "Header") {
            in_interest = true;
            in_trades = false;
            in_dividends = false;
            interest_headers = cols;
            continue;
        }

        if (in_interest && section != "Interest") {
            in_interest = false;
            interest_headers.clear();
        }

        if (in_interest && cols.size() >= 2 && cols[1] == "Data") {
            std::map<std::string, std::string> row;
            for (size_t i = 0; i < interest_headers.size() && i < cols.size(); ++i) {
                row[interest_headers[i]] = cols[i];
            }

            auto int_res = parse_interest_row(row);
            if (int_res) {
                result.total_rows++;
                result.interest_rows++;
                result.interests.push_back(std::move(*int_res));
            }
            continue;
        }
    }

    Logger::info("Activity Statement parsed: {} option trades, {} stock trades, {} dividends, {} interests, {} skipped",
                 result.trades.size(), result.stock_trades.size(), result.dividends.size(), result.interests.size(), result.skipped_rows);
    return result;
}

Result<TradeRecord> ActivityStatementParser::parse_trade_row(
    const std::map<std::string, std::string>& row) {

    const std::string symbol = get_val(row, "Symbol");
    const std::string datetime = get_val(row, "Date/Time");
    const std::string quantity_str = get_val(row, "Quantity");
    const std::string price_str = get_val(row, "T. Price");
    const std::string proceeds_str = get_val(row, "Proceeds");
    const std::string comm_str = get_val(row, "Comm/Fee");
    const std::string code = get_val(row, "Code");

    if (symbol.empty()) {
        return Error{"Empty symbol in trade row"};
    }

    auto opt = parse_activity_option_symbol(symbol);
    if (opt.underlying.empty()) {
        return Error{"Cannot parse option symbol", symbol};
    }

    TradeRecord rec;
    rec.trade_date = parse_trade_date(datetime);
    rec.symbol = symbol;
    rec.underlying_symbol = opt.underlying;
    rec.expiry = opt.expiry;
    rec.strike = opt.strike;
    rec.put_call = (opt.right == 'C') ? "C" : "P";
    rec.quantity = parse_double(quantity_str);
    rec.trade_price = parse_double(price_str);
    rec.proceeds = parse_double(proceeds_str);
    rec.commission = parse_double(comm_str);
    rec.net_cash = rec.proceeds - rec.commission;
    rec.notes_codes = code;
    rec.asset_class = "OPT";
    rec.multiplier = 100.0;
    rec.option_details = opt;

    return rec;
}

std::optional<StockTradeRecord> ActivityStatementParser::parse_stock_trade_row(
    const std::map<std::string, std::string>& row) {

    const std::string symbol = get_val(row, "Symbol");
    if (symbol.empty()) {
        return std::nullopt;
    }

    const std::string datetime = get_val(row, "Date/Time");
    const std::string quantity_str = get_val(row, "Quantity");
    const std::string price_str = get_val(row, "T. Price");
    const std::string proceeds_str = get_val(row, "Proceeds");
    const std::string comm_str = get_val(row, "Comm/Fee");
    const std::string code = get_val(row, "Code");
    const std::string desc = get_val(row, "Description");

    StockTradeRecord rec;
    rec.symbol = symbol;
    rec.description = desc;
    rec.trade_date = parse_trade_date(datetime);
    rec.quantity = parse_double(quantity_str);
    rec.trade_price = parse_double(price_str);
    rec.proceeds = parse_double(proceeds_str);
    rec.commission = parse_double(comm_str);
    rec.net_cash = rec.proceeds - rec.commission;
    rec.notes_codes = code;

    return rec;
}

std::optional<DividendRecord> ActivityStatementParser::parse_dividend_row(
    const std::map<std::string, std::string>& row) {

    // Symbol: try explicit "Symbol" column first, then extract from Description
    // IBKR Activity Statements embed the symbol in Description like:
    //   "1800(CNE1000002F5) Cash Dividend HKD 0.15353 per Share (Ordinary Dividend)"
    std::string symbol = get_val(row, "Symbol");
    const std::string desc = get_val(row, "Description");

    if (symbol.empty() && !desc.empty()) {
        // Extract leading ticker before '(' or space
        static const std::regex symbol_re(R"(^([A-Za-z0-9.]+)(?:\(|$))");
        std::smatch m;
        if (std::regex_search(desc, m, symbol_re) && m[1].matched) {
            symbol = m[1].str();
        }
    }
    if (symbol.empty()) {
        return std::nullopt;
    }

    // Pay date: try "Pay Date" first, then "Date"
    std::string pay_date = get_val(row, "Pay Date");
    if (pay_date.empty()) {
        pay_date = get_val(row, "Date");
    }
    if (pay_date.empty()) {
        return std::nullopt;
    }

    // Ex-date: try "Ex-Date" if available
    std::string ex_date = get_val(row, "Ex-Date");

    const std::string amount_str = get_val(row, "Amount");
    const std::string tax_str = get_val(row, "Tax");

    // Currency from "Currency" column if available
    std::string currency = get_val(row, "Currency");
    if (currency.empty()) {
        currency = "USD";
    }

    DividendRecord rec;
    rec.symbol = symbol;
    rec.description = desc;
    rec.ex_date = ex_date;
    rec.pay_date = pay_date;
    rec.amount = parse_double(amount_str);
    rec.tax_withheld = parse_double(tax_str);
    rec.currency = currency;

    return rec;
}

std::optional<InterestRecord> ActivityStatementParser::parse_interest_row(
    const std::map<std::string, std::string>& row) {

    const std::string currency = get_val(row, "Currency");
    // Skip summary rows: "Total", "Total in USD", "Total Interest in USD"
    if (currency.empty() || currency.find("Total") != std::string::npos) {
        return std::nullopt;
    }

    const std::string date_str = get_val(row, "Date");
    const std::string desc = get_val(row, "Description");
    const std::string amount_str = get_val(row, "Amount");

    InterestRecord rec;
    rec.currency = currency;
    rec.date = date_str;
    rec.description = desc;
    rec.amount = parse_double(amount_str);

    return rec;
}

OptionDetails ActivityStatementParser::parse_activity_option_symbol(
    const std::string& symbol) {

    // Format: "AAPL 05JAN24 210 C" or "1321 27JUN25 20000 P"
    static const std::regex pattern(R"((.+?)\s+(\d{1,2}[A-Z]{3}\d{2})\s+([\d.]+)\s*([CP]))");
    std::smatch m;
    if (!std::regex_match(symbol, m, pattern)) {
        Logger::debug("Activity option symbol regex failed: '{}'", symbol);
        return {};
    }

    OptionDetails opt;
    opt.underlying = m[1].str();
    opt.original_symbol = symbol;
    opt.right = m[4].str()[0];
    opt.strike = std::stod(m[3].str());

    // Convert DDMMMYY to YYYY-MM-DD
    const std::string& date_str = m[2].str();

    // Handle both "05JAN24" (2-digit day) and "5JAN24" (1-digit day)
    // Find where month starts
    size_t month_pos = date_str.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    if (month_pos == std::string::npos || month_pos == 0) return {};

    int day = std::stoi(date_str.substr(0, month_pos));
    std::string month_str = date_str.substr(month_pos, 3);
    int year_short = std::stoi(date_str.substr(month_pos + 3));

    static const std::map<std::string, int> months = {
        {"JAN", 1}, {"FEB", 2}, {"MAR", 3}, {"APR", 4},
        {"MAY", 5}, {"JUN", 6}, {"JUL", 7}, {"AUG", 8},
        {"SEP", 9}, {"OCT", 10}, {"NOV", 11}, {"DEC", 12}
    };

    auto month_it = months.find(month_str);
    if (month_it == months.end()) {
        Logger::debug("Unknown month in option symbol: '{}'", month_str);
        return {};
    }

    int month = month_it->second;
    int year = year_short + 2000;

    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    opt.expiry = buf;

    return opt;
}

std::string ActivityStatementParser::parse_trade_date(const std::string& datetime) {
    // Format: "2024-01-05, 16:20:00" — just take the date part
    auto comma = datetime.find(',');
    if (comma != std::string::npos) {
        std::string date = datetime.substr(0, comma);
        date.erase(0, date.find_first_not_of(" \t"));
        date.erase(date.find_last_not_of(" \t") + 1);
        return date;
    }
    return datetime;
}

std::string ActivityStatementParser::extract_account_from_filename(
    const std::string& filename) {

    // Pattern: U5668308_20240101_20241213.csv
    static const std::regex pattern(R"((U\d+)_\d{8}_\d{8})");
    std::smatch m;
    std::string fname = std::filesystem::path(filename).filename().string();
    if (std::regex_search(fname, m, pattern)) {
        return m[1].str();
    }
    return "";
}

double ActivityStatementParser::parse_double(const std::string& s) const {
    if (s.empty() || s == "-" || s == "--") return 0.0;
    // Strip comma thousands separators (e.g., "1,000" → "1000")
    // so std::stod doesn't stop at the comma and parse "-1,000" as -1.
    std::string clean;
    clean.reserve(s.size());
    for (char c : s) {
        if (c != ',') clean += c;
    }
    try {
        return std::stod(clean);
    } catch (...) {
        return 0.0;
    }
}

} // namespace ibkr::parser
