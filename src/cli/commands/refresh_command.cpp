#include "refresh_command.hpp"
#include "services/position_service.hpp"
#include "services/price_service.hpp"
#include "db/database.hpp"
#include "db/trade_repository.hpp"
#include "db/adjustment_repository.hpp"
#include "utils/logger.hpp"
#include "utils/json_output.hpp"
#include <iostream>
#include <algorithm>

namespace ibkr::commands {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<void> RefreshCommand::execute(
    const config::Config& config,
    const utils::OutputOptions& output_opts) {

    Logger::info("Starting refresh: fetching prices and earnings dates");

    // Initialize database
    db::Database database(config.database.path);
    auto init_result = database.initialize();
    if (!init_result) {
        return Error{"Failed to initialize database", init_result.error().message};
    }

    // Load all open positions to get underlyings
    services::PositionService position_service(database);
    auto positions_result = position_service.load_positions("", "");
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    if (positions.empty()) {
        if (output_opts.json) {
            std::cout << utils::JsonOutput::refresh_result(0, 0, {}) << "\n";
        } else {
            std::cout << "No open positions found. Nothing to refresh.\n";
        }
        return Result<void>{};
    }

    // Deduplicate underlyings
    std::vector<std::string> underlyings;
    for (const auto& pos : positions) underlyings.push_back(pos.underlying);

    // Also include symbols with open stock holdings (not in open_options)
    db::AdjustmentRepository adjustments(database.connection());
    db::TradeRepository trade_repo(database.connection(), adjustments);
    auto stock_symbols = trade_repo.get_stock_holding_symbols();
    if (stock_symbols) {
        for (const auto& sym : *stock_symbols) {
            underlyings.push_back(sym);
        }
    }

    std::sort(underlyings.begin(), underlyings.end());
    underlyings.erase(std::unique(underlyings.begin(), underlyings.end()), underlyings.end());

    Logger::info("Refreshing data for {} unique underlyings", underlyings.size());

    // Fetch prices (automatically cached to cached_prices table)
    services::PriceService price_service(database);
    auto current_prices = price_service.fetch_for_positions(underlyings);
    int prices_refreshed = static_cast<int>(current_prices.size());

    // Fetch and cache earnings dates via PriceService
    int earnings_refreshed = 0;
    auto earnings_result = price_service.fetch_and_cache_earnings(underlyings);
    if (earnings_result) {
        earnings_refreshed = *earnings_result;
    } else {
        Logger::warn("Failed to fetch some earnings dates: {}", earnings_result.error().message);
    }

    // Collect symbols that failed both price and earnings
    std::vector<std::string> failed_symbols;
    for (const auto& sym : underlyings) {
        bool has_price = current_prices.count(sym) > 0;
        // Only US underlyings have earnings; a price fetch is the fallback success signal
        if (!has_price) {
            failed_symbols.push_back(sym);
        }
    }

    // Output
    if (output_opts.json) {
        std::cout << utils::JsonOutput::refresh_result(
            prices_refreshed, earnings_refreshed, failed_symbols) << "\n";
    } else if (!output_opts.quiet) {
        std::cout << "Refreshed " << prices_refreshed << " prices, "
                  << earnings_refreshed << " earnings dates for "
                  << underlyings.size() << " underlyings.\n";
        if (!failed_symbols.empty()) {
            std::cout << "Failed: ";
            for (size_t i = 0; i < failed_symbols.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << failed_symbols[i];
            }
            std::cout << "\n";
        }
    }

    Logger::info("Refresh complete: {} prices, {} earnings, {} failed",
                prices_refreshed, earnings_refreshed, failed_symbols.size());
    return Result<void>{};
}

} // namespace ibkr::commands
