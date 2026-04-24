#pragma once

#include "utils/price_fetcher.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>

namespace ibkr::services {

class PriceService {
public:
    PriceService() = default;

    std::map<std::string, utils::StockPrice> fetch_prices(
        const std::vector<std::string>& symbols);

    std::map<std::string, utils::StockPrice> fetch_for_positions(
        const std::vector<std::string>& underlyings);

private:
    utils::PriceFetcher fetcher_;
};

} // namespace ibkr::services