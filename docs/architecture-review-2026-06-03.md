# Architecture Review — ibkr-options-analyzer

**Date:** 2026-06-03
**Scope:** Full-stack — C++ CLI, Python Dashboard, SQLite data layer

---

## 1. System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        IBKR Flex Web Service                        │
└──────────────────────────┬──────────────────────────────────────────┘
                           │ CSV reports
                           ▼
┌─────────────── C++ CLI (ibkr_core) ───────────────┐
│  Commands: download → import → analyze → trades    │
│  Services: FlexService, ImportService, ...          │
│  Analysis: StrategyDetector, RiskCalculator         │
│  Data:    Repositories → SQLite (data.db)           │
│  Output:  JSON (stdout) or text report              │
└──────────────────────────┬─────────────────────────┘
                           │ run_cli("trades") / run_cli("analyze", "open")
                           ▼
┌─────────────── Python Dashboard ───────────────────┐
│  FastAPI: /api/* routes (accounts, positions, ...)  │
│  Dash:    Tabs → Callbacks → CLI + DB queries       │
│  Services: cli.py (subprocess), db.py (SQLite3)     │
└─────────────────────────────────────────────────────┘
```

**Two independent read paths:**
1. **CLI path:** `run_cli("trades")` → C++ subprocess → JSON → callbacks parse
2. **DB path:** `query("SELECT ... FROM trades")` → SQLite directly → callbacks

This dual-path design is the central architectural tension.

---

## 2. C++ CLI — Architecture Assessment

### 2.1 Layer Structure

| Layer | Directory | Responsibility | Lines |
|-------|-----------|---------------|-------|
| CLI | `src/cli/commands/` (6 files) | Parse args, call services, format output | ~1,287 |
| Services | `src/core/services/` (9 files) | Business logic, CLI-agnostic | ~800 |
| Analysis | `src/core/analysis/` (4 files) | Strategy detection, risk calc | ~500 |
| Parsers | `src/core/parsers/` (2 files) | Option symbol parsing | ~100 |
| DB | `src/core/db/` (6 files) | Schema, repositories, migrations | ~600 |
| Flex | `src/core/flex/` (2 files) | IBKR API client | ~200 |
| Config | `src/core/config/` (2 files) | JSON config loading | ~200 |
| Utils | `src/core/utils/` (6 files) | Result<T>, logger, HTTP, JSON output | ~760 |

**Total: ~4,500 lines C++ across 36 files**

### 2.2 What Works Well ✅

1. **Clean layered architecture.** Commands → Services → Repositories → DB. No layer skips.
2. **Consistent error handling.** `Result<T, E>` everywhere — no exceptions in hot paths.
3. **Modern C++20.** RAII, smart pointers, structured bindings, concepts where useful.
4. **Dependency injection.** Services accept `Database&` via constructor, not globals.
5. **Single build target.** `ibkr_core` static library + thin CLI executable. All deps auto-fetched.
6. **Repository pattern.** Each entity (accounts, trades, positions) has its own repository class.

### 2.3 Issues ⚠️

#### C1. analyze_command.cpp is 791 lines — God Command

The `analyze` command handles 5 subtypes: `open`, `impact`, `strategy`, `portfolio`, `screener`. Each should be its own command or at least its own function in a separate file.

**Impact:** Hard to navigate, hard to test, mixes strategy detection with output formatting.

#### C2. No test infrastructure

Zero test files. No test target in CMake. For a financial calculations engine (risk, P&L, strategy detection), this is a significant gap.

**Impact:** Refactoring is risky. Strategy detection edge cases are unverified.

#### C3. Config relies on file paths

`config.json` must exist at `~/.ibkr-options-analyzer/config.json`. No env var override, no CLI-only mode. The config is tightly coupled to file I/O.

#### C4. Price fetching has no circuit breaker

`PriceFetcher` (utils/price_fetcher.hpp, 146 lines) tries Yahoo then Alpha Vantage. No rate limiting, no backoff on persistent failure, no stale-price warning.

---

## 3. Python Dashboard — Architecture Assessment

### 3.1 Layer Structure

| Layer | File | Responsibility | Lines |
|-------|------|---------------|-------|
| Entry | `app/main.py` | FastAPI + Dash mount | 49 |
| API | `app/api/*.py` (9 files) | REST endpoints | 491 |
| Frontend | `app/frontend/layout.py` | All UI components | 923 |
| Frontend | `app/frontend/callbacks.py` | All Dash callbacks | 2,617 |
| Services | `app/services/cli.py` | C++ subprocess wrapper | 69 |
| Services | `app/services/db.py` | SQLite3 thin wrapper | 32 |
| Config | `app/config.py` | Pydantic settings | 17 |

**Total: ~4,300 lines Python across 15 files**

### 3.2 What Works Well ✅

1. **FastAPI + Dash coexistence.** Clean mounting strategy — API routes and Dash don't conflict.
2. **CLI service abstraction.** `run_cli()` wraps subprocess cleanly with `CliError`.
3. **DB service simplicity.** 32 lines, does exactly what's needed — `query()`, `query_one()`, `execute()`.
4. **Currency policy.** All conversion in C++ CLI; dashboard treats everything as USD. Correctly prevents double-conversion bugs.

### 3.3 Issues 🔴

#### D1. callbacks.py is 2,617 lines — Monolith

**28 callback functions** in a single file. This is the single biggest architectural problem.

Callbacks mix 5 distinct concerns:
- Data fetching (CLI subprocess calls, SQL queries)
- Business logic (running cost basis, P&L aggregation, year/market filtering)
- Data transformation (formatting currency, computing win rates)
- External API calls (Yahoo Finance for prices and earnings dates)
- UI rendering (building Plotly charts, HTML tables)

**Recommendation:** Split into modules by feature:

```
app/frontend/callbacks/
├── __init__.py          # register_all(app)
├── positions.py         # Positions tab callbacks
├── summary.py           # Summary tab + sub-tabs
├── trade_review.py      # Trade Review tab callbacks
├── screener.py          # Screener tab callbacks
├── account_mgmt.py      # Account CRUD + download
└── shared.py            # _derive_market_from_symbol, _apply_year_filter, etc.
```

#### D2. Dual data paths create inconsistency

The dashboard reads data through two paths:

| Path | Used by | Returns |
|------|---------|---------|
| `run_cli("trades")` | Trade Review, Summary Options tab | JSON from C++ (all trades, no date filter at CLI level) |
| `query("SELECT ...")` | Dividends, Interest, Stock Holdings | Direct SQLite (filtered by SQL) |

The CLI `trades` command supports `--date-from`/`--date-to` but the dashboard doesn't use them — it fetches ALL trades then filters client-side. Meanwhile, dividends/interest are filtered server-side via SQL. This creates:
- **Performance waste:** Fetching 6+ years of trades to filter to 2025 in Python
- **Inconsistency:** Some filters are SQL-level, others are Python-level
- **Fragility:** If the CLI JSON format changes, Python filtering breaks silently

#### D3. Business logic in callbacks

`_compute_stock_holdings()` (lines 172–323) is **150 lines of business logic** — running average cost basis, realized P&L tracking, currency conversion, price fetching — all embedded in a callbacks file.

This function should be in a service layer, testable independently of Dash.

Similarly, `_fetch_exposure()`, `_fetch_expiry_calendar()`, and the dividend/interest SQL queries are all business logic that belongs in a service, not a callback.

#### D4. No repository/data access layer

SQL queries are scattered across callbacks.py as string literals:

```python
# In 7+ different callbacks:
"SELECT d.symbol, d.amount, d.tax_withheld FROM dividends d ..."
"SELECT i.amount FROM interest_expenses i ..."
"SELECT st.symbol, a.name AS account, SUM(ABS(st.quantity)) ..."
"SELECT underlying, right, SUM(ABS(quantity)) as cnt FROM open_options ..."
```

No single source of truth for queries. Changes to schema require hunting through callbacks.

#### D5. Repeated filter logic

The pattern `if market and market != "All Markets": option_trades = [t for t in option_trades if ...]` appears **12+ times** across callbacks. Similarly for year filtering. The helpers (`_apply_year_filter`, `_derive_market_from_symbol`) help, but the filter-and-then-apply pattern is duplicated in every callback.

A single filtered data store per tab would eliminate this repetition:

```python
# Instead of 5 callbacks each filtering independently:
@app.callback(Output("sum-filtered-trades", "data"), [Inputs...])
def filter_sum_trades(trade_data, account, market, year):
    # Filter once, store results
    ...

# Then all sub-tab callbacks read from the filtered store
```

#### D6. No error handling in callbacks

Most callbacks silently catch and swallow exceptions:

```python
try:
    for row in query(div_sql, ...):
        ...
except Exception:
    pass  # User sees empty data with no explanation
```

A single failed query produces a blank tab with no error indicator.

#### D7. No tests

Empty test directory. Zero test files.

---

## 4. Data Layer — Assessment

### 4.1 Schema

**Strengths:**
- Proper foreign keys (`account_id → accounts.id ON DELETE CASCADE`)
- Indexes on frequently queried columns
- Schema versioning via metadata table
- All dates as TEXT in ISO format (consistent)

**Issues:**
- `dividends.ex_date` is NULL for all rows — only `pay_date` is populated. The schema accepts NULL for a seemingly required field.
- No migration framework — schema changes require manual SQL updates.
- `open_options` is a materialized view of `trades` but has no automatic refresh mechanism.

### 4.2 Dual Write Path

The C++ CLI writes to the DB via `import` and `download` commands. The Python dashboard writes via `execute()` in account management. Both paths need to stay in sync on schema.

---

## 5. Recommended Refactoring Priorities

### Priority 1: Split callbacks.py (High impact, Low risk)

| From | To | Effort |
|------|----|--------|
| `callbacks.py` (2,617 lines) | `callbacks/` package with 6 modules | ~2 hours |

Move helper functions to `shared.py`, then split callbacks by tab. Each module registers its own callbacks via a `register(app)` function.

### Priority 2: Extract business logic to services (High impact, Medium risk)

| From | To | Effort |
|------|----|--------|
| `_compute_stock_holdings` (150 lines) | `app/services/holdings.py` | ~1 hour |
| `_fetch_exposure`, `_fetch_expiry_calendar` | `app/services/positions.py` | ~30 min |
| Dividend/interest SQL queries | `app/services/income.py` | ~1 hour |
| Yahoo price/earnings fetching | `app/services/market_data.py` | ~30 min |

### Priority 3: Add year filtering to CLI calls (Medium impact, Low risk)

Pass `--date-from` and `--date-to` to `run_cli("trades")` when a year is selected, instead of filtering all trades client-side. This reduces data transfer and makes the dual-path consistent.

### Priority 4: Consolidate data access (Medium impact, Medium risk)

Create `app/services/trade_repository.py` and `app/services/income_repository.py` that centralize all SQL queries. Callbacks call service methods, not raw SQL.

### Priority 5: Add tests (High impact, High effort)

Start with:
- Unit tests for `_compute_stock_holdings` (most complex business logic)
- Unit tests for `_derive_market_from_symbol`, `_apply_year_filter`
- Integration tests for CLI service (`run_cli`)

---

## 6. File Reference

### C++ Core
| Path | Purpose | Lines |
|------|---------|-------|
| `src/cli/main.cpp` | Entry point, CLI11 routing | 285 |
| `src/cli/commands/analyze_command.cpp` | Position analysis (5 types) | 791 |
| `src/cli/commands/trades_command.cpp` | Trade listing | 181 |
| `src/core/services/position_service.cpp` | Position queries | ~200 |
| `src/core/services/portfolio_service.cpp` | Portfolio view | ~200 |
| `src/core/services/screener_service.cpp` | Opportunity screening | ~300 |
| `src/core/db/schema.hpp` | Full schema DDL | 280 |
| `src/core/db/database.cpp` | Connection + repo init | ~150 |
| `src/core/analysis/strategy_detector.cpp` | Pattern detection | ~200 |
| `src/core/analysis/risk_calculator.cpp` | Risk metrics | ~200 |
| `src/core/utils/result.hpp` | Result<T, E> type | 197 |

### Python Dashboard
| Path | Purpose | Lines |
|------|---------|-------|
| `dashboard/app/main.py` | FastAPI entry | 49 |
| `dashboard/app/frontend/layout.py` | All UI layout | 923 |
| `dashboard/app/frontend/callbacks.py` | All Dash callbacks | 2,617 |
| `dashboard/app/services/cli.py` | C++ subprocess wrapper | 69 |
| `dashboard/app/services/db.py` | SQLite3 wrapper | 32 |
| `dashboard/app/config.py` | Settings | 17 |
