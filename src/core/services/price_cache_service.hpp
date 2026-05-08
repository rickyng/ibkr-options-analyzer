#pragma once

#include "db/database.hpp"
#include "utils/price_fetcher.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace ibkr::services {

class PriceCacheService {
public:
    PriceCacheService(db::Database& db, utils::PriceFetcher& fetcher);

    utils::Result<utils::StockPrice> get_price(const std::string& symbol);
    utils::Result<std::optional<utils::StockPrice>> get_price_cached_only(const std::string& symbol);
    std::map<std::string, utils::StockPrice> get_prices(const std::vector<std::string>& symbols);
    utils::Result<double> get_volatility(const std::string& symbol);
    utils::Result<utils::OptionChainData> get_option_chain(const std::string& symbol);
    utils::Result<std::optional<utils::OptionChainData>> get_option_chain_cached_only(const std::string& symbol);

    utils::Result<void> clear_cache();
    utils::Result<void> clear_expired();

private:
    db::Database& db_;
    utils::PriceFetcher& fetcher_;
    std::string get_current_trading_date();
    bool is_cache_valid(const std::string& trading_date);

    utils::Result<void> save_price(const utils::StockPrice& price, const std::string& trading_date);
    utils::Result<std::optional<utils::StockPrice>> load_cached_price(const std::string& symbol);

    utils::Result<void> save_volatility(const std::string& symbol, double vol,
                                  const std::string& source, const std::string& trading_date);
    utils::Result<std::optional<double>> load_cached_volatility(const std::string& symbol);

    utils::Result<void> save_option_chain(const utils::OptionChainData& chain,
                                    const std::string& source, const std::string& trading_date);
    utils::Result<std::optional<utils::OptionChainData>> load_cached_option_chain(const std::string& symbol);
};

} // namespace ibkr::services
