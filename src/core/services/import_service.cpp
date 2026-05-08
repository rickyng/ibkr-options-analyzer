#include "import_service.hpp"
#include "utils/logger.hpp"
#include <filesystem>
#include <algorithm>

namespace ibkr::services {

using utils::Result;
using utils::Error;
using utils::Logger;

ImportService::ImportService(db::Database& database)
    : database_(database) {}

Result<ImportStats> ImportService::import_files(
    const std::vector<std::string>& files,
    const std::string& account_filter,
    bool options_only,
    bool clear_existing) {

    ImportStats stats;

    for (const auto& file : files) {
        std::filesystem::path path(file);
        std::string filename = path.filename().string();
        std::string account_name = extract_account_name(filename);

        if (!account_filter.empty() && account_name != account_filter) {
            Logger::debug("Skipping file (account filter): {}", filename);
            continue;
        }

        Logger::info("Importing file: {} (account: {})", filename, account_name);

        parser::CSVParser csv_parser;
        auto parse_result = csv_parser.parse_file(file, options_only, true);
        if (!parse_result) {
            Logger::error("Failed to parse CSV: {}", parse_result.error().format());
            continue;
        }

        const auto& parse_data = *parse_result;
        Logger::info("Parsed {} trades, {} open positions from {}",
                    parse_data.trades.size(), parse_data.open_positions.size(), filename);

        if (clear_existing && !parse_data.open_positions.empty()) {
            auto clear_result = database_.clear_open_positions(account_name);
            if (!clear_result) {
                Logger::warn("Failed to clear existing positions: {}",
                           clear_result.error().message);
            }
        }

        if (!parse_data.trades.empty()) {
            auto import_result = database_.import_trades(account_name, parse_data.trades);
            if (!import_result) {
                Logger::error("Failed to import trades: {}", import_result.error().format());
            } else {
                stats.trades_imported += parse_data.trades.size();
            }
        }

        if (!parse_data.open_positions.empty()) {
            auto import_result = database_.import_open_positions(account_name, parse_data.open_positions);
            if (!import_result) {
                Logger::error("Failed to import open positions: {}", import_result.error().format());
            } else {
                stats.positions_imported += parse_data.open_positions.size();
            }
        }

        stats.files_imported++;
    }

    auto trades_count = database_.get_trades_count();
    auto positions_count = database_.get_open_positions_count();
    stats.db_total_trades = trades_count ? *trades_count : 0;
    stats.db_total_positions = positions_count ? *positions_count : 0;

    Logger::info("Import complete: {} files, {} trades, {} positions",
                stats.files_imported, stats.trades_imported, stats.positions_imported);

    return stats;
}

Result<ImportStats> ImportService::import_single_file(
    const std::string& file_path,
    bool options_only,
    bool clear_existing) {

    return import_files({file_path}, "", options_only, clear_existing);
}

Result<std::vector<std::string>> ImportService::discover_csv_files(
    const std::string& downloads_dir) {

    std::vector<std::string> csv_files;

    try {
        if (!std::filesystem::exists(downloads_dir)) {
            return Error{"Downloads directory not found", downloads_dir};
        }

        for (const auto& entry : std::filesystem::directory_iterator(downloads_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.ends_with(".csv")) {
                    csv_files.push_back(entry.path().string());
                }
            }
        }

        std::sort(csv_files.begin(), csv_files.end());
        return csv_files;

    } catch (const std::exception& e) {
        return Error{"Failed to discover CSV files", std::string(e.what())};
    }
}

std::string ImportService::extract_account_name(const std::string& filename) {
    std::string name = filename;

    size_t prefix_pos = name.find("flex_report_");
    if (prefix_pos != std::string::npos) {
        name = name.substr(prefix_pos + 12);
    }

    size_t last_underscore = name.rfind('_');
    if (last_underscore != std::string::npos) {
        size_t second_last_underscore = name.rfind('_', last_underscore - 1);
        if (second_last_underscore != std::string::npos) {
            name = name.substr(0, second_last_underscore);
        }
    }

    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

} // namespace ibkr::services