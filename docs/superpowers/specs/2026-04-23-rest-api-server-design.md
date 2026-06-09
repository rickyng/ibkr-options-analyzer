# Python Dashboard + REST API — Design Spec

**Date:** 2026-04-25
**Updated:** 2026-04-25 (revised from C++ to Python approach)
**Status:** Approved

---

## 1. Overview

Build a Python FastAPI server and Dash dashboard in a `dashboard/` subfolder of the monorepo. The Python layer reads from the shared SQLite database and calls the C++ CLI (`--format json`) for write operations. This avoids growing the C++ project into a long-running server while keeping it focused on parsing, analysis, and computation.

### Scope

- FastAPI REST API in `dashboard/` subfolder
- SQLite read access (shared DB with C++ engine)
- CLI subprocess calls for writes (download, import, manual-add)
- Dash dashboard for visualization (future)
- No authentication initially

### Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Python Layer (dashboard/)                     │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────┐  │
│  │  FastAPI    │───▶│  Services   │───▶│  SQLite (read-only) │  │
│  │  Routes     │    │  (Python)   │    │  + CLI subprocess   │  │
│  └─────────────┘    └─────────────┘    └─────────────────────┘  │
│                           │                                      │
│                           ▼                                      │
│                   ┌─────────────────┐                            │
│                   │  Dash Dashboard │ (future)                   │
│                   └─────────────────┘                            │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼ (SQLite shared)
┌─────────────────────────────────────────────────────────────────┐
│                    C++ Engine (src/)                             │
│  CLI commands: download, import, analyze, report, manual-add    │
│  All write operations own the schema                            │
└─────────────────────────────────────────────────────────────────┘
```

### Data Flow

| Operation | Python Action |
|-----------|---------------|
| Read positions/strategies | Query SQLite directly |
| Read prices | Query SQLite or call external API |
| Download from IBKR | `subprocess.run([cli, "download", ...])` |
| Import CSV | `subprocess.run([cli, "import", ...])` |
| Add manual position | `subprocess.run([cli, "manual-add", ...])` |
| Refresh prices | `subprocess.run([cli, "analyze", "open", "--format", "json"])` |

---

## 2. Project Structure

```
ibkr-options-analyzer/
├── src/                    C++ engine (unchanged)
├── CMakeLists.txt          C++ build (unchanged)
├── config.json.example
├── CLAUDE.md
├── README.md
└── dashboard/              Python project (NEW)
    ├── pyproject.toml      uv/pip config, dependencies
    ├── app/
    │   ├── __init__.py
    │   ├── main.py         FastAPI entry point
    │   ├── config.py       Settings (DB path, CLI path, env vars)
    │   ├── api/            REST API routes
    │   │   ├── __init__.py
    │   │   ├── accounts.py
    │   │   ├── positions.py
    │   │   ├── strategies.py
    │   │   ├── prices.py
    │   │   ├── import.py
    │   │   └── reports.py
    │   ├── services/       Business logic
    │   │   ├── __init__.py
    │   │   ├── db.py       SQLite read helpers
    │   │   ├── cli.py      CLI subprocess wrapper
    │   │   ├── position_service.py
    │   │   ├── strategy_service.py
    │   │   ├── price_service.py
    │   │   └── report_service.py
    │   └── models/         Pydantic models for JSON schemas
    │   │   ├── __init__.py
    │   │   ├── position.py
    │   │   ├── strategy.py
    │   │   ├── account.py
    │   │   └── risk.py
    │   └── dashboard/      Dash UI (future)
    │       └── __init__.py
    └── tests/
        ├── __init__.py
        ├── test_api/
        └── test_services/
```

---

## 3. Dependencies

**pyproject.toml (uv format):**

```toml
[project]
name = "ibkr-dashboard"
version = "0.1.0"
description = "FastAPI + Dash dashboard for IBKR Options Analyzer"
requires-python = ">=3.11"
dependencies = [
    "fastapi>=0.115",
    "uvicorn[standard]>=0.30",
    "pydantic>=2.0",
    "sqlite3-stdlib",  # built-in, but explicit
    "httpx>=0.27",     # for external price APIs
    "dash>=2.17",      # future dashboard
    "pandas>=2.2",     # Dash dependency
]

[project.optional-dependencies]
dev = [
    "pytest>=8.0",
    "pytest-asyncio>=0.23",
    "ruff>=0.4",
    "mypy>=1.10",
]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.ruff]
line-length = 100
target-version = "py311"

[tool.mypy]
python_version = "3.11"
strict = true
```

---

## 4. Configuration

**app/config.py:**

```python
from pathlib import Path
from pydantic_settings import BaseSettings

class Settings(BaseSettings):
    # Paths
    db_path: Path = Path.home() / ".ibkr-options-analyzer" / "data.db"
    cli_path: Path = Path("./build/release/ibkr-options-analyzer")  # relative to repo root

    # Server
    api_host: str = "127.0.0.1"
    api_port: int = 8000

    # External APIs
    yahoo_finance_enabled: bool = True

    class Config:
        env_prefix = "IBKR_"
```

Environment variables override defaults:
- `IBKR_DB_PATH`
- `IBKR_CLI_PATH`
- `IBKR_API_PORT`

---

## 5. REST API Endpoints

Same endpoints as the original C++ spec, implemented in Python.

### Server Configuration

- FastAPI with CORS middleware
- JSON request/response
- Health check: `GET /api/health`
- Port: `IBKR_API_PORT` env var (default 8000)

### Route Table

#### Health
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/health` | `{ "status": "ok" }` |

#### Accounts
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/accounts` | List all accounts (SQLite read) |
| GET | `/api/accounts/{id}` | Get account by ID |

#### Positions
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/positions` | List positions (SQLite read, optional `account_id` filter) |
| GET | `/api/positions/count` | Position count |
| DELETE | `/api/positions/{account_id}` | Clear positions via CLI |

#### Strategies
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/strategies` | Detected strategies (SQLite read) |
| GET | `/api/strategies/risk` | Risk summary (SQLite read + calculation) |
| GET | `/api/strategies/by-underlying` | Grouped by underlying |

#### Prices
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/prices/{symbol}` | Get cached price or fetch from Yahoo |
| POST | `/api/prices/batch` | Batch price fetch |
| POST | `/api/prices/refresh` | Trigger CLI to fetch prices |

#### Import (write operations via CLI)
| Method | Path | Handler |
|--------|------|---------|
| POST | `/api/import/upload` | Upload CSV, call CLI import |
| POST | `/api/import/discover` | Scan downloads dir, call CLI import |
| POST | `/api/import/flex-download` | Trigger CLI download |

#### Reports
| Method | Path | Handler |
|--------|------|---------|
| GET | `/api/reports/positions.csv` | CSV export |
| GET | `/api/reports/strategies.csv` | CSV export |
| GET | `/api/reports/summary` | Summary JSON |

---

## 6. JSON Format (Pydantic Models)

Same format as original spec. Python Pydantic models ensure validation.

### Position Model

```python
from pydantic import BaseModel
from datetime import date
from uuid import UUID

class Position(BaseModel):
    id: UUID
    account_id: UUID
    account_name: str
    symbol: str
    underlying: str
    expiry: date
    strike: float
    right: str  # "PUT" or "CALL"
    quantity: float
    multiplier: int = 100
    mark_price: float | None
    entry_premium: float | None
    current_value: float | None
    dte: int
    is_manual: bool = False
    notes: str = ""
```

### Strategy Model

```python
class StrategyLeg(BaseModel):
    option_id: UUID
    leg_role: str  # "SHORT" or "LONG"
    strike: float
    right: str
    quantity: float

class Strategy(BaseModel):
    id: UUID
    account_id: UUID
    strategy_type: str
    underlying: str
    expiry: date
    legs: list[StrategyLeg]
    net_premium: float
    max_profit: float
    max_loss: float | str  # float or "unlimited"
    breakeven_price: float
    risk_level: str  # "DEFINED" or "UNDEFINED"
    confidence: float
```

### Risk Summary Model

```python
class UnderlyingRisk(BaseModel):
    profit: float
    loss: float

class RiskyPosition(BaseModel):
    underlying: str
    strategy_type: str
    max_loss: float | str
    strike: float

class RiskSummary(BaseModel):
    margin_pct: int
    total_positions: int
    estimated_profit: float
    estimated_loss: float
    by_underlying: dict[str, UnderlyingRisk]
    top_riskiest: list[RiskyPosition]
```

---

## 7. Database Schema

The Python layer reads from the same SQLite schema managed by the C++ engine. No schema changes are made from Python — all migrations happen in C++.

### Existing Tables (read-only from Python)

- `accounts`
- `open_options`
- `detected_strategies`
- `strategy_legs`

### New Tables (managed by C++ engine, future)

When the C++ engine adds these, Python can read them:
- `market_prices` — cached prices
- `earnings_dates` — earnings calendar
- `metadata` — key-value config

---

## 8. CLI Subprocess Integration

Write operations call the C++ CLI with `--format json` and parse stdout.

**services/cli.py:**

```python
import subprocess
import json
from pathlib import Path
from .config import settings

def run_cli(command: str, *args: str) -> dict | None:
    """Run CLI command with --format json, return parsed output."""
    cmd = [str(settings.cli_path), "--format", json, "--quiet", command, *args]
    result = subprocess.run(cmd, capture_output=True, text=True, check=True)
    if result.stdout:
        return json.loads(result.stdout)
    return None

def download_flex(token: str, query_id: str, account: str, force: bool = False) -> None:
    args = ["--token", token, "--query-id", query_id, "--account", account]
    if force:
        args.append("--force")
    run_cli("download", *args)

def import_csv(file_path: Path | None = None) -> None:
    args = []
    if file_path:
        args.extend(["--file", str(file_path)])
    run_cli("import", *args)

def get_positions_json(account: str | None = None, underlying: str | None = None) -> dict:
    args = ["open"]
    if account:
        args.extend(["--account", account])
    if underlying:
        args.extend(["--underlying", underlying])
    return run_cli("analyze", *args) or {"data": []}
```

---

## 9. Error Handling

- CLI subprocess failure: catch `subprocess.CalledProcessError`, return 500 with stderr message
- SQLite read error: catch `sqlite3.Error`, return 500
- Missing prices: return `null` in JSON, don't block display
- Invalid request: Pydantic validation returns 422 automatically
- HTTP status codes: 200 (success), 422 (validation), 500 (internal)

---

## 10. Running the Server

```bash
# From dashboard/ directory
cd dashboard
uv sync                    # Install dependencies
uv run uvicorn app.main:app --reload --port 8000

# Or with environment variables
IBKR_DB_PATH=/custom/path/data.db IBKR_API_PORT=9000 uv run uvicorn app.main:app
```

---

## 11. Future: Dash Dashboard

The `app/dashboard/` module will contain Dash components for:

- Position table with sorting/filtering
- Strategy visualization (legs, risk metrics)
- Expiration timeline
- Risk summary charts
- Price alerts configuration

Dash reads from the same SQLite DB and can call internal Python services (no HTTP overhead).

---

## 12. Testing

```bash
# Run tests
cd dashboard
uv run pytest

# Type check
uv run mypy app/
```

---

## 13. Deferred

- Authentication (Google OAuth + JWT)
- Docker deployment (compose file for C++ build + Python API)
- Sync pipeline background job (Flex download → prices → earnings on schedule)