#pragma once

#include "utils/result.hpp"
#include <string>
#include <vector>
#include <optional>

namespace ibkr::parser {

/**
 * Parsed option details from IBKR symbol.
 *
 * IBKR option symbols come in multiple formats:
 * - "AAPL  250321P00150000" (with spaces, 8-digit strike)
 * - "AAPL250321P150" (compact, decimal strike)
 * - "AAPL 250321P150" (single space, decimal strike)
 */
struct OptionDetails {
    std::string underlying;      // e.g., "AAPL"
    std::string expiry;          // YYYY-MM-DD format (e.g., "2025-03-21")
    double strike;               // Strike price (e.g., 150.0)
    char right;                  // 'C' for Call, 'P' for Put
    std::string original_symbol; // Original IBKR symbol
};

/**
 * Option symbol parser for IBKR format.
 *
 * Parses IBKR option symbols and extracts:
 * - Underlying symbol
 * - Expiry date (converts YYMMDD to YYYY-MM-DD)
 * - Strike price (handles 8-digit format: divide by 1000)
 * - Option right (C/P)
 *
 * Usage:
 *   auto result = OptionSymbolParser::parse("AAPL  250321P00150000");
 *   if (result) {
 *       std::cout << "Underlying: " << result->underlying << "\n";
 *       std::cout << "Expiry: " << result->expiry << "\n";
 *       std::cout << "Strike: " << result->strike << "\n";
 *       std::cout << "Right: " << result->right << "\n";
 *   }
 */
class OptionSymbolParser {
public:
    /**
     * Parse IBKR option symbol.
     *
     * @param symbol IBKR option symbol (e.g., "AAPL  250321P00150000")
     * @return Result containing OptionDetails or Error
     */
    static utils::Result<OptionDetails> parse(const std::string& symbol);

    /**
     * Check if symbol is an option (contains expiry date pattern).
     *
     * @param symbol Symbol to check
     * @return true if symbol appears to be an option
     */
    static bool is_option_symbol(const std::string& symbol);

    /**
     * Check if option is expired (expiry <= current date).
     *
     * @param expiry_date Expiry date in YYYY-MM-DD format
     * @return true if option is expired
     */
    static bool is_expired(const std::string& expiry_date);

private:
    /**
     * Parse format 1: "AAPL  250321P00150000" (with spaces, 8-digit strike)
     */
    static utils::Result<OptionDetails> parse_format1(const std::string& symbol);

    /**
     * Parse format 2: "AAPL250321P150" (compact, decimal strike)
     */
    static utils::Result<OptionDetails> parse_format2(const std::string& symbol);

    /**
     * Convert YYMMDD to YYYY-MM-DD.
     *
     * @param yymmdd Date in YYMMDD format (e.g., "250321")
     * @return Date in YYYY-MM-DD format (e.g., "2025-03-21")
     */
    static std::string convert_date_format(const std::string& yymmdd);

    /**
     * Extract underlying symbol from option symbol.
     * Handles symbols with spaces and special characters.
     */
    static std::string extract_underlying(const std::string& symbol);
};

} // namespace ibkr::parser
