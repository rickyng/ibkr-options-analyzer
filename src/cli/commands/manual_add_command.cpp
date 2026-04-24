#include "manual_add_command.hpp"
#include "services/position_service.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <iostream>
#include <algorithm>

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

    auto validate_result = validate_input(underlying, expiry, strike, right, quantity, premium);
    if (!validate_result) return validate_result;

    char right_char = right[0];

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::PositionService position_service(database);
    auto add_result = position_service.add_manual_position(
        account_name, underlying, expiry, strike, right_char, quantity, premium, notes);

    if (!add_result) {
        return Error{"Failed to add position", add_result.error().message};
    }

    std::string symbol = underlying + expiry.substr(2) + right_char;
    if (strike == static_cast<int>(strike)) {
        symbol += std::to_string(static_cast<int>(strike));
    } else {
        symbol += std::to_string(strike);
    }

    if (output_opts.json) {
        std::cout << utils::JsonOutput::manual_add_result(*add_result, symbol, underlying) << "\n";
    } else if (!output_opts.quiet) {
        std::string expiry_formatted = expiry.substr(0, 4) + "-" + expiry.substr(4, 2) + "-" + expiry.substr(6, 2);
        std::cout << "✓ Position added:\n";
        std::cout << "  Account: " << account_name << "\n";
        std::cout << "  Symbol: " << symbol << "\n";
        std::cout << "  Underlying: " << underlying << "\n";
        std::cout << "  Expiry: " << expiry_formatted << "\n";
        std::cout << "  Strike: $" << strike << "\n";
        std::cout << "  Right: " << right << "\n";
        std::cout << "  Quantity: " << quantity << " (" << (quantity < 0 ? "SHORT" : "LONG") << ")\n";
        std::cout << "  Premium: $" << premium << " per share\n";
        std::cout << "  Contract Value: $" << (quantity * premium * 100.0) << "\n";
        if (!notes.empty()) std::cout << "  Notes: " << notes << "\n";
    }

    return Result<void>{};
}

Result<void> ManualAddCommand::validate_input(
    const std::string& underlying,
    const std::string& expiry,
    double strike,
    const std::string& right,
    double quantity,
    double premium) {

    if (underlying.empty()) return Error{"Underlying symbol cannot be empty"};
    if (!std::all_of(underlying.begin(), underlying.end(), ::isupper)) {
        return Error{"Underlying symbol must be uppercase"};
    }
    if (expiry.length() != 8 || !std::all_of(expiry.begin(), expiry.end(), ::isdigit)) {
        return Error{"Expiry must be in YYYYMMDD format"};
    }
    if (strike <= 0.0) return Error{"Strike price must be positive"};
    if (right.length() != 1 || (right[0] != 'C' && right[0] != 'P')) {
        return Error{"Right must be 'C' or 'P'"};
    }
    if (quantity == 0.0) return Error{"Quantity cannot be zero"};
    if (premium < 0.0) return Error{"Premium cannot be negative"};
    return Result<void>{};
}

Result<std::string> ManualAddCommand::convert_expiry_format(const std::string& yyyymmdd) {
    if (yyyymmdd.length() != 8) return Error{"Expiry must be 8 digits (YYYYMMDD)"};
    int year = std::stoi(yyyymmdd.substr(0, 4));
    int month = std::stoi(yyyymmdd.substr(4, 2));
    int day = std::stoi(yyyymmdd.substr(6, 2));
    if (year < 2020 || year > 2100) return Error{"Year must be between 2020 and 2100"};
    if (month < 1 || month > 12) return Error{"Month must be between 01 and 12"};
    if (day < 1 || day > 31) return Error{"Day must be between 01 and 31"};
    return yyyymmdd.substr(0, 4) + "-" + yyyymmdd.substr(4, 2) + "-" + yyyymmdd.substr(6, 2);
}

std::string ManualAddCommand::generate_symbol(
    const std::string& underlying,
    const std::string& expiry_yyyymmdd,
    double strike,
    char right) {

    std::string yy = expiry_yyyymmdd.substr(2, 2);
    std::string mmdd = expiry_yyyymmdd.substr(4, 4);
    std::string result = underlying + yy + mmdd + right;
    if (strike == static_cast<int>(strike)) {
        result += std::to_string(static_cast<int>(strike));
    } else {
        result += std::to_string(strike);
    }
    return result;
}

} // namespace ibkr::commands