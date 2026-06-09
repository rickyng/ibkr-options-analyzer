#include "strategy_detector.hpp"

namespace ibkr::analysis {

std::string strategy_type_to_string(Strategy::Type type) {
    switch (type) {
        case Strategy::Type::NakedShortPut:
            return "Naked Short Put";
        case Strategy::Type::NakedShortCall:
            return "Naked Short Call";
        default:
            return "Unknown";
    }
}

} // namespace ibkr::analysis
