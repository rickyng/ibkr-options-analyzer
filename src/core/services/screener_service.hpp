#pragma once

#include "config/config_manager.hpp"
#include "utils/price_fetcher.hpp"
#include "utils/result.hpp"
#include "analysis/strategy_detector.hpp"
#include "price_cache_service.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace ibkr::db { class Database; }

namespace ibkr::services {

struct ScreenerResult {
    std::string symbol;
    double current_price{0.0};
    double iv{0.0};
    double iv_percentile{0.0};
    double suggested_strike{0.0};
    int strike_dte{0};
    double premium{0.0};
    double annualized_yield{0.0};
    char risk_reward_grade{'C'};
    bool has_existing_position{false};
    std::string existing_position_detail;
};

struct ScreenerOutput {
    std::vector<ScreenerResult> results;
    int total_scanned{0};
    int passed_filter{0};
    std::vector<std::string> errors;  // symbols that failed to fetch
};

class ScreenerService {
public:
    explicit ScreenerService(const config::ScreenerConfig& config,
                            const config::AlphaVantageConfig& alpha_vantage_config = {});

    ScreenerService(const config::ScreenerConfig& config,
                    const config::AlphaVantageConfig& alpha_vantage_config,
                    db::Database& db);

    ScreenerOutput screen(
        const std::vector<analysis::Position>& existing_positions,
        bool cache_only = false);

private:
    config::ScreenerConfig config_;
    utils::PriceFetcher price_fetcher_;
    std::unique_ptr<PriceCacheService> cache_service_;

    void configure_fetcher(const config::AlphaVantageConfig& cfg);

    char calculate_grade(double otm_pct, double ann_yield, double iv) const;
    double find_best_premium(
        const utils::OptionChainData& chain,
        double target_strike,
        int min_dte,
        int max_dte,
        int& out_dte) const;
    double estimate_iv_percentile(
        const utils::OptionChainData& chain) const;
};

} // namespace ibkr::services
