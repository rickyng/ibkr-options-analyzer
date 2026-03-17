#include "strategy_detector.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>

namespace ibkr::analysis {

using utils::Result;
using utils::Error;
using utils::Logger;

Result<std::vector<Position>> StrategyDetector::load_positions(
    db::Database& db,
    int64_t account_id) {

    if (!db.is_initialized()) {
        return Error{"Database not initialized"};
    }

    try {
        auto db_ptr = db.get_db();
        if (!db_ptr) {
            return Error{"Database not initialized"};
        }

        std::string query = "SELECT id, account_id, symbol, underlying, expiry, strike, "
                           "right, quantity, mark_price, entry_premium, is_manual "
                           "FROM open_options";

        if (account_id > 0) {
            query += " WHERE account_id = ?";
        }

        query += " ORDER BY underlying, expiry, strike";

        SQLite::Statement stmt(*db_ptr, query);
        if (account_id > 0) {
            stmt.bind(1, account_id);
        }

        std::vector<Position> positions;
        while (stmt.executeStep()) {
            Position pos;
            pos.id = stmt.getColumn(0).getInt64();
            pos.account_id = stmt.getColumn(1).getInt64();
            pos.symbol = stmt.getColumn(2).getString();
            pos.underlying = stmt.getColumn(3).getString();
            pos.expiry = stmt.getColumn(4).getString();
            pos.strike = stmt.getColumn(5).getDouble();
            pos.right = stmt.getColumn(6).getString()[0];
            pos.quantity = stmt.getColumn(7).getDouble();
            pos.mark_price = stmt.getColumn(8).getDouble();
            pos.entry_premium = stmt.getColumn(9).getDouble();
            pos.is_manual = stmt.getColumn(10).getInt() == 1;
            positions.push_back(pos);
        }

        Logger::info("Loaded {} positions from database", positions.size());
        return positions;

    } catch (const std::exception& e) {
        return Error{
            "Failed to load positions",
            std::string(e.what())
        };
    }
}

Result<std::vector<Strategy>> StrategyDetector::detect_all_strategies(
    db::Database& db,
    int64_t account_id) {

    Logger::info("Detecting strategies for account_id={}", account_id);

    // Load all positions
    auto positions_result = load_positions(db, account_id);
    if (!positions_result) {
        return Error{
            "Failed to load positions",
            positions_result.error().message
        };
    }

    std::vector<Position> positions = *positions_result;
    std::vector<Strategy> all_strategies;

    // Step 1: Detect spreads (modifies positions vector)
    auto spreads = detect_spreads(positions);
    Logger::info("Detected {} spreads", spreads.size());
    all_strategies.insert(all_strategies.end(), spreads.begin(), spreads.end());

    // Step 2: Detect iron condors from spreads
    auto condors = detect_iron_condors(spreads);
    Logger::info("Detected {} iron condors", condors.size());
    all_strategies.insert(all_strategies.end(), condors.begin(), condors.end());

    // Step 3: Detect naked positions from remaining
    auto naked = detect_naked_positions(positions);
    Logger::info("Detected {} naked positions", naked.size());
    all_strategies.insert(all_strategies.end(), naked.begin(), naked.end());

    Logger::info("Total strategies detected: {}", all_strategies.size());
    return all_strategies;
}

std::vector<Strategy> StrategyDetector::detect_spreads(std::vector<Position>& positions) {
    std::vector<Strategy> spreads;
    std::vector<size_t> matched_indices;

    // Check all pairs of positions
    for (size_t i = 0; i < positions.size(); ++i) {
        if (std::find(matched_indices.begin(), matched_indices.end(), i) != matched_indices.end()) {
            continue;  // Already matched
        }

        for (size_t j = i + 1; j < positions.size(); ++j) {
            if (std::find(matched_indices.begin(), matched_indices.end(), j) != matched_indices.end()) {
                continue;  // Already matched
            }

            const auto& pos1 = positions[i];
            const auto& pos2 = positions[j];

            // Check for bull put spread
            auto bull_put = check_bull_put_spread(pos1, pos2);
            if (bull_put) {
                spreads.push_back(*bull_put);
                matched_indices.push_back(i);
                matched_indices.push_back(j);
                break;
            }

            // Check for bear call spread
            auto bear_call = check_bear_call_spread(pos1, pos2);
            if (bear_call) {
                spreads.push_back(*bear_call);
                matched_indices.push_back(i);
                matched_indices.push_back(j);
                break;
            }
        }
    }

    // Remove matched positions from vector
    std::sort(matched_indices.rbegin(), matched_indices.rend());
    for (size_t idx : matched_indices) {
        positions.erase(positions.begin() + idx);
    }

    return spreads;
}

std::optional<Strategy> StrategyDetector::check_bull_put_spread(
    const Position& pos1,
    const Position& pos2) {

    // Must be same underlying and expiry
    if (pos1.underlying != pos2.underlying || pos1.expiry != pos2.expiry) {
        return std::nullopt;
    }

    // Must both be puts
    if (pos1.right != 'P' || pos2.right != 'P') {
        return std::nullopt;
    }

    // One must be short, one must be long
    bool pos1_short = pos1.quantity < 0;
    bool pos2_short = pos2.quantity < 0;

    if (pos1_short == pos2_short) {
        // Both short or both long - not a spread
        return std::nullopt;
    }

    // Identify which is short and which is long
    const Position* short_leg = pos1_short ? &pos1 : &pos2;
    const Position* long_leg = pos1_short ? &pos2 : &pos1;

    // Short strike must be higher than long strike (bull put spread)
    if (short_leg->strike <= long_leg->strike) {
        return std::nullopt;
    }

    // Quantities must match (absolute value)
    if (std::abs(short_leg->quantity) != std::abs(long_leg->quantity)) {
        return std::nullopt;
    }

    Strategy strategy;
    strategy.type = Strategy::Type::BullPutSpread;
    strategy.underlying = pos1.underlying;
    strategy.expiry = pos1.expiry;
    strategy.legs.push_back(*short_leg);  // Short leg (higher strike)
    strategy.legs.push_back(*long_leg);   // Long leg (lower strike)

    return strategy;
}

std::optional<Strategy> StrategyDetector::check_bear_call_spread(
    const Position& pos1,
    const Position& pos2) {

    // Must be same underlying and expiry
    if (pos1.underlying != pos2.underlying || pos1.expiry != pos2.expiry) {
        return std::nullopt;
    }

    // Must both be calls
    if (pos1.right != 'C' || pos2.right != 'C') {
        return std::nullopt;
    }

    // One must be short, one must be long
    bool pos1_short = pos1.quantity < 0;
    bool pos2_short = pos2.quantity < 0;

    if (pos1_short == pos2_short) {
        // Both short or both long - not a spread
        return std::nullopt;
    }

    // Identify which is short and which is long
    const Position* short_leg = pos1_short ? &pos1 : &pos2;
    const Position* long_leg = pos1_short ? &pos2 : &pos1;

    // Short strike must be lower than long strike (bear call spread)
    if (short_leg->strike >= long_leg->strike) {
        return std::nullopt;
    }

    // Quantities must match (absolute value)
    if (std::abs(short_leg->quantity) != std::abs(long_leg->quantity)) {
        return std::nullopt;
    }

    Strategy strategy;
    strategy.type = Strategy::Type::BearCallSpread;
    strategy.underlying = pos1.underlying;
    strategy.expiry = pos1.expiry;
    strategy.legs.push_back(*short_leg);  // Short leg (lower strike)
    strategy.legs.push_back(*long_leg);   // Long leg (higher strike)

    return strategy;
}

std::vector<Strategy> StrategyDetector::detect_iron_condors(std::vector<Strategy>& strategies) {
    std::vector<Strategy> condors;
    std::vector<size_t> matched_indices;

    // Check all pairs of spreads
    for (size_t i = 0; i < strategies.size(); ++i) {
        if (std::find(matched_indices.begin(), matched_indices.end(), i) != matched_indices.end()) {
            continue;
        }

        for (size_t j = i + 1; j < strategies.size(); ++j) {
            if (std::find(matched_indices.begin(), matched_indices.end(), j) != matched_indices.end()) {
                continue;
            }

            auto condor = check_iron_condor(strategies[i], strategies[j]);
            if (condor) {
                condors.push_back(*condor);
                matched_indices.push_back(i);
                matched_indices.push_back(j);
                break;
            }
        }
    }

    // Remove matched spreads from vector
    std::sort(matched_indices.rbegin(), matched_indices.rend());
    for (size_t idx : matched_indices) {
        strategies.erase(strategies.begin() + idx);
    }

    return condors;
}

std::optional<Strategy> StrategyDetector::check_iron_condor(
    const Strategy& spread1,
    const Strategy& spread2) {

    // Must be same underlying and expiry
    if (spread1.underlying != spread2.underlying || spread1.expiry != spread2.expiry) {
        return std::nullopt;
    }

    // One must be bull put spread, one must be bear call spread
    bool has_bull_put = (spread1.type == Strategy::Type::BullPutSpread ||
                         spread2.type == Strategy::Type::BullPutSpread);
    bool has_bear_call = (spread1.type == Strategy::Type::BearCallSpread ||
                          spread2.type == Strategy::Type::BearCallSpread);

    if (!has_bull_put || !has_bear_call) {
        return std::nullopt;
    }

    Strategy condor;
    condor.type = Strategy::Type::IronCondor;
    condor.underlying = spread1.underlying;
    condor.expiry = spread1.expiry;

    // Add all legs from both spreads
    condor.legs.insert(condor.legs.end(), spread1.legs.begin(), spread1.legs.end());
    condor.legs.insert(condor.legs.end(), spread2.legs.begin(), spread2.legs.end());

    return condor;
}

std::vector<Strategy> StrategyDetector::detect_naked_positions(
    const std::vector<Position>& positions) {

    std::vector<Strategy> naked_strategies;

    for (const auto& pos : positions) {
        Strategy strategy;

        // Determine strategy type
        if (pos.right == 'P' && pos.quantity < 0) {
            strategy.type = Strategy::Type::NakedShortPut;
        } else if (pos.right == 'C' && pos.quantity < 0) {
            strategy.type = Strategy::Type::NakedShortCall;
        } else {
            strategy.type = Strategy::Type::Unknown;
        }

        strategy.underlying = pos.underlying;
        strategy.expiry = pos.expiry;
        strategy.legs.push_back(pos);

        naked_strategies.push_back(strategy);
    }

    return naked_strategies;
}

std::string StrategyDetector::strategy_type_to_string(Strategy::Type type) {
    switch (type) {
        case Strategy::Type::NakedShortPut:
            return "Naked Short Put";
        case Strategy::Type::NakedShortCall:
            return "Naked Short Call";
        case Strategy::Type::BullPutSpread:
            return "Bull Put Spread";
        case Strategy::Type::BearCallSpread:
            return "Bear Call Spread";
        case Strategy::Type::IronCondor:
            return "Iron Condor";
        case Strategy::Type::Straddle:
            return "Straddle";
        case Strategy::Type::Strangle:
            return "Strangle";
        default:
            return "Unknown";
    }
}

} // namespace ibkr::analysis
