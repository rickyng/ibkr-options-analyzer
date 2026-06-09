# Architecture: CLI & Dashboard Data Flow

## High-Level Overview

```
                         ┌──────────────────────────────────────────────────┐
                         │              External Data Sources               │
                         │  IBKR Flex API  │  Yahoo Finance  │  Alpha Vantage│
                         └────────┬────────┴────────┬────────┴───────┬──────┘
                                  │                 │                │
                         ┌────────▼─────────────────▼────────────────▼──────┐
                         │              C++ CLI (`ibkr-options-analyzer`)   │
                         │                                                │
                         │  download → import → analyze → report          │
                         │         ↕                                      │
                         │    SQLite DB  (trades, open_options,            │
                         │    detected_strategies, cached_*)               │
                         │                                                │
                         │  All monetary values stored as USD              │
                         │  (CurrencyConverter applied at import/cache)    │
                         └────────┬───────────────────────────────────────┘
                                  │
                    ┌─────────────┼──────────────┐
                    │             │              │
              ┌─────▼─────┐ ┌────▼────┐
              │ run_cli()  │ │ Direct  │
              │  (write    │ │ SQLite  │
              │   path     │ │ Reads   │
              │   only)    │ │         │
              └─────┬──────┘ └────┬────┘
                    │             │
              ┌─────▼─────────────▼──────────────────────────┐
              │         Python Dashboard (Dash)               │
              │  Trade Review │ Positions │ Summary │ Reports │
              └──────────────────────────────────────────────┘
```

**All external API calls → C++ CLI → SQLite cache tables**
**All monetary values → converted to USD at C++ import/cache time → Python reads USD directly**
**Python dashboard → reads from SQLite tables only, no currency conversion**

## Currency Policy

**USD is the base currency. All monetary values are stored in USD in the database.**

| Data Type | Conversion Point | Where |
|-----------|-----------------|-------|
| Option trades | Import time | `TradeRepository::import_trades()` |
| Stock trades | Import time | `TradeRepository::import_stock_trades()` |
| Open positions | Import time | `TradeRepository::import_open_positions()` |
| Dividends | Import time | `TradeRepository::import_dividends()` |
| Interest expenses | Import time | `TradeRepository::import_interest_expenses()` |
| Cached prices | Cache time | `PriceCacheService::get_price()` |

The C++ `CurrencyConverter` (`src/core/utils/currency.hpp`) converts using `deduce_currency()`
to detect the trading currency from the symbol pattern:
- `.T` suffix → JPY (e.g., `1321.T`, `6758.T`)
- `.HK` suffix → HKD (e.g., `0700.HK`)
- Numeric-only (≤5 digits) → HKD (IBKR HK tickers: `1299`, `388`)
- `.TO` suffix → CAD (e.g., `RY.TO`)
- Otherwise → USD

The Python dashboard **must not** perform currency conversion — no `_FX` dicts, no `_to_usd()` helpers.

---

## CLI Commands

### Data Pipeline Commands (Write Path)

These commands move data from external sources into SQLite.

#### `download` — Flex API → CSV

```
IBKR Flex API ──► FlexService ──► CSV file on disk
                                    (~/.ibkr-options-analyzer/data/)
```

| Flag | Purpose |
|------|---------|
| `--account` | Account name (required) |
| `--token` | Flex Web Service token |
| `--query-id` | Flex query ID |
| `--force` | Skip cache, force re-download |

**Flow**: POST SendRequest → poll GetStatement every 5s → save CSV.

#### `import` — CSV → SQLite

```
CSV file ──► ImportService ──► SQLite tables
                                 (trades, open_options, stock_trades,
                                  dividends, interest_expenses)
```

All monetary values are converted to USD during import using `CurrencyConverter`.

| Flag | Purpose |
|------|---------|
| `--file` | Specific CSV file (auto-discovers if omitted) |

#### `import-history` — Historical CSV → SQLite

```
Activity Statement CSV(s) ──► ImportService ──► SQLite tables
                                                (stock_trades, dividends,
                                                 interest_expenses)
```

| Flag | Purpose |
|------|---------|
| `--file` | CSV file path(s), space-separated |
| `--account` | Override account name |

### Analysis Commands (Read Path)

These commands read from SQLite, optionally enrich with live prices, and output JSON/text.

#### `analyze` — DB + Live Prices → Analysis

```
SQLite DB ──► PositionService ──┐
                                ├──► JSON or console text
SQLite DB ──► PortfolioService ─┤
                                │
Yahoo/AlphaVantage ──► PriceService
```

| Sub-command | Service | Output |
|-------------|---------|--------|
| `analyze open` | PositionService | Positions grouped by duration buckets, risk indicators |
| `analyze impact` | PositionService + RiskCalculator | P&L impact for price changes on an underlying |
| `analyze portfolio` | PortfolioService | Portfolio overview with risk alerts, loss scenarios |

| Flag | Purpose |
|------|---------|
| `--account` | Filter by account |
| `--underlying` | Filter by symbol |

#### `trades` — DB → Trade History

```
SQLite DB ──► TradeRepository ──► JSON or console text
```

| Flag | Purpose |
|------|---------|
| `--date-from` | Start date (YYYY-MM-DD) |
| `--date-to` | End date |
| `--underlying` | Filter by symbol |
| `--account` | Filter by account |

#### `report` — DB → Report Output

```
SQLite DB ──► ReportService ──► ReportGenerator ──► Console text
                                 CSVExporter      ──► CSV file
```

| Flag | Purpose |
|------|---------|
| `--type` | `full` (default) · `positions` · `strategies` · `summary` |
| `--output` | CSV file path |
| `--account` / `--underlying` | Filters |

**Note**: `report` is used only for CSV export (API endpoint), not for dashboard rendering.

### Global Flags (all commands)

| Flag | Purpose |
|------|---------|
| `--format json` | Machine-readable JSON output |
| `--format text` | Human-readable console output (default) |
| `--quiet` | Suppress human output (JSON only) |
| `--google-sheet` | Push output to Google Sheets |
| `--log-level` | trace · debug · info · warn · error |

---

## Analysis Pipeline Detail

```
┌─────────────────────────────────────────────────────────────────┐
│                     C++ Analysis Pipeline                       │
│                                                                 │
│  ┌──────────┐    ┌──────────────┐    ┌───────────────────┐     │
│  │ SQLite DB│───►│PositionSvc   │───►│ StrategyDetector  │     │
│  │          │    │ (load + filter│    │ (single-leg type  │     │
│  │          │    │  positions)   │    │  classification)  │     │
│  └──────────┘    └──────────────┘    └────────┬──────────┘     │
│       │                                        │                │
│       │                               ┌────────▼──────────┐     │
│       │                               │ RiskCalculator    │     │
│       │                               │ (breakeven, max   │     │
│       │                               │  P/L, exposure)   │     │
│       │                               └────────┬──────────┘     │
│       │                                        │                │
│  ┌────▼──────┐    ┌──────────────┐    ┌────────▼──────────┐     │
│  │Yahoo/Alpha│───►│PriceService  │───►│ PortfolioService  │     │
│  │  Vantage  │    │(fetch+cache) │    │ (P&L, yields,     │     │
│  │           │    │              │    │  risk alerts)      │     │
│  └───────────┘    └──────────────┘    └────────┬──────────┘     │
│                                                 │                │
│                                          ┌──────▼──────┐         │
│                                          │ReportService│         │
│                                          │(text/CSV)   │         │
│                                          └─────────────┘         │
└─────────────────────────────────────────────────────────────────┘
```

| Component | Input | Processing | Output |
|-----------|-------|-----------|--------|
| **PositionService** | DB positions | Load, filter, summarize | `PortfolioSummary` (counts, premium, max loss) |
| **StrategyDetector** | Positions | Classify single-leg type (naked put/call) | `Strategy::Type` per position |
| **RiskCalculator** | Strategies + positions | Breakeven, max P/L, exposure | `RiskMetrics`, `PortfolioRisk`, `UnderlyingExposure` |
| **PriceService** | Yahoo/AlphaVantage API | Fetch + convert to USD + cache; earnings dates | `map<symbol, StockPrice>` |
| **PriceCacheService** | PriceFetcher + DB | Cache read/write, TTL management | Cached prices in `cached_prices` table |
| **PriceFetcher** | Yahoo/AlphaVantage API | Multi-source fetch with fallback | `StockPrice`, `OptionChainData` |
| **PortfolioService** | Positions + prices | P&L, ITM/OTM, yields, alerts | `PortfolioView` (enriched positions, loss scenarios) |
| **ReportService** | All above | Orchestrate + format | Text report / CSV export |

---

## Dashboard Data Access

### How the Dashboard Gets Data

The dashboard uses **two** data access patterns (no external API calls):

```
┌─────────────────────────────────────────────────────────────────┐
│                     Python Dashboard                            │
│                                                                 │
│  ┌─────────────────────────┐  ┌──────────────────────────────┐  │
│  │  ① Direct SQLite Reads  │  │ ② run_cli() (write path only)│  │
│  │                         │  │                              │  │
│  │  services/positions.py  │  │  download, import            │  │
│  │  services/strategies.py │  │  refresh (prices + earnings) │  │
│  │  services/trades.py     │  │                              │  │
│  │  services/trade_repo.py │  │                              │  │
│  │  services/holdings.py   │  │                              │  │
│  │  services/reports.py    │  │                              │  │
│  └───────────┬─────────────┘  └──────────────┬───────────────┘  │
│              │                                │                  │
└──────────────┼────────────────────────────────┼──────────────────┘
               │                                │
        ┌──────▼──────┐                  ┌─────▼──────┐
        │  SQLite DB  │                  │  C++ CLI   │
        │  (all USD)  │                  │  (writes)  │
        └─────────────┘                  └────────────┘
```

### Per-Tab Data Sources

| Dashboard Tab | Data Source | Python Service / SQL Tables |
|---------------|-----------|-------------------------------|
| **Trade Review** | Direct SQLite | `services/trades.py` → `trades`, `stock_trades` |
| **Positions** | Direct SQLite | `services/positions.py` → `open_options`, `cached_prices` |
| **Positions (exposure)** | Direct SQLite | `services/strategies.py` → `open_options` |
| **Positions (earnings)** | Direct SQLite | `services/positions.py` → `cached_earnings_dates` |
| **Summary – Options Ledger** | Direct SQLite | `services/trades.py` → `trades` |
| **Summary – Stocks Ledger** | Direct SQLite | `stock_trades`, `share_events`, `dividends` |
| **Summary – Dividends Ledger** | Direct SQLite | `services/trade_repo.py` → `dividends` |
| **Summary – KPI Cards** | Direct SQLite | `services/trades.py` + `stock_trades`, `dividends`, `interest_expenses` |
| **Summary – Wheel Status** | Direct SQLite | `open_options`, `stock_trades` |
| **Summary – Stock Holdings** | Direct SQLite | `services/holdings.py` → `stock_trades`, `cached_prices` |
| **Account Management** | CLI (write) + SQLite | `download`/`import` + `accounts` table |
| **CSV Export (API)** | Direct SQLite | `services/reports.py` → CSV from DB queries |

### When Each Pattern Is Used

| Pattern | When | Why |
|---------|------|-----|
| **Direct SQLite** | All data reads | No process spawn overhead; CLI already cached results |
| **`run_cli()`** | Write operations (download, import, refresh) | CLI owns external API calls and cache writes |

---

## C++ Service Inventory

| Service | Location | Responsibility |
|---------|----------|---------------|
| **FlexService** | `src/core/services/` | IBKR Flex API download orchestration |
| **ImportService** | `src/core/services/` | CSV parsing and DB import coordination |
| **PositionService** | `src/core/services/` | Load and filter open positions |
| **PriceService** | `src/core/services/` | Price fetching orchestration (delegates to PriceCacheService); earnings date fetching |
| **PriceCacheService** | `src/core/services/` | Price/volatility/chain caching with TTL management |
| **PortfolioService** | `src/core/services/` | Portfolio view construction with risk alerts |
| **ReportService** | `src/core/services/` | Report generation orchestration |

**Removed services**:
- ~~StrategyService~~ → strategy detection removed; positions are classified as single-leg (naked put/call) inline
- ~~ScreenerService~~ → screener functionality removed from CLI
- ~~StrategyDetector~~ (multi-leg) → only `strategy_type_to_string()` and single-leg classification remain

## Python Service Inventory

| Service | Location | Responsibility |
|---------|----------|---------------|
| **cli.py** | `dashboard/app/services/` | C++ CLI bridge (`run_cli()` for write operations) |
| **db.py** | `dashboard/app/services/` | Centralized SQLite connection and query execution |
| **trades.py** | `dashboard/app/services/` | Option and stock trade queries |
| **trade_repo.py** | `dashboard/app/services/` | Dividends, interest, share events, assignments |
| **positions.py** | `dashboard/app/services/` | Position enrichment (DTE, risk, distance, buckets) |
| **strategies.py** | `dashboard/app/services/` | Underlying exposure from cached analysis |
| **holdings.py** | `dashboard/app/services/` | Stock holdings with running cost basis |
| **symbols.py** | `dashboard/app/services/` | Market derivation and SQL helpers |
| **reports.py** | `dashboard/app/services/` | CSV report generation |

---

## Database Schema

**Location**: `~/.ibkr-options-analyzer/ibkr.db`
**Version**: 3.2.0

### Entity-Relationship Diagram

```
accounts ──1:N── trades
accounts ──1:N── open_options ──1:N── strategy_legs ──N:1── detected_strategies
accounts ──1:N── stock_trades                           └──1:N── accounts
accounts ──1:N── dividends
accounts ──1:N── interest_expenses

cached_prices            (standalone, key = symbol)
cached_volatility        (standalone, key = symbol)
cached_option_chains     (standalone, key = symbol+expiry)
cached_earnings_dates    (standalone, key = symbol)
stock_adjustments        (standalone, key = symbol)
metadata                 (standalone, key-value)
```

**Note**: All monetary values across all tables are stored in USD. The C++ CLI
converts at import/cache time using `CurrencyConverter` + `deduce_currency()`.

### Table Definitions

#### `accounts` — IBKR account credentials

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `name` | TEXT | NOT NULL UNIQUE | Account display name |
| `token` | TEXT | NOT NULL | Flex Web Service token |
| `query_id` | TEXT | NOT NULL | Flex query ID |
| `ibkr_account_id` | TEXT | DEFAULT '' | IBKR account identifier |
| `enabled` | INTEGER | NOT NULL DEFAULT 1 | Active flag |
| `created_at` | TEXT | NOT NULL DEFAULT NOW | Creation timestamp |
| `updated_at` | TEXT | NOT NULL DEFAULT NOW | Last update timestamp |

#### `trades` — Option trade history (from Flex reports)

All monetary values are **USD** (converted at import time).

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `account_id` | INTEGER | FK → accounts, NOT NULL | Owning account |
| `trade_date` | TEXT | NOT NULL | Trade date (YYYY-MM-DD) |
| `symbol` | TEXT | NOT NULL | Option symbol (e.g. `SPY250620C00500000`) |
| `underlying` | TEXT | | Underlying symbol (e.g. `SPY`) |
| `expiry` | TEXT | | Expiration date |
| `strike` | REAL | | Strike price (USD) |
| `right` | TEXT | | `C` or `P` |
| `quantity` | REAL | NOT NULL | Number of contracts (negative = sold) |
| `trade_price` | REAL | | Price per contract (USD) |
| `proceeds` | REAL | | Total proceeds (USD) |
| `commission` | REAL | | Commission (USD) |
| `net_cash` | REAL | | Net cash after commission (USD) |
| `multiplier` | REAL | DEFAULT 100 | Contract multiplier |
| `fifo_pnl_realized` | REAL | DEFAULT 0 | FIFO realized P&L (USD) |
| `close_price` | REAL | DEFAULT 0 | Closing price (USD) |
| `notes_codes` | TEXT | DEFAULT '' | IBKR notes/codes |
| `imported_at` | TEXT | NOT NULL DEFAULT NOW | Import timestamp |

**Indexes**: `account_id`, `symbol`, `underlying`, `trade_date`

#### `open_options` — Current non-expired option positions

All monetary values are **USD** (converted at import time).

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `account_id` | INTEGER | FK → accounts, NOT NULL | Owning account |
| `symbol` | TEXT | NOT NULL | Full option symbol |
| `underlying` | TEXT | NOT NULL | Underlying symbol |
| `expiry` | TEXT | NOT NULL | Expiration date |
| `strike` | REAL | NOT NULL | Strike price (USD) |
| `right` | TEXT | NOT NULL, CHECK(C/P) | Call or Put |
| `quantity` | REAL | NOT NULL | Contracts (negative = short) |
| `mark_price` | REAL | | Current mark price (USD) |
| `entry_premium` | REAL | | Entry premium collected/paid (USD) |
| `current_value` | REAL | | Current position value (USD) |
| `multiplier` | REAL | NOT NULL DEFAULT 100 | Contract multiplier |
| `is_manual` | INTEGER | NOT NULL DEFAULT 0 | Manually entered flag |
| `notes` | TEXT | | Free-text notes |
| `created_at` | TEXT | NOT NULL DEFAULT NOW | Creation timestamp |
| `updated_at` | TEXT | NOT NULL DEFAULT NOW | Last update timestamp |

**Indexes**: `account_id`, `underlying`, `expiry`, `symbol`

#### `detected_strategies` — Auto-detected option strategies

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `account_id` | INTEGER | FK → accounts, NOT NULL | Owning account |
| `strategy_type` | TEXT | NOT NULL | `naked_put`, `bull_put_spread`, `iron_condor`, etc. |
| `underlying` | TEXT | NOT NULL | Underlying symbol |
| `expiry` | TEXT | NOT NULL | Strategy expiration |
| `leg_count` | INTEGER | NOT NULL | Number of legs |
| `net_premium` | REAL | | Net premium collected |
| `max_profit` | REAL | | Maximum profit |
| `max_loss` | REAL | | Maximum loss (−1 if unlimited) |
| `breakeven_price` | REAL | | Breakeven price |
| `confidence` | REAL | | Detection confidence (0–1) |
| `detected_at` | TEXT | NOT NULL DEFAULT NOW | Detection timestamp |
| `risk_level` | TEXT | | LOW, MEDIUM, HIGH, DEFINED |
| `net_premium_usd` | REAL | | Net premium in USD |
| `max_profit_usd` | REAL | | Max profit in USD |
| `max_loss_usd` | REAL | | Max loss in USD (−1 if unlimited) |
| `breakeven_price_usd` | REAL | | Breakeven in USD |
| `account_name` | TEXT | | Denormalized account name |
| `currency` | TEXT | DEFAULT 'USD' | Strategy currency |
| `analysis_timestamp` | TEXT | | When analysis was run |

**Indexes**: `account_id`, `underlying`, `strategy_type`

#### `strategy_legs` — Links options to strategies (M:N join)

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `strategy_id` | INTEGER | FK → detected_strategies, NOT NULL | Parent strategy |
| `option_id` | INTEGER | FK → open_options, NOT NULL | Constituent option |
| `leg_role` | TEXT | | Role (e.g. `short_put`, `long_put`, `short_call`) |

**Indexes**: `strategy_id`, `option_id`

#### `stock_trades` — Stock trade history (from Activity Statements)

All monetary values are **USD** (converted at import time).

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `account_id` | INTEGER | FK → accounts, NOT NULL | Owning account |
| `symbol` | TEXT | NOT NULL | Stock symbol |
| `description` | TEXT | DEFAULT '' | Trade description |
| `trade_date` | TEXT | NOT NULL | Trade date |
| `quantity` | REAL | NOT NULL | Shares (negative = sold) |
| `trade_price` | REAL | | Price per share (USD) |
| `proceeds` | REAL | | Total proceeds (USD) |
| `commission` | REAL | DEFAULT 0 | Commission (USD) |
| `net_cash` | REAL | DEFAULT 0 | Net cash (USD) |
| `notes_codes` | TEXT | DEFAULT '' | IBKR codes |
| `imported_at` | TEXT | NOT NULL DEFAULT NOW | Import timestamp |

**Indexes**: `account_id`, `symbol`, `trade_date`

#### `dividends` — Dividend income (from Activity Statements)

Stored in **USD** (converted at import time).

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `account_id` | INTEGER | FK → accounts, NOT NULL | Owning account |
| `symbol` | TEXT | NOT NULL | Paying stock symbol |
| `description` | TEXT | DEFAULT '' | Dividend description |
| `ex_date` | TEXT | | Ex-dividend date |
| `pay_date` | TEXT | NOT NULL | Payment date |
| `amount` | REAL | NOT NULL | Dividend amount (USD) |
| `tax_withheld` | REAL | DEFAULT 0 | Tax withheld (USD) |
| `currency` | TEXT | DEFAULT 'USD' | Always 'USD' after conversion |
| `imported_at` | TEXT | NOT NULL DEFAULT NOW | Import timestamp |

**Indexes**: `account_id`, `symbol`, `pay_date`

#### `interest_expenses` — Margin interest charges (from Activity Statements)

Stored in **USD** (converted at import time).

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `account_id` | INTEGER | FK → accounts, NOT NULL | Owning account |
| `currency` | TEXT | NOT NULL | Always 'USD' after conversion |
| `date` | TEXT | NOT NULL | Charge date |
| `description` | TEXT | DEFAULT '' | Description |
| `amount` | REAL | NOT NULL | Interest amount (USD) |
| `imported_at` | TEXT | NOT NULL DEFAULT NOW | Import timestamp |

**Indexes**: `account_id`, `date`

#### `cached_prices` — Stock price cache

Prices stored in **USD** (converted at cache time).

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `symbol` | TEXT | PK | Stock symbol |
| `price` | REAL | NOT NULL | Last price (USD) |
| `source` | TEXT | NOT NULL | `yahoo` / `alpha_vantage` |
| `fetched_at` | TEXT | NOT NULL | When fetched |
| `trading_date` | TEXT | NOT NULL | Trading date (cache TTL key) |

#### `cached_volatility` — Implied volatility cache

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `symbol` | TEXT | PK | Stock symbol |
| `volatility` | REAL | NOT NULL | Implied volatility |
| `source` | TEXT | NOT NULL | Data source |
| `fetched_at` | TEXT | NOT NULL | When fetched |
| `trading_date` | TEXT | NOT NULL | Trading date |

#### `cached_option_chains` — Option chain cache

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `symbol` | TEXT | NOT NULL | Underlying symbol |
| `expiry` | TEXT | NOT NULL | Expiration date |
| `chain_json` | TEXT | NOT NULL | Full chain as JSON |
| `source` | TEXT | NOT NULL | `yahoo` / `synthetic` |
| `fetched_at` | TEXT | NOT NULL | When fetched |
| `trading_date` | TEXT | NOT NULL | Trading date |

**Unique**: `(symbol, expiry)`

#### `stock_adjustments` — Stock splits and symbol renames

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `id` | INTEGER | PK AUTO | Row ID |
| `symbol` | TEXT | NOT NULL | Stock symbol |
| `effective_date` | TEXT | NOT NULL | When adjustment takes effect |
| `split_ratio` | REAL | NOT NULL DEFAULT 1.0 | Split ratio (e.g. 4.0 for 4:1) |
| `new_symbol` | TEXT | | New symbol after rename |
| `created_at` | TEXT | NOT NULL DEFAULT NOW | Creation timestamp |

#### `metadata` — Schema version and config

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `key` | TEXT | PK | Metadata key |
| `value` | TEXT | NOT NULL | Metadata value |
| `updated_at` | TEXT | NOT NULL DEFAULT NOW | Last update |

**Seeded with**: `schema_version` = `3.2.0`

#### `cached_earnings_dates` — Earnings date cache

| Column | Type | Constraints | Description |
|--------|------|-------------|-------------|
| `symbol` | TEXT | PK | Stock symbol |
| `next_earnings_date` | TEXT | | Next earnings date |
| `source` | TEXT | NOT NULL DEFAULT 'yahoo' | Data source |
| `fetched_at` | TEXT | NOT NULL | When fetched |
| `trading_date` | TEXT | NOT NULL | Cache TTL key |

---

## Schema Migration History

| Version | Changes |
|---------|---------|
| 3.0.0 | Added risk_level, USD columns, account_name to `detected_strategies` |
| 3.1.0 | Added `ibkr_account_id` to `accounts` |
| 3.2.0 | **Currency migration**: Converted all existing native-currency data (trades, stock_trades, open_options, cached_prices) to USD. New imports now convert at write time. Migration runs in a single transaction; uses numeric version comparison. |
