# CLI Workflow Guide

Quick reference for the CLI + dashboard workflow.

The CLI owns **all external API calls** (IBKR Flex, Yahoo Finance, Alpha Vantage)
and writes results to SQLite. The dashboard reads from SQLite directly.

---

## 1. Setup (one-time)

```bash
# Build release binary
cmake --preset release && cmake --build build/release

# Create config (fill in your Flex tokens and query IDs)
cp config.json.example ~/.ibkr-options-analyzer/config.json
$EDITOR ~/.ibkr-options-analyzer/config.json

# Start dashboard (separate terminal)
cd dashboard && uvicorn app.main:app --reload --port 8001
```

---

## 2. Download Flex Reports

Downloads position data from IBKR Flex Web Service. Token and query ID are read
from `config.json` by account name — no need to pass them on the CLI.

```bash
# Single account (token/query-id from config)
./build/release/ibkr-options-analyzer download --account "Account Name"

# Force re-download (ignore cache)
./build/release/ibkr-options-analyzer download --account "Account Name" --force

# Override token/query-id from CLI (takes precedence over config)
./build/release/ibkr-options-analyzer download \
  --token <FLEX_TOKEN> --query-id <QUERY_ID> --account "Account Name"
```

CSV files are saved to `~/.ibkr-options-analyzer/data/`.

---

## 3. Import into Database

Parses downloaded CSV files and loads positions into SQLite.
All monetary values are converted to USD during import.

```bash
# Import all downloaded files
./build/release/ibkr-options-analyzer import

# Import a specific file
./build/release/ibkr-options-analyzer import --file path/to/report.csv
```

---

## 3b. Import Historical Trades (optional)

Import historical Activity Statement CSVs for P&L tracking.

```bash
# Single file (account name auto-detected from filename)
./build/release/ibkr-options-analyzer import-history --file U1234567_activity.csv

# Multiple files, override account name
./build/release/ibkr-options-analyzer import-history \
  --file U1234567_activity.csv --file U7654321_activity.csv \
  --account "Main Account"
```

---

## 4. Check the Database

## 4. Query the Database

No built-in `db` command — use `sqlite3` directly. The database is at
`~/.ibkr-options-analyzer/data.db`.

```bash
# Open interactive session
sqlite3 ~/.ibkr-options-analyzer/data.db

# Quick overview (one-liners)
sqlite3 ~/.ibkr-options-analyzer/data.db ".tables"
sqlite3 ~/.ibkr-options-analyzer/data.db ".schema open_options"
sqlite3 ~/.ibkr-options-analyzer/data.db "SELECT COUNT(*) FROM open_options"

# Positions summary
sqlite3 ~/.ibkr-options-analyzer/data.db \
  "SELECT underlying, right, COUNT(*), SUM(quantity) FROM open_options GROUP BY underlying, right"

# Check schema version
sqlite3 ~/.ibkr-options-analyzer/data.db "SELECT * FROM metadata"

Just use sqlite3 directly — the DB is at ~/.ibkr-options-analyzer/data.db:
   
  # Quick overview
  sqlite3 ~/.ibkr-options-analyzer/data.db ".tables"
  sqlite3 ~/.ibkr-options-analyzer/data.db "SELECT COUNT(*) FROM open_options"

  # Schema
  sqlite3 ~/.ibkr-options-analyzer/data.db ".schema open_options"

  # Positions summary
  sqlite3 ~/.ibkr-options-analyzer/data.db \
    "SELECT underlying, right, COUNT(*), SUM(quantity) FROM open_options GROUP BY underlying, right"

  # Check schema version
  sqlite3 ~/.ibkr-options-analyzer/data.db \
    "SELECT * FROM metadata"

  For structured output, the CLI commands themselves are the "query tools" — they all support --format json:

  ./build/release/ibkr-options-analyzer analyze open --format json   # positions + prices
  ./build/release/ibkr-options-analyzer analyze portfolio --format json  # P&L + risk
  ./build/release/ibkr-options-analyzer trades --format json          # trade history

```


For structured output, use the CLI commands with `--format json`:

```bash
./build/release/ibkr-options-analyzer analyze open --format json      # positions + prices
./build/release/ibkr-options-analyzer analyze portfolio --format json  # P&L + risk alerts
./build/release/ibkr-options-analyzer trades --format json             # trade history
```

---

## 5. Analyze Positions

The `analyze` command fetches live prices and produces JSON or text output.

```bash
# Open positions summary (also fetches prices → cached_prices)
./build/release/ibkr-options-analyzer analyze open

# Price impact analysis (requires --underlying)
./build/release/ibkr-options-analyzer analyze impact --underlying AAPL

# Full portfolio view
./build/release/ibkr-options-analyzer analyze portfolio

# Filter by account or underlying
./build/release/ibkr-options-analyzer analyze open --account "Account Name"
./build/release/ibkr-options-analyzer analyze impact --underlying AAPL
```

All analyze commands support `--format json` for machine-readable output.

### What gets cached

| Command | Cache Tables Written |
|---------|---------------------|
| `analyze open` | `cached_prices` (via PriceService) |
| `analyze portfolio` | `cached_prices` |

---

## 6. Generate Report

```bash
# Full interactive report (text)
./build/release/ibkr-options-analyzer report

# Export to CSV
./build/release/ibkr-options-analyzer report --output report.csv --type strategies

# Report types: full (default), positions, strategies, summary
./build/release/ibkr-options-analyzer report --type summary

# Filtered report
./build/release/ibkr-options-analyzer report --account "Account Name" --underlying AAPL
```

---

## 7. List Trades

```bash
# All trades
./build/release/ibkr-options-analyzer trades

# Date range
./build/release/ibkr-options-analyzer trades --date-from 2026-01-01 --date-to 2026-06-30

# Filtered
./build/release/ibkr-options-analyzer trades --underlying AAPL --account "Account Name"
```

---

## Global Options (any command)

| Flag | Description |
|---|---|
| `--config PATH` | Config file path |
| `--log-level LEVEL` | trace, debug, info, warn, error |
| `--format FORMAT` | text (default) or json |
| `--quiet` | Suppress text output (JSON only) |

---

## Typical Daily Workflow

```bash
# 1. Download latest positions for each account
./build/release/ibkr-options-analyzer download --account "No1"
./build/release/ibkr-options-analyzer download --account "No2"

# 2. Import into DB
./build/release/ibkr-options-analyzer import

# 3. Run analysis (fetches prices, caches results for dashboard)
./build/release/ibkr-options-analyzer analyze portfolio

# 4. Refresh prices and earnings (updates cached_prices + cached_earnings_dates)
./build/release/ibkr-options-analyzer refresh

# 5. Open dashboard — reads from SQLite cache, no CLI calls needed
open http://localhost:8001
```

---

## Dashboard ↔ CLI Data Flow

```
┌──────────┐  download   ┌───────┐  import   ┌───────────┐
│  IBKR    │ ──────────► │  CSV  │ ────────► │  SQLite   │
│  Flex    │             └───────┘           │           │
└──────────┘                                 │  trades   │
                                             │  open_    │
┌──────────┐  fetch      ┌──────────┐        │  options  │
│  Yahoo / │ ──────────► │ cached_* │        │  stock_   │
│  Alpha   │  prices     │  tables  │        │  trades   │
└──────────┘             └──────────┘        └─────┬─────┘
                                                   │
                                          ┌────────▼────────┐
                                          │ Python Dashboard │
                                          │ (reads only)     │
                                          └─────────────────┘
```

The dashboard never makes external API calls. It reads all data from
SQLite tables populated by the CLI.
