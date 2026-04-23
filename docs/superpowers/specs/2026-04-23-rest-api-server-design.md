# REST API Server — Design Spec

**Date:** 2026-04-23
**Sub-project:** 1 of 3 (REST API + HTTP Server)
**Status:** Approved

---

## 1. Overview

Add a REST API server to the existing IBKR Options Analyzer C++ CLI tool. The server exposes existing business logic (positions, strategies, risk analysis, reports) as JSON endpoints using cpp-httplib. This is the backend foundation for the web dashboard (sub-project 2).

### Scope

- REST API server using cpp-httplib (existing dependency)
- Service layer extraction from CLI commands
- Additive database schema changes (3 new tables, new columns)
- No authentication, no static file serving, no sync pipeline
- Single-user model

### Deferred to later sub-projects

- Authentication (Google OAuth + JWT) → sub-project 3
- Web dashboard (HTML/CSS/JS) → sub-project 2
- Sync pipeline (Flex download → prices → earnings) → sub-project 2 or separate
- Docker deployment → sub-project 3

---

## 2. Architecture

**Approach: Service Layer Extraction**

Extract business logic from CLI command classes into a new `services/` layer. Both CLI commands and REST API controllers call the same services. The existing `analysis/` module is absorbed into services.

```
Current:  CLI → AnalyzeCommand → DB + analysis → text output
New:      CLI → AnalyzeCommand → PositionService → DB + analysis → struct → text output
          API → PositionController → PositionService → DB + analysis → struct → JSON
```

---

## 3. Module Structure

```
src/
├── main.cpp                        # CLI entry (updated to add `serve` command)
├── commands/                       # CLI commands (refactored to call services)
│   ├── analyze_command.cpp/hpp
│   ├── import_command.cpp/hpp
│   └── ...existing commands
├── services/                       # NEW — business logic, transport-agnostic
│   ├── position_service.cpp/hpp    # CRUD for positions
│   ├── strategy_service.cpp/hpp    # Strategy detection (absorbs strategy_detector)
│   ├── risk_service.cpp/hpp        # Risk calculations (absorbs risk_calculator)
│   ├── price_service.cpp/hpp       # Price fetching + caching
│   ├── account_service.cpp/hpp     # Account CRUD
│   └── report_service.cpp/hpp      # Report generation
├── api/                            # NEW — HTTP layer
│   ├── server.cpp/hpp              # cpp-httplib server setup, CORS, routing
│   ├── router.cpp/hpp              # Route registration
│   └── controllers/                # Request handlers
│       ├── position_controller.cpp/hpp
│       ├── strategy_controller.cpp/hpp
│       ├── price_controller.cpp/hpp
│       ├── account_controller.cpp/hpp
│       ├── import_controller.cpp/hpp
│       └── report_controller.cpp/hpp
├── db/                             # Existing — updated with new tables
├── parser/                         # Existing — unchanged
├── flex/                           # Existing — unchanged
└── utils/                          # Existing — unchanged
```

**Deleted:** `src/analysis/` — contents split into `strategy_service` (detection) and `risk_service` (calculations).

---

## 4. Service Layer Interfaces

All services return `Result<T>` and take `Database&` via constructor injection.

### PositionService

```
get_positions(account_id?) → Result<vector<OpenOption>>
get_position_count(account_id?) → Result<int>
create_position(OpenOption) → Result<OpenOption>
delete_positions_by_account(account_id) → Result<void>
get_unique_underlyings() → Result<vector<string>>
```

### StrategyService (absorbs strategy_detector)

```
detect_strategies(account_id?) → Result<vector<DetectedStrategy>>
get_strategies_by_underlying(account_id?) → Result<map<string, vector<DetectedStrategy>>>
get_risk_summary(margin_pct) → Result<RiskSummary>
```

### RiskService (absorbs risk_calculator)

```
calculate_strategy_risk(strategy, positions) → StrategyRisk
calculate_moneyness(position, stock_price) → Moneyness
bucket_by_expiration(positions) → map<ExpirationBucket, vector<Position>>
```

### PriceService

```
get_price(symbol) → Result<double>
get_prices_batch(symbols) → Result<map<string, double>>
refresh_all_prices() → Result<void>
```

### AccountService

```
get_accounts() → Result<vector<Account>>
get_account(id) → Result<Account>
create_account(Account) → Result<Account>
update_account(id, Account) → Result<Account>
delete_account(id) → Result<void>
```

### ReportService

```
generate_text_report(account_id?) → Result<string>
generate_positions_csv(account_id?) → Result<string>
generate_strategies_csv(account_id?) → Result<string>
generate_summary_csv(account_id?) → Result<string>
```

---

## 5. REST API Endpoints

### Server Configuration

- Single `httplib::Server` instance
- CORS headers on all responses
- JSON request/response via nlohmann/json
- Global error handler: all routes wrapped in try/catch → `{ "error": "..." }`
- Health check: `GET /api/health`
- Port: `--port` CLI flag or `IBKR_PORT` env var (default 8001 dev, 8000 prod)
- New `serve` CLI command starts the server

### Controller Pattern

Each controller function follows this flow:
1. Parse params from request (path params, query params, JSON body)
2. Call service method
3. On error → return 4xx/5xx with `{ "error": "..." }`
4. On success → return 200 with `{ "data": <payload> }`

### Route Table

#### Health
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/health` | `{ "status": "ok" }` |

#### Accounts
| Method | Path | Controller | Service Call |
|--------|------|-----------|-------------|
| GET | `/api/accounts` | AccountController::list | `get_accounts()` |
| POST | `/api/accounts` | AccountController::create | `create_account(...)` |
| GET | `/api/accounts/{id}` | AccountController::get | `get_account(id)` |
| PUT | `/api/accounts/{id}` | AccountController::update | `update_account(id, ...)` |
| DELETE | `/api/accounts/{id}` | AccountController::remove | `delete_account(id)` |

#### Positions
| Method | Path | Controller | Service Call |
|--------|------|-----------|-------------|
| GET | `/api/positions` | PositionController::list | `get_positions(account_id?)` |
| GET | `/api/positions/count` | PositionController::count | `get_position_count(account_id?)` |
| POST | `/api/positions` | PositionController::create | `create_position(...)` |
| DELETE | `/api/positions/{account_id}` | PositionController::clear | `delete_positions_by_account(id)` |

#### Strategies
| Method | Path | Controller | Service Call |
|--------|------|-----------|-------------|
| GET | `/api/strategies` | StrategyController::list | `detect_strategies(account_id?)` |
| GET | `/api/strategies/risk` | StrategyController::risk | `get_risk_summary(margin)` |
| GET | `/api/strategies/by-underlying` | StrategyController::by_underlying | `get_strategies_by_underlying(account_id?)` |

#### Prices
| Method | Path | Controller | Service Call |
|--------|------|-----------|-------------|
| GET | `/api/prices/{symbol}` | PriceController::get | `get_price(symbol)` |
| POST | `/api/prices/batch` | PriceController::batch | `get_prices_batch(symbols)` |
| POST | `/api/prices/refresh` | PriceController::refresh | `refresh_all_prices()` |

#### Import
| Method | Path | Controller | Service Call |
|--------|------|-----------|-------------|
| POST | `/api/import/upload` | ImportController::upload | parse + create positions |
| POST | `/api/import/discover` | ImportController::discover | scan downloads dir |

#### Reports
| Method | Path | Controller | Service Call |
|--------|------|-----------|-------------|
| GET | `/api/reports/text` | ReportController::text | `generate_text_report(...)` |
| GET | `/api/reports/positions.csv` | ReportController::positions_csv | `generate_positions_csv(...)` |
| GET | `/api/reports/strategies.csv` | ReportController::strategies_csv | `generate_strategies_csv(...)` |
| GET | `/api/reports/summary.csv` | ReportController::summary_csv | `generate_summary_csv(...)` |

#### Deferred (sync pipeline, auth)
- `POST /api/flex/download`, `GET /api/flex/{job_id}`
- `POST /api/sync/all`, `GET /api/sync/{job_id}`, `GET /api/sync/last-sync`, etc.
- `GET /api/me`

---

## 6. Database Schema Changes

Additive only — no data migration, existing data preserved.

### New Tables

```sql
CREATE TABLE market_prices (
    symbol TEXT PRIMARY KEY,
    price REAL NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE earnings_dates (
    symbol TEXT PRIMARY KEY,
    earnings_date TEXT NOT NULL,
    updated_at TEXT NOT NULL
);

CREATE TABLE metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_at TEXT NOT NULL
);
```

### New Columns

**open_options** table:
| Column | Type | Default |
|--------|------|---------|
| `is_manual` | INTEGER | 0 |
| `notes` | TEXT | '' |

**accounts** table:
| Column | Type | Default |
|--------|------|---------|
| `enabled` | INTEGER | 1 |

### Schema Versioning

Bump `schema_version` in metadata. `Database::initialize()` runs ALTER TABLE statements idempotently (check column existence before adding).

Note: The current schema may already have a `metadata` table for version tracking. If so, extend it rather than recreating. The new `market_prices` and `earnings_dates` tables are definitely new additions.

---

## 7. JSON Format

### Response Envelope

```json
{ "data": <payload> }       // Success
{ "error": "message" }      // Error
```

### Position JSON

```json
{
  "data": [
    {
      "id": "uuid",
      "account_id": "uuid",
      "account_name": "Ricky-IRA",
      "symbol": "SPY   250117P00450000",
      "underlying": "SPY",
      "expiry": "2025-01-17",
      "strike": 450.0,
      "right": "PUT",
      "quantity": -5.0,
      "multiplier": 100,
      "mark_price": 1.23,
      "entry_premium": 2.50,
      "current_value": -615.0,
      "dte": 42,
      "is_manual": false,
      "notes": ""
    }
  ]
}
```

### Strategy JSON

```json
{
  "data": [
    {
      "id": "uuid",
      "account_id": "uuid",
      "strategy_type": "BULL_PUT_SPREAD",
      "underlying": "SPY",
      "expiry": "2025-01-17",
      "legs": [
        { "option_id": "uuid", "leg_role": "SHORT", "strike": 450.0, "right": "PUT", "quantity": -3 },
        { "option_id": "uuid", "leg_role": "LONG", "strike": 440.0, "right": "PUT", "quantity": 3 }
      ],
      "net_premium": 1500.0,
      "max_profit": 1500.0,
      "max_loss": 8500.0,
      "breakeven_price": 448.50,
      "risk_level": "DEFINED",
      "confidence": 0.95
    }
  ]
}
```

### Risk Summary JSON

```json
{
  "data": {
    "margin_pct": 10,
    "total_positions": 25,
    "estimated_profit": 12500.0,
    "estimated_loss": -8000.0,
    "by_underlying": {
      "SPY": { "profit": 5000.0, "loss": -3000.0 },
      "AAPL": { "profit": 7500.0, "loss": -5000.0 }
    },
    "top_riskiest": [
      { "underlying": "AAPL", "strategy_type": "NAKED_SHORT_PUT", "max_loss": "unlimited", "strike": 185.0 }
    ]
  }
}
```

### Conventions

- `max_loss: "unlimited"` — string sentinel for naked positions, otherwise float
- CSV endpoints return `Content-Type: text/csv` with raw CSV body (no JSON envelope)
- `?account_id=uuid` — optional filter (omit for consolidated)
- `?margin=10` — margin percentage (default 0)

---

## 8. Error Handling

- External API failures: retry with exponential backoff (max 3 retries)
- Missing prices: return `null` in JSON, don't block position display
- Invalid CSV: row-level errors, skip bad rows, import valid ones
- Database errors: transaction rollback, return error JSON with user-friendly message
- JSON infinity: `float('inf')` max_loss serialized as `"unlimited"` string
- HTTP status codes: 200 (success), 400 (bad request), 404 (not found), 500 (internal error)

---

## 9. CLI Integration

New `serve` command added via CLI11:

```bash
# Start API server on default port
./ibkr-options-analyzer serve

# Custom port
./ibkr-options-analyzer serve --port 9000

# With config
./ibkr-options-analyzer serve --config config.json --log-level info
```

Existing CLI commands (`download`, `import`, `analyze`, `report`, `manual-add`) continue to work unchanged after refactoring to call services.
