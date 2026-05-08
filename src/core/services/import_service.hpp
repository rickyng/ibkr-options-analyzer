#pragma once

#include "config/config_manager.hpp"
#include "db/database.hpp"
#include "parsers/csv_parser.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>

namespace ibkr::services {

struct ImportStats {
    int files_imported{0};
    int trades_imported{0};
    int positions_imported{0};
    int db_total_trades{0};
    int db_total_positions{0};
};

class ImportService {
public:
    explicit ImportService(db::Database& database);

    utils::Result<ImportStats> import_files(
        const std::vector<std::string>& files,
        const std::string& account_filter = "",
        bool options_only = false,
        bool clear_existing = true);

    utils::Result<ImportStats> import_single_file(
        const std::string& file_path,
        bool options_only = false,
        bool clear_existing = true);

    static utils::Result<std::vector<std::string>> discover_csv_files(
        const std::string& downloads_dir);

    static std::string extract_account_name(const std::string& filename);

private:
    db::Database& database_;
};

} // namespace ibkr::services
