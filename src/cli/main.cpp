#include <CLI/CLI.hpp>
#include "config/config_manager.hpp"
#include "utils/logger.hpp"
#include "utils/json_output.hpp"
#include "commands/download_command.hpp"
#include "commands/import_command.hpp"
#include "commands/import_history_command.hpp"
#include "commands/analyze_command.hpp"
#include "commands/report_command.hpp"
#include "commands/trades_command.hpp"
#include "commands/refresh_command.hpp"
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
 *   ibkr-options-analyzer analyze [open|impact|portfolio] [--account NAME] [--underlying SYM]
 *   ibkr-options-analyzer report [--output PATH] [--account NAME]
 */

int main(int argc, char** argv) {
    CLI::App app{"IBKR Options Analyzer - Track and analyze option positions"};
    app.require_subcommand(1);
    app.fallthrough();

    // Global options
    std::string config_path;
    std::string log_level;
    std::string format;
    bool quiet = false;
    bool google_sheet = false;
    app.add_option("--config", config_path, "Path to config.json")
        ->default_val("");
    app.add_option("--log-level", log_level, "Log level (trace|debug|info|warn|error)")
        ->default_val("");
    app.add_option("--format", format, "Output format (text|json)")
        ->default_val("text")
        ->check([](const std::string& val) -> std::string {
            if (val != "text" && val != "json") return "Must be 'text' or 'json'";
            return {};
        });
    app.add_flag("--quiet", quiet, "Suppress human-readable output (only JSON)");
    app.add_flag("--google-sheet", google_sheet, "Push output to Google Sheets (creates new spreadsheet)");

    // Download subcommand
    auto* download_cmd = app.add_subcommand("download", "Download Flex reports from IBKR");
    std::string download_token;
    std::string download_query_id;
    std::string download_account;
    bool download_force = false;
    download_cmd->add_option("--token", download_token, "IBKR Flex Web Service token (default: from config)");
    download_cmd->add_option("--query-id", download_query_id, "IBKR Flex query ID (default: from config)");
    download_cmd->add_option("--account", download_account, "Account name")->required();
    download_cmd->add_flag("--force", download_force, "Force re-download (skip cache)");

    // Import subcommand
    auto* import_cmd = app.add_subcommand("import", "Import downloaded CSV files into database");
    std::string import_file;
    import_cmd->add_option("--file", import_file, "Import specific CSV file");

    // Import-history subcommand (Activity Statement format)
    auto* import_history_cmd = app.add_subcommand("import-history", "Import historical Activity Statement CSV files");
    std::vector<std::string> import_history_files;
    std::string import_history_account;
    import_history_cmd->add_option("--file", import_history_files, "CSV file path (Activity Statement format)")->required()->expected(1, 100);
    import_history_cmd->add_option("--account", import_history_account, "Override account name (default: extract from filename)");

    // Analyze subcommand
    auto* analyze_cmd = app.add_subcommand("analyze", "Analyze positions");
    std::string analyze_type;
    std::string analyze_account;
    std::string analyze_underlying;
    analyze_cmd->add_option("type", analyze_type, "Analysis type (open|impact|portfolio)")->required();
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

    // Trades subcommand
    auto* trades_cmd = app.add_subcommand("trades", "List option and stock trades");
    std::string trades_date_from;
    std::string trades_date_to;
    std::string trades_underlying;
    std::string trades_account;
    trades_cmd->add_option("--date-from", trades_date_from, "Filter trades from this date (YYYY-MM-DD)");
    trades_cmd->add_option("--date-to", trades_date_to, "Filter trades to this date (YYYY-MM-DD)");
    trades_cmd->add_option("--underlying", trades_underlying, "Filter by underlying symbol");
    trades_cmd->add_option("--account", trades_account, "Filter by account name");

    // Refresh subcommand
    auto* refresh_cmd = app.add_subcommand("refresh", "Refresh market prices and earnings dates for open positions");

    // Parse command line
    CLI11_PARSE(app, argc, argv);

    bool json_mode = (format == "json");

    // Initialize logger early with defaults so config loading logs go to stderr in JSON mode
    ibkr::utils::Logger::init(
        "~/.ibkr-options-analyzer/logs/app.log",
        "info", 10, 5,
        json_mode  // use stderr when JSON output is active
    );

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

    // Re-initialize logger with config settings
    ibkr::utils::Logger::shutdown();
    std::string effective_log_level = log_level.empty() ? config.logging.level : log_level;
    ibkr::utils::Logger::init(
        config.logging.file,
        effective_log_level,
        config.logging.max_file_size_mb,
        config.logging.max_files,
        json_mode
    );

    ibkr::utils::Logger::info("IBKR Options Analyzer v1.0.0");
    if (!config.accounts.empty()) {
        ibkr::utils::Logger::debug("Config loaded: {} accounts, database: {}",
                                  config.accounts.size(), config.database.path);
    } else {
        ibkr::utils::Logger::debug("Config loaded: no accounts configured (using command-line args), database: {}",
                                  config.database.path);
    }

    // Build output options
    ibkr::utils::OutputOptions output_opts;
    output_opts.json = (format == "json");
    output_opts.quiet = quiet;
    output_opts.google_sheet = google_sheet;

    // Execute subcommand
    try {
        if (*download_cmd) {
            // Resolve token/query_id from config when not provided on CLI
            if (download_token.empty() || download_query_id.empty()) {
                const auto* acct = ibkr::commands::DownloadCommand::find_account(
                    config, download_account);
                if (!acct) {
                    ibkr::utils::Logger::error(
                        "Account '{}' not found in config. Available accounts:",
                        download_account);
                    for (const auto& a : config.accounts) {
                        std::cerr << "  - " << a.name << "\n";
                    }
                    ibkr::utils::Logger::shutdown();
                    return EXIT_FAILURE;
                }
                if (download_token.empty()) download_token = acct->token;
                if (download_query_id.empty()) download_query_id = acct->query_id;
            }

            auto result = ibkr::commands::DownloadCommand::execute(
                config,
                download_token,
                download_query_id,
                download_account,
                download_force,
                output_opts
            );

            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Download failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*import_cmd) {
            auto result = ibkr::commands::ImportCommand::execute(
                config,
                import_file,
                "",  // account_filter (not implemented yet)
                false,  // options_only
                true,   // clear_existing
                output_opts
            );

            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Import failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*import_history_cmd) {
            auto result = ibkr::commands::ImportHistoryCommand::execute(
                config,
                import_history_files,
                import_history_account,
                output_opts
            );

            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Historical import failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*analyze_cmd) {
            auto result = ibkr::commands::AnalyzeCommand::execute(
                config,
                analyze_type,
                analyze_account,
                analyze_underlying,
                output_opts
            );

            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Analyze failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }

        } else if (*report_cmd) {
            auto result = ibkr::commands::ReportCommand::execute(
                config,
                report_output,
                report_type,
                report_account,
                report_underlying,
                output_opts
            );

            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Report failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }
        } else if (*trades_cmd) {
            auto result = ibkr::commands::TradesCommand::execute(
                config, trades_date_from, trades_date_to,
                trades_underlying, trades_account, output_opts);
            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Trades failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
                ibkr::utils::Logger::shutdown();
                return EXIT_FAILURE;
            }
        } else if (*refresh_cmd) {
            auto result = ibkr::commands::RefreshCommand::execute(config, output_opts);
            if (!result) {
                if (output_opts.json) {
                    std::cout << ibkr::utils::JsonOutput::error(result.error().format()) << "\n";
                } else {
                    ibkr::utils::Logger::error("Refresh failed: {}", result.error().format());
                    std::cerr << "Error: " << result.error().format() << "\n";
                }
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
