# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**ibkr-options-analyzer** is a modern C++20/23 command-line tool for tracking and analyzing non-expired open option positions from multiple Interactive Brokers accounts. The focus is on option selling strategies (especially short puts) with comprehensive risk analysis.

## Build System

### Prerequisites
- CMake 3.26+
- Ninja build system
- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)
- All dependencies are fetched automatically via CMake FetchContent

### Build Commands

```bash
# Configure debug build (with sanitizers)
cmake --preset debug

# Build debug
cmake --build build/debug

# Configure release build (optimized)
cmake --preset release

# Build release
cmake --build build/release

# Run the tool
./build/release/ibkr-options-analyzer --help
```

### Dependencies (Auto-fetched)
- CLI11: Command-line parser
- fmt: Modern formatting
- spdlog: Fast logging with colors
- nlohmann/json: JSON parsing
- pugixml: XML parsing (Flex responses)
- cpp-httplib: HTTP client
- rapidcsv: CSV parsing
- SQLiteCpp: SQLite wrapper
- date: Howard Hinnant's date library

## Architecture

### Layer Structure (WAT Framework Inspired)
1. **Commands** (`src/cli/commands/`): Thin CLI wrappers delegating to services
2. **Services** (`src/core/services/`): Business logic, transport-agnostic (callable from CLI or API)
3. **Core Logic** (`src/core/analysis/`, `src/core/parsers/`, `src/core/flex/`): Domain logic
4. **Data Layer** (`src/core/db/`): SQLite database operations
5. **Utilities** (`src/core/utils/`): Cross-cutting concerns (logging, HTTP, JSON output, errors)

### Key Design Patterns
- **Result<T, E>** type for error handling (no exceptions in hot paths)
- **RAII** for resource management (no raw new/delete)
- **Dependency injection** via constructors
- **Separation of concerns**: parsing, business logic, and presentation are separate

### Module Responsibilities
- `src/core/config/`: Load and validate config.json
- `src/core/flex/`: IBKR Flex Web Service client (SendRequest → Poll → GetStatement)
- `src/core/parsers/`: Parse IBKR option symbols and CSV reports
- `src/core/db/`: SQLite schema and CRUD operations
- `src/core/analysis/`: Strategy detection and risk calculations
- `src/core/services/`: Business logic orchestration (FlexService, ImportService, PositionService, PriceService, PriceCacheService, StrategyService, PortfolioService, ScreenerService, ReportService)
- `src/core/report/`: Report generation and CSV export
- `src/core/utils/`: Logger, HTTP client, JSON output, Result type
- `src/cli/commands/`: CLI command handlers (thin wrappers)

## Development Workflow

### Adding a New Command
1. Create header/cpp in `src/cli/commands/`
2. Add command to `src/cli/main.cpp` CLI11 setup
3. Delegate to existing service in `src/core/services/` (or create new service if needed)
4. Update CMakeLists.txt with new files

### Adding a New Strategy Detector
1. Add strategy type enum in `src/core/analysis/strategy_detector.hpp`
2. Implement detection rules in `strategy_detector.cpp`
3. Add risk calculation logic in `src/core/analysis/risk_calculator.cpp`

### Database Schema Changes
1. Update `src/core/db/schema.hpp` with new CREATE TABLE statements
2. Increment schema version in metadata table
3. Add migration logic in `database.cpp` (future)

## Testing

### Manual Testing
```bash
# Test config loading
./build/debug/ibkr-options-analyzer --config config.json.example --log-level debug

# Test with verbose logging
./build/debug/ibkr-options-analyzer download --log-level trace
```

### Unit Tests (Future)
```bash
cmake --build build/debug --target test
```

## Common Issues

### Build Failures
- Ensure CMake 3.26+ is installed: `cmake --version`
- Ensure Ninja is installed: `ninja --version`
- Clear build cache: `rm -rf build/`

### Runtime Errors
- Check config.json exists at `~/.ibkr-options-analyzer/config.json`
- Verify IBKR Flex tokens are valid (not expired, correct IP)
- Check log file: `~/.ibkr-options-analyzer/logs/app.log`

## Code Style

### Naming Conventions
- Classes: `PascalCase` (e.g., `ConfigManager`)
- Functions/methods: `snake_case` (e.g., `load_config()`)
- Member variables: `snake_case_` with trailing underscore (e.g., `config_path_`)
- Constants: `UPPER_SNAKE_CASE` (e.g., `MAX_RETRIES`)
- Namespaces: `lowercase` (e.g., `ibkr::config`)

### File Organization
- Header files: `.hpp` extension
- Implementation files: `.cpp` extension
- One class per file (with matching filename)
- Headers include guards: `#pragma once`

### Error Handling
- Use `Result<T, E>` for all fallible operations
- Provide context in error messages (account name, file path, etc.)
- Log errors before returning them
- No exceptions in hot paths (only for unrecoverable errors)

### Comments
- Document public APIs with Doxygen-style comments
- Explain "why" not "what" in implementation comments
- Use `TODO:` for future improvements
- Use `FIXME:` for known issues

## IBKR Flex Web Service

### Endpoints (2026)
- SendRequest: `https://ndcdyn.interactivebrokers.com/AccountManagement/FlexWebService/SendRequest?t={token}&q={query_id}&v=3`
- GetStatement: `https://ndcdyn.interactivebrokers.com/AccountManagement/FlexWebService/GetStatement?t={token}&q={reference_code}&v=3`

### Flow
1. POST SendRequest with token + query_id
2. Parse XML response → extract ReferenceCode
3. Poll GetStatement every 5 seconds (max 5 minutes)
4. When status == "Success", save CSV content

### Error Handling
- Token expired: HTTP 401 → user-friendly message
- Query not found: HTTP 404 → check query_id
- IP restriction: HTTP 403 → regenerate token
- Timeout: Retry with exponential backoff

## Security

### Sensitive Data
- NEVER commit `config.json` (may contain Flex tokens in legacy format)
- Credentials provided via CLI args (not stored in config file)
- Tokens are tied to IP address (may need regeneration)

### SQL Injection Prevention
- Always use parameterized queries via SQLiteCpp
- Never concatenate user input into SQL strings

## Implemented Features

### Dashboard (Phase 5)
- Web dashboard with Dash/Plotly frontend at `dashboard/`
- Account management UI with DB storage
- Positions tab: expiry timeline chart + table, exposure table, risk distribution
- Portfolio tab: assignment risk alerts, position details, loss scenarios, expiration calendar
- Screener tab: put-selling opportunity scanner with parameter overrides and cache-only mode
- Run: `cd dashboard && uvicorn app.main:app --reload --port 8001`

### Analysis Services (Phase 5-6)
- Option chain fetching via Yahoo Finance v7/v8 API
- Synthetic option chain generation using Black-Scholes (when Yahoo blocked)
- ScreenerService: watchlist-based opportunity screening with grading
- PortfolioService: consolidated portfolio view construction
- Multi-currency support with automatic symbol-to-currency deduction
- Screener parameter overrides via CLI flags (--min-iv-percentile, --min-premium-yield, --min-dte, --max-dte, --otm-buffer)
- Cache-only mode (--cache-only) for screener re-runs without API fetches

### Price/Option Caching (Phase 7)
- PriceCacheService: SQLite-backed caching for prices, volatility, and option chains
- Trading-day-based TTL: cached data valid for current trading day only
- Automatic expired cache cleanup on PriceService startup
- US market holiday awareness (2025-2027) for trading date calculation
- Cache tables: cached_prices, cached_volatility, cached_option_chains (schema v1.1.0)

## Future Enhancements

### Not Yet Implemented
- Black-Scholes Greeks calculation (delta, gamma, theta, vega)
- TWS API integration for live prices
- Email/Slack alerts for risk thresholds
- Backtesting framework
- Tax reporting (wash sales, P&L)
- Unit test framework

## Resources

- IBKR Flex Web Service: https://www.interactivebrokers.com/en/software/am/am/reports/flex_web_service_version_3.htm
- C++20 Reference: https://en.cppreference.com/w/cpp/20
- CMake Documentation: https://cmake.org/cmake/help/latest/
