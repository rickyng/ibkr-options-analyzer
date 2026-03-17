#pragma once

#include "utils/result.hpp"
#include "option_symbol_parser.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace ibkr::parser {

/**
 * Parsed trade record from IBKR Flex CSV.
 */
struct TradeRecord {
    std::string account_id;
    std::string trade_date;
    std::string symbol;
    std::string description;
    std::string underlying_symbol;
    std::string expiry;
    double strike{0.0};
    std::string put_call;
    double quantity{0.0};
    double trade_price{0.0};
    double proceeds{0.0};
    double commission{0.0};
    double net_cash{0.0};
    std::string asset_class;

    // Parsed option details (if applicable)
    std::optional<OptionDetails> option_details;
};

/**
 * Parsed open position record from IBKR Flex CSV.
 */
struct OpenPositionRecord {
    std::string account_id;
    std::string symbol;
    std::string description;
    std::string underlying_symbol;
    std::string expiry;
    double strike{0.0};
    std::string put_call;
    double quantity{0.0};
    double mark_price{0.0};
    double position_value{0.0};
    double open_price{0.0};
    double cost_basis_price{0.0};
    double cost_basis_money{0.0};
    double unrealized_pnl{0.0};
    std::string asset_class;
    std::string report_date;

    // Parsed option details (if applicable)
    std::optional<OptionDetails> option_details;
};

/**
 * CSV parser for IBKR Flex reports.
 *
 * Parses CSV files downloaded from IBKR Flex Web Service.
 * Extracts trades and open positions, with special handling for options.
 *
 * Usage:
 *   CSVParser parser;
 *   auto result = parser.parse_file("/path/to/flex_report.csv");
 *   if (result) {
 *       for (const auto& trade : result->trades) {
 *           // Process trade
 *       }
 *       for (const auto& position : result->open_positions) {
 *           // Process position
 *       }
 *   }
 */
class CSVParser {
public:
    struct ParseResult {
        std::vector<TradeRecord> trades;
        std::vector<OpenPositionRecord> open_positions;
        int total_rows{0};
        int skipped_rows{0};
        int option_rows{0};
        int non_option_rows{0};
    };

    /**
     * Parse IBKR Flex CSV file.
     *
     * @param file_path Path to CSV file
     * @param filter_options_only If true, only parse option records (AssetClass=OPT)
     * @param filter_non_expired If true, skip expired options
     * @return Result containing ParseResult or Error
     */
    utils::Result<ParseResult> parse_file(
        const std::string& file_path,
        bool filter_options_only = false,
        bool filter_non_expired = true);

    /**
     * Parse CSV content from string.
     *
     * @param csv_content CSV content as string
     * @param filter_options_only If true, only parse option records
     * @param filter_non_expired If true, skip expired options
     * @return Result containing ParseResult or Error
     */
    utils::Result<ParseResult> parse_content(
        const std::string& csv_content,
        bool filter_options_only = false,
        bool filter_non_expired = true);

private:
    /**
     * Parse a single row into TradeRecord.
     */
    utils::Result<TradeRecord> parse_trade_row(
        const std::map<std::string, std::string>& row);

    /**
     * Parse a single row into OpenPositionRecord.
     */
    utils::Result<OpenPositionRecord> parse_position_row(
        const std::map<std::string, std::string>& row);

    /**
     * Check if row is an option (AssetClass=OPT or symbol is option format).
     */
    bool is_option_row(const std::map<std::string, std::string>& row);

    /**
     * Get column value with default.
     */
    std::string get_column(
        const std::map<std::string, std::string>& row,
        const std::string& column_name,
        const std::string& default_value = "") const;

    /**
     * Parse double from string, return 0.0 on error.
     */
    double parse_double(const std::string& str) const;
};

} // namespace ibkr::parser
