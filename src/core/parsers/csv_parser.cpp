#include "csv_parser.hpp"
#include "utils/logger.hpp"
#include <rapidcsv.h>
#include <fstream>
#include <sstream>

namespace ibkr::parser {

// Convert YYYYMMDD to YYYY-MM-DD format
static std::string format_expiry_date(const std::string& yyyymmdd) {
    if (yyyymmdd.length() == 8) {
        return yyyymmdd.substr(0, 4) + "-" + yyyymmdd.substr(4, 2) + "-" + yyyymmdd.substr(6, 2);
    }
    return yyyymmdd;  // Return as-is if not 8 digits
}

using utils::Result;
using utils::Error;
using utils::Logger;

Result<CSVParser::ParseResult> CSVParser::parse_file(
    const std::string& file_path,
    bool filter_options_only,
    bool filter_non_expired) {

    Logger::info("Parsing CSV file: {}", file_path);

    // Read file content
    std::ifstream file(file_path);
    if (!file) {
        return Error{"Failed to open file", file_path};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    return parse_content(content, filter_options_only, filter_non_expired);
}

Result<CSVParser::ParseResult> CSVParser::parse_content(
    const std::string& csv_content,
    bool filter_options_only,
    bool filter_non_expired) {

    if (csv_content.empty()) {
        return Error{"Empty CSV content"};
    }

    ParseResult result;

    try {
        // Parse CSV using rapidcsv
        std::stringstream ss(csv_content);
        rapidcsv::Document doc(ss, rapidcsv::LabelParams(0, -1));

        // Get column names
        std::vector<std::string> column_names = doc.GetColumnNames();
        Logger::debug("CSV has {} columns", column_names.size());

        // Get row count
        size_t row_count = doc.GetRowCount();
        Logger::info("CSV has {} rows", row_count);

        result.total_rows = static_cast<int>(row_count);

        // Parse each row
        for (size_t i = 0; i < row_count; ++i) {
            // Build row map
            std::map<std::string, std::string> row_map;
            for (const auto& col_name : column_names) {
                try {
                    std::string value = doc.GetCell<std::string>(col_name, i);
                    row_map[col_name] = value;
                } catch (...) {
                    row_map[col_name] = "";
                }
            }

            // Skip duplicate header rows (IBKR Flex reports can repeat headers mid-file)
            {
                auto it = row_map.find("UnderlyingSymbol");
                if (it != row_map.end() && it->second == "UnderlyingSymbol") {
                    result.skipped_rows++;
                    continue;
                }
            }

            // Check if this is an option row
            bool is_option = is_option_row(row_map);

            if (filter_options_only && !is_option) {
                result.skipped_rows++;
                result.non_option_rows++;
                continue;
            }

            if (is_option) {
                result.option_rows++;
            } else {
                result.non_option_rows++;
            }

            // Skip trade execution rows (they have LevelOfDetail = EXECUTION)
            // These are trade history, not open positions
            std::string level_of_detail = get_column(row_map, "LevelOfDetail");
            if (level_of_detail == "EXECUTION") {
                Logger::debug("Skipping trade execution row for: {}", get_column(row_map, "Symbol"));
                result.skipped_rows++;
                continue;
            }

            // Also skip rows where Quantity looks like a date (> 1000)
            // This catches any malformed data that slipped through
            double quantity = parse_double(get_column(row_map, "Quantity"));
            if (std::abs(quantity) > 10000) {
                Logger::debug("Skipping row with invalid quantity: {} (quantity={})",
                             get_column(row_map, "Symbol"), quantity);
                result.skipped_rows++;
                continue;
            }

            // Try to parse as open position (has MarkPrice, PositionValue)
            if (row_map.count("MarkPrice") && !row_map["MarkPrice"].empty()) {
                auto pos_result = parse_position_row(row_map);
                if (pos_result) {
                    // Check if expired
                    if (filter_non_expired && pos_result->option_details) {
                        if (OptionSymbolParser::is_expired(pos_result->option_details->expiry)) {
                            Logger::debug("Skipping expired option: {} (expiry: {})",
                                        pos_result->symbol, pos_result->option_details->expiry);
                            result.skipped_rows++;
                            continue;
                        }
                    }
                    result.open_positions.push_back(*pos_result);
                } else {
                    Logger::warn("Failed to parse position row {}: {}", i, pos_result.error().message);
                }
            }
            // Try to parse as trade (has TradeDate, TradePrice)
            else if (row_map.count("TradeDate") && !row_map["TradeDate"].empty()) {
                auto trade_result = parse_trade_row(row_map);
                if (trade_result) {
                    // Check if expired
                    if (filter_non_expired && trade_result->option_details) {
                        if (OptionSymbolParser::is_expired(trade_result->option_details->expiry)) {
                            Logger::debug("Skipping expired option trade: {} (expiry: {})",
                                        trade_result->symbol, trade_result->option_details->expiry);
                            result.skipped_rows++;
                            continue;
                        }
                    }
                    result.trades.push_back(*trade_result);
                } else {
                    Logger::warn("Failed to parse trade row {}: {}", i, trade_result.error().message);
                }
            }
        }

        Logger::info("Parsed {} trades, {} open positions (skipped {} rows)",
                    result.trades.size(), result.open_positions.size(), result.skipped_rows);
        Logger::info("Option rows: {}, Non-option rows: {}",
                    result.option_rows, result.non_option_rows);

        return result;

    } catch (const std::exception& e) {
        return Error{
            "CSV parse error",
            std::string(e.what())
        };
    }
}

Result<TradeRecord> CSVParser::parse_trade_row(
    const std::map<std::string, std::string>& row) {

    TradeRecord trade;

    trade.account_id = get_column(row, "ClientAccountID");
    trade.trade_date = get_column(row, "TradeDate");
    trade.symbol = get_column(row, "Symbol");
    trade.description = get_column(row, "Description");
    trade.underlying_symbol = get_column(row, "UnderlyingSymbol");
    trade.expiry = get_column(row, "Expiry");
    trade.strike = parse_double(get_column(row, "Strike"));
    trade.put_call = get_column(row, "Put/Call");
    trade.quantity = parse_double(get_column(row, "Quantity"));
    trade.trade_price = parse_double(get_column(row, "TradePrice"));
    trade.proceeds = parse_double(get_column(row, "Proceeds"));
    trade.commission = parse_double(get_column(row, "Commission"));
    trade.net_cash = parse_double(get_column(row, "NetCash"));
    trade.asset_class = get_column(row, "AssetClass");

    // Try to parse option details from symbol, fall back to CSV columns
    if (OptionSymbolParser::is_option_symbol(trade.symbol)) {
        auto option_result = OptionSymbolParser::parse(trade.symbol);
        if (option_result) {
            trade.option_details = *option_result;
        }
    }
    if (!trade.option_details && !trade.put_call.empty()
        && (trade.put_call[0] == 'C' || trade.put_call[0] == 'P')
        && !trade.underlying_symbol.empty() && !trade.expiry.empty()) {
        OptionDetails details;
        details.underlying = trade.underlying_symbol;
        details.expiry = format_expiry_date(trade.expiry);
        details.strike = trade.strike;
        details.right = trade.put_call[0];
        details.original_symbol = trade.symbol;
        trade.option_details = details;
    }

    return trade;
}

Result<OpenPositionRecord> CSVParser::parse_position_row(
    const std::map<std::string, std::string>& row) {

    OpenPositionRecord position;

    position.account_id = get_column(row, "ClientAccountID");
    position.symbol = get_column(row, "Symbol");
    position.description = get_column(row, "Description");
    position.underlying_symbol = get_column(row, "UnderlyingSymbol");
    position.expiry = get_column(row, "Expiry");
    position.strike = parse_double(get_column(row, "Strike"));
    position.put_call = get_column(row, "Put/Call");
    position.quantity = parse_double(get_column(row, "Quantity"));
    position.mark_price = parse_double(get_column(row, "MarkPrice"));
    position.position_value = parse_double(get_column(row, "PositionValue"));
    position.open_price = parse_double(get_column(row, "OpenPrice"));
    position.cost_basis_price = parse_double(get_column(row, "CostBasisPrice"));
    position.cost_basis_money = parse_double(get_column(row, "CostBasisMoney"));
    position.unrealized_pnl = parse_double(get_column(row, "FifoPnlUnrealized"));
    position.asset_class = get_column(row, "AssetClass");
    position.report_date = get_column(row, "ReportDate");
    position.multiplier = parse_double(get_column(row, "Multiplier"));
    if (position.multiplier == 0.0) {
        position.multiplier = 100.0;
    }

    // Try to parse option details from symbol, fall back to CSV columns
    if (OptionSymbolParser::is_option_symbol(position.symbol)) {
        auto option_result = OptionSymbolParser::parse(position.symbol);
        if (option_result) {
            position.option_details = *option_result;
        }
    }
    if (!position.option_details && !position.put_call.empty()
        && (position.put_call[0] == 'C' || position.put_call[0] == 'P')
        && !position.underlying_symbol.empty() && !position.expiry.empty()) {
        OptionDetails details;
        details.underlying = position.underlying_symbol;
        details.expiry = format_expiry_date(position.expiry);
        details.strike = position.strike;
        details.right = position.put_call[0];
        details.original_symbol = position.symbol;
        position.option_details = details;
    }

    return position;
}

bool CSVParser::is_option_row(const std::map<std::string, std::string>& row) {
    // Check AssetClass column
    auto it = row.find("AssetClass");
    if (it != row.end() && it->second == "OPT") {
        return true;
    }

    // Check if symbol looks like an option
    auto symbol_it = row.find("Symbol");
    if (symbol_it != row.end()) {
        return OptionSymbolParser::is_option_symbol(symbol_it->second);
    }

    return false;
}

std::string CSVParser::get_column(
    const std::map<std::string, std::string>& row,
    const std::string& column_name,
    const std::string& default_value) const {

    auto it = row.find(column_name);
    if (it != row.end()) {
        return it->second;
    }
    return default_value;
}

double CSVParser::parse_double(const std::string& str) const {
    if (str.empty()) {
        return 0.0;
    }

    try {
        return std::stod(str);
    } catch (const std::exception& e) {
        Logger::warn("Failed to parse double from '{}': {}", str, e.what());
        return 0.0;
    }
}

} // namespace ibkr::parser
