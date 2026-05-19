#pragma once

#include "db/database.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>

namespace ibkr::services {

/**
 * Wheel cycle tracking service.
 *
 * Identifies and tracks complete wheel cycles:
 * 1. Sell put → assigned → acquire stock
 * 2. Hold stock → sell covered call
 * 3. Call assigned/expired → cycle complete
 *
 * Calculates:
 * - Option P&L: put premium + call premium
 * - Stock P&L: (call_strike - put_strike) * quantity * multiplier
 * - Total Wheel P&L: Option P&L + Stock P&L
 */

struct WheelOverviewMetrics {
    double total_option_pnl{0.0};
    double total_stock_pnl{0.0};
    double total_wheel_pnl{0.0};
    int completed_cycles{0};
    int stock_held_cycles{0};
    int incomplete_cycles{0};
};

struct WheelCycleDisplay {
    int64_t id{0};
    std::string account_name;
    std::string underlying;
    double put_strike{0.0};
    std::optional<double> call_strike;
    int quantity{0};
    int multiplier{100};
    double put_premium{0.0};
    std::optional<double> call_premium;
    std::optional<double> stock_pnl;
    double option_pnl{0.0};
    double total_pnl{0.0};
    std::string put_assigned_date;
    std::string call_close_date;
    std::string call_close_reason;
    std::string cycle_status;
    int64_t put_round_trip_id{0};
    std::optional<int64_t> call_round_trip_id;
};

class WheelCycleService {
public:
    explicit WheelCycleService(db::Database& database);

    /**
     * Build wheel cycles from existing round_trips.
     * Matches put assignments to covered calls.
     * @param account_id Optional account filter (0 = all accounts)
     * @return Result containing number of cycles created
     */
    [[nodiscard]] utils::Result<int> build_wheel_cycles(int64_t account_id = 0);

    /**
     * Get wheel cycle overview metrics.
     */
    [[nodiscard]] utils::Result<WheelOverviewMetrics> get_wheel_overview(
        int64_t account_id = 0,
        const std::string& underlying = "");

    /**
     * Get individual wheel cycles.
     */
    [[nodiscard]] utils::Result<std::vector<WheelCycleDisplay>> get_wheel_cycles(
        int64_t account_id = 0,
        const std::string& underlying = "");

private:
    /**
     * Derive multiplier from underlying symbol.
     * JP stocks (.T suffix) = 1, US/HK stocks = 100.
     */
    static int derive_multiplier(const std::string& underlying);

    /**
     * Derive multiplier from actual trade data (net_premium / qty * open_price).
     * Falls back to derive_multiplier() if data is insufficient.
     */
    static int derive_multiplier_from_data(const db::Database::RoundTrip& rt);

    db::Database& database_;
};

} // namespace ibkr::services