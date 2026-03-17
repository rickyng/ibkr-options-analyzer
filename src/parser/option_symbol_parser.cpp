#include "option_symbol_parser.hpp"
#include "utils/logger.hpp"
#include <regex>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <date/date.h>

namespace ibkr::parser {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<OptionDetails> OptionSymbolParser::parse(const std::string& symbol) {
    if (symbol.empty()) {
        return Error{"Empty symbol"};
    }

    Logger::debug("Parsing option symbol: {}", symbol);

    // Try format 1: "AAPL  250321P00150000" (with spaces, 8-digit strike)
    auto result1 = parse_format1(symbol);
    if (result1) {
        Logger::debug("Parsed as format 1: underlying={}, expiry={}, strike={}, right={}",
                     result1->underlying, result1->expiry, result1->strike, result1->right);
        return result1;
    }

    // Try format 2: "AAPL250321P150" (compact, decimal strike)
    auto result2 = parse_format2(symbol);
    if (result2) {
        Logger::debug("Parsed as format 2: underlying={}, expiry={}, strike={}, right={}",
                     result2->underlying, result2->expiry, result2->strike, result2->right);
        return result2;
    }

    return Error{
        "Failed to parse option symbol",
        "Symbol does not match any known IBKR option format: " + symbol
    };
}

bool OptionSymbolParser::is_option_symbol(const std::string& symbol) {
    // Check for date pattern (6 digits) followed by C or P
    std::regex pattern(R"(\d{6}[CP])");
    return std::regex_search(symbol, pattern);
}

bool OptionSymbolParser::is_expired(const std::string& expiry_date) {
    try {
        // Parse expiry date (YYYY-MM-DD)
        std::istringstream ss(expiry_date);
        date::year_month_day ymd;
        ss >> date::parse("%F", ymd);

        if (ss.fail()) {
            Logger::warn("Failed to parse expiry date: {}", expiry_date);
            return false;
        }

        // Get current date
        auto now = std::chrono::system_clock::now();
        auto today = date::year_month_day{date::floor<date::days>(now)};

        // Option is expired if expiry <= today
        return date::sys_days{ymd} <= date::sys_days{today};

    } catch (const std::exception& e) {
        Logger::warn("Exception checking expiry: {}", e.what());
        return false;
    }
}

Result<OptionDetails> OptionSymbolParser::parse_format1(const std::string& symbol) {
    // Format: "AAPL  250321P00150000"
    // Pattern: <underlying><spaces><YYMMDD><C/P><8-digit strike>

    std::regex pattern(R"(^([A-Z]+)\s+(\d{6})([CP])(\d{8})$)");
    std::smatch matches;

    if (!std::regex_match(symbol, matches, pattern)) {
        return Error{"Does not match format 1"};
    }

    OptionDetails details;
    details.original_symbol = symbol;
    details.underlying = matches[1].str();

    // Convert date from YYMMDD to YYYY-MM-DD
    std::string yymmdd = matches[2].str();
    details.expiry = convert_date_format(yymmdd);

    // Extract right (C or P)
    details.right = matches[3].str()[0];

    // Parse strike (8 digits, divide by 1000)
    std::string strike_str = matches[4].str();
    try {
        long strike_int = std::stol(strike_str);
        details.strike = static_cast<double>(strike_int) / 1000.0;
    } catch (const std::exception& e) {
        return Error{
            "Failed to parse strike price",
            std::string(e.what())
        };
    }

    return details;
}

Result<OptionDetails> OptionSymbolParser::parse_format2(const std::string& symbol) {
    // Format: "AAPL250321P150" or "AAPL 250321P150"
    // Pattern: <underlying><optional space><YYMMDD><C/P><decimal strike>

    std::regex pattern(R"(^([A-Z]+)\s?(\d{6})([CP])([\d.]+)$)");
    std::smatch matches;

    if (!std::regex_match(symbol, matches, pattern)) {
        return Error{"Does not match format 2"};
    }

    OptionDetails details;
    details.original_symbol = symbol;
    details.underlying = matches[1].str();

    // Convert date from YYMMDD to YYYY-MM-DD
    std::string yymmdd = matches[2].str();
    details.expiry = convert_date_format(yymmdd);

    // Extract right (C or P)
    details.right = matches[3].str()[0];

    // Parse strike (decimal format)
    std::string strike_str = matches[4].str();
    try {
        details.strike = std::stod(strike_str);
    } catch (const std::exception& e) {
        return Error{
            "Failed to parse strike price",
            std::string(e.what())
        };
    }

    return details;
}

std::string OptionSymbolParser::convert_date_format(const std::string& yymmdd) {
    if (yymmdd.length() != 6) {
        return "";
    }

    // Extract components
    std::string yy = yymmdd.substr(0, 2);
    std::string mm = yymmdd.substr(2, 2);
    std::string dd = yymmdd.substr(4, 2);

    // Convert YY to YYYY (assume 20YY for years 00-99)
    int year_int = std::stoi(yy);
    int full_year = 2000 + year_int;

    // Format as YYYY-MM-DD
    std::ostringstream oss;
    oss << full_year << "-" << mm << "-" << dd;
    return oss.str();
}

std::string OptionSymbolParser::extract_underlying(const std::string& symbol) {
    // Extract underlying by finding first digit or space
    size_t pos = symbol.find_first_of("0123456789 ");
    if (pos == std::string::npos) {
        return symbol;
    }
    return symbol.substr(0, pos);
}

} // namespace ibkr::parser
