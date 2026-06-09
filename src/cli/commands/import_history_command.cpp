#include "import_history_command.hpp"
#include "parsers/activity_statement_parser.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <filesystem>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<void> ImportHistoryCommand::execute(
    const config::Config& config,
    const std::vector<std::string>& files,
    const std::string& account,
    const utils::OutputOptions& output_opts) {

    if (files.empty()) {
        return Error{"No files specified", "Use --file to specify CSV files"};
    }

    Logger::info("Importing historical Activity Statement files: {} files", files.size());

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    parser::ActivityStatementParser parser;
    int total_trades_imported = 0;
    int total_files_imported = 0;

    for (const auto& file : files) {
        std::filesystem::path path(file);
        std::string filename = path.filename().string();
        std::string account_name = account;

        // Extract account from filename if not provided
        if (account_name.empty()) {
            account_name = parser::ActivityStatementParser::extract_account_from_filename(filename);
            if (account_name.empty()) {
                Logger::warn("Cannot extract account from filename: {}, skipping", filename);
                continue;
            }
        }

        // Resolve account_name -> account_id
        auto account_result = database.accounts().get_or_create_account(account_name);
        if (!account_result) {
            Logger::error("Failed to get account for {}: {}", account_name, account_result.error().message);
            continue;
        }
        int64_t account_id = *account_result;

        Logger::info("Parsing file: {} (account: {})", filename, account_name);

        auto parse_result = parser.parse_file(file);
        if (!parse_result) {
            Logger::error("Failed to parse {}: {}", filename, parse_result.error().format());
            continue;
        }

        const auto& data = *parse_result;
        Logger::info("Parsed {} trades from {}", data.trades.size(), filename);

        if (!data.trades.empty()) {
            auto import_result = database.trades().import_trades(account_id, data.trades);
            if (!import_result) {
                Logger::error("Failed to import trades from {}: {}", filename, import_result.error().message);
            } else {
                total_trades_imported += data.trades.size();
            }
        }

        if (!data.stock_trades.empty()) {
            auto stock_result = database.trades().import_stock_trades(account_id, data.stock_trades);
            if (!stock_result) {
                Logger::error("Failed to import stock trades from {}: {}", filename, stock_result.error().message);
            } else {
                Logger::info("Imported {} stock trades from {}", data.stock_trades.size(), filename);
            }
        }

        if (!data.dividends.empty()) {
            auto div_result = database.trades().import_dividends(account_id, data.dividends);
            if (!div_result) {
                Logger::error("Failed to import dividends from {}: {}", filename, div_result.error().message);
            } else {
                Logger::info("Imported {} dividends from {}", data.dividends.size(), filename);
            }
        }

        if (!data.interests.empty()) {
            auto int_result = database.trades().import_interest_expenses(account_id, data.interests);
            if (!int_result) {
                Logger::error("Failed to import interest from {}: {}", filename, int_result.error().message);
            } else {
                Logger::info("Imported {} interest records from {}", data.interests.size(), filename);
            }
        }

        total_files_imported++;
    }

    // Get total counts
    auto trades_count = database.trades().get_trades_count();
    int db_trades = trades_count ? *trades_count : 0;

    if (output_opts.json) {
        nlohmann::json output = {
            {"files_imported", total_files_imported},
            {"trades_imported", total_trades_imported},
            {"db_total_trades", db_trades}
        };
        std::cout << output.dump(2) << "\n";
    } else if (!output_opts.quiet) {
        std::cout << "Historical import complete:\n";
        std::cout << "  Files imported: " << total_files_imported << "\n";
        std::cout << "  Trades imported: " << total_trades_imported << "\n";
        std::cout << "  Total in database: " << db_trades << " trades\n";
    }

    return Result<void>{};
}

} // namespace ibkr::commands
