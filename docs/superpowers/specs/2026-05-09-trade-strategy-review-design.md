---
name: trade-strategy-review
description: Full trade lifecycle tracking and strategy performance analytics for IBKR options
type: project
---

# Trade Strategy Review — Design Specification

**Date:** 2026-05-09
**Status:** Approved for implementation

## Overview

This feature adds comprehensive trade review capabilities to the IBKR Options Analyzer. It tracks full position lifecycles (open → close), computes realized P&L, and provides strategy-level performance analytics. The goal is to answer: "Which strategies actually make money?" and "How much did I actually make?"

## Goals

- Track complete trade lifecycles from opening to closing
- Compute accurate realized P&L per trade and per strategy
- Provide strategy-level performance metrics (win rate, expectancy, profit factor, ROC)
- Enable granular breakdowns by DTE, OTM distance, underlying, and close reason
- Surface loss patterns to identify what's not working
- Build on existing dashboard infrastructure with a new "Trade Review" tab

## Scope

### In Scope

- Option trades only (schema designed to be extensible to other asset types)
- All available history from `trades` table, with date range filters
- Metrics: win/loss rate, expectancy, return on capital, profit factor
- Breakdowns: DTE bucket, OTM %, underlying, day of week, close reason
- Loss clustering and streak detection
- 4 dashboard sub-tabs with charts and tables
- CLI command `analyze-trades` with JSON output
- CSV export from trades table

### Out of Scope

- Stock/ETF trade analysis (future extension)
- Tax reporting (wash sales, 1256 contracts)
- Live P&L streaming
- Backtesting framework
- Greeks tracking over time
- Email/Slack alerts

---

## Data Model

### New Tables

#### `round_trips`

One row per completed open→close cycle.

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER PK | Primary key |
| `account_id` | INTEGER FK | References `accounts.id` |
| `underlying` | TEXT | Underlying symbol (e.g., "SPY") |
| `strike` | REAL | Strike price |
| `right` | TEXT | 'P' or 'C' |
| `expiry` | TEXT | Expiry date YYYY-MM-DD |
| `quantity` | INTEGER | Number of contracts |
| `open_date` | TEXT | Date position opened |
| `close_date` | TEXT | Date position closed |
| `holding_days` | INTEGER | Days held |
| `open_price` | REAL | Average entry price per contract |
| `close_price` | REAL | Average exit price per contract |
| `net_premium` | REAL | Total premium collected/paid |
| `commission` | REAL | Total commission paid |
| `realized_pnl` | REAL | Net profit/loss after commissions |
| `close_reason` | TEXT | Enum: `closed`, `expired`, `assigned`, `rolled`, `exercised` |
| `match_method` | TEXT | Enum: `trade_match`, `snapshot`, `manual` |
| `strategy_type` | TEXT | Detected strategy (nullable) |
| `strategy_group_id` | INTEGER FK | References `strategy_round_trips.id` for multi-leg |
| `created_at` | TEXT | Timestamp |
| `updated_at` | TEXT | Timestamp |

#### `round_trip_legs`

Links round_trips back to source trades.

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER PK | Primary key |
| `round_trip_id` | INTEGER FK | References `round_trips.id` |
| `trade_id` | INTEGER FK | References `trades.id` |
| `role` | TEXT | Enum: `open`, `close`, `adjust`, `partial` |
| `matched_quantity` | INTEGER | Contracts contributed by this trade |

#### `strategy_round_trips`

Groups multi-leg round-trips into composite strategies.

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER PK | Primary key |
| `account_id` | INTEGER FK | References `accounts.id` |
| `strategy_type` | TEXT | e.g., `bull_put_spread`, `iron_condor` |
| `underlying` | TEXT | Underlying symbol |
| `expiry` | TEXT | Expiry date |
| `open_date` | TEXT | First leg open date |
| `close_date` | TEXT | Last leg close date |
| `net_premium` | REAL | Combined premium |
| `realized_pnl` | REAL | Combined P&L |
| `leg_count` | INTEGER | Number of legs |
| `created_at` | TEXT | Timestamp |

#### `position_snapshots`

Daily position state for snapshot-based matching fallback.

| Column | Type | Description |
|--------|------|-------------|
| `id` | INTEGER PK | Primary key |
| `account_id` | INTEGER FK | References `accounts.id` |
| `snapshot_date` | TEXT | Date of snapshot |
| `symbol` | TEXT | Option symbol |
| `underlying` | TEXT | Underlying |
| `expiry` | TEXT | Expiry date |
| `strike` | REAL | Strike price |
| `right` | TEXT | 'P' or 'C' |
| `quantity` | INTEGER | Position quantity |
| `mark_price` | REAL | Mark price |
| `entry_price` | REAL | Entry price |
| `created_at` | TEXT | Timestamp |

**Why:** Snapshot matching catches positions that trade matching misses (data gaps, assignments that don't show as closing trades). Storing daily snapshots enables diff-based detection of closed positions.

**How to apply:** Run snapshot capture during every Flex import. When matching fails, diff snapshots to find disappeared positions and estimate close price from last mark.

---

## Trade Matching Engine

### Algorithm

The matching pipeline runs in 4 steps:

1. **Group trades** by `account_id + underlying + strike + right + expiry`. Each group represents one contract's full lifecycle.

2. **FIFO match** within each group. Sort by `trade_date`. Match buys to sells (or vice versa for short positions). Track running quantity.

3. **Detect close reason**:
   - `close_date == expiry` → `expired`
   - Quantity reversed before expiry → `closed`
   - Same day, same underlying, different strike/expiry opens → `rolled`
   - Stock position appears at expiry for short put ITM → `assigned`

4. **Snapshot fallback** for unmatched positions:
   - Position in snapshot N, gone in snapshot N+1 → closed
   - Use last `mark_price` as estimated close price
   - Set `match_method = 'snapshot'`

### Edge Cases

| Case | Handling |
|------|----------|
| **Partial fills** | Multiple trades contribute to one round-trip. `round_trip_legs` tracks each fill with `matched_quantity`. |
| **Rolls** | Detected by same-day same-underlying position close + open at different strike/expiry. Original position marked `rolled`. |
| **Assignment** | Short put ITM at expiry, stock position appears. Flagged as `assigned`. P&L = premium - (strike - stock_price). |
| **Data gaps** | Trade missing from Flex report? Snapshot diff catches position disappearance. Marked `match_method = 'snapshot'`. |
| **Early exercise** | Rare for short options. Detected by position gone before expiry with no closing trade. Flagged as `exercised`. |

### Implementation

**Service:** `TradeMatcher` in `src/core/services/trade_matcher.hpp`

**Responsibilities:**
- Load trades from DB grouped by contract
- FIFO matching algorithm
- Close reason inference
- Snapshot diffing fallback
- Write `round_trips` and `round_trip_legs` tables

**Dependencies:**
- `PositionService` (for snapshot access)
- `Database` (read/write)

---

## Analytics Computation

### Per-Trade Metrics

Computed for each `round_trip`:

| Metric | Formula |
|--------|---------|
| `realized_pnl` | For short: `net_premium - commission`. For long: `close_price - open_price - commission` |
| `holding_days` | `close_date - open_date` |
| `return_on_capital` | `realized_pnl / margin_required`. Margin = strike × quantity × multiplier for naked puts |
| `annualized_return` | `ROC × (365 / holding_days)` |

### Aggregate Metrics

Computed per strategy type, DTE bucket, underlying:

| Metric | Formula |
|--------|---------|
| `win_rate` | `count(winners) / count(trades)` |
| `avg_winner` | `mean(realized_pnl where pnl > 0)` |
| `avg_loser` | `mean(realized_pnl where pnl < 0)` |
| `profit_factor` | `sum(winners) / abs(sum(losers))` |
| `expectancy` | `(win_rate × avg_winner) - (loss_rate × avg_loser)` |
| `total_pnl` | `sum(realized_pnl)` |

### Breakdown Buckets

| Dimension | Buckets |
|-----------|---------|
| DTE at entry | 0-7, 8-14, 15-30, 31-45, 46-60, 60+ |
| OTM % | 0-2%, 2-5%, 5-10%, 10%+ |
| Underlying | Per ticker |
| Day of week | Mon, Tue, Wed, Thu, Fri |
| Close reason | closed, expired, assigned, rolled, exercised |

### Loss Pattern Detection

- **Clustering:** Group losing trades by shared attributes (underlying, DTE bucket, strategy type)
- **Streak tracking:** Max consecutive losses, recovery time
- **Attribution:** % of total losses by close reason

**Why:** Loss patterns reveal systemic issues (e.g., "TSLA naked puts consistently lose", "short DTE trades have low win rate").

**How to apply:** Highlight clusters in dashboard. Suggest parameter adjustments based on patterns.

---

## Services

### TradeMatcher

**Location:** `src/core/services/trade_matcher.hpp/cpp`

**Methods:**
- `match_all_trades(account_id?)` — Run full matching pipeline
- `match_contract_trades(underlying, strike, right, expiry)` — Match specific contract
- `rebuild_round_trips()` — Clear and re-match all (for data corrections)

**Output:** Writes to `round_trips`, `round_trip_legs`

### TradeAnalyticsService

**Location:** `src/core/services/trade_analytics_service.hpp/cpp`

**Methods:**
- `get_overview_metrics(filters)` — Total trades, win rate, P&L, profit factor, ROC
- `get_round_trips(filters)` — List trades with P&L
- `get_strategy_performance(filters)` — Per-strategy-type metrics
- `get_dte_breakdown(filters)` — P&L by DTE bucket
- `get_underlying_breakdown(filters)` — P&L and win rate by ticker
- `get_loss_clusters(filters)` — Grouped losing trades
- `get_streak_info(filters)` — Max loss streak, recovery

**Filters:** `account_id`, `date_from`, `date_to`, `strategy_type`, `underlying`

### SnapshotService

**Location:** `src/core/services/snapshot_service.hpp/cpp`

**Methods:**
- `capture_snapshot(account_id)` — Store current positions from `open_options`
- `get_snapshot(account_id, date)` — Retrieve historical snapshot
- `diff_snapshots(account_id, date1, date2)` — Find disappeared positions

**Trigger:** Called during Flex import after positions are loaded.

---

## CLI Command

### `analyze-trades`

**Usage:**
```
ibkr-options-analyzer analyze-trades [options]
```

**Options:**
| Flag | Description |
|------|-------------|
| `--rebuild` | Clear `round_trips` table and re-match all trades |
| `--date-from YYYY-MM-DD` | Filter trades from this date |
| `--date-to YYYY-MM-DD` | Filter trades to this date |
| `--strategy <type>` | Filter by strategy type |
| `--underlying <symbol>` | Filter by underlying |
| `--format json|text` | Output format |

**Output:** JSON structure matching analytics service methods.

---

## Dashboard

### New Tab: Trade Review

Added to existing dashboard navigation alongside Positions, Portfolio, Screener.

#### Sub-tab 1: Overview

**Components:**
- KPI cards: Total Trades, Win Rate, Net P&L, Profit Factor, Avg ROC (annualized)
- Cumulative P&L chart: Line chart, x=date, y=running total P&L
- Filters: Account, Date Range, Strategy Type, Underlying

#### Sub-tab 2: Trades

**Components:**
- Sortable table: Underlying, Strategy, Strike/Right, Opened, Closed, Days, Premium, P&L, ROC%, Close Reason
- Row click: Expand to show fill-level detail (which trades contributed)
- Export CSV button

#### Sub-tab 3: Strategy Analysis

**Components:**
- Performance table: Strategy type, # Trades, Win%, P&L, Profit Factor
- DTE breakdown bar chart: Stacked win/loss per DTE bucket, labeled with win rate

#### Sub-tab 4: Loss Review

**Components:**
- Loss clusters: Highlighted groups (by underlying, DTE bucket) with P&L totals
- Loss streak: Max consecutive losses, recovery date
- Loss distribution histogram: Size distribution with median marker
- Close reason breakdown: % losses by assigned/rolled/closed early

### API Endpoints

**New routes under `/api/trades/`:**

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/trades/overview` | GET | KPI metrics |
| `/api/trades/list` | GET | Round-trip table data |
| `/api/trades/strategy-performance` | GET | Per-strategy metrics |
| `/api/trades/dte-breakdown` | GET | P&L by DTE bucket |
| `/api/trades/underlying-breakdown` | GET | P&L by underlying |
| `/api/trades/loss-clusters` | GET | Grouped losses |
| `/api/trades/streaks` | GET | Loss streak info |
| `/api/trades/export` | GET | CSV download |

All endpoints accept query params: `account_id`, `date_from`, `date_to`, `strategy`, `underlying`

---

## Data Flow

```
IBKR Flex Import
    ↓
trades table (existing)
open_options table (existing)
    ↓
SnapshotService.capture_snapshot()
    ↓
position_snapshots table (new)
    ↓
TradeMatcher.match_all_trades()
    ↓
round_trips table (new)
round_trip_legs table (new)
    ↓
StrategyDetector.detect() (existing, reused)
    ↓
strategy_round_trips table (new)
    ↓
TradeAnalyticsService
    ↓
Dashboard API → Frontend
```

---

## Implementation Notes

### Schema Migration

- Add 4 new tables to `schema.hpp`
- Increment schema version in `metadata` table
- Migration runs automatically on next app start

### Reuse Existing Components

- `StrategyDetector` — same logic, applied to round_trips instead of open_options
- `RiskCalculator` — margin calculation for ROC
- `PriceCacheService` / `PriceService` — historical underlying prices needed for OTM % calculation at entry time. If no cached price exists for the open_date, use the strike price as a fallback (mark as approximate)
- Dashboard dark theme and layout patterns — extend existing CSS/components

### Performance

- Index `round_trips` on `account_id`, `underlying`, `close_date`, `strategy_type`
- Analytics queries aggregate from `round_trips` (pre-computed), not raw `trades`
- `--rebuild` flag for one-time full recompute; incremental updates thereafter

---

## Testing

### Manual Testing

```bash
# Run trade matching
./build/release/ibkr-options-analyzer analyze-trades --rebuild --format json

# Check output
./build/release/ibkr-options-analyzer analyze-trades --date-from 2026-01-01 --format text

# Dashboard
cd dashboard && uvicorn app.main:app --reload --port 8001
# Open http://localhost:8001 → Trade Review tab
```

### Edge Case Tests

- Position with partial fills (sell 5, buy 2, buy 3)
- Roll (close 560P, open 555P same day)
- Assignment (short put ITM, stock position appears)
- Data gap (missing closing trade, caught by snapshot)