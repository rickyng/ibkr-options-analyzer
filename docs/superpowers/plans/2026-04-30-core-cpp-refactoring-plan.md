# Core C++ Refactoring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `src/core/` to fix 20 identified issues: correctness bugs, design problems, performance issues, and modernization opportunities.

**Architecture:** Changes are localized per module. Start with foundational utilities (`result.hpp`, `http_client`), then core analysis, then data layer, then parsers, services, and finally flex integration. Each module compiles before moving to next.

**Tech Stack:** C++20, Howard Hinnant's `date` library (already a dependency), SQLiteCpp, httplib, rapidcsv, spdlog.

---

## Task 1: Add `[[nodiscard]]` and optimize `Result<void>` in result.hpp

**Files:**
- Modify: `src/core/utils/result.hpp:59-155, 159-179`

- [ ] **Step 1: Add `[[nodiscard]]` to Result class template**

```cpp
// result.hpp, line 59 - add [[nodiscard]] before class declaration
template<typename T>
class [[nodiscard]] Result {
```

- [ ] **Step 2: Add `[[nodiscard]]` to Result<void> specialization**

```cpp
// result.hpp, line 159 - add [[nodiscard]] before class declaration
template<>
class [[nodiscard]] Result<void> {
```

- [ ] **Step 3: Add `[[nodiscard]]` to make_error helper functions**

```cpp
// result.hpp, lines 182-195 - add [[nodiscard]] before each function
template<typename T>
[[nodiscard]] Result<T> make_error(std::string message) {
    return Error{std::move(message)};
}

template<typename T>
[[nodiscard]] Result<T> make_error(std::string message, std::string context) {
    return Error{std::move(message), std::move(context)};
}

template<typename T>
[[nodiscard]] Result<T> make_error(std::string message, std::string context, int code) {
    return Error{std::move(message), std::move(context), code};
}
```

- [ ] **Step 4: Optimize Result<void> to use std::optional<Error>**

Replace the entire `Result<void>` specialization (lines 159-179):

```cpp
template<>
class [[nodiscard]] Result<void> {
public:
    Result() : error_{} {}  // success state, no allocation
    Result(Error error) : error_{std::move(error)} {}

    explicit operator bool() const noexcept { return !error_.has_value(); }
    bool has_value() const noexcept { return !error_.has_value(); }

    const Error& error() const & { return *error_; }
    Error& error() & { return *error_; }
    Error&& error() && { return std::move(*error_); }

    void value() const {
        if (error_.has_value()) throw std::runtime_error(error_->format());
    }

private:
    std::optional<Error> error_;  // empty = success, contains Error on failure
};
```

- [ ] **Step 5: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully with no errors

- [ ] **Step 6: Commit**

```bash
git add src/core/utils/result.hpp
git commit -m "refactor(utils): add [[nodiscard]] and optimize Result<void> with std::optional"
```

---

## Task 2: Add RiskLevel enum and helper functions to risk_calculator.hpp

**Files:**
- Modify: `src/core/analysis/risk_calculator.hpp:1-122`

- [ ] **Step 1: Add RiskLevel enum before RiskMetrics struct**

```cpp
// risk_calculator.hpp, after includes, before RiskMetrics
enum class RiskLevel {
    Low,
    Medium,
    High,
    Defined
};

inline std::string risk_level_to_string(RiskLevel level) {
    switch (level) {
        case RiskLevel::Low: return "LOW";
        case RiskLevel::Medium: return "MEDIUM";
        case RiskLevel::High: return "HIGH";
        case RiskLevel::Defined: return "DEFINED";
        default: return "UNKNOWN";
    }
}
```

- [ ] **Step 2: Add CONTRACT_MULTIPLIER constant and premium_for helper**

```cpp
// risk_calculator.hpp, after RiskLevel enum
namespace constants {
    constexpr double CONTRACT_MULTIPLIER = 100.0;
    constexpr int MAX_DTE_INVALID = std::numeric_limits<int>::max();
}

inline double premium_for(double quantity, double price_per_contract) {
    return std::abs(quantity) * price_per_contract * constants::CONTRACT_MULTIPLIER;
}
```

- [ ] **Step 3: Change RiskMetrics::risk_level from string to enum**

```cpp
// risk_calculator.hpp, line 18 - change string to enum
struct RiskMetrics {
    double breakeven_price{0.0};
    double breakeven_price_2{0.0};  // For iron condors (two breakevens)
    double max_profit{0.0};
    double max_loss{0.0};
    RiskLevel risk_level{RiskLevel::Low};  // Changed from std::string
    double net_premium{0.0};
    int days_to_expiry{0};
};
```

- [ ] **Step 4: Build to verify header changes compile**

Run: `cmake --build build/debug`
Expected: Some errors in downstream files (json_output, csv_exporter, analyze_command) - that's expected, will fix in later tasks

- [ ] **Step 5: Commit**

```bash
git add src/core/analysis/risk_calculator.hpp
git commit -m "refactor(analysis): add RiskLevel enum and premium helper"
```

---

## Task 3: Update risk_calculator.cpp to use RiskLevel enum and premium helper

**Files:**
- Modify: `src/core/analysis/risk_calculator.cpp:12-246`

- [ ] **Step 1: Replace risk_level string assignments with enum**

```cpp
// risk_calculator.cpp, line 51 - change to enum
metrics.risk_level = RiskLevel::High;

// line 75
metrics.risk_level = RiskLevel::High;

// line 105
metrics.risk_level = RiskLevel::Defined;

// line 135
metrics.risk_level = RiskLevel::Defined;

// line 215
metrics.risk_level = RiskLevel::Defined;
```

- [ ] **Step 2: Replace manual premium calculations with premium_for helper**

```cpp
// risk_calculator.cpp, line 39 - replace
metrics.net_premium = premium_for(pos.quantity, pos.entry_premium);

// line 48
metrics.max_loss = (pos.strike - pos.entry_premium) * constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);

// line 63
metrics.net_premium = premium_for(pos.quantity, pos.entry_premium);

// line 90-91
double short_premium = premium_for(short_leg.quantity, short_leg.entry_premium);
double long_premium = premium_for(long_leg.quantity, long_leg.entry_premium);

// line 102
metrics.max_loss = (strike_diff * constants::CONTRACT_MULTIPLIER * std::abs(short_leg.quantity)) - metrics.net_premium;

// lines 120-121, 132 - similar replacements for bear call spread

// lines 176-185 - iron condor premium loop
if (leg.quantity < 0) {
    total_premium += premium_for(leg.quantity, leg.entry_premium);
} else {
    total_premium -= premium_for(leg.quantity, leg.entry_premium);
}

// line 212
metrics.max_loss = (max_spread_width * constants::CONTRACT_MULTIPLIER) - metrics.net_premium;
```

- [ ] **Step 3: Replace calculate_days_to_expiry with date library implementation**

```cpp
// risk_calculator.cpp, replace entire function (lines 223-246)
int RiskCalculator::calculate_days_to_expiry(const std::string& expiry_date) {
    using namespace date;
    
    // Parse expiry date (YYYY-MM-DD)
    std::istringstream ss(expiry_date);
    year_month_day ymd;
    ss >> parse("%F", ymd);
    
    if (ss.fail() || !ymd.ok()) {
        Logger::warn("Invalid expiry date format: '{}'", expiry_date);
        return constants::MAX_DTE_INVALID;
    }
    
    // Get current date
    auto today = floor<days>(system_clock::now());
    auto expiry = sys_days{ymd};
    
    // Calculate difference in days
    auto diff = expiry - today;
    return static_cast<int>(diff.count());
}
```

- [ ] **Step 4: Add missing include for date library**

```cpp
// risk_calculator.cpp, at top after existing includes
#include <date/date.h>
```

- [ ] **Step 5: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 6: Commit**

```bash
git add src/core/analysis/risk_calculator.cpp
git commit -m "refactor(analysis): use RiskLevel enum and premium helper in calculations"
```

---

## Task 4: Remove dead fields from Strategy struct in strategy_detector.hpp

**Files:**
- Modify: `src/core/analysis/strategy_detector.hpp:44-53`

- [ ] **Step 1: Remove breakeven_price, max_profit, max_loss, risk_level from Strategy**

```cpp
// strategy_detector.hpp, lines 44-53 - remove these fields
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
    // Removed: breakeven_price, max_profit, max_loss, risk_level
};
```

- [ ] **Step 2: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles (fields were never populated anyway)

- [ ] **Step 3: Commit**

```bash
git add src/core/analysis/strategy_detector.hpp
git commit -m "refactor(analysis): remove dead risk fields from Strategy struct"
```

---

## Task 5: Consolidate spread detection functions in strategy_detector.cpp

**Files:**
- Modify: `src/core/analysis/strategy_detector.cpp:153-245`

- [ ] **Step 1: Add new check_credit_spread function after detect_spreads**

```cpp
// strategy_detector.cpp, after detect_spreads function (around line 152)
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
```

- [ ] **Step 2: Replace check_bull_put_spread to call check_credit_spread**

```cpp
// strategy_detector.cpp, replace existing check_bull_put_spread (lines 153-198)
std::optional<Strategy> StrategyDetector::check_bull_put_spread(
    const Position& pos1,
    const Position& pos2) {
    return check_credit_spread(pos1, pos2, 'P', true);
}
```

- [ ] **Step 3: Replace check_bear_call_spread to call check_credit_spread**

```cpp
// strategy_detector.cpp, replace existing check_bear_call_spread (lines 200-245)
std::optional<Strategy> StrategyDetector::check_bear_call_spread(
    const Position& pos1,
    const Position& pos2) {
    return check_credit_spread(pos1, pos2, 'C', false);
}
```

- [ ] **Step 4: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 5: Commit**

```bash
git add src/core/analysis/strategy_detector.cpp
git commit -m "refactor(analysis): consolidate spread detection into check_credit_spread"
```

---

## Task 6: Fix O(n²) matching in strategy_detector.cpp

**Files:**
- Modify: `src/core/analysis/strategy_detector.cpp:106-151, 247-279`

- [ ] **Step 1: Add unordered_set include**

```cpp
// strategy_detector.cpp, after existing includes
#include <unordered_set>
```

- [ ] **Step 2: Replace detect_spreads to use unordered_set and swap-and-pop**

```cpp
// strategy_detector.cpp, replace detect_spreads (lines 106-151)
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
```

- [ ] **Step 3: Replace detect_iron_condors to use unordered_set**

```cpp
// strategy_detector.cpp, replace detect_iron_condors (lines 247-279)
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
```

- [ ] **Step 4: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 5: Commit**

```bash
git add src/core/analysis/strategy_detector.cpp
git commit -m "perf(analysis): fix O(n²) matching with unordered_set and swap-and-pop"
```

---

## Task 7: Remove StrategyDetector::load_positions and refactor detect_all_strategies

**Files:**
- Modify: `src/core/analysis/strategy_detector.hpp:75-78`
- Modify: `src/core/analysis/strategy_detector.cpp:12-67, 69-104`

- [ ] **Step 1: Remove load_positions declaration from header**

```cpp
// strategy_detector.hpp - remove these lines (75-78)
// DELETE:
//     static utils::Result<std::vector<Position>> load_positions(
//         db::Database& db,
//         int64_t account_id = 0);
```

- [ ] **Step 2: Change detect_all_strategies signature to accept span**

```cpp
// strategy_detector.hpp, replace existing declaration (lines 66-68)
static std::vector<Strategy> detect_all_strategies(
    std::span<const Position> positions);
```

- [ ] **Step 3: Remove load_positions implementation from cpp**

```cpp
// strategy_detector.cpp - DELETE lines 12-67 (entire load_positions function)
```

- [ ] **Step 4: Update detect_all_strategies implementation**

```cpp
// strategy_detector.cpp, replace existing detect_all_strategies (lines 69-104)
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
```

- [ ] **Step 5: Add span include**

```cpp
// strategy_detector.cpp, after includes
#include <span>
```

- [ ] **Step 6: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Errors in position_service.cpp and strategy_service.cpp - will fix in later tasks

- [ ] **Step 7: Commit**

```bash
git add src/core/analysis/strategy_detector.hpp src/core/analysis/strategy_detector.cpp
git commit -m "refactor(analysis): remove load_positions, change detect_all_strategies to accept span"
```

---

## Task 8: Add [[nodiscard]] to Database methods and consolidate PositionInfo

**Files:**
- Modify: `src/core/db/database.hpp:28-177`

- [ ] **Step 1: Add [[nodiscard]] to all Result-returning methods**

```cpp
// database.hpp - add [[nodiscard]] before each Result-returning method
[[nodiscard]] utils::Result<void> initialize();

[[nodiscard]] utils::Result<void> import_trades(...);

[[nodiscard]] utils::Result<void> import_open_positions(...);

[[nodiscard]] utils::Result<int64_t> get_or_create_account(...);

[[nodiscard]] utils::Result<void> clear_open_positions(...);

[[nodiscard]] utils::Result<int> get_open_positions_count();

[[nodiscard]] utils::Result<int> get_trades_count();

[[nodiscard]] utils::Result<std::vector<AccountInfo>> list_accounts();

[[nodiscard]] utils::Result<AccountInfo> get_account(int64_t account_id);

[[nodiscard]] utils::Result<void> update_account(...);

[[nodiscard]] utils::Result<void> delete_account(int64_t account_id);

[[nodiscard]] utils::Result<std::vector<analysis::Position>> get_all_positions(
    const std::string& account_name = "",
    int64_t account_id = 0);

[[nodiscard]] utils::Result<std::vector<RiskSummary>> get_consolidated_risk();

[[nodiscard]] utils::Result<std::vector<ExposureInfo>> get_underlying_exposure();
```

- [ ] **Step 2: Remove PositionInfo struct, change get_all_positions to return analysis::Position**

```cpp
// database.hpp - DELETE PositionInfo struct (lines 113-125)

// Replace get_all_positions declaration to return analysis::Position
[[nodiscard]] utils::Result<std::vector<analysis::Position>> get_all_positions(
    const std::string& account_name = "",
    int64_t account_id = 0);
```

- [ ] **Step 3: Add forward declaration for analysis::Position**

```cpp
// database.hpp, after namespace declaration
namespace ibkr::analysis {
    struct Position;
}
```

- [ ] **Step 4: Add include for strategy_detector.hpp (for Position definition)**

```cpp
// database.hpp, in includes section
#include "analysis/strategy_detector.hpp"
```

- [ ] **Step 5: Commit header changes**

```bash
git add src/core/db/database.hpp
git commit -m "refactor(db): add [[nodiscard]] and consolidate PositionInfo with analysis::Position"
```

---

## Task 9: Update database.cpp implementation for Position consolidation

**Files:**
- Modify: `src/core/db/database.cpp:417-503`

- [ ] **Step 1: Update get_all_positions implementation**

```cpp
// database.cpp, replace get_all_positions (lines 417-452)
Result<std::vector<analysis::Position>> Database::get_all_positions(
    const std::string& account_name,
    int64_t account_id) {
    
    if (!initialized_) return Error{"Database not initialized"};
    try {
        std::string sql =
            "SELECT o.id, o.account_id, o.symbol, o.underlying, "
            "o.expiry, o.strike, o.right, o.quantity, o.mark_price, o.entry_premium, "
            "0 as is_manual "
            "FROM open_options o";
        
        if (!account_name.empty()) {
            sql += " JOIN accounts a ON a.id = o.account_id WHERE a.name = ?";
        } else if (account_id > 0) {
            sql += " WHERE o.account_id = ?";
        }
        sql += " ORDER BY o.expiry, o.underlying";

        SQLite::Statement q(*db_, sql);
        if (!account_name.empty()) {
            q.bind(1, account_name);
        } else if (account_id > 0) {
            q.bind(1, account_id);
        }

        std::vector<analysis::Position> result;
        while (q.executeStep()) {
            analysis::Position p;
            p.id = q.getColumn(0).getInt64();
            p.account_id = q.getColumn(1).getInt64();
            p.symbol = q.getColumn(2).getString();
            p.underlying = q.getColumn(3).getString();
            p.expiry = q.getColumn(4).getString();
            p.strike = q.getColumn(5).getDouble();
            std::string right_str = q.getColumn(6).getString();
            p.right = right_str.empty() ? ' ' : right_str[0];
            p.quantity = q.getColumn(7).getDouble();
            p.mark_price = q.getColumn(8).getDouble();
            p.entry_premium = q.getColumn(9).getDouble();
            p.is_manual = q.getColumn(10).getInt() == 1;
            result.push_back(p);
        }
        return result;
    } catch (const std::exception& e) {
        return Error{"Failed to get positions", std::string(e.what())};
    }
}
```

- [ ] **Step 2: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Errors in json_output and report_service (will fix in later tasks)

- [ ] **Step 3: Commit**

```bash
git add src/core/db/database.cpp
git commit -m "refactor(db): update get_all_positions to return analysis::Position"
```

---

## Task 10: Document REAL for money in schema.hpp

**Files:**
- Modify: `src/core/db/schema.hpp:7-16`

- [ ] **Step 1: Add comment explaining REAL for monetary values**

```cpp
// schema.hpp, after the opening comment block (line 7)
/**
 * SQLite database schema for IBKR options analyzer.
 *
 * Design principles:
 * - Normalized structure (accounts, trades, open_options, strategies)
 * - Indexes on frequently queried columns (account_id, underlying, expiry)
 * - Foreign key constraints for referential integrity
 * - Timestamps for audit trail
 *
 * Note on monetary values:
 * REAL is used for strike prices, premiums, and P&L values.
 * This is acceptable for this use case as option premiums and P&L
 * calculations do not require sub-cent precision. If future requirements
 * demand exact monetary accuracy (e.g., tax reporting), a migration to
 * INTEGER storing cents would be needed.
 */
```

- [ ] **Step 2: Commit**

```bash
git add src/core/db/schema.hpp
git commit -m "docs(db): document REAL usage for monetary values"
```

---

## Task 11: Log warnings on parse_double failures in csv_parser.cpp

**Files:**
- Modify: `src/core/parsers/csv_parser.cpp:281-291`

- [ ] **Step 1: Update parse_double to log warnings**

```cpp
// csv_parser.cpp, replace parse_double (lines 281-291)
double CSVParser::parse_double(const std::string& str) const {
    if (str.empty()) {
        return 0.0;
    }

    try {
        return std::stod(str);
    } catch (const std::exception& e) {
        Logger::warn("Failed to parse double from '{}': {}", str, e.what());
        return 0.0;
    }
}
```

- [ ] **Step 2: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 3: Commit**

```bash
git add src/core/parsers/csv_parser.cpp
git commit -m "fix(parsers): log warnings on parse_double failures"
```

---

## Task 12: Document year pivot constant in option_symbol_parser.cpp

**Files:**
- Modify: `src/core/parsers/option_symbol_parser.cpp:148-168`

- [ ] **Step 1: Add YEAR_PIVOT constant and update convert_date_format**

```cpp
// option_symbol_parser.cpp, after includes, before namespace
namespace {
    constexpr int YEAR_PIVOT = 2000;  // Revisit before 2050 for YY year conversion
}

// Then update convert_date_format (lines 148-168)
std::string OptionSymbolParser::convert_date_format(const std::string& yymmdd) {
    if (yymmdd.length() != 6) {
        return "";
    }

    std::string yy = yymmdd.substr(0, 2);
    std::string mm = yymmdd.substr(2, 2);
    std::string dd = yymmdd.substr(4, 2);

    int year_int = std::stoi(yy);
    int full_year = YEAR_PIVOT + year_int;  // Using constant

    std::ostringstream oss;
    oss << full_year << "-" << mm << "-" << dd;
    return oss.str();
}
```

- [ ] **Step 2: Commit**

```bash
git add src/core/parsers/option_symbol_parser.cpp
git commit -m "refactor(parsers): document year pivot constant for future revisiting"
```

---

## Task 13: Fix N+1 query in position_service.cpp

**Files:**
- Modify: `src/core/services/position_service.cpp:16-54`

- [ ] **Step 1: Update load_positions to use Database::get_all_positions and filter in-memory**

```cpp
// position_service.cpp, replace load_positions (lines 16-54)
Result<std::vector<analysis::Position>> PositionService::load_positions(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    // Load account names for filtering
    auto account_names_result = load_account_names();
    if (!account_names_result) {
        return Error{"Failed to load account names", account_names_result.error().message};
    }
    auto account_names = *account_names_result;

    // Load positions using Database method
    auto positions_result = database_.get_all_positions(account_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    // Filter by underlying if specified
    if (!underlying_filter.empty()) {
        positions.erase(
            std::remove_if(positions.begin(), positions.end(),
                [&](const analysis::Position& pos) {
                    return pos.underlying != underlying_filter;
                }),
            positions.end()
        );
    }

    return positions;
}
```

- [ ] **Step 2: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 3: Commit**

```bash
git add src/core/services/position_service.cpp
git commit -m "fix(services): eliminate N+1 query in position loading"
```

---

## Task 14: Update strategy_service.cpp for new detect_all_strategies signature

**Files:**
- Modify: `src/core/services/strategy_service.cpp:15-54`

- [ ] **Step 1: Update analyze_strategies to use Database::get_all_positions**

```cpp
// strategy_service.cpp, replace analyze_strategies (lines 15-54)
Result<StrategyAnalysis> StrategyService::analyze_strategies(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    // Load positions from database
    auto positions_result = database_.get_all_positions(account_filter);
    if (!positions_result) {
        return Error{"Failed to load positions", positions_result.error().message};
    }

    auto positions = *positions_result;

    // Filter by underlying if specified
    if (!underlying_filter.empty()) {
        positions.erase(
            std::remove_if(positions.begin(), positions.end(),
                [&](const analysis::Position& pos) {
                    return pos.underlying != underlying_filter;
                }),
            positions.end()
        );
    }

    // Detect strategies
    auto strategies = analysis::StrategyDetector::detect_all_strategies(positions);

    // Calculate risk metrics
    std::vector<analysis::RiskMetrics> all_metrics;
    for (const auto& strategy : strategies) {
        all_metrics.push_back(analysis::RiskCalculator::calculate_risk(strategy));
    }

    StrategyAnalysis result;
    result.strategies = std::move(strategies);
    result.metrics = std::move(all_metrics);
    result.portfolio_risk = analysis::RiskCalculator::calculate_portfolio_risk(
        result.strategies, result.metrics);
    result.account_risks = analysis::RiskCalculator::calculate_account_risks(
        result.strategies, result.metrics);
    result.underlying_exposure = analysis::RiskCalculator::calculate_underlying_exposure(
        result.strategies, result.metrics);

    return result;
}
```

- [ ] **Step 2: Remove SQLite include (no longer needed)**

```cpp
// strategy_service.cpp - remove this include
// #include <SQLiteCpp/SQLiteCpp.h>  // DELETE this line
```

- [ ] **Step 3: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add src/core/services/strategy_service.cpp
git commit -m "refactor(services): update strategy_service for new detect_all_strategies signature"
```

---

## Task 15: Update report_service.cpp for new detect_all_strategies signature

**Files:**
- Modify: `src/core/services/report_service.cpp:14-74`
- Modify: `src/core/services/report_service.hpp:16`

- [ ] **Step 1: Update ReportData struct in header**

```cpp
// report_service.hpp, change positions field (line 16)
struct ReportData {
    std::vector<analysis::Position> positions;  // Changed from db::Database::PositionInfo
    std::vector<db::Database::RiskSummary> risk_summaries;
    std::vector<db::Database::ExposureInfo> exposures;
    std::vector<analysis::Strategy> strategies;
    std::vector<analysis::RiskMetrics> metrics;
    std::map<int64_t, std::string> account_names;
};
```

- [ ] **Step 2: Update gather_report_data in cpp**

```cpp
// report_service.cpp, update gather_report_data (lines 14-74)
Result<ReportData> ReportService::gather_report_data(
    const std::string& account_filter,
    const std::string& underlying_filter) {

    ReportData data;

    // Load positions from database
    auto pos_result = database_.get_all_positions(account_filter);
    if (!pos_result) {
        return Error{"Failed to load positions", pos_result.error().message};
    }
    data.positions = *pos_result;

    // Load risk summaries
    auto risk_result = database_.get_consolidated_risk();
    if (!risk_result) {
        return Error{"Failed to load risk summaries", risk_result.error().message};
    }
    data.risk_summaries = *risk_result;

    // Load exposures
    auto exp_result = database_.get_underlying_exposure();
    if (!exp_result) {
        return Error{"Failed to load exposures", exp_result.error().message};
    }
    data.exposures = *exp_result;

    // Detect strategies and calculate risk
    auto strategies = analysis::StrategyDetector::detect_all_strategies(data.positions);

    if (!underlying_filter.empty()) {
        strategies.erase(
            std::remove_if(strategies.begin(), strategies.end(),
                [&](const auto& s) { return s.underlying != underlying_filter; }),
            strategies.end()
        );
    }

    for (const auto& strategy : strategies) {
        data.metrics.push_back(analysis::RiskCalculator::calculate_risk(strategy));
    }
    data.strategies = std::move(strategies);

    // Load account names
    try {
        auto db_ptr = database_.get_db();
        if (db_ptr) {
            SQLite::Statement q(*db_ptr, "SELECT id, name FROM accounts");
            while (q.executeStep()) {
                data.account_names[q.getColumn(0).getInt64()] = q.getColumn(1).getString();
            }
        }
    } catch (const std::exception& e) {
        Logger::warn("Failed to load account names: {}", e.what());
    }

    return data;
}
```

- [ ] **Step 3: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Errors in json_output (will fix in next task)

- [ ] **Step 4: Commit**

```bash
git add src/core/services/report_service.hpp src/core/services/report_service.cpp
git commit -m "refactor(services): update report_service for Position consolidation"
```

---

## Task 16: Update json_output.hpp/cpp for RiskLevel enum and Position changes

**Files:**
- Modify: `src/core/utils/json_output.hpp:73, 111, 116`
- Modify: `src/core/utils/json_output.cpp:34, 155-159, 314-443`

- [ ] **Step 1: Update json_output.hpp declarations**

```cpp
// json_output.hpp - change PositionInfo references to Position

// Line 73: change report() signature
static nlohmann::json report(
    const std::vector<analysis::Position>& positions,
    const std::vector<db::Database::RiskSummary>& risk_summaries,
    const std::vector<db::Database::ExposureInfo>& exposures);

// Line 111: change position() signature
static nlohmann::json position(const analysis::Position& pos);

// Line 116: change positions() signature
static nlohmann::json positions(const std::vector<analysis::Position>& positions);
```

- [ ] **Step 2: Update risk_metrics_to_json to use risk_level_to_string**

```cpp
// json_output.cpp, line 34
j["risk_level"] = risk_level_to_string(m.risk_level);
```

- [ ] **Step 3: Update open_positions to use premium_for**

```cpp
// json_output.cpp, line 155
double prem = premium_for(pos.quantity, pos.entry_premium);

// line 159
total_max_loss += (pos.strike * constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity)) - prem;
```

- [ ] **Step 4: Update report() function**

```cpp
// json_output.cpp, replace report() (lines 314-364)
nlohmann::json JsonOutput::report(
    const std::vector<analysis::Position>& positions,
    const std::vector<db::Database::RiskSummary>& risk_summaries,
    const std::vector<db::Database::ExposureInfo>& exposures) {

    json j;
    j["status"] = "ok";

    // Positions
    json pos_arr = json::array();
    for (const auto& p : positions) {
        json pj;
        pj["id"] = p.id;
        pj["account_id"] = p.account_id;
        pj["underlying"] = p.underlying;
        pj["expiry"] = p.expiry;
        pj["strike"] = p.strike;
        pj["right"] = std::string(1, p.right);
        pj["quantity"] = p.quantity;
        pj["mark_price"] = p.mark_price;
        pj["entry_premium"] = p.entry_premium;
        pos_arr.push_back(pj);
    }
    j["positions"] = pos_arr;
    j["position_count"] = positions.size();

    // Risk summaries by account
    json risk_arr = json::array();
    for (const auto& r : risk_summaries) {
        json rj;
        rj["account"] = r.account_name;
        rj["total_max_loss"] = std::round(r.total_max_loss * 100.0) / 100.0;
        rj["total_max_profit"] = std::round(r.total_max_profit * 100.0) / 100.0;
        rj["strategy_count"] = r.strategy_count;
        risk_arr.push_back(rj);
    }
    j["risk_summaries"] = risk_arr;

    // Exposure by underlying
    json exp_arr = json::array();
    for (const auto& e : exposures) {
        json ej;
        ej["underlying"] = e.underlying;
        ej["total_max_loss"] = std::round(e.total_max_loss * 100.0) / 100.0;
        ej["position_count"] = e.position_count;
        exp_arr.push_back(ej);
    }
    j["underlying_exposure"] = exp_arr;

    return j;
}
```

- [ ] **Step 5: Update position() and positions() functions**

```cpp
// json_output.cpp, replace position() (lines 420-433)
nlohmann::json JsonOutput::position(const analysis::Position& pos) {
    json j;
    j["id"] = pos.id;
    j["account_id"] = pos.account_id;
    j["underlying"] = pos.underlying;
    j["expiry"] = pos.expiry;
    j["strike"] = pos.strike;
    j["right"] = std::string(1, pos.right);
    j["quantity"] = pos.quantity;
    j["mark_price"] = pos.mark_price;
    j["entry_premium"] = pos.entry_premium;
    return j;
}

// Replace positions() (lines 435-443)
nlohmann::json JsonOutput::positions(const std::vector<analysis::Position>& positions) {
    json j;
    j["status"] = "ok";
    json arr = json::array();
    for (const auto& p : positions) arr.push_back(position(p));
    j["positions"] = arr;
    j["count"] = positions.size();
    return j;
}
```

- [ ] **Step 6: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 7: Commit**

```bash
git add src/core/utils/json_output.hpp src/core/utils/json_output.cpp
git commit -m "refactor(utils): update json_output for RiskLevel enum and Position consolidation"
```

---

## Task 17: Update csv_exporter.cpp for RiskLevel enum

**Files:**
- Modify: `src/core/report/csv_exporter.cpp:62, 132`

- [ ] **Step 1: Update escape_csv_field calls to use risk_level_to_string**

```cpp
// csv_exporter.cpp, line 62
file << "," << escape_csv_field(risk_level_to_string(metric.risk_level)) << "\n";

// line 132
file << escape_csv_field(risk_level_to_string(metric.risk_level)) << ","
```

- [ ] **Step 2: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 3: Commit**

```bash
git add src/core/report/csv_exporter.cpp
git commit -m "refactor(report): update csv_exporter for RiskLevel enum"
```

---

## Task 18: Update analyze_command.cpp for RiskLevel enum and premium helper

**Files:**
- Modify: `src/cli/commands/analyze_command.cpp:195, 349-351, 456`

- [ ] **Step 1: Update risk_level output**

```cpp
// analyze_command.cpp, line 456
std::cout << "    Risk Level: " << risk_level_to_string(metrics->risk_level) << "\n";
```

- [ ] **Step 2: Update premium calculations**

```cpp
// analyze_command.cpp, line 195
double assignment_capital = pos.strike * constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);

// line 349
double premium = premium_for(pos.quantity, pos.entry_premium);

// line 351
double max_loss = (pos.strike - pos.entry_premium) * constants::CONTRACT_MULTIPLIER * std::abs(pos.quantity);
```

- [ ] **Step 3: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 4: Commit**

```bash
git add src/cli/commands/analyze_command.cpp
git commit -m "refactor(cli): update analyze_command for RiskLevel enum and premium helper"
```

---

## Task 19: HTTP client reuse connections

**Files:**
- Modify: `src/core/utils/http_client.hpp:31-108`
- Modify: `src/core/utils/http_client.cpp:11-247`

- [ ] **Step 1: Add httplib client as class member in header**

```cpp
// http_client.hpp, in private section (add after custom_headers_)
private:
    std::string base_url_;
    std::string user_agent_;
    int timeout_seconds_;
    int max_retries_;
    int initial_retry_delay_ms_;
    std::map<std::string, std::string> custom_headers_;
    std::unique_ptr<httplib::Client> client_;  // Add this
```

- [ ] **Step 2: Update constructor to initialize client**

```cpp
// http_client.cpp, update constructor (lines 11-21)
HttpClient::HttpClient(std::string base_url,
                      std::string user_agent,
                      int timeout_seconds,
                      int max_retries,
                      int initial_retry_delay_ms)
    : base_url_(std::move(base_url))
    , user_agent_(std::move(user_agent))
    , timeout_seconds_(timeout_seconds)
    , max_retries_(max_retries)
    , initial_retry_delay_ms_(initial_retry_delay_ms)
    , client_(std::make_unique<httplib::Client>(base_url_)) {  // Initialize here
    client_->set_connection_timeout(timeout_seconds_);
    client_->set_read_timeout(timeout_seconds_);
    client_->set_write_timeout(timeout_seconds_);
}
```

- [ ] **Step 3: Update get() to use member client**

```cpp
// http_client.cpp, update get() (lines 27-111)
Result<HttpResponse> HttpClient::get(const std::string& path,
                                     const std::map<std::string, std::string>& params) {
    std::string full_path = path;
    if (!params.empty()) {
        full_path += "?" + build_query_string(params);
    }

    Logger::debug("HTTP GET: {}{}", base_url_, full_path);

    return execute_with_retry([this, &full_path]() -> Result<HttpResponse> {
        httplib::Headers headers;
        headers.emplace("User-Agent", user_agent_);
        for (const auto& [key, value] : custom_headers_) {
            headers.emplace(key, value);
        }

        auto res = client_->Get(full_path, headers);  // Use member client_

        if (!res) {
            auto err = res.error();
            std::string error_msg = httplib_error_to_string(err);
            return Error{error_msg, base_url_ + full_path, static_cast<int>(err)};
        }

        HttpResponse response;
        response.status_code = res->status;
        response.body = res->body;
        for (const auto& [key, value] : res->headers) {
            response.headers[key] = value;
        }

        if (response.status_code >= 400) {
            return Error{
                "HTTP error " + std::to_string(response.status_code),
                base_url_ + full_path,
                response.status_code
            };
        }

        return response;
    });
}
```

- [ ] **Step 4: Add httplib_error_to_string helper**

```cpp
// http_client.cpp, add before HttpClient constructor
namespace {
std::string httplib_error_to_string(httplib::Error err) {
    switch (err) {
        case httplib::Error::Connection: return "Connection failed";
        case httplib::Error::BindIPAddress: return "Failed to bind IP address";
        case httplib::Error::Read: return "Read error";
        case httplib::Error::Write: return "Write error";
        case httplib::Error::ExceedRedirectCount: return "Too many redirects";
        case httplib::Error::Canceled: return "Request canceled";
        case httplib::Error::SSLConnection: return "SSL connection failed";
        case httplib::Error::SSLLoadingCerts: return "Failed to load SSL certificates";
        case httplib::Error::SSLServerVerification: return "SSL server verification failed";
        case httplib::Error::UnsupportedMultipartBoundaryChars: return "Unsupported multipart boundary characters";
        case httplib::Error::Compression: return "Compression error";
        default: return "Unknown error";
    }
}
}
```

- [ ] **Step 5: Update post() similarly**

```cpp
// http_client.cpp, update post() (lines 113-156)
Result<HttpResponse> HttpClient::post(const std::string& path,
                                      const std::string& body,
                                      const std::string& content_type) {
    Logger::debug("HTTP POST: {}{}", base_url_, path);

    return execute_with_retry([this, &path, &body, &content_type]() -> Result<HttpResponse> {
        httplib::Headers headers;
        headers.emplace("User-Agent", user_agent_);
        headers.emplace("Content-Type", content_type);
        for (const auto& [key, value] : custom_headers_) {
            headers.emplace(key, value);
        }

        auto res = client_->Post(path, headers, body, content_type);  // Use member client_

        if (!res) {
            auto err = res.error();
            return Error{"HTTP request failed", base_url_ + path, static_cast<int>(err)};
        }

        HttpResponse response;
        response.status_code = res->status;
        response.body = res->body;
        for (const auto& [key, value] : res->headers) {
            response.headers[key] = value;
        }

        if (response.status_code >= 400) {
            return Error{
                "HTTP error " + std::to_string(response.status_code),
                base_url_ + path,
                response.status_code
            };
        }

        return response;
    });
}
```

- [ ] **Step 6: Move url_encode to free function**

```cpp
// http_client.cpp, move url_encode outside class (before namespace closing)
std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }

    return escaped.str();
}
```

- [ ] **Step 7: Update header to declare url_encode as free function**

```cpp
// http_client.hpp, after HttpClient class declaration
std::string url_encode(const std::string& value);
```

- [ ] **Step 8: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 9: Commit**

```bash
git add src/core/utils/http_client.hpp src/core/utils/http_client.cpp
git commit -m "refactor(utils): reuse HTTP connections and make url_encode free function"
```

---

## Task 20: FlexDownloader constructor injection and double URL fix

**Files:**
- Modify: `src/core/flex/flex_downloader.hpp:54-89`
- Modify: `src/core/flex/flex_downloader.cpp:17-28, 162-226`
- Modify: `src/core/services/flex_service.cpp:35`

- [ ] **Step 1: Update FlexDownloader constructor signature in header**

```cpp
// flex_downloader.hpp, replace constructor declaration (line 60)
explicit FlexDownloader(const config::Config& config,
                       std::unique_ptr<utils::HttpClient> http_client = nullptr);
```

- [ ] **Step 2: Update private member in header**

```cpp
// flex_downloader.hpp, in private section
private:
    const config::Config& config_;
    std::unique_ptr<utils::HttpClient> http_client_;  // Already exists, keep
```

- [ ] **Step 3: Update FlexDownloader constructor implementation**

```cpp
// flex_downloader.cpp, replace constructor (lines 17-28)
FlexDownloader::FlexDownloader(const config::Config& config,
                               std::unique_ptr<utils::HttpClient> http_client)
    : config_(config) {

    if (http_client) {
        http_client_ = std::move(http_client);
    } else {
        // Create default HTTP client if none provided
        http_client_ = std::make_unique<utils::HttpClient>(
            FLEX_BASE_URL,
            config_.http.user_agent,
            config_.http.timeout_seconds,
            config_.http.max_retries,
            config_.http.retry_delay_ms
        );
    }
}
```

- [ ] **Step 4: Fix double URL building in get_statement**

```cpp
// flex_downloader.cpp, replace get_statement URL building (lines 177-182)
// Build the actual URL once
std::string full_path = std::string(GET_STATEMENT_PATH) + "?t=" + token 
                       + "&q=" + reference_code + "&v=3";

// Log with redacted token
Logger::debug("Request path (redacted): {}?t=[REDACTED]&q={}&v=3", 
              GET_STATEMENT_PATH, reference_code);
Logger::debug("Token length: {}", token.length());
```

- [ ] **Step 5: Update FlexService to create HttpClient**

```cpp
// flex_service.cpp, replace line 35
// Create HttpClient with config
auto http_client = std::make_unique<utils::HttpClient>(
    "https://ndcdyn.interactivebrokers.com",
    config_.http.user_agent,
    config_.http.timeout_seconds,
    config_.http.max_retries,
    config_.http.retry_delay_ms
);

flex::FlexDownloader downloader(config_, std::move(http_client));
```

- [ ] **Step 6: Build to verify changes compile**

Run: `cmake --build build/debug`
Expected: Compiles successfully

- [ ] **Step 7: Commit**

```bash
git add src/core/flex/flex_downloader.hpp src/core/flex/flex_downloader.cpp \
        src/core/services/flex_service.cpp
git commit -m "refactor(flex): constructor injection for HttpClient and fix double URL building"
```

---

## Task 21: Final build verification and integration test

- [ ] **Step 1: Build release version**

Run: `cmake --build build/release`
Expected: Compiles successfully with no warnings

- [ ] **Step 2: Run smoke test**

Run: `./build/release/ibkr-options-analyzer --help`
Expected: Shows help output

- [ ] **Step 3: Run with debug logging to verify date parsing**

Run: `./build/release/ibkr-options-analyzer analyze open --log-level debug`
Expected: No crashes, proper date parsing output

- [ ] **Step 4: Final commit for plan completion**

```bash
git add -A
git commit -m "refactor(core): complete 20-item refactoring of src/core/"
```

---

## Summary

| Module | Fixes Applied |
|--------|---------------|
| utils/ | #13 (nodiscard), #16 (Result<void> optimization), #15 (HTTP reuse), #20 (url_encode) |
| analysis/ | #1 (spread consolidation), #2 (O(n²)), #3 (premium helper), #4 (date library), #5 (Strategy cleanup), #11 (date validation), #14 (RiskLevel enum) |
| db/ | #6 (PositionInfo consolidation), #7 (load_positions removal), #13 (nodiscard), #19 (REAL docs) |
| parsers/ | #10 (parse_double warnings), #18 (year pivot) |
| services/ | #12 (N+1 fix), #7 (detect_all_strategies integration) |
| flex/ | #8 (HttpClient injection), #9 (double URL), #15 (connection reuse) |
| report/cli/ | #14 (RiskLevel integration) |