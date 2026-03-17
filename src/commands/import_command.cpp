#include "import_command.hpp"
#include "parser/csv_parser.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<void> ImportCommand::execute(
    const config::Config& config,
    const std::string& file_path,
    const std::string& account_filter,
    bool options_only,
    bool clear_existing) {

    Logger::info("Starting import command");

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{
            "Failed to initialize database",
            init_result.error().message
        };
    }

    // Get files to import
    std::vector<std::string> files_to_import;

    if (!file_path.empty()) {
        // Import specific file
        files_to_import.push_back(file_path);
        Logger::info("Importing specific file: {}", file_path);
    } else {
        // Auto-discover files
        std::string downloads_dir = config::ConfigManager::expand_path(
            "~/.ibkr-options-analyzer/downloads");

        auto discover_result = discover_csv_files(downloads_dir);
        if (!discover_result) {
            return Error{
                "Failed to discover CSV files",
                discover_result.error().message
            };
        }

        files_to_import = *discover_result;

        if (files_to_import.empty()) {
            return Error{
                "No CSV files found",
                "Run 'download' command first to download Flex reports"
            };
        }

        Logger::info("Found {} CSV files to import", files_to_import.size());
    }

    // Import each file
    parser::CSVParser csv_parser;
    int total_trades = 0;
    int total_positions = 0;
    int files_imported = 0;

    for (const auto& file : files_to_import) {
        // Extract account name from filename
        std::filesystem::path path(file);
        std::string filename = path.filename().string();
        std::string account_name = extract_account_name(filename);

        // Apply account filter
        if (!account_filter.empty() && account_name != account_filter) {
            Logger::debug("Skipping file (account filter): {}", filename);
            continue;
        }

        Logger::info("Importing file: {} (account: {})", filename, account_name);

        // Parse CSV
        auto parse_result = csv_parser.parse_file(file, options_only, true);
        if (!parse_result) {
            Logger::error("Failed to parse CSV: {}", parse_result.error().format());
            continue;
        }

        const auto& parse_data = *parse_result;
        Logger::info("Parsed {} trades, {} open positions from {}",
                    parse_data.trades.size(), parse_data.open_positions.size(), filename);

        // Clear existing positions if requested
        if (clear_existing && !parse_data.open_positions.empty()) {
            auto clear_result = database.clear_open_positions(account_name);
            if (!clear_result) {
                Logger::warn("Failed to clear existing positions: {}",
                           clear_result.error().message);
            }
        }

        // Import trades
        if (!parse_data.trades.empty()) {
            auto import_trades_result = database.import_trades(account_name, parse_data.trades);
            if (!import_trades_result) {
                Logger::error("Failed to import trades: {}",
                            import_trades_result.error().format());
            } else {
                total_trades += parse_data.trades.size();
            }
        }

        // Import open positions
        if (!parse_data.open_positions.empty()) {
            auto import_positions_result = database.import_open_positions(
                account_name, parse_data.open_positions);
            if (!import_positions_result) {
                Logger::error("Failed to import open positions: {}",
                            import_positions_result.error().format());
            } else {
                total_positions += parse_data.open_positions.size();
            }
        }

        files_imported++;
    }

    // Get final counts
    auto trades_count_result = database.get_trades_count();
    auto positions_count_result = database.get_open_positions_count();

    int db_trades = trades_count_result ? *trades_count_result : 0;
    int db_positions = positions_count_result ? *positions_count_result : 0;

    Logger::info("Import complete: {} files, {} trades, {} positions",
                files_imported, total_trades, total_positions);

    std::cout << "✓ Import complete:\n";
    std::cout << "  Files imported: " << files_imported << "\n";
    std::cout << "  Trades imported: " << total_trades << "\n";
    std::cout << "  Open positions imported: " << total_positions << "\n";
    std::cout << "  Total in database: " << db_trades << " trades, "
              << db_positions << " open positions\n";

    return Result<void>{};
}

Result<std::vector<std::string>> ImportCommand::discover_csv_files(
    const std::string& downloads_dir) {

    std::vector<std::string> csv_files;

    try {
        if (!std::filesystem::exists(downloads_dir)) {
            return Error{
                "Downloads directory not found",
                downloads_dir
            };
        }

        for (const auto& entry : std::filesystem::directory_iterator(downloads_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.ends_with(".csv")) {
                    csv_files.push_back(entry.path().string());
                }
            }
        }

        // Sort by filename (includes timestamp)
        std::sort(csv_files.begin(), csv_files.end());

        return csv_files;

    } catch (const std::exception& e) {
        return Error{
            "Failed to discover CSV files",
            std::string(e.what())
        };
    }
}

std::string ImportCommand::extract_account_name(const std::string& filename) {
    // Expected format: flex_report_Main_Account_20260315_233006.csv
    // Extract: Main_Account -> Main Account

    std::string name = filename;

    // Remove prefix "flex_report_"
    size_t prefix_pos = name.find("flex_report_");
    if (prefix_pos != std::string::npos) {
        name = name.substr(prefix_pos + 12);
    }

    // Remove timestamp and extension (last two underscores + .csv)
    size_t last_underscore = name.rfind('_');
    if (last_underscore != std::string::npos) {
        size_t second_last_underscore = name.rfind('_', last_underscore - 1);
        if (second_last_underscore != std::string::npos) {
            name = name.substr(0, second_last_underscore);
        }
    }

    // Replace underscores with spaces
    std::replace(name.begin(), name.end(), '_', ' ');

    return name;
}

} // namespace ibkr::commands
