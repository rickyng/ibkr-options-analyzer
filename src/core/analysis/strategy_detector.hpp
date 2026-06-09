#pragma once

#include <string>
#include <vector>

namespace ibkr::analysis {

/**
 * Represents an option position.
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
    std::string currency;
    bool is_manual;
    double multiplier{100.0};
};

/**
 * Represents a strategy (used by risk calculator and impact analysis).
 */
struct Strategy {
    enum class Type {
        NakedShortPut,
        NakedShortCall,
        Unknown
    };

    Type type;
    std::string underlying;
    std::string expiry;
    std::string currency;
    std::vector<Position> legs;
};

/**
 * Convert strategy type enum to human-readable string.
 */
std::string strategy_type_to_string(Strategy::Type type);

} // namespace ibkr::analysis
