#include "manual_add_command.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <regex>
#include <sstream>
#include <iomanip>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<void> ManualAddCommand::execute(
    const config::Config& config,
    const std::string& account_name,
    const std::string& underlying,
    const std::string& expiry,
    double strike,
    const std::string& right,
    double quantity,
    double premium,
    const std::string& notes,
    const utils::OutputOptions& output_opts) {

    Logger::info("Starting manual-add command");

    // Validate input
    auto validate_result = validate_input(underlying, expiry, strike, right, quantity, premium);
    if (!validate_result) {
        return validate_result;
    }

    // Convert expiry format
    auto expiry_result = convert_expiry_format(expiry);
    if (!expiry_result) {
        return Error{
            "Invalid expiry format",
            expiry_result.error().message
        };
    }
    std::string expiry_formatted = *expiry_result;

    // Generate symbol
    char right_char = right[0];
    std::string symbol = generate_symbol(underlying, expiry, strike, right_char);

    Logger::info("Adding manual position: {} {} {} {} @ {} (qty: {})",
                underlying, expiry_formatted, strike, right, premium, quantity);

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{
            "Failed to initialize database",
            init_result.error().message
        };
    }

    // Get or create account
    auto account_result = database.get_or_create_account(account_name);
    if (!account_result) {
        return Error{
            "Failed to get account",
            account_result.error().message
        };
    }
    int64_t account_id = *account_result;

    // Insert into database
    try {
        auto db_ptr = database.get_db();
        if (!db_ptr) {
            return Error{"Database not initialized"};
        }

        SQLite::Statement insert(*db_ptr,
            "INSERT INTO open_options (account_id, symbol, underlying, expiry, strike, "
            "right, quantity, mark_price, entry_premium, current_value, is_manual, notes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?)");

        insert.bind(1, account_id);
        insert.bind(2, symbol);
        insert.bind(3, underlying);
        insert.bind(4, expiry_formatted);
        insert.bind(5, strike);
        insert.bind(6, right);
        insert.bind(7, quantity);
        insert.bind(8, premium);  // Use premium as mark_price
        insert.bind(9, premium);  // entry_premium
        insert.bind(10, quantity * premium * 100.0);  // current_value (per contract)
        insert.bind(11, notes);

        insert.exec();

        int64_t position_id = db_ptr->getLastInsertRowid();
        Logger::info("Manual position added successfully");

        if (output_opts.json) {
            std::cout << utils::JsonOutput::manual_add_result(position_id, symbol, underlying) << "\n";
        } else if (!output_opts.quiet) {
            std::cout << "✓ Position added:\n";
            std::cout << "  Account: " << account_name << "\n";
            std::cout << "  Symbol: " << symbol << "\n";
            std::cout << "  Underlying: " << underlying << "\n";
            std::cout << "  Expiry: " << expiry_formatted << "\n";
            std::cout << "  Strike: $" << strike << "\n";
            std::cout << "  Right: " << right << "\n";
            std::cout << "  Quantity: " << quantity << " (";
            if (quantity < 0) {
                std::cout << "SHORT";
            } else {
                std::cout << "LONG";
            }
            std::cout << ")\n";
            std::cout << "  Premium: $" << premium << " per share\n";
            std::cout << "  Contract Value: $" << (quantity * premium * 100.0) << "\n";
            if (!notes.empty()) {
                std::cout << "  Notes: " << notes << "\n";
            }
        }

        return Result<void>{};

    } catch (const std::exception& e) {
        return Error{
            "Failed to insert position",
            std::string(e.what())
        };
    }
}

Result<void> ManualAddCommand::validate_input(
    const std::string& underlying,
    const std::string& expiry,
    double strike,
    const std::string& right,
    double quantity,
    double premium) {

    // Validate underlying (must be uppercase letters)
    if (underlying.empty()) {
        return Error{"Underlying symbol cannot be empty"};
    }
    if (!std::all_of(underlying.begin(), underlying.end(), ::isupper)) {
        return Error{
            "Underlying symbol must be uppercase",
            "Use: " + underlying + " -> " + underlying
        };
    }

    // Validate expiry (must be YYYYMMDD format)
    if (expiry.length() != 8) {
        return Error{
            "Expiry must be in YYYYMMDD format",
            "Example: 20250321 for March 21, 2025"
        };
    }
    if (!std::all_of(expiry.begin(), expiry.end(), ::isdigit)) {
        return Error{
            "Expiry must contain only digits",
            "Example: 20250321"
        };
    }

    // Validate strike (must be positive)
    if (strike <= 0.0) {
        return Error{
            "Strike price must be positive",
            "Got: " + std::to_string(strike)
        };
    }

    // Validate right (must be C or P)
    if (right.length() != 1 || (right[0] != 'C' && right[0] != 'P')) {
        return Error{
            "Right must be 'C' (Call) or 'P' (Put)",
            "Got: " + right
        };
    }

    // Validate quantity (cannot be zero)
    if (quantity == 0.0) {
        return Error{"Quantity cannot be zero"};
    }

    // Validate premium (must be non-negative)
    if (premium < 0.0) {
        return Error{
            "Premium cannot be negative",
            "Got: " + std::to_string(premium)
        };
    }

    return Result<void>{};
}

Result<std::string> ManualAddCommand::convert_expiry_format(const std::string& yyyymmdd) {
    if (yyyymmdd.length() != 8) {
        return Error{"Expiry must be 8 digits (YYYYMMDD)"};
    }

    std::string yyyy = yyyymmdd.substr(0, 4);
    std::string mm = yyyymmdd.substr(4, 2);
    std::string dd = yyyymmdd.substr(6, 2);

    // Basic validation
    int year = std::stoi(yyyy);
    int month = std::stoi(mm);
    int day = std::stoi(dd);

    if (year < 2020 || year > 2100) {
        return Error{"Year must be between 2020 and 2100"};
    }
    if (month < 1 || month > 12) {
        return Error{"Month must be between 01 and 12"};
    }
    if (day < 1 || day > 31) {
        return Error{"Day must be between 01 and 31"};
    }

    return yyyy + "-" + mm + "-" + dd;
}

std::string ManualAddCommand::generate_symbol(
    const std::string& underlying,
    const std::string& expiry_yyyymmdd,
    double strike,
    char right) {

    // Format: "AAPL250321P150"
    // Extract YYMMDD from YYYYMMDD
    std::string yy = expiry_yyyymmdd.substr(2, 2);
    std::string mmdd = expiry_yyyymmdd.substr(4, 4);

    std::ostringstream oss;
    oss << underlying << yy << mmdd << right;

    // Format strike (remove trailing zeros)
    if (strike == static_cast<int>(strike)) {
        oss << static_cast<int>(strike);
    } else {
        oss << strike;
    }

    return oss.str();
}

} // namespace ibkr::commands
