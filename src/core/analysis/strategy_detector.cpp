#include "strategy_detector.hpp"
#include "utils/logger.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ibkr::analysis {

using utils::Logger;

std::vector<Strategy> StrategyDetector::detect_all_strategies(
    std::span<const Position> positions) {

    Logger::info("Detecting strategies from {} positions", positions.size());

    // Copy positions since we'll modify during detection
    std::vector<Position> remaining(positions.begin(), positions.end());
    std::vector<Strategy> all_strategies;

    // Step 1: Detect spreads (modifies remaining vector)
    auto spreads = detect_spreads(remaining);
    Logger::info("Detected {} spreads", spreads.size());
    all_strategies.insert(all_strategies.end(), spreads.begin(), spreads.end());

    // Step 2: Detect iron condors from spreads
    auto condors = detect_iron_condors(spreads);
    Logger::info("Detected {} iron condors", condors.size());
    all_strategies.insert(all_strategies.end(), condors.begin(), condors.end());

    // Step 3: Detect naked positions from remaining
    auto naked = detect_naked_positions(remaining);
    Logger::info("Detected {} naked positions", naked.size());
    all_strategies.insert(all_strategies.end(), naked.begin(), naked.end());

    Logger::info("Total strategies detected: {}", all_strategies.size());
    return all_strategies;
}

std::vector<Strategy> StrategyDetector::detect_spreads(std::vector<Position>& positions) {
    std::vector<Strategy> spreads;
    std::unordered_set<size_t> matched;

    for (size_t i = 0; i < positions.size(); ++i) {
        if (matched.count(i)) continue;

        for (size_t j = i + 1; j < positions.size(); ++j) {
            if (matched.count(j)) continue;

            const auto& pos1 = positions[i];
            const auto& pos2 = positions[j];

            // Check for bull put spread
            auto bull_put = check_bull_put_spread(pos1, pos2);
            if (bull_put) {
                spreads.push_back(*bull_put);
                matched.insert(i);
                matched.insert(j);
                break;
            }

            // Check for bear call spread
            auto bear_call = check_bear_call_spread(pos1, pos2);
            if (bear_call) {
                spreads.push_back(*bear_call);
                matched.insert(i);
                matched.insert(j);
                break;
            }
        }
    }

    // Swap-and-pop: move unmatched to front, then truncate
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < positions.size(); ++read_idx) {
        if (!matched.count(read_idx)) {
            if (write_idx != read_idx) {
                positions[write_idx] = std::move(positions[read_idx]);
            }
            ++write_idx;
        }
    }
    positions.resize(write_idx);

    return spreads;
}

static std::optional<Strategy> check_credit_spread(
    const Position& pos1,
    const Position& pos2,
    char expected_right,
    bool short_strike_must_be_higher) {

    // Must be same underlying and expiry
    if (pos1.underlying != pos2.underlying || pos1.expiry != pos2.expiry) {
        return std::nullopt;
    }

    // Must both be the expected right (P or C)
    if (pos1.right != expected_right || pos2.right != expected_right) {
        return std::nullopt;
    }

    // One must be short, one must be long
    bool pos1_short = pos1.quantity < 0;
    bool pos2_short = pos2.quantity < 0;

    if (pos1_short == pos2_short) {
        return std::nullopt;  // Both short or both long
    }

    // Identify which is short and which is long
    const Position* short_leg = pos1_short ? &pos1 : &pos2;
    const Position* long_leg = pos1_short ? &pos2 : &pos1;

    // Check strike relationship based on spread type
    if (short_strike_must_be_higher) {
        // Bull put spread: short strike > long strike
        if (short_leg->strike <= long_leg->strike) return std::nullopt;
    } else {
        // Bear call spread: short strike < long strike
        if (short_leg->strike >= long_leg->strike) return std::nullopt;
    }

    // Quantities must match (absolute value)
    if (std::abs(short_leg->quantity) != std::abs(long_leg->quantity)) {
        return std::nullopt;
    }

    Strategy strategy;
    strategy.type = short_strike_must_be_higher ? Strategy::Type::BullPutSpread : Strategy::Type::BearCallSpread;
    strategy.underlying = pos1.underlying;
    strategy.expiry = pos1.expiry;
    strategy.legs.push_back(*short_leg);
    strategy.legs.push_back(*long_leg);

    return strategy;
}

std::optional<Strategy> StrategyDetector::check_bull_put_spread(
    const Position& pos1,
    const Position& pos2) {
    return check_credit_spread(pos1, pos2, 'P', true);
}

std::optional<Strategy> StrategyDetector::check_bear_call_spread(
    const Position& pos1,
    const Position& pos2) {
    return check_credit_spread(pos1, pos2, 'C', false);
}

std::vector<Strategy> StrategyDetector::detect_iron_condors(std::vector<Strategy>& strategies) {
    std::vector<Strategy> condors;
    std::unordered_set<size_t> matched;

    for (size_t i = 0; i < strategies.size(); ++i) {
        if (matched.count(i)) continue;

        for (size_t j = i + 1; j < strategies.size(); ++j) {
            if (matched.count(j)) continue;

            auto condor = check_iron_condor(strategies[i], strategies[j]);
            if (condor) {
                condors.push_back(*condor);
                matched.insert(i);
                matched.insert(j);
                break;
            }
        }
    }

    // Swap-and-pop for strategies
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < strategies.size(); ++read_idx) {
        if (!matched.count(read_idx)) {
            if (write_idx != read_idx) {
                strategies[write_idx] = std::move(strategies[read_idx]);
            }
            ++write_idx;
        }
    }
    strategies.resize(write_idx);

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
