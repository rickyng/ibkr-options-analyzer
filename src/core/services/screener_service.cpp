#include "screener_service.hpp"
#include "price_cache_service.hpp"
#include "db/database.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>

namespace ibkr::services {

using utils::Logger;

ScreenerService::ScreenerService(const config::ScreenerConfig& config,
                                 const config::AlphaVantageConfig& alpha_vantage_config)
    : config_(config) {
    configure_fetcher(alpha_vantage_config);
}

ScreenerService::ScreenerService(const config::ScreenerConfig& config,
                                 const config::AlphaVantageConfig& alpha_vantage_config,
                                 db::Database& db)
    : config_(config) {
    configure_fetcher(alpha_vantage_config);
    cache_service_ = std::make_unique<PriceCacheService>(db, price_fetcher_);
    Logger::info("ScreenerService configured with Alpha Vantage fallback and caching");
}

void ScreenerService::configure_fetcher(const config::AlphaVantageConfig& cfg) {
    if (!cfg.api_key.empty()) {
        price_fetcher_.set_alpha_vantage_config(
            cfg.api_key, cfg.default_volatility, cfg.risk_free_rate,
            cfg.volatility_lookback_days, cfg.api_call_delay_ms
        );
        price_fetcher_.set_allow_synthetic_options(config_.allow_synthetic_options);
    }
}

ScreenerOutput ScreenerService::screen(
    const std::vector<analysis::Position>& existing_positions,
    bool cache_only) {

    ScreenerOutput output;
    output.total_scanned = static_cast<int>(config_.watchlist.size());

    // Build map of existing positions by underlying
    std::map<std::string, std::string> existing_by_underlying;
    for (const auto& pos : existing_positions) {
        if (pos.right == 'P' && pos.quantity < 0) {
            std::string detail = pos.underlying + " $" +
                std::to_string(static_cast<int>(pos.strike)) + "P exp " + pos.expiry;
            existing_by_underlying[pos.underlying] = detail;
        }
    }

    std::vector<ScreenerResult> all_results;

    for (const auto& symbol : config_.watchlist) {
        // Fetch current price
        double current_price = 0.0;
        utils::OptionChainData chain;

        if (cache_only && cache_service_) {
            auto price_result = cache_service_->get_price_cached_only(symbol);
            if (!price_result || !*price_result) {
                Logger::debug("Screener (cache-only): skipping {}, no cached price", symbol);
                output.errors.push_back(symbol);
                continue;
            }
            current_price = (*price_result)->price;

            auto chain_result = cache_service_->get_option_chain_cached_only(symbol);
            if (!chain_result || !*chain_result) {
                Logger::debug("Screener (cache-only): skipping {}, no cached option chain", symbol);
                output.errors.push_back(symbol);
                continue;
            }
            chain = **chain_result;
        } else {
            auto price_result = cache_service_
                ? cache_service_->get_price(symbol)
                : price_fetcher_.fetch_price(symbol);
            if (!price_result) {
                Logger::warn("Screener: failed to fetch price for {}", symbol);
                output.errors.push_back(symbol);
                continue;
            }
            current_price = price_result->price;

            auto chain_result = cache_service_
                ? cache_service_->get_option_chain(symbol)
                : price_fetcher_.fetch_option_chain(symbol);
            if (!chain_result) {
                Logger::warn("Screener: failed to fetch option chain for {}", symbol);
                output.errors.push_back(symbol);
                continue;
            }
            chain = *chain_result;
        }

        // Calculate suggested strike (OTM buffer)
        double target_strike = current_price * (1.0 - config_.otm_buffer_percent / 100.0);

        // Find best matching premium
        int strike_dte = 0;
        double premium = find_best_premium(chain, target_strike,
                                           config_.min_dte, config_.max_dte, strike_dte);

        if (premium <= 0.0 && existing_by_underlying.find(symbol) == existing_by_underlying.end()) {
            continue;
        }

        // Estimate IV percentile from chain data
        double iv = 0.0;
        double iv_pctl = estimate_iv_percentile(chain);

        // Get average IV from chain puts
        int iv_count = 0;
        for (const auto& [expiry, strikes] : chain.puts_by_expiry) {
            for (const auto& s : strikes) {
                if (s.iv > 0) {
                    iv += s.iv;
                    iv_count++;
                }
            }
        }
        if (iv_count > 0) iv /= iv_count;

        // Annualized yield
        double ann_yield = 0.0;
        if (strike_dte > 0 && target_strike > 0 && premium > 0) {
            ann_yield = (premium / target_strike) * (365.0 / strike_dte) * 100.0;
        }

        // Risk/reward grade
        double otm_pct = ((current_price - target_strike) / current_price) * 100.0;
        char grade = calculate_grade(otm_pct, ann_yield, iv);

        ScreenerResult result;
        result.symbol = symbol;
        result.current_price = current_price;
        result.iv = iv;
        result.iv_percentile = iv_pctl;
        result.suggested_strike = target_strike;
        result.strike_dte = strike_dte;
        result.premium = premium;
        result.annualized_yield = ann_yield;
        result.risk_reward_grade = grade;
        result.has_existing_position = existing_by_underlying.count(symbol) > 0;
        result.existing_position_detail = existing_by_underlying[symbol];

        all_results.push_back(result);
    }

    // Filter: apply min_iv_percentile and min_premium_yield thresholds
    // But always keep symbols with existing positions
    for (auto& r : all_results) {
        bool passes = r.has_existing_position ||
                     (r.iv_percentile >= config_.min_iv_percentile &&
                      r.annualized_yield >= config_.min_premium_yield);
        if (passes) {
            output.results.push_back(r);
            output.passed_filter++;
        }
    }

    // Sort: grade A first, then by annualized yield descending
    std::sort(output.results.begin(), output.results.end(),
        [](const ScreenerResult& a, const ScreenerResult& b) {
            if (a.risk_reward_grade != b.risk_reward_grade)
                return a.risk_reward_grade < b.risk_reward_grade;
            return a.annualized_yield > b.annualized_yield;
        });

    return output;
}

char ScreenerService::calculate_grade(double otm_pct, double ann_yield, double iv) const {
    int score = 0;
    if (otm_pct >= 10.0) score += 3;
    else if (otm_pct >= 5.0) score += 2;
    else score += 1;

    if (ann_yield >= 15.0) score += 3;
    else if (ann_yield >= 8.0) score += 2;
    else score += 1;

    if (iv >= 0.40) score += 3;
    else if (iv >= 0.25) score += 2;
    else score += 1;

    if (score >= 8) return 'A';
    if (score >= 6) return 'B';
    if (score >= 4) return 'C';
    return 'D';
}

double ScreenerService::find_best_premium(
    const utils::OptionChainData& chain,
    double target_strike,
    int min_dte,
    int max_dte,
    int& out_dte) const {

    double best_premium = 0.0;
    out_dte = 0;

    for (const auto& expiry : chain.expirations) {
        if (expiry.dte < min_dte || expiry.dte > max_dte) continue;

        auto it = chain.puts_by_expiry.find(expiry.expiry_date);
        if (it == chain.puts_by_expiry.end()) continue;

        for (const auto& strike : it->second) {
            if (strike.strike <= target_strike) {
                double premium = strike.bid > 0 ? strike.bid : strike.last;
                if (premium > best_premium) {
                    best_premium = premium;
                    out_dte = expiry.dte;
                }
            }
        }
    }

    return best_premium;
}

double ScreenerService::estimate_iv_percentile(
    const utils::OptionChainData& chain) const {

    if (chain.puts_by_expiry.empty()) return 0.0;

    // Get ATM IV: use puts with strikes closest to the first expiry's midpoint
    double atm_iv = 0.0;
    int atm_count = 0;
    for (const auto& [expiry, strikes] : chain.puts_by_expiry) {
        for (const auto& s : strikes) {
            if (s.iv > 0) {
                atm_iv += s.iv;
                atm_count++;
            }
        }
    }

    if (atm_count == 0) return 0.0;
    double avg_iv = atm_iv / atm_count;

    // Estimate percentile from IV level using typical market ranges:
    // <15% → ~10th pctl, 15-25% → ~30th, 25-35% → ~50th,
    // 35-50% → ~70th, >50% → ~90th
    if (avg_iv < 0.15) return 10.0;
    if (avg_iv < 0.25) return 30.0 + (avg_iv - 0.15) / 0.10 * 20.0;
    if (avg_iv < 0.35) return 50.0 + (avg_iv - 0.25) / 0.10 * 20.0;
    if (avg_iv < 0.50) return 70.0 + (avg_iv - 0.35) / 0.15 * 20.0;
    return 90.0;
}

} // namespace ibkr::services
