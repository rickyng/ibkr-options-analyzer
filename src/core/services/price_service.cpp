#include "price_service.hpp"
#include "utils/logger.hpp"

namespace ibkr::services {

using utils::Logger;

std::map<std::string, utils::StockPrice> PriceService::fetch_prices(
    const std::vector<std::string>& symbols) {

    Logger::info("Fetching current prices for {} symbols", symbols.size());
    return fetcher_.fetch_prices(symbols);
}

std::map<std::string, utils::StockPrice> PriceService::fetch_for_positions(
    const std::vector<std::string>& underlyings) {

    std::vector<std::string> unique(underlyings.begin(), underlyings.end());
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    Logger::info("Fetching prices for {} unique underlyings", unique.size());
    return fetcher_.fetch_prices(unique);
}

} // namespace ibkr::services