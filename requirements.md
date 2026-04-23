# IBKR Options Analyzer — Hybrid Architecture Requirements

## Overview

Build a **C++ core engine** (CLI + static library) for tracking and analyzing non-expired open option positions from **multiple** Interactive Brokers accounts, plus a **separate Python web dashboard** project for visualization and interaction.

The C++ engine handles all business logic, data processing, and IBKR integration. The Python dashboard reads the shared SQLite database and triggers C++ CLI commands for writes. This separation ensures the C++ engine is independently usable from the terminal while the Python layer provides a rich web UI.

## Architecture

```
┌─────────────────────────────────┐       ┌──────────────────────────────┐
│  C++ Engine (this repo)         │       │  Python Dashboard (separate  │
│                                 │       │  repo: ibkr-dashboard)       │
│  libibkr_core (static lib)      │       │                              │
│    ├── parsers                  │       │  FastAPI REST API            │
│    ├── analysis                 │       │  Dash Web Dashboard          │
│    ├── services ──── SQLite ────┼──────►│                              │
│    ├── db                       │       │  Reads SQLite directly       │
│    └── models                   │       │  Calls CLI for writes/sync   │
│                                 │       │                              │
│  ibkr-cli (executable)          │       │  Integration:                │
│    ├── download                 │       │    - Shared SQLite DB        │
│    ├── import  ─────────────────┼──────►│    - CLI --format json       │
│    ├── analyze                  │       │    - subprocess calls        │
│    ├── report                   │       │                              │
│    ├── manual-add               │       │                              │
│    ├── accounts                 │       │                              │
│    └── sync                     │       │                              │
└─────────────────────────────────┘       └──────────────────────────────┘
```

### Integration Contract
1. **C++ writes** to SQLite (download, import, manual-add, sync)
2. **C++ outputs JSON** via `--format json` flag on all commands
3. **Python reads** SQLite directly for queries and display
4. **Python triggers** C++ CLI via subprocess for write/sync operations
5. **Shared schema** versioned in metadata table — both sides validate on startup

## Tech Stack

### C++ Engine (This Repo)
| Layer | Technology |
|-------|-----------|
| Language | C++20/23 |
| Build | CMake 3.26+ with Ninja |
| CLI | CLI11 |
| Database | SQLite via SQLiteCpp |
| HTTP | cpp-httplib |
| JSON | nlohmann/json |
| XML | pugixml |
| CSV | rapidcsv |
| Logging | spdlog + fmt |
| Dates | Howard Hinnant's date library |
| Testing | Catch2 or doctest |

### Python Dashboard (Separate Repo)
| Layer | Technology |
|-------|-----------|
| Language | Python 3.12+ |
| Backend | FastAPI |
| Dashboard | Dash + Plotly + dash-bootstrap-components |
| Database | SQLite via SQLAlchemy (read-only) |
| HTTP Client | httpx (async, for CLI subprocess calls) |
| Validation | Pydantic v2 |
| Testing | pytest + pytest-asyncio |
| Container | Docker (multi-stage) |
| Deployment | Railway or Render |

## Project Structure

### C++ Engine (This Repo)
```
ibkr-options-analyzer/
├── CMakeLists.txt
├── CMakePresets.json
├── config.json.example
├── src/
│   ├── core/                        # Static library: libibkr_core.a
│   │   ├── models/
│   │   │   ├── option.hpp           # Option position data structures
│   │   │   ├── strategy.hpp         # Strategy types + enums
│   │   │   ├── risk.hpp             # Risk metrics structs
│   │   │   ├── account.hpp          # Account model
│   │   │   └── trade.hpp            # Trade record model
│   │   ├── parsers/
│   │   │   ├── option_symbol_parser.hpp/cpp
│   │   │   └── csv_parser.hpp/cpp
│   │   ├── analysis/
│   │   │   ├── strategy_detector.hpp/cpp
│   │   │   └── risk_calculator.hpp/cpp
│   │   ├── db/
│   │   │   ├── database.hpp/cpp
│   │   │   └── schema.hpp
│   │   ├── services/
│   │   │   ├── flex_service.hpp/cpp
│   │   │   ├── import_service.hpp/cpp
│   │   │   ├── price_service.hpp/cpp
│   │   │   ├── report_service.hpp/cpp
│   │   │   ├── position_service.hpp/cpp
│   │   │   └── strategy_service.hpp/cpp
│   │   └── utils/
│   │       ├── result.hpp
│   │       ├── logger.hpp/cpp
│   │       └── http_client.hpp/cpp
│   ├── cli/                         # CLI executable: ibkr-options-analyzer
│   │   ├── main.cpp
│   │   ├── cli_app.hpp/cpp
│   │   └── commands/
│   │       ├── download_command.hpp/cpp
│   │       ├── import_command.hpp/cpp
│   │       ├── analyze_command.hpp/cpp
│   │       ├── report_command.hpp/cpp
│   │       ├── manual_add_command.hpp/cpp
│   │       ├── accounts_command.hpp/cpp
│   │       └── sync_command.hpp/cpp
│   └── json/
│       └── json_output.hpp/cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_option_symbol.cpp
│   ├── test_csv_parser.cpp
│   ├── test_strategy_detector.cpp
│   ├── test_risk_calculator.cpp
│   ├── test_database.cpp
│   └── test_json_output.cpp
└── data/                            # SQLite DB (gitignored)
    └── .gitkeep
```

### Python Dashboard (Separate Repo: ibkr-dashboard)
```
ibkr-dashboard/
├── pyproject.toml
├── Dockerfile
├── docker-compose.yml
├── .env.example
├── app/
│   ├── __init__.py
│   ├── main.py                     # FastAPI + Dash app factory
│   ├── config.py                   # Pydantic Settings
│   ├── database.py                 # SQLAlchemy async engine (read-only)
│   ├── cli_client.py               # Subprocess wrapper for C++ CLI
│   ├── models/                     # SQLAlchemy ORM models (read-only)
│   │   ├── __init__.py
│   │   ├── account.py
│   │   ├── trade.py
│   │   ├── open_option.py
│   │   ├── detected_strategy.py
│   │   ├── strategy_leg.py
│   │   └── metadata.py
│   ├── schemas/                    # Pydantic response models
│   │   ├── __init__.py
│   │   ├── position.py
│   │   ├── strategy.py
│   │   ├── risk.py
│   │   └── account.py
│   ├── api/                        # REST endpoints (read from DB, write via CLI)
│   │   ├── __init__.py
│   │   ├── deps.py
│   │   ├── router.py
│   │   ├── positions.py
│   │   ├── strategies.py
│   │   ├── import_csv.py
│   │   ├── flex.py
│   │   ├── prices.py
│   │   ├── reports.py
│   │   ├── accounts.py
│   │   └── dashboard.py
│   ├── dashboard/                  # Dash frontend
│   │   ├── __init__.py
│   │   ├── app.py
│   │   ├── layouts/
│   │   │   ├── __init__.py
│   │   │   ├── main.py
│   │   │   ├── positions.py
│   │   │   ├── strategies.py
│   │   │   ├── risk.py
│   │   │   ├── expiration.py
│   │   │   ├── import_layout.py
│   │   │   └── settings.py
│   │   └── callbacks/
│   │       ├── __init__.py
│   │       ├── positions.py
│   │       ├── strategies.py
│   │       ├── risk.py
│   │       ├── expiration.py
│   │       ├── import_cb.py
│   │       └── settings.py
│   └── utils/
│       └── __init__.py
├── tests/
│   ├── conftest.py
│   ├── test_cli_client.py
│   ├── test_positions_api.py
│   └── test_strategies_api.py
└── data/                           # Symlink or mount to C++ data dir
    └── .gitkeep
```

---

## Data Models

### Database Schema (SQLite)

Shared between C++ engine (writes) and Python dashboard (reads). C++ owns the schema; Python reads it.

#### `accounts` table
| Column | Type | Constraints |
|--------|------|-------------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT |
| name | TEXT | NOT NULL UNIQUE |
| token | TEXT | NOT NULL |
| query_id | TEXT | NOT NULL |
| enabled | INTEGER | NOT NULL DEFAULT 1 |
| created_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |
| updated_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |

#### `trades` table
| Column | Type | Constraints |
|--------|------|-------------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT |
| account_id | INTEGER | NOT NULL, FK → accounts(id) ON DELETE CASCADE |
| trade_date | TEXT | NOT NULL |
| symbol | TEXT | NOT NULL |
| underlying | TEXT | |
| expiry | TEXT | |
| strike | REAL | |
| right | TEXT | |
| quantity | REAL | NOT NULL |
| trade_price | REAL | |
| proceeds | REAL | |
| commission | REAL | |
| net_cash | REAL | |
| imported_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |

Indexes: `idx_trades_account`, `idx_trades_symbol`, `idx_trades_underlying`, `idx_trades_date`

#### `open_options` table
| Column | Type | Constraints |
|--------|------|-------------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT |
| account_id | INTEGER | NOT NULL, FK → accounts(id) ON DELETE CASCADE |
| symbol | TEXT | NOT NULL |
| underlying | TEXT | NOT NULL |
| expiry | TEXT | NOT NULL |
| strike | REAL | NOT NULL |
| right | TEXT | NOT NULL, CHECK(right IN ('C', 'P')) |
| quantity | REAL | NOT NULL |
| mark_price | REAL | |
| entry_premium | REAL | |
| current_value | REAL | |
| is_manual | INTEGER | NOT NULL DEFAULT 0 |
| notes | TEXT | |
| created_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |
| updated_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |

Indexes: `idx_open_options_account`, `idx_open_options_underlying`, `idx_open_options_expiry`, `idx_open_options_symbol`

#### `detected_strategies` table
| Column | Type | Constraints |
|--------|------|-------------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT |
| account_id | INTEGER | NOT NULL, FK → accounts(id) ON DELETE CASCADE |
| strategy_type | TEXT | NOT NULL |
| underlying | TEXT | NOT NULL |
| expiry | TEXT | NOT NULL |
| leg_count | INTEGER | NOT NULL |
| net_premium | REAL | |
| max_profit | REAL | |
| max_loss | REAL | |
| breakeven_price | REAL | |
| confidence | REAL | |
| detected_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |

Indexes: `idx_strategies_account`, `idx_strategies_underlying`, `idx_strategies_type`

#### `strategy_legs` table
| Column | Type | Constraints |
|--------|------|-------------|
| id | INTEGER | PRIMARY KEY AUTOINCREMENT |
| strategy_id | INTEGER | NOT NULL, FK → detected_strategies(id) ON DELETE CASCADE |
| option_id | INTEGER | NOT NULL, FK → open_options(id) ON DELETE CASCADE |
| leg_role | TEXT | |

Indexes: `idx_strategy_legs_strategy`, `idx_strategy_legs_option`

#### `metadata` table
| Column | Type | Constraints |
|--------|------|-------------|
| key | TEXT | PRIMARY KEY |
| value | TEXT | NOT NULL |
| updated_at | TEXT | NOT NULL DEFAULT CURRENT_TIMESTAMP |

Default row: `('schema_version', '1.0.0')`

### Pydantic Schemas

#### Position
```python
class Position:
    id: int
    account_id: int
    account_name: str      # Joined from accounts table for display
    symbol: str
    underlying: str
    expiry: str            # YYYY-MM-DD
    strike: float
    right: Literal["C", "P"]
    quantity: float
    mark_price: float
    entry_premium: float
    is_manual: bool
```

#### OptionDetails
```python
class OptionDetails:
    underlying: str      # e.g., "AAPL"
    expiry: str          # YYYY-MM-DD
    strike: float
    right: Literal["C", "P"]
    original_symbol: str
```

#### TradeRecord
```python
class TradeRecord:
    account_id: str
    trade_date: str
    symbol: str
    description: str
    underlying_symbol: str
    expiry: str
    strike: float = 0.0
    put_call: str
    quantity: float = 0.0
    trade_price: float = 0.0
    proceeds: float = 0.0
    commission: float = 0.0
    net_cash: float = 0.0
    asset_class: str
    option_details: OptionDetails | None = None
```

#### OpenPositionRecord
```python
class OpenPositionRecord:
    account_id: str
    symbol: str
    description: str
    underlying_symbol: str
    expiry: str
    strike: float = 0.0
    put_call: str
    quantity: float = 0.0
    mark_price: float = 0.0
    position_value: float = 0.0
    open_price: float = 0.0
    cost_basis_price: float = 0.0
    cost_basis_money: float = 0.0
    unrealized_pnl: float = 0.0
    asset_class: str
    report_date: str
    option_details: OptionDetails | None = None
```

#### StrategyType (enum)
```python
class StrategyType(str, Enum):
    NAKED_SHORT_PUT = "naked_short_put"
    NAKED_SHORT_CALL = "naked_short_call"
    BULL_PUT_SPREAD = "bull_put_spread"
    BEAR_CALL_SPREAD = "bear_call_spread"
    IRON_CONDOR = "iron_condor"
    STRADDLE = "straddle"
    STRANGLE = "strangle"
    UNKNOWN = "unknown"
```

#### Strategy
```python
class Strategy:
    type: StrategyType
    underlying: str
    expiry: str
    legs: list[Position]
    account_id: int       # Account this strategy belongs to
    breakeven_price: float = 0.0
    max_profit: float = 0.0
    max_loss: float = 0.0
    risk_level: str = ""
```

#### RiskMetrics
```python
class RiskMetrics:
    breakeven_price: float = 0.0
    breakeven_price_2: float = 0.0  # For iron condors
    max_profit: float = 0.0
    max_loss: float = 0.0           # float('inf') for unlimited
    risk_level: str = ""            # "LOW", "MEDIUM", "HIGH", "DEFINED"
    net_premium: float = 0.0
    days_to_expiry: int = 0
```

#### PortfolioRisk
```python
class PortfolioRisk:
    total_max_profit: float = 0.0
    total_max_loss: float = 0.0
    total_capital_at_risk: float = 0.0
    positions_expiring_soon: int = 0  # < 7 days
    total_strategies: int = 0

class AccountRisk:
    account_id: int
    account_name: str
    position_count: int
    strategy_count: int
    max_profit: float = 0.0
    max_loss: float = 0.0
    capital_at_risk: float = 0.0
    net_premium: float = 0.0
    expiring_soon: int = 0           # < 7 days
```

### Multi-Account Consolidated Views

#### AccountSummary
```python
class AccountSummary:
    account_id: int
    account_name: str
    enabled: bool
    total_positions: int
    total_strategies: int
    total_premium: float
    max_profit: float
    max_loss: float
    capital_at_risk: float
    last_import_at: str | None       # Timestamp of last data import
```

#### ConsolidatedPortfolio
```python
class ConsolidatedPortfolio:
    # Aggregate totals across ALL accounts
    total_accounts: int
    total_positions: int
    total_strategies: int
    total_max_profit: float
    total_max_loss: float
    total_capital_at_risk: float
    total_net_premium: float
    total_expiring_soon: int

    # Per-account breakdowns
    account_risks: list[AccountRisk]

    # Per-underlying breakdowns (cross-account aggregation)
    underlying_exposure: dict[str, UnderlyingExposure]

    # Per-strategy-type breakdowns (cross-account aggregation)
    strategy_type_summary: dict[StrategyType, StrategyTypeSummary]

class UnderlyingExposure:
    underlying: str
    total_positions: int             # Across all accounts
    total_contracts: float           # Sum of |quantity|
    account_ids: list[int]           # Which accounts hold this underlying
    max_profit: float
    max_loss: float
    net_premium: float
    nearest_expiry: str              # Earliest expiry date

class StrategyTypeSummary:
    strategy_type: StrategyType
    count: int                       # Across all accounts
    total_max_profit: float
    total_max_loss: float
    total_net_premium: float
    avg_days_to_expiry: float
```

---

## Business Logic (C++ Engine)

### Option Symbol Parsing

Two IBKR symbol formats must be parsed:

**Format 1:** `"AAPL  250321P00150000"` (spaces + 8-digit strike)
- Regex: `r"^([A-Z]+)\s+(\d{6})([CP])(\d{8})$"`
- Strike = matched digits / 1000 (e.g., `00150000` → `150.0`)

**Format 2:** `"AAPL250321P150"` (compact + decimal strike)
- Regex: `r"^([A-Z]+)\s?(\d{6})([CP])([\d.]+)$"`
- Strike = direct float parse

**Date conversion:** YYMMDD → YYYY-MM-DD using `2000 + year_int` (assumes 20XX).

**Is option check:** `bool(re.search(r"\d{6}[CP]", symbol))`

**Is expired check:** `expiry_date <= today`

### CSV Column Mappings

**Trade rows** (from Flex report):
| CSV Column | Field |
|-----------|-------|
| ClientAccountID | account_id |
| TradeDate | trade_date |
| Symbol | symbol |
| Description | description |
| UnderlyingSymbol | underlying_symbol |
| Expiry | expiry |
| Strike | strike |
| Put/Call | put_call |
| Quantity | quantity |
| TradePrice | trade_price |
| Proceeds | proceeds |
| Commission | commission |
| NetCash | net_cash |
| AssetClass | asset_class |

**Open position rows** (from Flex report):
| CSV Column | Field |
|-----------|-------|
| ClientAccountID | account_id |
| Symbol | symbol |
| Description | description |
| UnderlyingSymbol | underlying_symbol |
| Expiry | expiry |
| Strike | strike |
| Put/Call | put_call |
| Quantity | quantity |
| MarkPrice | mark_price |
| PositionValue | position_value |
| OpenPrice | open_price |
| CostBasisPrice | cost_basis_price |
| CostBasisMoney | cost_basis_money |
| FifoPnlUnrealized | unrealized_pnl |
| AssetClass | asset_class |
| ReportDate | report_date |

**Filtering rules:**
1. Only rows where AssetClass = "OPT"
2. Skip expired options (if filter enabled)
3. Skip rows where |quantity| > 10000
4. Skip rows where LevelOfDetail = "EXECUTION" (trades only)

### Risk Calculation Formulas

**Naked Short Put:**
```
net_premium = |quantity| * entry_premium * 100
breakeven = strike - entry_premium
max_profit = net_premium
max_loss = (strike - entry_premium) * 100 * |quantity|
risk_level = "HIGH"
```

**Naked Short Call:**
```
net_premium = |quantity| * entry_premium * 100
breakeven = strike + entry_premium
max_profit = net_premium
max_loss = UNLIMITED (float('inf'))
risk_level = "HIGH"
```

**Bull Put Spread:**
```
short_premium = |short_qty| * short_entry_premium * 100
long_premium = |long_qty| * long_entry_premium * 100
net_premium = short_premium - long_premium
breakeven = short_strike - (net_premium / 100)
max_profit = net_premium
max_loss = (strike_diff * 100 * |short_qty|) - net_premium
risk_level = "DEFINED"
```

**Bear Call Spread:**
```
short_premium = |short_qty| * short_entry_premium * 100
long_premium = |long_qty| * long_entry_premium * 100
net_premium = short_premium - long_premium
breakeven = short_strike + (net_premium / 100)
max_profit = net_premium
max_loss = (strike_diff * 100 * |short_qty|) - net_premium
risk_level = "DEFINED"
```

**Iron Condor:**
```
# Separate puts and calls from all 4 legs
# Total premium: sum(short legs premium) - sum(long legs premium)
net_premium_per_share = net_premium / 100

# Identify short put (highest strike put with negative qty)
# Identify short call (lowest strike call with negative qty)
breakeven_1 = short_put_strike - net_premium_per_share
breakeven_2 = short_call_strike + net_premium_per_share

max_profit = net_premium
put_spread_width = high_put_strike - low_put_strike
call_spread_width = high_call_strike - low_call_strike
max_loss = max(put_spread_width, call_spread_width) * 100 - net_premium
risk_level = "DEFINED"
```

**Days to Expiry:** Parse expiry (YYYY-MM-DD), compute difference from today.

**Portfolio Risk:** Sum all max_profit, max_loss, capital_at_risk across strategies. Count positions expiring in < 7 days.

---

## IBKR Flex Web Service Integration (C++ Engine)

### Endpoints (API v3)
- **SendRequest:** `GET https://ndcdyn.interactivebrokers.com/AccountManagement/FlexWebService/SendRequest?t={token}&q={query_id}&v=3`
- **GetStatement:** `GET https://ndcdyn.interactivebrokers.com/AccountManagement/FlexWebService/GetStatement?t={token}&q={reference_code}&v=3`

### Flow
1. POST SendRequest with token + query_id (one request per account)
2. Parse XML response → extract ReferenceCode
3. Poll GetStatement every 5 seconds (configurable, max 5 minutes)
4. When status == "Success", save CSV content
5. Parse CSV and import into database, tagged with the account

### Multi-Account Download
- Each account has its own token + query_id in config
- Flex downloads run sequentially per account (IBKR rate limits)
- Each download is tracked as a separate job with its own status
- Failed accounts don't block others — partial success is reported
- Frontend shows per-account download progress

### XML Response Parsing

**SendRequest response:** `<FlexStatementResponse><Status>...</Status><ReferenceCode>...</ReferenceCode><Url>...</Url></FlexStatementResponse>`

**GetStatement response variants:**
- XML with `<FlexStatementResponse>`: Status element (Success/Pending/Warn/Fail)
- XML with `<FlexQueryResponse>`: actual data (treat as Success)
- Raw CSV content: if starts with `"` or contains "Client", treat as Success

### Error Handling
- HTTP 401 → "Token expired or invalid"
- HTTP 403 → "IP address not authorized"
- HTTP 404 → "Query ID not found"
- Network errors → retry with exponential backoff

### Filename Generation
Format: `flex_report_{account_name}_{YYYYMMDD_HHMMSS}.csv`
- Account name sanitized (spaces → underscores)
- Saved to configurable downloads directory

---

## Stock Price Fetching (C++ Engine)

### Yahoo Finance (primary, no API key)
- Endpoint: `https://query1.finance.yahoo.com/v8/finance/chart/{symbol}?interval=1d&range=1d`
- Parse: `response["chart"]["result"][0]["meta"]["regularMarketPrice"]`

### Alpha Vantage (fallback, requires API key)
- Endpoint: `https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol={symbol}&apikey={key}`
- Parse: `response["Global Quote"]["05. price"]`

### Symbol Mapping
```python
SYMBOL_MAPPING = {
    "BRKB": "BRK-B",
    "BRKA": "BRK-A",
    "BRK.B": "BRK-B",
    "BRK.A": "BRK-A",
}
```

---

## CLI JSON Output (C++ Engine)

All CLI commands support `--format json` for machine-readable output. This is the primary integration point for the Python dashboard.

### Global Flags
```
--format text|json    Output format (default: text)
--quiet               Suppress human-readable output (only JSON to stdout)
```

### Command JSON Schemas

#### `download --format json`
```json
{
  "account": "IRA",
  "status": "success",
  "file": "flex_report_IRA_20260423_143052.csv",
  "rows_downloaded": 42
}
```

#### `import --format json`
```json
{
  "account": "IRA",
  "file": "flex_report_IRA_20260423_143052.csv",
  "rows_imported": 38,
  "positions_added": 12,
  "positions_updated": 3,
  "trades_added": 23
}
```

#### `analyze --format json`
```json
{
  "strategies": [
    {
      "type": "bull_put_spread",
      "underlying": "AAPL",
      "expiry": "2026-05-16",
      "account_id": 1,
      "account_name": "IRA",
      "legs": [...],
      "breakeven_price": 185.0,
      "max_profit": 500.0,
      "max_loss": 4500.0,
      "risk_level": "DEFINED",
      "net_premium": 500.0,
      "days_to_expiry": 23
    }
  ],
  "portfolio_risk": {
    "total_max_profit": 5000.0,
    "total_max_loss": 12000.0,
    "total_capital_at_risk": 8000.0,
    "positions_expiring_soon": 2,
    "total_strategies": 5
  },
  "account_risks": [
    {
      "account_id": 1,
      "account_name": "IRA",
      "position_count": 8,
      "strategy_count": 3,
      "max_profit": 3000.0,
      "max_loss": 7000.0,
      "capital_at_risk": 5000.0
    }
  ]
}
```

#### `report --format json`
```json
{
  "report_type": "full",
  "generated_at": "2026-04-23T14:30:52Z",
  "content": "...(text report)...",
  "summary": {
    "total_positions": 12,
    "total_strategies": 5,
    "total_max_profit": 5000.0,
    "total_max_loss": 12000.0
  }
}
```

#### `manual-add --format json`
```json
{
  "position_id": 42,
  "account": "IRA",
  "symbol": "AAPL  260516P00185000",
  "underlying": "AAPL",
  "strike": 185.0,
  "right": "P",
  "quantity": -2.0,
  "premium": 3.50
}
```

#### `sync --format json`
```json
{
  "accounts": [
    {"account": "IRA", "status": "success", "positions_updated": 12},
    {"account": "Margin", "status": "success", "positions_updated": 8},
    {"account": "Spouse", "status": "failed", "error": "Token expired"}
  ],
  "total_positions": 20,
  "total_time_ms": 15432
}
```

#### `accounts --format json`
```json
{
  "accounts": [
    {
      "id": 1,
      "name": "IRA",
      "enabled": true,
      "last_sync": "2026-04-23T14:30:52Z",
      "position_count": 12,
      "strategy_count": 3
    }
  ]
}
```

---

## REST API Endpoints (Python Dashboard)

These endpoints are served by the Python FastAPI application. Read endpoints query SQLite directly. Write endpoints call the C++ CLI via subprocess and return the JSON result.

### Accounts
| Method | Path | Description |
|--------|------|-------------|
| GET | `/health` | Health check with DB connectivity |
| GET | `/api/accounts` | List all accounts with summary stats |
| POST | `/api/accounts` | Create new account |
| GET | `/api/accounts/{id}` | Get account details |
| PUT | `/api/accounts/{id}` | Update account (token, query_id, enabled) |
| DELETE | `/api/accounts/{id}` | Delete account and all its data |

### Positions
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/positions` | List positions. Query params: `account_id` (optional, omit for all accounts), `underlying` (optional) |
| POST | `/api/positions` | Add manual position (requires account_id) |
| DELETE | `/api/positions/{account}` | Clear positions for account |
| GET | `/api/positions/count` | Position count. Query params: `account_id` (optional) |

### Strategies + Risk
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/strategies` | Detect and return strategies with risk metrics. Query params: `account_id` (optional, omit for consolidated cross-account view) |
| GET | `/api/strategies/risk` | Portfolio-level risk summary. Query params: `account_id` (optional, omit for consolidated) |
| GET | `/api/strategies/by-underlying` | Group strategies by underlying across accounts |

### Consolidated Dashboard
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/dashboard/summary` | Consolidated portfolio summary (all accounts + per-account breakdown + per-underlying exposure) |
| GET | `/api/dashboard/accounts` | Per-account risk comparison data |
| GET | `/api/dashboard/underlyings` | Cross-account underlying exposure aggregation |

### Data Import
| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/import/upload` | Upload CSV file. Query param: `account_id` (required). Parse and import into specified account. |
| POST | `/api/import/discover` | Auto-discover and import CSV files from downloads dir (one per account) |
| POST | `/api/flex/download` | Trigger Flex download for one or more accounts. Body: `{"account_ids": [1, 2]}` or omit for all enabled accounts. Returns job IDs per account. |
| GET | `/api/flex/download/{job_id}` | Poll download job status |

### Prices
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/prices/{symbol}` | Fetch single stock price |
| POST | `/api/prices/batch` | Fetch multiple prices |

### Reports
| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/reports/text` | Generate text report. Query param: `account_id` (optional, omit for consolidated) |
| GET | `/api/reports/positions.csv` | Download positions CSV. Query param: `account_id` (optional) |
| GET | `/api/reports/strategies.csv` | Download strategies CSV. Query param: `account_id` (optional) |
| GET | `/api/reports/summary.csv` | Download summary CSV |

---

## Dashboard (Dash)

### Global Controls
- **Account selector** — multi-select dropdown at the top of every tab. Default: "All Accounts" (consolidated view). Selecting specific accounts filters all data on the current tab.
- **Account toggle persists** across tabs within a session.

### Tabs
1. **Overview (Consolidated)** — Landing page. Portfolio summary cards (total profit, total loss, capital at risk, net premium) across selected accounts. Per-account comparison cards showing each account's contribution. Bar chart: max loss by underlying (aggregated). Pie chart: strategy type distribution. Underlying exposure table (cross-account aggregation showing which accounts hold each underlying).
2. **Positions** — DataTable with all open positions across selected accounts. Columns: Account, Underlying, Expiry, Strike, Right, Quantity, Entry Premium, Mark Price, Days to Expiry. Filterable by account (dropdown), underlying (text search), expiry range. Color coding: red=short, yellow=near-expiry, account-specific row highlighting.
3. **Strategies** — Strategy cards grouped by type. Each card: account name, type, underlying, expiry, legs, breakeven, max profit/loss, risk level badge (green=DEFINED, red=HIGH). Cross-account strategies on the same underlying are shown side-by-side for comparison.
4. **Risk** — Per-account risk comparison (grouped bar chart). Portfolio risk trend (if historical data available). Top 10 riskiest positions across all accounts. Underlying concentration heatmap (accounts × underlyings). Expiring positions timeline across accounts.
5. **Expiration** — Calendar-style view grouped by timeframe (This Week <7 days, This Month 7-30 days, Later >30 days). Warning badges for <3 days. Color-coded by account. Aggregated view shows all accounts merged; per-account filter available.
6. **Import** — File upload component (drag & drop) with account selector dropdown. Import progress per account. Recent imports table showing account name, file, timestamp, row count. Manual position entry form (requires account selection).
7. **Settings** — Account list with add/edit/disable/delete. Per-account: Flex token, query ID, enabled toggle, last sync time. "Sync All" button to download from all enabled accounts. Alpha Vantage API key.

### Integration
Dash mounts on the FastAPI app at `/dashboard/`. Single process, single port. API at `/api/...`, dashboard at `/dashboard/...`, Swagger docs at `/docs`.

---

## Configuration

### Environment Variables
```bash
# Database
IBKR_DB_PATH=~/.ibkr-options-analyzer/data.db

# HTTP Client
IBKR_HTTP_TIMEOUT=30
IBKR_HTTP_MAX_RETRIES=5
IBKR_HTTP_RETRY_DELAY_MS=2000

# Flex Polling
IBKR_FLEX_POLL_INTERVAL=5
IBKR_FLEX_MAX_POLL_DURATION=300

# Logging
IBKR_LOG_LEVEL=INFO

# Price Fetching
ALPHAVANTAGE_API_KEY=  # Optional, for fallback price source
```

### Config File (config.json)
```json
{
  "accounts": [
    {
      "name": "IRA",
      "token": "...",
      "query_id": "...",
      "enabled": true
    },
    {
      "name": "Margin",
      "token": "...",
      "query_id": "...",
      "enabled": true
    },
    {
      "name": "Spouse",
      "token": "...",
      "query_id": "...",
      "enabled": false
    }
  ],
  "database": { "path": "~/.ibkr-options-analyzer/data.db" },
  "http": {
    "user_agent": "IBKROptionsAnalyzer/2.0",
    "timeout_seconds": 30,
    "max_retries": 5,
    "retry_delay_ms": 2000
  },
  "flex": {
    "poll_interval_seconds": 5,
    "max_poll_duration_seconds": 300
  },
  "logging": {
    "level": "info",
    "file": "~/.ibkr-options-analyzer/logs/app.log",
    "max_file_size_mb": 10,
    "max_files": 5
  }
}
```

All accounts share a single SQLite database. Each position and strategy row is tagged with `account_id` to isolate per-account data while enabling cross-account consolidation queries.

Priority: Environment variables > .env file > config.json > defaults.

---

## Deployment

### C++ Engine (This Repo)
```bash
# Build release
cmake --preset release
cmake --build build/release

# Install (optional)
cmake --install build/release --prefix /usr/local

# Run
./build/release/ibkr-options-analyzer --help
./build/release/ibkr-options-analyzer sync --all --format json
```

### Python Dashboard (Separate Repo)
```dockerfile
FROM python:3.12-slim AS base
WORKDIR /app
# C++ CLI binary must be available (copy or mount)
COPY --from=ibkr-engine /usr/local/bin/ibkr-options-analyzer /usr/local/bin/
COPY pyproject.toml .
RUN pip install --no-cache-dir .

FROM base AS production
COPY app/ app/
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
```

### Docker Compose (Multi-Container)
```yaml
services:
  engine:
    build:
      context: ./ibkr-options-analyzer
      dockerfile: Dockerfile
    volumes:
      - ibkr-data:/data
    # CLI commands run on-demand, not as a long-running service

  dashboard:
    build:
      context: ./ibkr-dashboard
      dockerfile: Dockerfile
    ports:
      - "8000:8000"
    volumes:
      - ibkr-data:/data  # Shared SQLite
    depends_on:
      - engine
    environment:
      - IBKR_CLI_PATH=/usr/local/bin/ibkr-options-analyzer
      - IBKR_DB_PATH=/data/ibkr.db
```

### Railway / Render
- Dashboard Dockerfile includes C++ CLI binary
- Mount persistent volume at `/data/` for SQLite database
- Set `IBKR_DB_PATH=/data/ibkr.db`
- Set `IBKR_CLI_PATH=/usr/local/bin/ibkr-options-analyzer`
- Start with 1 worker (SQLite concurrent write limitation)

---

## Implementation Phases

### Phase 1: Restructure C++ Project Layout
Separate core engine library from CLI application. Create `src/core/` with models, parsers, analysis, db, services, utils. Move CLI code to `src/cli/`. Update CMakeLists.txt with `ibkr_core` static library target.
**Verify:** `cmake --preset debug && cmake --build build/debug` succeeds. CLI works as before.

### Phase 2: Rebuild Analysis Module
Reimplement deleted `strategy_detector` and `risk_calculator` with multi-account support. All strategy types (naked puts/calls, spreads, iron condors, straddle, strangle) and risk formulas.
**Verify:** All analysis unit tests pass (50+ test cases). `analyze` command works.

### Phase 3: Database Schema for Multi-Account
Add `accounts` table with FKs on all data tables. Add `metadata` table for schema versioning. Implement CRUD operations and consolidated cross-account queries.
**Verify:** `download --account IRA`, `import --account IRA`, `analyze --account IRA` all work with proper account isolation.

### Phase 4: JSON Output Layer
Add `--format json` and `--quiet` flags to all CLI commands. Implement structured JSON output for every command.
**Verify:** `analyze --format json | python3 -m json.tool` produces valid JSON for all commands.

### Phase 5: Service Layer Extraction
Extract orchestration logic from command classes into clean service classes (FlexService, ImportService, PriceService, ReportService, PositionService, StrategyService). Commands become thin wrappers.
**Verify:** All CLI commands still work after refactor. No behavior change.

### Phase 6: CLI Multi-Account Support
Update all commands for multi-account operations. Add `accounts` and `sync` commands. Support `--account NAME` or `--all` flags.
**Verify:** `sync --all --format json` downloads from all enabled accounts sequentially.

### Phase 7: Unit Test Suite
Add Catch2 test framework. Write comprehensive tests for parsers, analysis, database, JSON output. Add CI via GitHub Actions.
**Verify:** `ctest --test-dir build/debug --output-on-failure` — all tests pass.

### Phase 8: Python Dashboard (Separate Repo)
Create `ibkr-dashboard` project with FastAPI + Dash. SQLite reader for queries. CLI subprocess caller for writes. All dashboard tabs.
**Verify:** Dashboard renders at `http://localhost:8000/dashboard/`. All tabs display data from shared DB.

### Phase 9: Docker + Deploy
Containerize both projects. Multi-stage Docker builds. Docker Compose for local dev. Deploy to Railway/Render.
**Verify:** `docker compose up` starts both services. Dashboard accessible and functional.

