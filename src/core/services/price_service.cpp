#include "price_service.hpp"
#include "price_cache_service.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"

namespace ibkr::services {

using utils::Logger;

PriceService::PriceService(db::Database& db) {
    cache_service_ = std::make_unique<PriceCacheService>(db, fetcher_);
    auto clear_result = cache_service_->clear_expired();
    if (!clear_result) {
        Logger::warn("Failed to clear expired cache: {}", clear_result.error().message);
    }
    Logger::info("PriceService initialized with SQLite caching");
}

PriceService::~PriceService() = default;

std::map<std::string, utils::StockPrice> PriceService::fetch_prices(
    const std::vector<std::string>& symbols) {

    Logger::info("Fetching prices for {} symbols (with cache)", symbols.size());
    return cache_service_->get_prices(symbols);
}

std::map<std::string, utils::StockPrice> PriceService::fetch_for_positions(
    const std::vector<std::string>& underlyings) {

    std::vector<std::string> unique(underlyings.begin(), underlyings.end());
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    Logger::info("Fetching prices for {} unique underlyings (with cache)", unique.size());
    return cache_service_->get_prices(unique);
}

} // namespace ibkr::services
