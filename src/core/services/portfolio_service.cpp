#include "portfolio_service.hpp"
#include "analysis/risk_calculator.hpp"
#include "utils/logger.hpp"
#include "utils/currency.hpp"
#include <algorithm>
#include <cmath>

namespace ibkr::services {

using utils::Logger;

PortfolioView PortfolioService::build_portfolio_view(
    const std::vector<analysis::Position>& positions,
    const std::map<std::string, utils::StockPrice>& current_prices,
    const std::map<int64_t, std::string>& account_names) {

    PortfolioView view;
    view.total_positions = static_cast<int>(positions.size());
    utils::CurrencyConverter converter;

    for (const auto& pos : positions) {
        PortfolioPosition pp;
        pp.position = pos;

        auto acct_it = account_names.find(pos.account_id);
        pp.account_name = (acct_it != account_names.end()) ? acct_it->second : "Unknown";

        auto price_it = current_prices.find(pos.underlying);
        pp.has_price = (price_it != current_prices.end());
        if (pp.has_price) {
            pp.current_price = price_it->second.price;
        }

        std::string currency = utils::deduce_currency(pos.underlying);
        double fx_rate = converter.convert(1.0, currency);

        int dte = analysis::RiskCalculator::calculate_days_to_expiry(pos.expiry);

        // P&L calculation (for short positions)
        if (pos.quantity < 0) {
            double entry_total = std::abs(pos.quantity) * pos.entry_premium
                               * pos.multiplier * fx_rate;
            view.total_premium_collected += entry_total;

            pp.current_premium = pos.mark_price > 0 ? pos.mark_price : pos.entry_premium;
            pp.pnl = entry_total - (std::abs(pos.quantity) * pp.current_premium
                     * pos.multiplier * fx_rate);
            pp.pnl_percent = entry_total > 0 ? (pp.pnl / entry_total) * 100.0 : 0.0;
        }

        // OTM% and risk alerts (short puts only)
        if (pp.has_price && pos.right == 'P' && pos.quantity < 0) {
            double cp = pp.current_price;
            pp.otm_percent = ((cp - pos.strike) / cp) * 100.0;

            if (cp < pos.strike) {
                pp.risk_alert = "ITM";
                view.itm_count++;
            } else if (pp.otm_percent < 5.0) {
                pp.risk_alert = "NEAR";
                view.near_money_count++;
            }

            if (dte > 0 && pos.strike > 0) {
                pp.annualized_yield = (pos.entry_premium / pos.strike) * (365.0 / dte) * 100.0;
            }
        }

        // Expiring soon alert
        if (dte <= 7 && dte >= 0) {
            if (pp.risk_alert.empty()) {
                pp.risk_alert = "EXPIRING";
            }
            view.expiring_soon_count++;
        }

        view.total_unrealized_pnl += pp.pnl;

        // DTE buckets
        if (dte >= 0) {
            std::string bucket;
            if (dte <= 7) bucket = "<=7";
            else if (dte <= 30) bucket = "8-30";
            else if (dte <= 60) bucket = "31-60";
            else bucket = "60+";
            view.dte_buckets[bucket]++;
        }

        // 10%/20% loss per account
        if (pos.right == 'P' && pos.quantity < 0) {
            double loss_10 = std::max(0.0, pos.strike * 0.10 - pos.entry_premium)
                           * pos.multiplier * std::abs(pos.quantity) * fx_rate;
            double loss_20 = std::max(0.0, pos.strike * 0.20 - pos.entry_premium)
                           * pos.multiplier * std::abs(pos.quantity) * fx_rate;
            view.loss_10pct[pp.account_name] += loss_10;
            view.loss_20pct[pp.account_name] += loss_20;
        }

        view.positions.push_back(pp);
    }

    // Sort: ITM first, then NEAR, then by DTE ascending, then by underlying
    std::sort(view.positions.begin(), view.positions.end(),
        [](const PortfolioPosition& a, const PortfolioPosition& b) {
            auto alert_rank = [](const std::string& s) {
                if (s == "ITM") return 0;
                if (s == "NEAR") return 1;
                if (s == "EXPIRING") return 2;
                return 3;
            };
            int ra = alert_rank(a.risk_alert), rb = alert_rank(b.risk_alert);
            if (ra != rb) return ra < rb;
            int da = analysis::RiskCalculator::calculate_days_to_expiry(a.position.expiry);
            int db = analysis::RiskCalculator::calculate_days_to_expiry(b.position.expiry);
            if (da != db) return da < db;
            return a.position.underlying < b.position.underlying;
        });

    return view;
}

} // namespace ibkr::services
