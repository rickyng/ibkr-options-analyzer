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
1. **Commands** (`src/commands/`): CLI command handlers (thin layer)
2. **Core Logic** (`src/flex/`, `src/parser/`, `src/analyzer/`): Business logic
3. **Data Layer** (`src/db/`): SQLite database operations
4. **Utilities** (`src/utils/`): Cross-cutting concerns (logging, HTTP, errors)

### Key Design Patterns
- **Result<T, E>** type for error handling (no exceptions in hot paths)
- **RAII** for resource management (no raw new/delete)
- **Dependency injection** via constructors
- **Separation of concerns**: parsing, business logic, and presentation are separate

### Module Responsibilities
- `config/`: Load and validate config.json
- `flex/`: IBKR Flex Web Service client (SendRequest → Poll → GetStatement)
- `parser/`: Parse IBKR option symbols and CSV reports
- `db/`: SQLite schema and CRUD operations
- `analyzer/`: Strategy detection and risk calculations
- `commands/`: CLI command implementations
- `utils/`: Logger, HTTP client, Result type

## Development Workflow

### Adding a New Command
1. Create header/cpp in `src/commands/`
2. Add command to `main.cpp` CLI11 setup
3. Implement command logic (delegate to core modules)
4. Update CMakeLists.txt with new files

### Adding a New Strategy Detector
1. Add strategy type enum in `src/analyzer/strategy_detector.hpp`
2. Implement detection rules in `strategy_detector.cpp`
3. Add risk calculation logic in `risk_calculator.cpp`

### Database Schema Changes
1. Update `src/db/schema.hpp` with new CREATE TABLE statements
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
- NEVER commit `config.json` (contains Flex tokens)
- Set file permissions: `chmod 600 ~/.ibkr-options-analyzer/config.json`
- Tokens are tied to IP address (may need regeneration)

### SQL Injection Prevention
- Always use parameterized queries via SQLiteCpp
- Never concatenate user input into SQL strings

## Future Enhancements

### Phase 7+ (Not Yet Implemented)
- Black-Scholes Greeks calculation
- TWS API integration for live prices
- Web dashboard (HTTP server + React frontend)
- Email/Slack alerts for risk thresholds
- Backtesting framework
- Multi-currency support
- Tax reporting (wash sales, P&L)

## Resources

- IBKR Flex Web Service: https://www.interactivebrokers.com/en/software/am/am/reports/flex_web_service_version_3.htm
- C++20 Reference: https://en.cppreference.com/w/cpp/20
- CMake Documentation: https://cmake.org/cmake/help/latest/
