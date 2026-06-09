# Core C++ Refactoring Design

**Date:** 2026-04-30
**Scope:** All 20 improvement items from code review of `src/core/`
**Approach:** Single cohesive refactoring pass — all fixes are localized and independent

---

## 1. Common Utilities (`src/core/utils/`)

### 1a. `[[nodiscard]]` on Result (#13)

Add `[[nodiscard]]` to the `Result<T>` class and all free functions returning `Result<T>`. Prevents silent discarding of error-returning calls.

### 1b. Optimize `Result<void>` (#16)

Replace always-allocated `Error error_{"Success"}` with `std::optional<Error>`. On the success path, no Error object is constructed.

### 1c. Reuse HTTP connections (#15)

Move `httplib::Client` creation from per-request lambda to a class member. Set timeouts once in the constructor. This enables TCP/TLS session reuse across retries and sequential requests.

### 1d. `url_encode` as free function (#20)

Make `url_encode` a free function in `ibkr::utils` namespace since it uses no member state. Keep it in `http_client.cpp` as a file-local free function (or move to a small `url_utils` header if needed elsewhere).

---

## 2. Analysis Module (`src/core/analysis/`)

### 2a. Consolidate spread detection (#1)

Merge `check_bull_put_spread` and `check_bear_call_spread` into a single `check_credit_spread(pos1, pos2, expected_right, short_higher_strike)` function. The two existing functions are 90% identical — only the `right` check (`'P'` vs `'C'`) and strike comparison direction differ.

### 2b. Fix O(n²) matching (#2)

In `detect_spreads` and `detect_iron_condors`:
- Replace `std::vector<size_t> matched_indices` + `std::find()` with `std::unordered_set<size_t> matched`
- Replace sorted-reverse-erase with swap-and-pop idiom

### 2c. Extract premium helper and constant (#3)

Add to `risk_calculator.hpp`:
```cpp
static constexpr double CONTRACT_MULTIPLIER = 100.0;
static double premium_for(double quantity, double price_per_contract) {
    return std::abs(quantity) * price_per_contract * CONTRACT_MULTIPLIER;
}
```
Replace all 6+ occurrences of `std::abs(pos.quantity) * pos.entry_premium * 100.0`.

### 2d. Unify date parsing with `date` library (#4)

Replace `risk_calculator.cpp::calculate_days_to_expiry` manual `istringstream` + `mktime` approach with Howard Hinnant's `date` library (already a dependency). This matches `option_symbol_parser.cpp` and handles invalid input correctly.

### 2e. Add date validation (#11)

`calculate_days_to_expiry` returns `std::numeric_limits<int>::max()` and logs a warning when the input string is not valid YYYY-MM-DD format. With the `date` library approach, failed parsing is detectable.

### 2f. `risk_level` as enum (#14)

Replace `std::string risk_level` in `RiskMetrics` with:
```cpp
enum class RiskLevel { Low, Medium, High, Defined };
```
Add `risk_level_to_string(RiskLevel)` converter. Update all call sites.

### 2g. Remove dead fields from `Strategy` (#5)

Remove `breakeven_price`, `max_profit`, `max_loss`, `risk_level` from `Strategy` struct. These duplicate `RiskMetrics` and are never populated by `StrategyDetector`. Update any code that reads these fields to use `RiskMetrics` instead.

---

## 3. Database Module (`src/core/db/`)

### 3a. `[[nodiscard]]` on all Result-returning methods (#13)

Add `[[nodiscard]]` to every method in `Database` that returns `Result<T>`.

### 3b. Consolidate position types (#6)

Make `analysis::Position` the canonical position type. Remove `db::Database::PositionInfo`. Update `Database::get_all_positions` to return `std::vector<analysis::Position>`. Add `account_name` field to `analysis::Position` if needed by consumers.

### 3c. Move position loading out of StrategyDetector (#7)

`StrategyDetector::load_positions` currently reaches into `db.get_db()` and writes raw SQL. Instead:
- `Database::get_all_positions` already exists — enhance it to support `account_id` filtering
- `StrategyDetector::detect_all_strategies` takes `span<const Position>` instead of `Database&`
- `StrategyDetector::load_positions` is removed — callers use `Database::get_all_positions` directly

### 3d. Document REAL for money (#19)

Add a comment in `schema.hpp` explaining that `REAL` is used for monetary values and precision loss is acceptable for this use case (option premiums, P&L are not sub-cent precision). A migration to integer cents would be a future enhancement.

---

## 4. Parsers Module (`src/core/parsers/`)

### 4a. Log warnings on parse failures (#10)

In `CSVParser::parse_double`, log a warning when parsing fails instead of silently returning 0.0. Use `Logger::warn` with the input string for debugging.

### 4b. Document year pivot constant (#18)

In `option_symbol_parser.cpp`, extract the pivot year as a named constant:
```cpp
constexpr int YEAR_PIVOT = 2000; // Revisit before 2050
```

---

## 5. Services Module (`src/core/services/`)

### 5a. Fix N+1 query in position filter (#12)

In `PositionService::load_positions`, when `account_filter` is set, load the account map once via `load_account_names()`, then filter positions in-memory. No per-position database queries.

---

## 6. Flex Module (`src/core/flex/`)

### 6a. Constructor injection for HttpClient (#8)

Change `FlexDownloader` constructor to accept `std::unique_ptr<utils::HttpClient>` (or a reference). The caller creates the `HttpClient`. This enables testing with mock clients.

### 6b. Remove double URL building (#9)

In `get_statement`, build the real URL once, then log the redacted version. Don't build a redacted string, log it, then overwrite.

---

## 7. CSV Row Map Optimization (#17)

Replace `std::map<std::string, std::string>` row maps with direct `rapidcsv::Document` access. Change `parse_trade_row` and `parse_position_row` to accept `const rapidcsv::Document& doc, size_t row_index` instead of a map. Internally use `doc.GetCell<std::string>(col_name, row_index)` with try/catch. This eliminates N×M heap allocations per file.

---

## Implementation Order

1. **utils/** first (result.hpp, http_client) — foundational, other modules depend on these
2. **analysis/** next (risk_calculator, strategy_detector) — core business logic
3. **db/** (database, schema) — data layer
4. **parsers/** (csv_parser, option_symbol_parser) — input processing
5. **services/** (position_service) — orchestration
6. **flex/** (flex_downloader) — external integration

Each step should compile and pass existing tests before moving to the next.

---

## Breaking Changes

- `Strategy` struct loses 4 fields — any code reading `strategy.breakeven_price` etc. must use `RiskMetrics` instead
- `RiskMetrics::risk_level` changes from `std::string` to `RiskLevel` enum — consumers need `risk_level_to_string()`
- `StrategyDetector::detect_all_strategies` signature changes from `(Database&, account_id)` to `(span<const Position>)`
- `FlexDownloader` constructor changes to accept `HttpClient` injection
- `Database::PositionInfo` removed in favor of `analysis::Position`
