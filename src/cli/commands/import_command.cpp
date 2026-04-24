#include "import_command.hpp"
#include "services/import_service.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <iostream>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<void> ImportCommand::execute(
    const config::Config& config,
    const std::string& file_path,
    const std::string& account_filter,
    bool options_only,
    bool clear_existing,
    const utils::OutputOptions& output_opts) {

    Logger::info("Starting import command");

    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    services::ImportService import_service(database);

    std::vector<std::string> files;
    if (!file_path.empty()) {
        files.push_back(file_path);
    } else {
        auto discover_result = services::ImportService::discover_csv_files(
            config::ConfigManager::expand_path("~/.ibkr-options-analyzer/downloads"));
        if (!discover_result) {
            return Error{"Failed to discover CSV files", discover_result.error().message};
        }
        files = *discover_result;
        if (files.empty()) {
            return Error{"No CSV files found", "Run 'download' command first"};
        }
    }

    auto stats_result = import_service.import_files(files, account_filter, options_only, clear_existing);
    if (!stats_result) {
        return Error{"Import failed", stats_result.error().message};
    }

    const auto& stats = *stats_result;

    if (output_opts.json) {
        std::cout << utils::JsonOutput::import_result(stats.trades_imported, stats.positions_imported, file_path) << "\n";
    } else if (!output_opts.quiet) {
        std::cout << "✓ Import complete:\n";
        std::cout << "  Files imported: " << stats.files_imported << "\n";
        std::cout << "  Trades imported: " << stats.trades_imported << "\n";
        std::cout << "  Open positions imported: " << stats.positions_imported << "\n";
        std::cout << "  Total in database: " << stats.db_total_trades << " trades, "
                  << stats.db_total_positions << " open positions\n";
    }

    return Result<void>{};
}

Result<std::vector<std::string>> ImportCommand::discover_csv_files(
    const std::string& downloads_dir) {
    return services::ImportService::discover_csv_files(downloads_dir);
}

std::string ImportCommand::extract_account_name(const std::string& filename) {
    return services::ImportService::extract_account_name(filename);
}

} // namespace ibkr::commands