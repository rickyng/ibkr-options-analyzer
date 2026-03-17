#pragma once

#include "db/database.hpp"
#include "utils/result.hpp"
#include <string>
#include <vector>
#include <optional>

namespace ibkr::analysis {

/**
 * Represents an option position for strategy detection.
 */
struct Position {
    int64_t id;
    int64_t account_id;
    std::string symbol;
    std::string underlying;
    std::string expiry;
    double strike;
    char right;  // 'C' or 'P'
    double quantity;
    double mark_price;
    double entry_premium;
    bool is_manual;
};

/**
 * Represents a detected strategy.
 */
struct Strategy {
    enum class Type {
        NakedShortPut,
        NakedShortCall,
        BullPutSpread,
        BearCallSpread,
        IronCondor,
        Straddle,
        Strangle,
        Unknown
    };

    Type type;
    std::string underlying;
    std::string expiry;
    std::vector<Position> legs;

    // Risk metrics (calculated separately)
    double breakeven_price{0.0};
    double max_profit{0.0};
    double max_loss{0.0};
    std::string risk_level;
};

/**
 * Strategy detector for option positions.
 */
class StrategyDetector {
public:
    /**
     * Detect all strategies for an account.
     * @param db Database instance
     * @param account_id Account ID (0 = all accounts)
     * @return Result containing vector of detected strategies
     */
    static utils::Result<std::vector<Strategy>> detect_all_strategies(
        db::Database& db,
        int64_t account_id = 0);

    /**
     * Load all open positions from database.
     * @param db Database instance
     * @param account_id Account ID (0 = all accounts)
     * @return Result containing vector of positions
     */
    static utils::Result<std::vector<Position>> load_positions(
        db::Database& db,
        int64_t account_id = 0);

    /**
     * Detect spreads (bull put, bear call).
     * @param positions Vector of positions (will be modified - matched positions removed)
     * @return Vector of detected spread strategies
     */
    static std::vector<Strategy> detect_spreads(std::vector<Position>& positions);

    /**
     * Detect iron condors.
     * @param strategies Vector of strategies (must contain spreads)
     * @return Vector of detected iron condor strategies
     */
    static std::vector<Strategy> detect_iron_condors(std::vector<Strategy>& strategies);

    /**
     * Detect naked positions (single-leg).
     * @param positions Vector of remaining positions
     * @return Vector of naked position strategies
     */
    static std::vector<Strategy> detect_naked_positions(const std::vector<Position>& positions);

    /**
     * Convert strategy type to string.
     */
    static std::string strategy_type_to_string(Strategy::Type type);

private:
    /**
     * Check if two positions form a bull put spread.
     */
    static std::optional<Strategy> check_bull_put_spread(
        const Position& pos1,
        const Position& pos2);

    /**
     * Check if two positions form a bear call spread.
     */
    static std::optional<Strategy> check_bear_call_spread(
        const Position& pos1,
        const Position& pos2);

    /**
     * Check if two spreads form an iron condor.
     */
    static std::optional<Strategy> check_iron_condor(
        const Strategy& spread1,
        const Strategy& spread2);
};

} // namespace ibkr::analysis
