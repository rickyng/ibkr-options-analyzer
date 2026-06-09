# Frontend Dashboard Design

**Date:** 2026-04-28
**Status:** Approved

## Goal

Build a Dash (Python) frontend that visualizes the IBKR Options Analyzer API, providing an expiry-focused view of an options selling portfolio with drill-down position details.

## Tech Stack

- **Dash** (Plotly Dash) — Python web framework
- **Dash Bootstrap Components** — layout grid and UI components
- **Plotly** — charts (bar charts for expiry timeline, risk distribution)
- **pandas** — data manipulation
- **httpx** — async API client (already a dependency)

The frontend runs as a separate page within the existing FastAPI app, served alongside the API endpoints.

## Architecture

The Dash app is mounted as a WSGI middleware inside the existing FastAPI app at the root path `/`. API endpoints remain at `/api/*`. Single process, single container.

```
FastAPI app (dashboard/app/main.py)
├── /api/*         → REST API endpoints (existing)
├── /              → Dash frontend (new)
└── /docs          → Swagger UI (existing)
```

## Pages

### Main Dashboard (single page, no routing)

**Data sources (API calls on page load):**
- `GET /api/analyze/open` — all positions grouped by duration bucket
- `GET /api/portfolio/risk` — portfolio summary and per-account totals
- `GET /api/portfolio/exposure` — per-underlying exposure
- `GET /api/portfolio/expiry-calendar` — positions grouped by expiry date

**Layout (top to bottom):**

1. **Summary cards row** — 4 cards: Total Positions, Max Profit, Capital at Risk, Expiring This Week
2. **Account filter tabs** — All Accounts / No1 / No2 / No3. Filters all data on the page.
3. **Two-column section:**
   - Left: **Expiry Timeline** — horizontal bar chart, one bar per expiry date, stacked by risk category (CRITICAL/HIGH/MODERATE/SAFE). Color-coded: red/orange/yellow/green.
   - Right: **Top Exposure Table** — top 10 underlyings by max profit, showing position count and max profit. Below it: **Risk Distribution** — 4 color blocks showing count per risk category.
4. **Position table** — all positions for the selected account, sorted by expiry date then risk. Columns: Underlying, Expiry, Strike, Right, Qty, Premium, Distance %, Risk (badge), Account.

**Interactivity:**
- Click an account tab → filters all components
- Click a position row → expands inline detail panel below the row

**Detail panel (expanded row):**
- 8 metric cards: Strike, Current Price, Entry Premium, Distance to Strike, Breakeven (computed: strike - premium for short puts), Max Profit (premium * 100 * |qty|), Max Loss (strike * 100 - premium * 100, for naked short puts), Days to Expiry
- Visual risk gauge: horizontal bar showing price position relative to strike, color-coded zones (ITM / 1-5% / 5-10% / SAFE >10%)

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `dashboard/app/frontend/__init__.py` | Create | Package init |
| `dashboard/app/frontend/app.py` | Create | Dash app factory, mount as FastAPI middleware |
| `dashboard/app/frontend/layout.py` | Create | All UI components and layout tree |
| `dashboard/app/frontend/callbacks.py` | Create | Dash callbacks: account filter, row expand, data loading |
| `dashboard/app/frontend/api.py` | Create | API client functions (fetch data from localhost) |
| `dashboard/app/main.py` | Modify | Mount Dash middleware at root path |
| `dashboard/pyproject.toml` | Modify | Add dash, dash-bootstrap-components, pandas, plotly dependencies |

## API Client

The Dash app calls the FastAPI endpoints via HTTP on localhost (same process). Functions in `api.py`:

- `fetch_open_positions(account=None)` → `GET /api/analyze/open`
- `fetch_portfolio_risk(account=None)` → `GET /api/portfolio/risk`
- `fetch_exposure(account=None)` → `GET /api/portfolio/exposure`
- `fetch_expiry_calendar(account=None)` → `GET /api/portfolio/expiry-calendar`

All return parsed JSON dicts. Error handling: return empty data structure on failure.

## Styling

- Dark theme (dark navy background `#0f172a`, card backgrounds `#1e293b`)
- Bootstrap dark theme via `dash-bootstrap-components`
- Risk colors: SAFE=#22c55e, MODERATE=#fbbf24, HIGH=#f97316, CRITICAL=#f87171
- Expiry date coloring: expiring today=red, this week=yellow, later=green

## Out of Scope

- Authentication/login
- Multiple pages or URL routing
- Real-time WebSocket updates (refresh page for new data)
- Price charts (candlestick, historical)
- Edit/delete positions from UI
- Mobile-responsive design (desktop-first)
