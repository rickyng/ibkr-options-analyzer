# IBKR Options Analyzer — C++ Engine + Python Dashboard Plan

## Goal
Build an industrial-standard C++ core engine with CLI, and a separate Python project for the web dashboard. They share a SQLite database as the integration point.

## Architecture

```
┌─────────────────────────────────┐       ┌──────────────────────────────┐
│  C++ Engine (this repo)         │       │  Python Dashboard (new repo) │
│                                 │       │                              │
│  libibkr_core (static lib)      │       │  FastAPI REST API            │
│    ├── parsers                  │       │  Dash Web Dashboard          │
│    ├── analysis                 │  DB   │                              │
│    ├── services ──── SQLite ────┼──────►│  Reads SQLite directly       │
│    ├── db                       │       │  Calls CLI for writes/sync   │
│    └── models                   │       │                              │
│                                 │       │  Integration:                │
│  ibkr-cli (executable)          │       │    - Shared SQLite           │
│    ├── download                 │  CLI  │    - CLI --format json       │
│    ├── import  ─────────────────┼──────►│    - subprocess calls        │
│    ├── analyze                  │       │                              │
│    ├── report                   │       │                              │
│    └── manual-add               │       │                              │
└─────────────────────────────────┘       └──────────────────────────────┘
```

## Integration Contract
- **C++ writes** to SQLite (download, import, manual-add)
- **C++ outputs JSON** via `--format json` flag on all commands
- **Python reads** SQLite directly for queries/display
- **Python triggers** C++ CLI via subprocess for writes/sync operations
- **Shared schema** versioned in metadata table — both sides validate on startup

## Current State
- [x] CLI framework (5 commands: download, import, manual-add, analyze, report)
- [x] Database layer (SQLite with SQLiteCpp)
- [x] Config manager (JSON config)
- [x] Flex downloader (IBKR Flex Web Service)
- [x] CSV parser (IBKR report format)
- [x] Option symbol parser (both formats)
- [x] Price fetcher (Yahoo Finance + Alpha Vantage)
- [x] Report generator (text + CSV)
- [x] HTTP client (cpp-httplib)
- [x] Logging (spdlog)
- **DELETED**: strategy_detector, risk_calculator (need rebuild)
- **MISSING**: JSON output, core library separation, multi-account support

---

## Phase 1: Restructure Project Layout
**Status:** pending

Separate core engine library from CLI application so the core can be reused.

### Target Structure
```
src/
├── core/                    # Static library: libibkr_core.a
│   ├── models/
│   │   ├── option.hpp       # Option data structures
│   │   ├── strategy.hpp     # Strategy types + enums
│   │   ├── risk.hpp         # Risk metrics structs
│   │   ├── account.hpp      # Account model
│   │   └── trade.hpp        # Trade record model
│   ├── parsers/
│   │   ├── option_symbol_parser.hpp/cpp   # (move from src/parser/)
│   │   └── csv_parser.hpp/cpp             # (move from src/parser/)
│   ├── analysis/
│   │   ├── strategy_detector.hpp/cpp      # NEW: rebuild
│   │   └── risk_calculator.hpp/cpp        # NEW: rebuild
│   ├── db/
│   │   ├── database.hpp/cpp               # (move from src/db/)
│   │   └── schema.hpp                     # (move from src/db/)
│   ├── services/
│   │   ├── flex_service.hpp/cpp           # Wraps flex_downloader
│   │   ├── import_service.hpp/cpp         # Orchestrates CSV import
│   │   ├── price_service.hpp/cpp          # Wraps price_fetcher
│   │   └── report_service.hpp/cpp         # Wraps report_generator
│   └── utils/
│       ├── result.hpp                     # (move from src/utils/)
│       ├── logger.hpp/cpp                 # (move from src/utils/)
│       └── http_client.hpp/cpp            # (move from src/utils/)
├── cli/                     # CLI executable: ibkr-cli
│   ├── main.cpp                          # (move from src/main.cpp)
│   ├── cli_app.hpp/cpp                   # CLI11 app builder
│   └── commands/                         # (move from src/commands/)
│       ├── download_command.hpp/cpp
│       ├── import_command.hpp/cpp
│       ├── analyze_command.hpp/cpp
│       ├── report_command.hpp/cpp
│       └── manual_add_command.hpp/cpp
└── json/                    # JSON serialization helpers
    └── json_output.hpp/cpp               # NEW: structured JSON output
```

### Tasks
- [ ] Create `src/core/` directory structure
- [ ] Move existing modules into core subdirectories
- [ ] Create `src/cli/` directory structure
- [ ] Move CLI-specific code to cli/
- [ ] Update CMakeLists.txt: add `ibkr_core` static library target
- [ ] Update CMakeLists.txt: CLI links against `ibkr_core`
- [ ] Verify build succeeds after restructure

### Verify
```bash
cmake --preset debug && cmake --build build/debug
./build/debug/ibkr-options-analyzer --help
```

---

## Phase 2: Rebuild Analysis Module
**Status:** pending

Reimplement the deleted strategy_detector and risk_calculator with multi-account support and clean interfaces.

### 2a: Strategy Detector
- [ ] Define `StrategyType` enum: `NAKED_SHORT_PUT`, `NAKED_SHORT_CALL`, `BULL_PUT_SPREAD`, `BEAR_CALL_SPREAD`, `IRON_CONDOR`, `STRADDLE`, `STRANGLE`, `UNKNOWN`
- [ ] Define `DetectedStrategy` struct with legs, account_id, confidence score
- [ ] Implement grouping logic: group positions by (account_id, underlying, expiry)
- [ ] Implement pattern matching:
  - Single short put → NAKED_SHORT_PUT
  - Single short call → NAKED_SHORT_CALL
  - Short put + long put (same underlying/expiry) → BULL_PUT_SPREAD
  - Short call + long call (same underlying/expiry) → BEAR_CALL_SPREAD
  - BULL_PUT_SPREAD + BEAR_CALL_SPREAD → IRON_CONDOR
  - Short put + short call (same strike) → STRADDLE
  - Short put + short call (different strikes) → STRANGLE
- [ ] Write unit tests (15+ cases)

### 2b: Risk Calculator
- [ ] Define `RiskMetrics` struct: breakeven, max_profit, max_loss, risk_level, net_premium, days_to_expiry
- [ ] Implement per-strategy formulas:
  - Naked short put: breakeven = strike - premium, max_loss = (strike - premium) * 100 * |qty|
  - Naked short call: breakeven = strike + premium, max_loss = UNLIMITED
  - Bull put spread: net_premium based, max_loss = (width * 100) - premium
  - Bear call spread: same structure as bull put
  - Iron condor: two breakevens, max_loss = max(put_width, call_width) * 100 - premium
- [ ] Implement `PortfolioRisk` aggregation across strategies
- [ ] Implement `AccountRisk` per-account breakdown
- [ ] Implement `UnderlyingExposure` cross-account aggregation
- [ ] Write unit tests (35+ cases)

### Verify
```bash
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

---

## Phase 3: Database Schema Update for Multi-Account
**Status:** pending

Update SQLite schema to support multiple accounts with proper foreign keys and consolidated queries.

### Tasks
- [ ] Update `accounts` table: add `enabled`, `created_at`, `updated_at` columns
- [ ] Add foreign key `account_id` to all data tables (positions, trades, strategies)
- [ ] Add indexes for multi-account queries: `idx_positions_account`, `idx_strategies_account`
- [ ] Add `metadata` table for schema versioning
- [ ] Implement schema migration (version check + ALTER TABLE)
- [ ] Add CRUD operations: `list_accounts()`, `get_account(id)`, `create_account()`, `update_account()`, `delete_account()`
- [ ] Add consolidated queries: `get_all_positions()`, `get_consolidated_risk()`, `get_underlying_exposure()`

### Schema
```sql
-- accounts table
CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    token TEXT NOT NULL,
    query_id TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- open_options table (add account_id FK)
-- trades table (add account_id FK)
-- detected_strategies table (add account_id FK)
-- strategy_legs table (FK to detected_strategies + open_options)
-- metadata table (schema version)
```

### Verify
```bash
./build/debug/ibkr-options-analyzer download --account IRA --token ... --query-id ...
./build/debug/ibkr-options-analyzer import --account IRA
./build/debug/ibkr-options-analyzer analyze --account IRA
```

---

## Phase 4: JSON Output Layer
**Status:** pending

Add structured JSON output to all CLI commands for Python integration.

### Tasks
- [ ] Create `json_output.hpp/cpp` — common JSON serialization helpers
- [ ] Add `--format json` flag to CLI11 global options
- [ ] Implement JSON output for each command:
  - `download --format json` → `{"job_id": "...", "status": "success", "file": "...", "account": "..."}`
  - `import --format json` → `{"account": "...", "rows_imported": N, "positions_added": N}`
  - `analyze --format json` → `{"strategies": [...], "portfolio_risk": {...}, "account_risks": [...]}`
  - `report --format json` → `{"report": "...", "generated_at": "..."}`
  - `manual-add --format json` → `{"position_id": N, "account": "..."}`
- [ ] Add `--quiet` flag to suppress human-readable output (only JSON)
- [ ] Write integration tests: run CLI, parse JSON output, verify structure

### Verify
```bash
./build/debug/ibkr-options-analyzer analyze --format json | python3 -m json.tool
./build/debug/ibkr-options-analyzer report --format json --quiet
```

---

## Phase 5: Service Layer
**Status:** pending

Extract orchestration logic from commands into clean service classes that the CLI calls.

### Tasks
- [ ] `FlexService` — wraps flex_downloader, handles multi-account sequential download, partial failure reporting
- [ ] `ImportService` — wraps csv_parser + db, handles account tagging, deduplication
- [ ] `PriceService` — wraps price_fetcher, batch fetching, caching in DB
- [ ] `ReportService` — wraps report_generator + csv_exporter, consolidated cross-account reports
- [ ] `PositionService` — CRUD for positions, filtering, multi-account queries
- [ ] `StrategyService` — runs detection + risk calc, persists results, consolidated views
- [ ] Refactor commands to be thin wrappers that call service methods

### Verify
```bash
# All existing CLI commands still work after refactor
./build/debug/ibkr-options-analyzer download --account IRA
./build/debug/ibkr-options-analyzer analyze --format json
```

---

## Phase 6: CLI Multi-Account Support
**Status:** pending

Update all CLI commands to support multi-account operations.

### Tasks
- [ ] `download` — accept `--account NAME` or `--all` for all enabled accounts
- [ ] `import` — accept `--account NAME` or `--all`, auto-match CSV files to accounts
- [ ] `analyze` — default shows consolidated view, `--account NAME` for single account
- [ ] `report` — consolidated report by default, `--account NAME` for single
- [ ] `manual-add` — require `--account NAME`
- [ ] Add `ibkr-options-analyzer accounts` command — list/manage accounts
- [ ] Add `ibkr-options-analyzer sync` command — download + import for all enabled accounts

### Verify
```bash
./build/debug/ibkr-options-analyzer sync --all --format json
./build/debug/ibkr-options-analyzer analyze --format json
```

---

## Phase 7: Unit Test Suite
**Status:** pending

Build comprehensive unit tests using a C++ test framework.

### Tasks
- [ ] Add Catch2 or doctest via FetchContent
- [ ] Test option symbol parser (both formats, edge cases)
- [ ] Test CSV parser (trade rows, position rows, filtering)
- [ ] Test strategy detector (all strategy types)
- [ ] Test risk calculator (all formulas, edge cases)
- [ ] Test database CRUD operations
- [ ] Test JSON output formatting
- [ ] Add test target to CMakeLists.txt
- [ ] CI integration (GitHub Actions)

### Verify
```bash
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

---

## Phase 8: Python Dashboard Project (Separate Repo)
**Status:** pending

Create `ibkr-options-analyzer-dashboard` Python project.

### Tasks
- [ ] Create new repo with FastAPI + Dash project
- [ ] Implement SQLite reader (reads DB written by C++ engine)
- [ ] Implement CLI caller (subprocess wrapper for writes/sync)
- [ ] Implement REST API endpoints (thin wrappers over DB reads)
- [ ] Implement Dash dashboard tabs:
  - Overview (consolidated portfolio summary)
  - Positions (data table with filters)
  - Strategies (cards with risk badges)
  - Risk (charts and heatmaps)
  - Expiration (calendar view)
  - Import (file upload + CLI trigger)
  - Settings (account management)
- [ ] Docker setup

### Verify
Dashboard renders at `http://localhost:8000/dashboard/`, all tabs display data.

---

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|

## Decisions Log
| Decision | Reason | Date |
|----------|--------|------|
| Shared SQLite + JSON CLI for integration | Simplest industrial pattern, proven by many tools | 2026-04-23 |
| Static library for core engine | Enables future pybind11 if needed, clean separation | 2026-04-23 |
| Multi-account in C++ engine | Core data model belongs in engine, not presentation | 2026-04-23 |
