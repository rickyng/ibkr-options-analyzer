# Findings — IBKR Options Analyzer

## Codebase Exploration (2026-04-23)

### What Exists
- **Complete CLI** with 5 commands: download, import, manual-add, analyze, report
- **Complete database layer** (SQLite with SQLiteCpp)
- **Complete Flex client** (HTTP + XML parsing)
- **Complete parsers** (option symbol + CSV)
- **Complete price fetcher** (Yahoo Finance + Alpha Vantage fallback)
- **Complete report generator** (text + CSV export)
- **Complete HTTP client** (cpp-httplib wrapper)
- **Complete logging** (spdlog with rotation)

### What's Missing / Deleted
- `src/analysis/risk_calculator.cpp/hpp` — DELETED, needs rebuild
- `src/analysis/strategy_detector.cpp/hpp` — DELETED, needs rebuild
- JSON output format (`--format json`) — not implemented
- Multi-account database schema — partially there, needs FKs and consolidation queries
- Service layer (logic is embedded in command classes)
- Core library separation (everything in flat src/ structure)
- Unit test framework and tests

### Key Observations
1. Command classes are large (analyze_command: 7200+ lines, import_command: 7060 lines, manual_add_command: 7148 lines). Service layer extraction will reduce these significantly.
2. The CMakeLists.txt already uses modern CMake with FetchContent and presets.
3. Config format (config.json) already supports multiple accounts in the accounts array.
4. Database schema already has some multi-account support but lacks proper FKs and consolidation queries.

### Build
- CMake presets: debug, release (with sanitizers in debug)
- Dependencies: CLI11, fmt, spdlog, nlohmann_json, pugixml, cpp-httplib, rapidcsv, SQLiteCpp, date
- C++20/23 standard
