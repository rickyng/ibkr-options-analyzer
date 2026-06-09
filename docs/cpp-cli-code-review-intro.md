# Code Review: C++ CLI (`src/`)

**Date:** 2024-06-04
**Reviewer:** _________
**Scope:** `src/` — ~12K LoC across 75 files

---

## What This Is

A C++20/23 CLI tool for tracking and analyzing **non-expired open option positions** from multiple Interactive Brokers accounts. Focuses on option-selling strategies (especially short puts) with risk analysis and portfolio reporting.

**Binary:** `ibkr-options-analyzer`
**Build:** CMake 3.26+ / Ninja / C++20 (all deps auto-fetched via FetchContent)

---

## Architecture at a Glance

```
src/
├── cli/                        # Thin CLI wrappers
│   ├── main.cpp                # Entry point, CLI11 command wiring
│   └── commands/               # One file per CLI subcommand
│       ├── analyze_command     # Position analysis & risk
│       ├── download_command    # Flex Web Service fetch
│       ├── import_command      # CSV → DB import (current positions)
│       ├── import_history_command  # Historical trade import
│       ├── report_command      # Report generation
│       └── trades_command      # Trade listing & filtering
│
└── core/                       # Business logic (transport-agnostic)
    ├── analysis/               # Strategy detection & risk calc
    ├── config/                 # config.json loading & validation
    ├── db/                     # SQLite schema & CRUD
    ├── flex/                   # IBKR Flex Web Service client
    ├── parsers/                # Option symbol & CSV parsing
    ├── report/                 # Report generation & CSV export
    ├── services/               # Business orchestration layer
    │   ├── flex_service        # Download orchestration
    │   ├── import_service      # Import orchestration
    │   ├── portfolio_service   # Consolidated portfolio view
    │   ├── position_service    # Position queries
    │   ├── price_cache_service # Price caching & Yahoo Finance
    │   ├── price_service       # Price fetching
    │   ├── report_service      # Report orchestration
    │   ├── screener_service    # Watchlist-based screening
    │   └── strategy_service    # Strategy detection
    └── utils/                  # Logger, HTTP, JSON, Result<T>, currency
```

### Layer Pattern (top → bottom)

| Layer | Directory | Role |
|-------|-----------|------|
| **CLI** | `cli/commands/` | Parse args → delegate to service → format output |
| **Services** | `core/services/` | Business logic orchestration |
| **Core** | `core/analysis/`, `core/parsers/`, `core/flex/` | Domain logic |
| **Data** | `core/db/` | SQLite operations |
| **Utils** | `core/utils/` | Cross-cutting (logging, HTTP, Result type) |

---

## Key Design Decisions

1. **`Result<T, E>` error handling** — recoverable errors use `Result` (forces explicit handling); exceptions reserved for unrecoverable bugs only
2. **All money in USD** — `CurrencyConverter` converts at the CLI layer; JSON output is always USD
3. **Auto-fetched deps** — CLI11, fmt, spdlog, nlohmann/json, pugixml, cpp-httplib, rapidcsv, SQLiteCpp, Howard Hinnant's date
4. **SQLite storage** — positions, trades, and prices persisted to local DB
5. **Yahoo Finance for market data** — with synthetic Black-Scholes fallback when API is blocked

---

## Review Dimensions to Consider

### Correctness
- [ ] `Result<T, E>` propagated correctly (no silent drops)
- [ ] Currency conversion applied once and only once
- [ ] IBKR Flex XML/CSV parsing edge cases (multi-currency, corporate actions)
- [ ] Option symbol parser handles non-standard formats (e.g. Nikkei ETF 1329 multiplier)

### Error Handling
- [ ] All fallible paths return `Result` (not bare exceptions)
- [ ] Network retries with backoff in Flex client
- [ ] Graceful degradation when Yahoo Finance is rate-limited

### API Surface
- [ ] CLI arg validation (missing account, invalid date ranges)
- [ ] JSON output schema consistency across commands
- [ ] Help text accuracy

### Data Integrity
- [ ] SQLite queries use parameterized statements (no string concatenation)
- [ ] Import idempotency (re-importing same data doesn't duplicate)
- [ ] Schema migrations handled correctly

### Performance
- [ ] No unnecessary copies of large position vectors
- [ ] Price cache avoids redundant Yahoo Finance calls
- [ ] SQLite queries indexed appropriately

### Code Hygiene
- [ ] One class per file, naming conventions consistent
- [ ] No dead code from partially-implemented features
- [ ] Header includes minimal (no transitive include bloat)

---

## How to Build & Run

```bash
cmake --preset debug && cmake --build build/debug
./build/debug/ibkr-options-analyzer --help
```

## Key Files to Start With

| File | Why |
|------|-----|
| `src/cli/main.cpp` | Entry point, command registration |
| `src/core/services/import_service.cpp` | Core import logic (5.5K) |
| `src/core/services/price_cache_service.cpp` | Largest service (16K), Yahoo + cache |
| `src/core/services/screener_service.cpp` | Screening logic (8.8K) |
| `src/core/db/schema.hpp` | DB schema definition |
| `src/core/utils/` | Shared infrastructure (Result, logger, currency) |
