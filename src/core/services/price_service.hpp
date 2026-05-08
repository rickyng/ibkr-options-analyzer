#pragma once

#include "utils/price_fetcher.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace ibkr::db { class Database; }

namespace ibkr::services {

class PriceCacheService;

class PriceService {
public:
    explicit PriceService(db::Database& db);
    ~PriceService();

    PriceService(const PriceService&) = delete;
    PriceService& operator=(const PriceService&) = delete;

    std::map<std::string, utils::StockPrice> fetch_prices(
        const std::vector<std::string>& symbols);

    std::map<std::string, utils::StockPrice> fetch_for_positions(
        const std::vector<std::string>& underlyings);

    PriceCacheService& cache_service() { return *cache_service_; }

private:
    utils::PriceFetcher fetcher_;
    std::unique_ptr<PriceCacheService> cache_service_;
};

} // namespace ibkr::services
