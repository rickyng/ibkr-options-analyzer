#include <CLI/CLI.hpp>
#include "config/config_manager.hpp"
#include "utils/logger.hpp"
#include "commands/download_command.hpp"
#include "commands/import_command.hpp"
#include "commands/manual_add_command.hpp"
#include "commands/analyze_command.hpp"
#include "commands/report_command.hpp"
#include <iostream>
#include <cstdlib>

/**
 * IBKR Options Analyzer - Main Entry Point
 *
 * Phase 1: Basic CLI skeleton with config loading and logging setup.
 *
 * Command structure:
 *   ibkr-options-analyzer download [--account NAME] [--force]
 *   ibkr-options-analyzer import [--file PATH]
 *   ibkr-options-analyzer manual-add --underlying SYM --expiry YYYYMMDD --strike N --right C/P --quantity N --premium N
 *   ibkr-options-analyzer analyze [open|impact|strategy] [--account NAME] [--underlying SYM]
 *   ibkr-options-analyzer report [--output PATH] [--account NAME]
 */

int main(int argc, char** argv) {
    CLI::App app{"IBKR Options Analyzer - Track and analyze option positions"};
    app.require_subcommand(1);

    // Global options
    std::string config_path;
    std::string log_level;
    app.add_option("--config", config_path, "Path to config.json")
        ->default_val("");
    app.add_option("--log-level", log_level, "Log level (trace|debug|info|warn|error)")
        ->default_val("");

    // Download subcommand
    auto* download_cmd = app.add_subcommand("download", "Download Flex reports from IBKR");
    std::string download_token;
    std::string download_query_id;
    std::string download_account;
    bool download_force = false;
    download_cmd->add_option("--token", download_token, "IBKR Flex Web Service token")->required();
    download_cmd->add_option("--query-id", download_query_id, "IBKR Flex query ID")->required();
    download_cmd->add_option("--account", download_account, "Account name")->required();
    download_cmd->add_flag("--force", download_force, "Force re-download (skip cache)");

    // Import subcommand
    auto* import_cmd = app.add_subcommand("import", "Import downloaded CSV files into database");
    std::string import_file;
    import_cmd->add_option("--file", import_file, "Import specific CSV file");

    // Manual-add subcommand
    auto* manual_cmd = app.add_subcommand("manual-add", "Manually add a position");
    std::string manual_account;
    std::string manual_underlying;
    std::string manual_expiry;
    double manual_strike = 0.0;
    std::string manual_right;
    double manual_quantity = 0.0;
    double manual_premium = 0.0;
    std::string manual_notes;

    manual_cmd->add_option("--account", manual_account, "Account name")->required();
    manual_cmd->add_option("--underlying", manual_underlying, "Underlying symbol (e.g., AAPL)")->required();
    manual_cmd->add_option("--expiry", manual_expiry, "Expiry date (YYYYMMDD)")->required();
    manual_cmd->add_option("--strike", manual_strike, "Strike price")->required();
    manual_cmd->add_option("--right", manual_right, "Option right (C or P)")->required();
    manual_cmd->add_option("--quantity", manual_quantity, "Quantity (negative=short, positive=long)")->required();
    manual_cmd->add_option("--premium", manual_premium, "Entry premium per share")->required();
    manual_cmd->add_option("--notes", manual_notes, "Optional notes");

    // Analyze subcommand
    auto* analyze_cmd = app.add_subcommand("analyze", "Analyze positions");
    std::string analyze_type;
    std::string analyze_account;
    std::string analyze_underlying;
    analyze_cmd->add_option("type", analyze_type, "Analysis type (open|impact|strategy)")->required();
    analyze_cmd->add_option("--account", analyze_account, "Filter by account");
    analyze_cmd->add_option("--underlying", analyze_underlying, "Filter by underlying");

    // Report subcommand
    auto* report_cmd = app.add_subcommand("report", "Generate comprehensive report");
    std::string report_output;
    std::string report_type;
    std::string report_account;
    std::string report_underlying;
    report_cmd->add_option("--output", report_output, "Output CSV file path");
    report_cmd->add_option("--type", report_type, "Report type (full|positions|strategies|summary)")
        ->default_val("full");
    report_cmd->add_option("--account", report_account, "Filter by account");
    report_cmd->add_option("--underlying", report_underlying, "Filter by underlying");

    // Parse command line
    CLI11_PARSE(app, argc, argv);

    // Load configuration
    auto config_result = ibkr::config::ConfigManager::load(config_path);
    if (!config_result) {
        std::cerr << "Error: " << config_result.error().format() << "\n";
        std::cerr << "\nTo get started:\n";
        std::cerr << "  1. Copy config.json.example to ~/.ibkr-options-analyzer/config.json\n";
        std::cerr << "  2. Edit config.json with your IBKR Flex tokens and query IDs\n";
        std::cerr << "  3. See README.md for Flex Query setup instructions\n";
        return EXIT_FAILURE;
    }

    const auto& config = *config_result;

    // Initialize logger
    std::string effective_log_level = log_level.empty() ? config.logging.level : log_level;
    ibkr::utils::Logger::init(
        config.logging.file,
        effective_log_level,
        config.logging.max_file_size_mb,
        config.logging.max_files
    );

    ibkr::utils::Logger::info("IBKR Options Analyzer v1.0.0");
    if (!config.accounts.empty()) {
        ibkr::utils::Logger::debug("Config loaded: {} accounts, database: {}",
                                  config.accounts.size(), config.database.path);
    } else {
        ibkr::utils::Logger::debug("Config loaded: no accounts configured (using command-line args), database: {}",
                                  config.database.path);
    }

    // Execute subcommand
    try {
        if (*download_cmd) {
            auto result = ibkr::commands::DownloadCommand::execute(
                config,
                download_token,
                download_query_id,
                download_account,
                download_force
            );

            if (!result) {
                ibkr::utils::Logger::error("Download failed: {}", result.error().format());
                std::cerr << "Error: " << result.error().format() << "\n";
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*import_cmd) {
            auto result = ibkr::commands::ImportCommand::execute(
                config,
                import_file,
                "",  // account_filter (not implemented yet)
                false,  // options_only
                true   // clear_existing
            );

            if (!result) {
                ibkr::utils::Logger::error("Import failed: {}", result.error().format());
                std::cerr << "Error: " << result.error().format() << "\n";
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*manual_cmd) {
            auto result = ibkr::commands::ManualAddCommand::execute(
                config,
                manual_account,
                manual_underlying,
                manual_expiry,
                manual_strike,
                manual_right,
                manual_quantity,
                manual_premium,
                manual_notes
            );

            if (!result) {
                ibkr::utils::Logger::error("Manual-add failed: {}", result.error().format());
                std::cerr << "Error: " << result.error().format() << "\n";
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*analyze_cmd) {
            auto result = ibkr::commands::AnalyzeCommand::execute(
                config,
                analyze_type,
                analyze_account,
                analyze_underlying
            );

            if (!result) {
                ibkr::utils::Logger::error("Analyze failed: {}", result.error().format());
                std::cerr << "Error: " << result.error().format() << "\n";
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*report_cmd) {
            auto result = ibkr::commands::ReportCommand::execute(
                config,
                report_output,
                report_type,
                report_account,
                report_underlying
            );

            if (!result) {
                ibkr::utils::Logger::error("Report failed: {}", result.error().format());
                std::cerr << "Error: " << result.error().format() << "\n";
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }
        }

    } catch (const std::exception& e) {
        ibkr::utils::Logger::critical("Unhandled exception: {}", e.what());
        std::cerr << "Fatal error: " << e.what() << "\n";
        ibkr::utils::Logger::shutdown();
        return EXIT_FAILURE;
    }

    ibkr::utils::Logger::info("Command completed successfully");
    ibkr::utils::Logger::shutdown();
    return EXIT_SUCCESS;
}
