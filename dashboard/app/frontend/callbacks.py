"""Dash callbacks for IBKR Options Dashboard."""

from collections import defaultdict, Counter
from datetime import datetime, date

import plotly.graph_objects as go
import httpx
from dash import Dash, Output, Input, State, html, no_update
import dash_bootstrap_components as dbc

from ..services.cli import run_cli, download_flex, import_csv, CliError
from ..services.db import query, execute
from .layout import RISK_COLORS, GRADE_COLORS, BG_CARD, TEXT_MUTED

# FX rates to convert to USD (same as backend defaults)
FX_RATES = {
    "USD": 1.0,
    "EUR": 1.08,
    "GBP": 1.25,
    "JPY": 0.0067,
    "HKD": 0.13,
    "CAD": 0.74,
    "AUD": 0.65,
    "CHF": 1.12,
    "SGD": 0.75,
}

# Contract multiplier by market (JP options have multiplier of 1)
CONTRACT_MULTIPLIER = {"US": 100, "HK": 100, "JP": 1}

YAHOO_CHART_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"


def _fetch_price_yahoo(symbol: str) -> float | None:
    """Fetch current price from Yahoo Finance (synchronous)."""
    try:
        with httpx.Client(timeout=5) as client:
            resp = client.get(
                YAHOO_CHART_URL.format(symbol=symbol),
                params={"range": "1d", "interval": "1d"},
                headers={"User-Agent": "Mozilla/5.0"},
            )
            if resp.status_code == 200:
                meta = resp.json()["chart"]["result"][0]["meta"]
                return meta.get("regularMarketPrice")
    except Exception:
        pass
    return None


def _derive_currency_from_symbol(underlying: str) -> str:
    """Derive currency from underlying symbol format.

    - JP (.T suffix): JPY
    - HK (all digits): HKD
    - US (alphabetic): USD
    """
    if not underlying:
        return "USD"
    if underlying.endswith(".T"):
        return "JPY"
    symbol_core = underlying.replace(".HK", "").replace(".T", "")
    if symbol_core.isdigit():
        return "HKD"
    return "USD"


def _convert_to_usd(amount: float, currency: str) -> float:
    """Convert amount from given currency to USD using FX rates."""
    if not amount or not currency:
        return amount
    rate = FX_RATES.get(currency, 1.0)
    return amount * rate


def _fetch_portfolio_risk(account: str | None = None) -> dict:
    """Fetch portfolio risk summary by calling CLI service directly."""
    args = ["strategy"]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)
    return {
        "portfolio": data.get("portfolio", {}),
        "accounts": data.get("accounts", []),
    }


def _fetch_exposure(account: str | None = None) -> list:
    """Fetch underlying exposure by calling CLI service directly."""
    args = ["strategy"]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)
    return data.get("underlying_exposure", [])


def _fetch_expiry_calendar(account: str | None = None) -> dict:
    """Fetch positions grouped by expiry by calling CLI service directly."""
    args = ["open"]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)

    positions_by_expiry: dict[str, list] = defaultdict(list)
    buckets = data.get("positions", {})
    for _bucket_name, positions in buckets.items():
        for pos in positions:
            expiry = pos.get("expiry", "")
            positions_by_expiry[expiry].append(pos)

    return dict(positions_by_expiry)


def _fetch_accounts() -> list:
    """Fetch account list by querying database directly."""
    return query("SELECT id, name, token, query_id, enabled FROM accounts ORDER BY name")


def _clear_account_downloads(account_name: str) -> None:
    """Remove previous CSV downloads for an account (YTD data supersedes old files)."""
    import os
    from pathlib import Path

    download_dir = Path.home() / ".ibkr-options-analyzer" / "downloads"
    if not download_dir.exists():
        return

    sanitized_name = account_name.replace(" ", "_")
    pattern = f"flex_report_{sanitized_name}_"

    for csv_file in download_dir.glob("*.csv"):
        if csv_file.name.startswith(pattern):
            os.remove(csv_file)


def _parse_expiry(expiry_str: str) -> date | None:
    """Parse expiry string in YYYYMMDD or YYYY-MM-DD format."""
    if not expiry_str:
        return None
    for fmt in ("%Y%m%d", "%Y-%m-%d"):
        try:
            return datetime.strptime(expiry_str, fmt).date()
        except ValueError:
            continue
    return None


def _derive_market_from_symbol(underlying: str) -> str:
    """Derive market from underlying symbol format.

    - JP: symbols ending with .T (e.g., 1321.T, 7203.T)
    - HK: numeric symbols (all digits, e.g., 00733, 388, 700)
    - US: alphabetic symbols (e.g., AAPL, SPY, TSLA)
    """
    if not underlying:
        return "US"
    if underlying.endswith(".T"):
        return "JP"
    # HK symbols are numeric (may have leading zeros, may also have .HK suffix)
    symbol_core = underlying.replace(".HK", "").replace(".T", "")
    if symbol_core.isdigit():
        return "HK"
    return "US"


def _rt_pnl(rt: dict) -> float:
    """Get the effective P&L for a round-trip, preferring net_pnl over realized_pnl."""
    if "net_pnl" in rt:
        return rt["net_pnl"]
    return rt.get("realized_pnl", 0)


def _compute_dte_at_entry(rt: dict) -> int | None:
    """Compute DTE at entry from expiry and open_date fields."""
    expiry = rt.get("expiry", "")
    open_date = rt.get("open_date", "")
    if not expiry or not open_date:
        return None
    try:
        if len(open_date) == 8:
            od = datetime.strptime(open_date, "%Y%m%d").date()
        elif "-" in open_date:
            od = datetime.strptime(open_date, "%Y-%m-%d").date()
        else:
            return None
        ed = datetime.strptime(expiry, "%Y-%m-%d").date()
        return (ed - od).days
    except ValueError:
        return None


def _compute_optimal_premium(trips: list[dict], target_win_rate: float = 0.80) -> dict:
    """Compute optimal premium analysis for a set of round trips.

    Uses premium as % of strike as a proxy for moneyness (how close to ATM).
    Higher premium % = closer to ATM = higher assignment risk.
    Finds the max premium (as % of strike) that still meets the target win rate.
    """
    # Group by underlying + right
    groups = defaultdict(list)
    for rt in trips:
        key = (rt.get("underlying", ""), rt.get("right", "P"))
        strike = rt.get("strike", 0)
        open_price = rt.get("open_price", 0)
        if strike <= 0 or open_price <= 0:
            continue

        premium_pct = (open_price / strike) * 100
        groups[key].append({
            **rt,
            "_premium_pct": premium_pct,
        })

    per_underlying = []
    all_trades = []

    for (underlying, right), group_trips in groups.items():
        # Sort by premium_pct ascending (lowest premium = most OTM)
        sorted_trades = sorted(group_trips, key=lambda t: t["_premium_pct"])
        assigned_count = sum(1 for t in sorted_trades if t.get("close_reason") == "assigned")
        total = len(sorted_trades)
        win_rate = (total - assigned_count) / total if total > 0 else 0

        win_premiums = [t["_premium_pct"] for t in sorted_trades if t.get("close_reason") != "assigned"]
        assigned_premiums = [t["_premium_pct"] for t in sorted_trades if t.get("close_reason") == "assigned"]
        win_dollar = [t.get("open_price", 0) for t in sorted_trades if t.get("close_reason") != "assigned"]
        assigned_dollar = [t.get("open_price", 0) for t in sorted_trades if t.get("close_reason") == "assigned"]

        avg_win_prem = sum(win_dollar) / len(win_dollar) if win_dollar else 0
        avg_assigned_prem = sum(assigned_dollar) / len(assigned_dollar) if assigned_dollar else 0
        avg_win_pct = sum(win_premiums) / len(win_premiums) if win_premiums else 0
        avg_assigned_pct = sum(assigned_premiums) / len(assigned_premiums) if assigned_premiums else 0

        # Walk from lowest premium upward, find max premium % that meets target win rate
        max_safe_pct = None
        cum_assigned = 0
        for i, t in enumerate(sorted_trades):
            if t.get("close_reason") == "assigned":
                cum_assigned += 1
            cum_win_rate = (i + 1 - cum_assigned) / (i + 1)
            if cum_win_rate >= target_win_rate:
                max_safe_pct = t["_premium_pct"]

        per_underlying.append({
            "underlying": underlying,
            "right": right,
            "trade_count": total,
            "assigned_count": assigned_count,
            "win_rate": win_rate,
            "avg_win_premium": avg_win_prem,
            "avg_assigned_premium": avg_assigned_prem if assigned_dollar else None,
            "avg_win_pct": avg_win_pct,
            "avg_assigned_pct": avg_assigned_pct if assigned_premiums else None,
            "max_safe_premium_pct": max_safe_pct,
            "low_confidence": total < 5,
        })

        for t in sorted_trades:
            all_trades.append({
                "underlying": underlying,
                "premium_pct": t["_premium_pct"],
                "open_price": t.get("open_price", 0),
                "strike": t.get("strike", 0),
                "is_assigned": t.get("close_reason") == "assigned",
            })

    # Aggregate: find max safe premium % across all trades
    all_sorted = sorted(all_trades, key=lambda t: t["premium_pct"])
    recommended_pct = None
    cum_assigned = 0
    for i, t in enumerate(all_sorted):
        if t["is_assigned"]:
            cum_assigned += 1
        cum_win_rate = (i + 1 - cum_assigned) / (i + 1)
        if cum_win_rate >= target_win_rate:
            recommended_pct = t["premium_pct"]

    total_trades = len(all_trades)
    total_assigned = sum(1 for t in all_trades if t["is_assigned"])
    overall_win_rate = (total_trades - total_assigned) / total_trades if total_trades > 0 else 0

    return {
        "per_underlying": per_underlying,
        "aggregate_trades": all_trades,
        "overall_win_rate": overall_win_rate,
        "total_trades": total_trades,
        "recommended_premium_pct": recommended_pct,
        "target_win_rate": target_win_rate,
    }


def _flatten_calendar(calendar: dict) -> list:
    """Flatten calendar data into a flat position list with enriched fields."""
    positions = []
    for expiry, items in calendar.items():
        for pos in items:
            underlying = pos.get("underlying", "")
            currency = _derive_currency_from_symbol(underlying)
            market = _derive_market_from_symbol(underlying)
            positions.append({
                "underlying": underlying,
                "expiry": expiry,
                "strike": pos.get("strike", 0),
                "right": pos.get("right", ""),
                "quantity": pos.get("quantity", 0),
                "premium": pos.get("entry_premium", 0),
                "distance_pct": pos.get("distance_from_strike_pct", 0),
                "risk_category": pos.get("risk_category", "SAFE"),
                "current_price": pos.get("current_price"),
                "account_name": pos.get("account", ""),
                "currency": currency,
                "market": market,
                "multiplier": pos.get("multiplier", 100),
            })
    return positions


def register_callbacks(app: Dash) -> None:
    """Register all callbacks with the Dash app."""

    # Callback: Load all data on page load (triggered by one-shot interval)
    @app.callback(
        [
            Output("portfolio-data", "data"),
            Output("exposure-data", "data"),
            Output("calendar-data", "data"),
            Output("positions-data", "data"),
            Output("accounts-data", "data"),
        ],
        Input("load-trigger", "n_intervals"),
    )
    def load_initial_data(_):
        """Fetch all data on initial page load."""
        portfolio = _fetch_portfolio_risk()
        exposure = _fetch_exposure()
        calendar = _fetch_expiry_calendar()
        positions = _flatten_calendar(calendar)
        accounts = _fetch_accounts()
        return portfolio, exposure, calendar, positions, accounts

    # Callback: Update account tabs from fetched accounts
    @app.callback(
        Output("account-tabs", "children"),
        Input("accounts-data", "data"),
    )
    def update_account_tabs(accounts):
        """Populate account tabs from API data."""
        tabs = [dbc.Tab(label="All Accounts", tab_id="all")]
        for acc in accounts:
            name = acc.get("name", "")
            tabs.append(dbc.Tab(label=name, tab_id=name))
        return tabs

    # Callback: Re-fetch data when account tab changes
    @app.callback(
        [
            Output("portfolio-data", "data", allow_duplicate=True),
            Output("exposure-data", "data", allow_duplicate=True),
            Output("calendar-data", "data", allow_duplicate=True),
            Output("positions-data", "data", allow_duplicate=True),
        ],
        Input("account-tabs", "active_tab"),
        prevent_initial_call=True,
    )
    def filter_by_account(active_tab):
        """Re-fetch data filtered by account selection."""
        account = None if active_tab == "all" else active_tab
        portfolio = _fetch_portfolio_risk(account)
        exposure = _fetch_exposure(account)
        calendar = _fetch_expiry_calendar(account)
        positions = _flatten_calendar(calendar)
        return portfolio, exposure, calendar, positions

    # Callback: Apply market/risk filters to positions
    @app.callback(
        Output("filtered-positions-data", "data"),
        [
            Input("positions-data", "data"),
            Input("market-filter", "value"),
            Input("risk-filter", "value"),
        ],
    )
    def apply_filters(positions, market, risk):
        """Filter positions by market and risk category."""
        if not positions:
            return []
        filtered = positions
        if market and market != "All Markets":
            filtered = [p for p in filtered if p.get("market") == market]
        if risk and risk != "All Risks":
            filtered = [p for p in filtered if p.get("risk_category") == risk]
        return filtered

    # --- Portfolio tab filters ---

    @app.callback(
        Output("pf-account-tabs", "children"),
        Input("accounts-data", "data"),
    )
    def update_pf_account_tabs(accounts):
        """Populate portfolio account tabs from API data."""
        tabs = [dbc.Tab(label="All Accounts", tab_id="all")]
        for acc in accounts:
            name = acc.get("name", "")
            tabs.append(dbc.Tab(label=name, tab_id=name))
        return tabs

    @app.callback(
        Output("pf-filtered-data", "data"),
        [
            Input("portfolio-review-data", "data"),
            Input("pf-account-tabs", "active_tab"),
            Input("pf-market-filter", "value"),
            Input("pf-risk-filter", "value"),
        ],
    )
    def apply_pf_filters(data, active_account, market, risk):
        """Filter portfolio data by account, market, and risk."""
        if not data or "data" not in data:
            return data

        positions = data["data"].get("positions", [])
        account = None if active_account == "all" else active_account

        if account:
            positions = [p for p in positions if p.get("account") == account]
        if market and market != "All Markets":
            positions = [p for p in positions if _derive_market_from_symbol(p.get("underlying", "")) == market]
        if risk and risk != "All Risks":
            positions = [p for p in positions if p.get("risk_alert", "").upper() == risk or p.get("risk_category") == risk]

        filtered = {**data["data"], "positions": positions}

        # Recompute summary from filtered positions
        premium = sum(
            _convert_to_usd(
                abs(p.get("entry_premium", 0) * p.get("quantity", 1) * p.get("multiplier", 100)),
                p.get("currency", "USD"),
            )
            for p in positions
        )
        pnl = sum(p.get("pnl", 0) for p in positions)
        itm = sum(1 for p in positions if p.get("risk_alert") == "ITM")
        near = sum(1 for p in positions if p.get("risk_alert") == "NEAR")
        expiring = sum(1 for p in positions if p.get("risk_alert") == "EXPIRING")

        filtered["total_premium_collected"] = premium
        filtered["total_unrealized_pnl"] = pnl
        filtered["itm_count"] = itm
        filtered["near_money_count"] = near
        filtered["expiring_soon_count"] = expiring

        return {"data": filtered}

    # --- Trade Review tab filters ---

    @app.callback(
        Output("tr-account-tabs", "children"),
        Input("accounts-data", "data"),
    )
    def update_tr_account_tabs(accounts):
        """Populate trade review account tabs from API data."""
        tabs = [dbc.Tab(label="All Accounts", tab_id="all")]
        for acc in accounts:
            name = acc.get("name", "")
            tabs.append(dbc.Tab(label=name, tab_id=name))
        return tabs

    @app.callback(
        Output("tr-filtered-data", "data"),
        [
            Input("trade-review-data", "data"),
            Input("tr-account-tabs", "active_tab"),
            Input("tr-market-filter", "value"),
            Input("tr-risk-filter", "value"),
        ],
    )
    def apply_tr_filters(data, active_account, market, risk):
        """Filter trade review data by account, market, and risk."""
        if not data:
            return data

        round_trips = data.get("round_trips", [])
        account = None if active_account == "all" else active_account

        if account:
            round_trips = [rt for rt in round_trips if rt.get("account") == account]
        if market and market != "All Markets":
            round_trips = [rt for rt in round_trips if _derive_market_from_symbol(rt.get("underlying", "")) == market]
        if risk and risk != "All Risks":
            # Round-trips don't have risk_category; pass all through
            pass

        # Recompute overview metrics from filtered round-trips
        total = len(round_trips)
        winners = [rt for rt in round_trips if _rt_pnl(rt) > 0]
        losers = [rt for rt in round_trips if _rt_pnl(rt) < 0]
        win_rate = len(winners) / total if total > 0 else 0
        total_pnl = sum(_rt_pnl(rt) for rt in round_trips)
        gross_profit = sum(_rt_pnl(rt) for rt in winners)
        gross_loss = abs(sum(_rt_pnl(rt) for rt in losers))
        profit_factor = gross_profit / gross_loss if gross_loss > 0 else float("inf") if gross_profit > 0 else 0
        avg_winner = sum(_rt_pnl(rt) for rt in winners) / len(winners) if winners else 0
        avg_loser = sum(_rt_pnl(rt) for rt in losers) / len(losers) if losers else 0
        expectancy = (win_rate * avg_winner + (1 - win_rate) * avg_loser) if total > 0 else 0
        avg_days = sum(rt.get("holding_days", 0) for rt in round_trips) / total if total > 0 else 0
        avg_roc = sum(rt.get("roc", 0) for rt in round_trips) / total if total > 0 else 0
        avg_ann = sum(rt.get("annualized_return", 0) for rt in round_trips) / total if total > 0 else 0

        # Monthly breakdown (YTD)
        from datetime import date as _fdate
        _fy = _fdate.today().year
        # Normalize close_date to YYYY-MM format (handle both YYYY-MM-DD and YYYYMMDD)
        def _normalize_month(cd: str) -> str:
            if not cd:
                return ""
            if "-" in cd:
                return cd[:7]  # YYYY-MM-DD → YYYY-MM
            elif len(cd) >= 6:
                return cd[:4] + "-" + cd[4:6]  # YYYYMMDD → YYYY-MM
            return cd

        _mm: dict[str, list] = {}
        for rt in round_trips:
            cd = _normalize_month(rt.get("close_date", ""))
            if not cd or int(cd[:4]) != _fy:
                continue
            _mm.setdefault(cd, []).append(rt)

        _mnames = {
            "01": "Jan", "02": "Feb", "03": "Mar", "04": "Apr",
            "05": "May", "06": "Jun", "07": "Jul", "08": "Aug",
            "09": "Sep", "10": "Oct", "11": "Nov", "12": "Dec",
        }
        monthly = []
        for _mk in sorted(_mm.keys()):
            _mt = _mm[_mk]
            monthly.append({
                "month": _mk,
                "month_label": _mnames.get(_mk[5:], _mk),
                "trade_count": len(_mt),
                "option_pnl": sum(t.get("realized_pnl", 0) for t in _mt),
                "stock_pnl": sum(t.get("assigned_stock_pnl", 0) for t in _mt),
                "total_pnl": sum(_rt_pnl(t) for t in _mt),
            })

        return {
            **data,
            "round_trips": round_trips,
            "monthly_breakdown": monthly,
            "overview": {
                "total_trades": total,
                "winning_trades": len(winners),
                "losing_trades": len(losers),
                "win_rate": win_rate,
                "total_option_pnl": sum(rt.get("realized_pnl", 0) for rt in round_trips),
                "total_stock_pnl": sum(rt.get("assigned_stock_pnl", 0) for rt in round_trips),
                "total_pnl": total_pnl,
                "avg_winner": avg_winner,
                "avg_loser": avg_loser,
                "profit_factor": profit_factor,
                "expectancy": expectancy,
                "avg_holding_days": avg_days,
                "avg_roc": avg_roc,
                "avg_annualized_return": avg_ann,
            },
        }

    # Callback: Update summary cards
    @app.callback(
        [
            Output("card-total-positions", "children"),
            Output("card-max-profit", "children"),
            Output("card-loss-10pct", "children"),
            Output("card-loss-20pct", "children"),
            Output("card-expiring-soon", "children"),
        ],
        Input("filtered-positions-data", "data"),
    )
    def update_summary_cards(positions):
        """Update summary card values from filtered positions."""
        if not positions:
            return "0", "$0", "$0", "$0", "0"

        total = len(positions)
        max_profit = 0
        loss_10pct = 0
        loss_20pct = 0
        expiring_soon = 0

        for pos in positions:
            premium = pos.get("premium", 0) or 0
            quantity = abs(pos.get("quantity", 0) or 0)
            strike = pos.get("strike", 0) or 0
            currency = pos.get("currency", "USD") or "USD"
            multiplier = pos.get("multiplier", 100)

            max_profit += _convert_to_usd(premium * multiplier * quantity, currency)

            loss_10pct += _convert_to_usd(
                max(0, strike * 0.10 - premium) * multiplier * quantity, currency
            )
            loss_20pct += _convert_to_usd(
                max(0, strike * 0.20 - premium) * multiplier * quantity, currency
            )

            # Expiring this week (<= 7 days)
            expiry_date = _parse_expiry(pos.get("expiry", ""))
            if expiry_date:
                days_to_expiry = (expiry_date - date.today()).days
                if 0 <= days_to_expiry <= 7:
                    expiring_soon += 1

        return [
            str(total),
            f"${max_profit:,.0f}",
            f"${loss_10pct:,.0f}",
            f"${loss_20pct:,.0f}",
            str(expiring_soon),
        ]

    # Callback: Update expiry timeline chart
    @app.callback(
        Output("expiry-timeline-chart", "figure"),
        Input("filtered-positions-data", "data"),
    )
    def update_expiry_chart(positions):
        """Create stacked bar chart of positions grouped by days-to-expiry buckets."""
        if not positions:
            return go.Figure()

        # Define DTE buckets
        bucket_labels = ["<7 days", "7-14 days", "14-21 days", "21-28 days", ">28 days"]
        bucket_ranges = [(0, 7), (7, 14), (14, 21), (21, 28), (28, 999)]
        bucket_risks: dict[str, Counter] = {label: Counter() for label in bucket_labels}

        for pos in positions:
            expiry_date = _parse_expiry(pos.get("expiry", ""))
            if not expiry_date:
                continue
            days_to_expiry = (expiry_date - date.today()).days

            # Find appropriate bucket
            bucket_idx = 0
            for i, (low, high) in enumerate(bucket_ranges):
                if low <= days_to_expiry < high:
                    bucket_idx = i
                    break

            risk = pos.get("risk_category", "SAFE")
            bucket_risks[bucket_labels[bucket_idx]][risk] += 1

        # Build stacked bar chart
        fig = go.Figure()
        risk_order = ["SAFE", "MODERATE", "HIGH", "CRITICAL"]
        for risk in risk_order:
            values = [bucket_risks[b].get(risk, 0) for b in bucket_labels]
            fig.add_trace(
                go.Bar(
                    name=risk,
                    x=bucket_labels,
                    y=values,
                    marker_color=RISK_COLORS.get(risk, TEXT_MUTED),
                )
            )

        fig.update_layout(
            barmode="stack",
            xaxis_title="Days to Expiry",
            yaxis_title="Positions",
            paper_bgcolor=BG_CARD,
            plot_bgcolor=BG_CARD,
            font={"color": "#f8fafc"},
            xaxis={"gridcolor": "#334155"},
            yaxis={"gridcolor": "#334155"},
            margin={"l": 40, "r": 20, "t": 20, "b": 40},
            legend={
                "orientation": "h",
                "yanchor": "bottom",
                "y": 1.02,
                "xanchor": "right",
                "x": 1,
            },
        )
        return fig

    # Callback: Update exposure table
    @app.callback(
        Output("exposure-table", "children"),
        Input("filtered-positions-data", "data"),
    )
    def update_exposure_table(positions):
        """Create exposure table showing top underlyings from filtered positions."""
        if not positions:
            return html.P("No position data", className="text-muted")

        # Aggregate exposure by underlying, collecting accounts
        exposure_map: dict[str, dict] = {}
        for pos in positions:
            underlying = pos.get("underlying", "")
            if underlying not in exposure_map:
                exposure_map[underlying] = {"position_count": 0, "max_profit": 0, "max_loss": 0, "accounts": set()}
            exposure_map[underlying]["position_count"] += 1
            exposure_map[underlying]["accounts"].add(pos.get("account_name", ""))
            premium = pos.get("premium", 0) or 0
            quantity = abs(pos.get("quantity", 0) or 0)
            strike = pos.get("strike", 0) or 0
            currency = pos.get("currency", "USD") or "USD"
            multiplier = pos.get("multiplier", 100)
            exposure_map[underlying]["max_profit"] += _convert_to_usd(premium * multiplier * quantity, currency)
            exposure_map[underlying]["max_loss"] += _convert_to_usd((strike * multiplier * quantity) - (premium * multiplier * quantity), currency) if quantity > 0 else 0

        # Take top 10 by max_profit
        sorted_exposure = sorted(
            [{"underlying": k, "accounts": ", ".join(sorted(v["accounts"])), "position_count": v["position_count"],
              "max_profit": v["max_profit"], "max_loss": v["max_loss"]}
             for k, v in exposure_map.items()],
            key=lambda x: x.get("max_profit", 0),
            reverse=True,
        )[:10]

        rows = [
            html.Tr(
                [
                    html.Td(e.get("underlying", ""), style={"textAlign": "left"}),
                    html.Td(e.get("accounts", ""), style={"textAlign": "left"}),
                    html.Td(str(e.get("position_count", 0))),
                    html.Td(f"${e.get('max_profit', 0):,.0f}"),
                    html.Td(f"${e.get('max_loss', 0):,.0f}"),
                ],
                style={"backgroundColor": BG_CARD, "color": "#f8fafc"},
            )
            for e in sorted_exposure
        ]

        return dbc.Table(
            [
                html.Thead(
                    html.Tr(
                        [
                            html.Th("Underlying", style={"textAlign": "left"}),
                            html.Th("Accounts", style={"textAlign": "left"}),
                            html.Th("Positions"),
                            html.Th("Max Profit"),
                            html.Th("Max Loss"),
                        ],
                        style={"backgroundColor": "#253449", "color": "#f8fafc"},
                    )
                ),
                html.Tbody(rows),
            ],
            bordered=True,
            className="mb-0",
            style={"backgroundColor": BG_CARD},
        )

    # Callback: Update risk distribution
    @app.callback(
        Output("risk-distribution", "children"),
        Input("filtered-positions-data", "data"),
    )
    def update_risk_distribution(positions):
        """Create risk category distribution display."""
        if not positions:
            return html.P("No position data", className="text-muted")

        counts = Counter()
        for pos in positions:
            risk = pos.get("risk_category", "SAFE")
            counts[risk] += 1

        risk_order = ["SAFE", "MODERATE", "HIGH", "CRITICAL"]
        blocks = []
        for risk in risk_order:
            count = counts.get(risk, 0)
            color = RISK_COLORS.get(risk, TEXT_MUTED)
            blocks.append(
                dbc.Col(
                    html.Div(
                        [
                            html.Strong(risk, style={"color": color}),
                            html.Span(f": {count}", style={"color": "#f8fafc"}),
                        ],
                        className="p-2 text-center",
                        style={"backgroundColor": "#253449", "borderRadius": "4px"},
                    ),
                    width=3,
                )
            )

        return dbc.Row(blocks, className="g-2")

    # Callback: Update position table data
    @app.callback(
        Output("position-table", "data"),
        Input("filtered-positions-data", "data"),
    )
    def update_position_table(positions):
        """Update position table with filtered data."""
        if not positions:
            return []

        formatted = []
        for pos in positions:
            formatted.append({
                "underlying": pos.get("underlying", ""),
                "market": pos.get("market", ""),
                "expiry": pos.get("expiry", ""),
                "strike": pos.get("strike", 0),
                "right": pos.get("right", ""),
                "quantity": pos.get("quantity", 0),
                "premium": pos.get("premium", 0),
                "distance_pct": pos.get("distance_pct", 0),
                "risk_badge": pos.get("risk_category", "SAFE"),
                "risk_category": pos.get("risk_category", "SAFE"),
                "account_name": pos.get("account_name", ""),
                "current_price": pos.get("current_price"),
                "currency": pos.get("currency", "USD"),
                "multiplier": pos.get("multiplier", 100),
            })
        return formatted

    # Callback: Populate account dropdown from DB
    @app.callback(
        [
            Output("account-select-dropdown", "options"),
            Output("account-select-dropdown", "value"),
        ],
        Input("accounts-data", "data"),
    )
    def update_account_dropdown(accounts):
        """Populate account dropdown from database."""
        if not accounts:
            return [], None
        options = [{"label": a["name"], "value": a["name"]} for a in accounts]
        return options, None

    # Callback: Handle download & import for all accounts
    @app.callback(
        [
            Output("download-status", "children"),
            Output("download-status", "style"),
            Output("portfolio-data", "data", allow_duplicate=True),
            Output("exposure-data", "data", allow_duplicate=True),
            Output("calendar-data", "data", allow_duplicate=True),
            Output("positions-data", "data", allow_duplicate=True),
            Output("accounts-data", "data", allow_duplicate=True),
        ],
        Input("download-import-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def handle_download_import(n_clicks):
        """Download Flex reports for all accounts and import, then refresh."""
        if not n_clicks:
            return "", {"color": TEXT_MUTED}, no_update, no_update, no_update, no_update, no_update

        accounts = _fetch_accounts()
        if not accounts:
            return (
                "No accounts configured",
                {"color": RISK_COLORS["CRITICAL"]},
                no_update, no_update, no_update, no_update, no_update,
            )

        updated_accounts = []
        errors = []

        for acc in accounts:
            if not acc.get("enabled"):
                continue
            name = acc.get("name", "")
            token = acc.get("token", "")
            query_id = acc.get("query_id", "")
            if not token or not query_id:
                errors.append(f"{name}: missing token/query_id")
                continue

            # Remove previous downloads for this account (YTD data supersedes)
            _clear_account_downloads(name)

            try:
                download_flex(token, query_id, name, force=True)
                updated_accounts.append(name)
            except CliError as e:
                errors.append(f"{name}: {e}")

        if errors:
            return (
                f"Errors: {', '.join(errors)}",
                {"color": RISK_COLORS["CRITICAL"]},
                no_update, no_update, no_update, no_update, no_update,
            )

        # Import all downloaded CSVs
        import_result_data = {}
        try:
            import_result_data = import_csv()
        except CliError as e:
            return (
                f"Import error: {e}",
                {"color": RISK_COLORS["CRITICAL"]},
                no_update, no_update, no_update, no_update, no_update,
            )

        # Run trade matching after import
        match_count = 0
        try:
            rebuild_result = run_cli("trades", "--rebuild")
            match_count = rebuild_result.get("overview", {}).get("total_trades", 0)
        except CliError:
            pass  # Non-fatal — matching failure shouldn't block the UI

        # Refresh all dashboard data
        portfolio = _fetch_portfolio_risk()
        exposure = _fetch_exposure()
        calendar = _fetch_expiry_calendar()
        positions = _flatten_calendar(calendar)
        accounts = _fetch_accounts()

        trades_in = import_result_data.get("trades_imported", 0)
        pos_in = import_result_data.get("positions_imported", 0)
        status = f"Updated {len(updated_accounts)}: {', '.join(updated_accounts)}"
        if trades_in or pos_in:
            status += f" | {trades_in} trades, {pos_in} positions"
        status += f" | Matched {match_count} round-trips"

        return (
            status,
            {"color": RISK_COLORS["SAFE"]},
            portfolio, exposure, calendar, positions, accounts,
        )

    # Callback: Show account form when Add clicked
    @app.callback(
        [
            Output("account-form-container", "style", allow_duplicate=True),
            Output("account-name-input", "value", allow_duplicate=True),
            Output("account-name-input", "disabled", allow_duplicate=True),
            Output("account-token-input", "value", allow_duplicate=True),
            Output("account-query-id-input", "value", allow_duplicate=True),
            Output("account-enabled-check", "value", allow_duplicate=True),
            Output("editing-account-id", "data", allow_duplicate=True),
            Output("delete-account-btn", "style", allow_duplicate=True),
        ],
        Input("add-account-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def show_add_account_form(n_clicks):
        """Show empty account form for new account."""
        return {"display": "block"}, "", False, "", "", [1], None, {"display": "none"}

    # Callback: Populate form when account selected from dropdown
    @app.callback(
        [
            Output("account-form-container", "style"),
            Output("account-name-input", "value"),
            Output("account-name-input", "disabled"),
            Output("account-token-input", "value"),
            Output("account-query-id-input", "value"),
            Output("account-enabled-check", "value"),
            Output("editing-account-id", "data"),
            Output("delete-account-btn", "style"),
        ],
        Input("account-select-dropdown", "value"),
        State("accounts-data", "data"),
        prevent_initial_call=True,
    )
    def show_edit_account_form(selected_name, accounts):
        """Populate form with existing account data."""
        if not selected_name or not accounts:
            return no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update

        for acc in accounts:
            if acc.get("name") == selected_name:
                enabled = [1] if acc.get("enabled") else []
                return (
                    {"display": "block"},
                    acc["name"], True,
                    acc["token"], acc["query_id"], enabled,
                    acc["id"],
                    {"display": "inline-block"},
                )

        return no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update

    # Callback: Hide account form
    @app.callback(
        Output("account-form-container", "style", allow_duplicate=True),
        Input("cancel-account-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def hide_account_form(n_clicks):
        return {"display": "none"}

    # Callback: Save account (create or update)
    @app.callback(
        [
            Output("account-form-status", "children"),
            Output("account-form-status", "style"),
            Output("accounts-data", "data", allow_duplicate=True),
            Output("account-form-container", "style", allow_duplicate=True),
        ],
        Input("save-account-btn", "n_clicks"),
        State("account-name-input", "value"),
        State("account-token-input", "value"),
        State("account-query-id-input", "value"),
        State("account-enabled-check", "value"),
        State("editing-account-id", "data"),
        prevent_initial_call=True,
    )
    def save_account(n_clicks, name, token, query_id, enabled_val, editing_id):
        name = (name or "").strip()
        token = (token or "").strip()
        query_id = (query_id or "").strip()

        if not n_clicks or not name or not token or not query_id:
            return "All fields required", {"color": RISK_COLORS["CRITICAL"]}, no_update, no_update

        enabled = 1 if (enabled_val and 1 in enabled_val) else 0

        if editing_id:
            execute(
                "UPDATE accounts SET token = ?, query_id = ?, enabled = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?",
                (token, query_id, enabled, editing_id),
            )
        else:
            execute(
                "INSERT INTO accounts (name, token, query_id, enabled) VALUES (?, ?, ?, ?)",
                (name, token, query_id, enabled),
            )

        refreshed = _fetch_accounts()
        return (
            f"Saved {name}",
            {"color": RISK_COLORS["SAFE"]},
            refreshed,
            {"display": "none"},
        )

    # Callback: Delete account
    @app.callback(
        [
            Output("account-form-status", "children", allow_duplicate=True),
            Output("account-form-status", "style", allow_duplicate=True),
            Output("accounts-data", "data", allow_duplicate=True),
            Output("account-form-container", "style", allow_duplicate=True),
        ],
        Input("delete-account-btn", "n_clicks"),
        State("editing-account-id", "data"),
        prevent_initial_call=True,
    )
    def delete_account(n_clicks, account_id):
        if not n_clicks or not account_id:
            return no_update, no_update, no_update, no_update

        execute("DELETE FROM accounts WHERE id = ?", (account_id,))

        refreshed = _fetch_accounts()
        return (
            "Deleted",
            {"color": RISK_COLORS["HIGH"]},
            refreshed,
            {"display": "none"},
        )

    # Callback: Show position detail when row selected
    @app.callback(
        Output("position-detail", "children"),
        Input("position-table", "selected_rows"),
        State("position-table", "data"),
    )
    def show_position_detail(selected_rows, table_data):
        """Display detail panel when a position row is selected."""
        if not selected_rows or not table_data:
            return html.Div()

        idx = selected_rows[0]
        pos = table_data[idx]

        # Calculate computed values
        strike = pos.get("strike", 0)
        premium = pos.get("premium", 0)
        quantity = abs(pos.get("quantity", 0))
        current_price = pos.get("current_price") or 0
        currency = pos.get("currency", "USD") or "USD"
        multiplier = pos.get("multiplier", 100)

        # Convert to USD
        strike_usd = _convert_to_usd(strike, currency)
        premium_usd = _convert_to_usd(premium, currency)
        current_price_usd = _convert_to_usd(current_price, currency) if current_price else 0

        # Breakeven for short put: strike - premium
        breakeven = strike_usd - premium_usd

        # Max profit: premium * multiplier * |qty|
        max_profit = premium_usd * multiplier * quantity

        # Max loss for naked short put: strike * multiplier * qty - premium * multiplier * qty
        max_loss = (strike_usd * multiplier * quantity) - (premium_usd * multiplier * quantity) if quantity > 0 else 0

        # Days to expiry
        expiry_date = _parse_expiry(pos.get("expiry", ""))
        days_to_expiry = (expiry_date - date.today()).days if expiry_date else 0

        # Distance to strike %
        distance_pct = pos.get("distance_pct", 0)

        # Risk gauge: show price position relative to strike
        risk = pos.get("risk_category", "SAFE")
        risk_color = RISK_COLORS.get(risk, TEXT_MUTED)

        # Build detail panel
        metric_cards = dbc.Row(
            [
                dbc.Col(_detail_card("Strike", f"${strike_usd:,.2f} ({currency})"), width=3),
                dbc.Col(_detail_card("Current Price", f"${current_price_usd:,.2f}" if current_price_usd else "N/A"), width=3),
                dbc.Col(_detail_card("Premium", f"${premium_usd:,.2f} ({currency})"), width=3),
                dbc.Col(_detail_card("Distance", f"{distance_pct:.1f}%"), width=3),
                dbc.Col(_detail_card("Breakeven", f"${breakeven:,.2f}"), width=3),
                dbc.Col(_detail_card("Max Profit", f"${max_profit:,.0f}"), width=3),
                dbc.Col(_detail_card("Max Loss", f"${max_loss:,.0f}"), width=3),
                dbc.Col(_detail_card("Days to Expiry", str(days_to_expiry)), width=3),
            ],
            className="g-2",
        )

        # Risk gauge visualization
        gauge = _risk_gauge(current_price_usd, strike_usd, risk_color)

        return dbc.Card(
            dbc.CardBody(
                [
                    html.H5(
                        f"{pos.get('underlying', '')} {pos.get('expiry', '')} {strike} {pos.get('right', '')} [{currency}]",
                        className="mb-3",
                        style={"color": "#f8fafc"},
                    ),
                    metric_cards,
                    html.Div(gauge, className="mt-4"),
                ]
            ),
            style={"backgroundColor": "#253449", "border": "none"},
        )


    # --- Portfolio tab callbacks ---

    @app.callback(
        Output("portfolio-review-data", "data"),
        Input("main-tabs", "active_tab"),
    )
    def load_portfolio_review(active_tab):
        """Load portfolio review data when Portfolio tab is selected."""
        if active_tab != "tab-portfolio":
            return no_update
        try:
            return run_cli("analyze", "portfolio")
        except CliError:
            return {}

    @app.callback(
        [
            Output("pf-card-premium", "children"),
            Output("pf-card-pnl", "children"),
            Output("pf-card-itm", "children"),
            Output("pf-card-near", "children"),
            Output("pf-card-expiring", "children"),
        ],
        Input("pf-filtered-data", "data"),
    )
    def update_portfolio_cards(data):
        """Update portfolio summary cards."""
        if not data or "data" not in data:
            return "—", "—", "—", "—", "—"
        d = data["data"]
        premium = d.get("total_premium_collected", 0)
        pnl = d.get("total_unrealized_pnl", 0)
        return (
            f"${premium:,.0f}",
            f"${pnl:,.0f}",
            str(d.get("itm_count", 0)),
            str(d.get("near_money_count", 0)),
            str(d.get("expiring_soon_count", 0)),
        )

    @app.callback(
        Output("pf-risk-alerts", "children"),
        Input("pf-filtered-data", "data"),
    )
    def update_portfolio_alerts(data):
        """Render risk alerts section."""
        if not data or "data" not in data:
            return html.P("No data", className="text-muted")

        positions = data["data"].get("positions", [])
        alerts = [p for p in positions if p.get("risk_alert")]

        if not alerts:
            return html.P("No active alerts", className="text-muted", style={"color": RISK_COLORS["SAFE"]})

        rows = []
        for p in alerts:
            alert = p.get("risk_alert", "")
            color = RISK_COLORS.get(alert, TEXT_MUTED)
            underlying = p.get("underlying", "")
            strike = p.get("strike", 0)
            expiry = p.get("expiry", "")
            account = p.get("account", "")
            current_price = p.get("current_price")
            price_str = f" (now: ${current_price:,.2f})" if current_price else ""

            rows.append(
                html.Div(
                    [
                        dbc.Badge(alert, color="danger" if alert == "ITM" else "warning" if alert == "NEAR" else "secondary", className="me-2"),
                        html.Span(f"{account} | {underlying} ${strike:,.2f}P exp {expiry}{price_str}", style={"color": "#f8fafc"}),
                    ],
                    className="mb-1",
                )
            )

        return html.Div(rows)

    @app.callback(
        Output("pf-position-table", "data"),
        Input("pf-filtered-data", "data"),
    )
    def update_portfolio_table(data):
        """Update portfolio positions table."""
        if not data or "data" not in data:
            return []

        positions = data["data"].get("positions", [])
        rows = []
        for p in positions:
            expiry = p.get("expiry", "")
            expiry_date = _parse_expiry(expiry)
            dte = (expiry_date - date.today()).days if expiry_date else 0
            rows.append({
                "account": p.get("account", ""),
                "underlying": p.get("underlying", ""),
                "strike": p.get("strike", 0),
                "expiry": expiry,
                "dte": dte,
                "entry_premium": p.get("entry_premium", 0),
                "pnl": p.get("pnl", 0),
                "otm_percent": p.get("otm_percent", 0),
                "annualized_yield": p.get("annualized_yield", 0),
                "risk_alert": p.get("risk_alert", ""),
            })
        return rows

    @app.callback(
        Output("pf-loss-scenarios", "children"),
        Input("pf-filtered-data", "data"),
    )
    def update_loss_scenarios(data):
        """Render per-account loss scenarios."""
        if not data or "data" not in data:
            return html.P("No data", className="text-muted")

        d = data["data"]
        loss_10 = d.get("loss_10pct", {})
        loss_20 = d.get("loss_20pct", {})

        if not loss_10:
            return html.P("No short put positions", className="text-muted")

        rows = []
        for acct in sorted(loss_10.keys()):
            l10 = loss_10.get(acct, 0)
            l20 = loss_20.get(acct, 0)
            rows.append(
                html.Tr(
                    [
                        html.Td(acct, style={"textAlign": "left", "color": "#f8fafc"}),
                        html.Td(f"${l10:,.0f}", style={"color": "#fbbf24"}),
                        html.Td(f"${l20:,.0f}", style={"color": "#f97316"}),
                    ],
                    style={"backgroundColor": BG_CARD, "color": "#f8fafc"},
                )
            )

        return dbc.Table(
            [
                html.Thead(html.Tr(
                    [
                        html.Th("Account", style={"textAlign": "left"}),
                        html.Th("10% Loss"),
                        html.Th("20% Loss"),
                    ],
                    style={"backgroundColor": "#253449", "color": "#f8fafc"},
                )),
                html.Tbody(rows),
            ],
            bordered=True,
            className="mb-0",
            style={"backgroundColor": BG_CARD},
        )

    @app.callback(
        Output("pf-expiry-calendar", "children"),
        Input("pf-filtered-data", "data"),
    )
    def update_expiry_calendar(data):
        """Render expiration calendar buckets."""
        if not data or "data" not in data:
            return html.P("No data", className="text-muted")

        d = data["data"]
        buckets = d.get("dte_buckets", {})

        if not buckets:
            return html.P("No positions", className="text-muted")

        bucket_info = [
            ("<=7", "Expiring (<=7 days)", RISK_COLORS["CRITICAL"]),
            ("8-30", "Near-term (8-30 days)", RISK_COLORS["HIGH"]),
            ("31-60", "Medium (31-60 days)", RISK_COLORS["MODERATE"]),
            ("60+", "Far (60+ days)", RISK_COLORS["SAFE"]),
        ]

        blocks = []
        for key, label, color in bucket_info:
            count = buckets.get(key, 0)
            blocks.append(
                dbc.Col(
                    html.Div(
                        [
                            html.Strong(str(count), style={"fontSize": "1.5rem", "color": color}),
                            html.Br(),
                            html.Small(label, className="text-muted"),
                        ],
                        className="p-3 text-center",
                        style={"backgroundColor": "#253449", "borderRadius": "4px"},
                    ),
                    width=3,
                )
            )

        return dbc.Row(blocks, className="g-2")

    # --- Screener tab callbacks ---

    @app.callback(
        [
            Output("screener-data", "data"),
            Output("screener-status", "children"),
            Output("screener-status", "style"),
        ],
        Input("run-screener-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def run_screener(n_clicks):
        """Run screener and store results."""
        if not n_clicks:
            return {}, "", {"color": TEXT_MUTED}

        try:
            data = run_cli("analyze", "screener")
            d = data.get("data", {})
            scanned = d.get("total_scanned", 0)
            passed = d.get("passed_filter", 0)
            errors = d.get("errors", [])
            status = f"Scanned {scanned}, {passed} passed filter"
            if errors:
                status += f", {len(errors)} errors"
            return data, status, {"color": RISK_COLORS["SAFE"]}
        except CliError as e:
            return {}, str(e), {"color": RISK_COLORS["CRITICAL"]}

    @app.callback(
        [
            Output("sc-card-scanned", "children"),
            Output("sc-card-passed", "children"),
            Output("sc-card-errors", "children"),
        ],
        Input("screener-data", "data"),
    )
    def update_screener_cards(data):
        """Update screener summary cards."""
        if not data or "data" not in data:
            return "—", "—", "—"
        d = data["data"]
        return (
            str(d.get("total_scanned", 0)),
            str(d.get("passed_filter", 0)),
            str(len(d.get("errors", []))),
        )

    @app.callback(
        Output("screener-table", "data"),
        Input("screener-data", "data"),
    )
    def update_screener_table(data):
        """Update screener results table."""
        if not data or "data" not in data:
            return []

        results = data["data"].get("results", [])
        rows = []
        for r in results:
            grade = r.get("grade", "C")
            rows.append({
                "symbol": r.get("symbol", ""),
                "current_price": r.get("current_price", 0),
                "iv_display": round(r.get("iv", 0) * 100, 1),
                "suggested_strike": r.get("suggested_strike", 0),
                "strike_dte": r.get("strike_dte", 0),
                "premium": r.get("premium", 0),
                "annualized_yield": round(r.get("annualized_yield", 0), 1),
                "iv_percentile": round(r.get("iv_percentile", 0), 1),
                "grade": grade,
                "existing": "YES" if r.get("has_existing_position") else "",
            })
        return rows

    # --- Trade Review tab callbacks ---

    def _convert_trades_to_usd(data: dict) -> dict:
        """Convert all P&L values in trade review data to USD.

        For assigned trades: Option P&L = premium only.
        Stock P&L = assignment exposure at current market price.
        Net P&L = option P&L + stock P&L.
        """
        if not data:
            return data

        trips = data.get("round_trips", [])
        if not trips:
            return data

        # Derive actual multiplier from each round-trip's data
        # (net_premium = qty × open_price × multiplier)
        for rt in trips:
            qty = abs(rt.get("quantity", 0))
            op = abs(rt.get("open_price", 0))
            np_val = abs(rt.get("net_premium", 0))
            if qty > 0 and op > 0:
                rt["_multiplier"] = round(np_val / (qty * op))
            else:
                # Fallback to market-based multiplier when derivation fails
                market = _derive_market_from_symbol(rt.get("underlying", ""))
                rt["_multiplier"] = CONTRACT_MULTIPLIER.get(market, 100)

        # Recalculate P&L for assigned trades: option P&L = premium only,
        # stock P&L = assignment exposure at current market price, net = sum.
        assigned = [rt for rt in trips if rt.get("close_reason") == "assigned"]
        if assigned:
            underlyings = [s for s in set(rt.get("underlying", "") for rt in assigned) if s]
            if underlyings:
                placeholders = ",".join("?" for _ in underlyings)
                price_rows = query(
                    f"SELECT symbol, price FROM cached_prices WHERE symbol IN ({placeholders})",
                    tuple(underlyings),
                )
            else:
                price_rows = []
            prices = {r["symbol"]: r["price"] for r in price_rows}

            # Fetch missing prices from Yahoo Finance
            missing = [s for s in underlyings if s not in prices]
            for symbol in missing:
                price = _fetch_price_yahoo(symbol)
                if price is not None:
                    prices[symbol] = price

            for rt in assigned:
                underlying = rt.get("underlying", "")
                strike = rt.get("strike", 0)
                right = rt.get("right", "P")
                qty = rt.get("quantity", 1)
                open_price = rt.get("open_price", 0)
                commission = rt.get("commission", 0)
                multiplier = rt.get("_multiplier", 100)
                currency = _derive_currency_from_symbol(underlying)

                # Option P&L is always just the premium received (regardless of current price)
                option_pnl = qty * multiplier * open_price - commission
                rt["realized_pnl"] = _convert_to_usd(option_pnl, currency)
                rt["close_price"] = 0

                # Stock P&L from assignment (only if current price available)
                current_price = prices.get(underlying)
                if current_price is not None:
                    # Short put assigned: bought stock at strike, P&L = (current - strike) × qty × mult
                    # Short call assigned: sold stock at strike, P&L = (strike - current) × qty × mult
                    if right == "P":
                        stock_pnl = (current_price - strike) * qty * multiplier
                    else:
                        stock_pnl = (strike - current_price) * qty * multiplier
                    rt["assigned_stock_pnl"] = _convert_to_usd(stock_pnl, currency)
                    rt["current_price"] = current_price
                else:
                    rt["assigned_stock_pnl"] = 0

                # Net P&L = Option + Stock
                rt["net_pnl"] = rt["realized_pnl"] + rt.get("assigned_stock_pnl", 0)
                rt["assigned_shares"] = int(qty * multiplier)

                # Recalculate ROC using net_pnl / collateral
                collateral = strike * qty * multiplier
                if collateral > 0:
                    rt["roc"] = (rt["net_pnl"] / collateral) * 100

        # Convert remaining P&L values to USD
        for rt in trips:
            if rt.get("close_reason") != "assigned":
                underlying = rt.get("underlying", "")
                currency = _derive_currency_from_symbol(underlying)
                for key in ("realized_pnl", "net_premium"):
                    if key in rt:
                        rt[key] = _convert_to_usd(rt[key], currency)

            # Clean up internal keys
            rt.pop("_multiplier", None)

        # Recompute overview from converted round-trips
        if trips:
            # Assignment-based classification: non-assigned = win, assigned = loss
            non_assigned = [t for t in trips if t.get("close_reason") != "assigned"]
            assigned_trips = [t for t in trips if t.get("close_reason") == "assigned"]

            winning_trades = len(non_assigned)
            losing_trades = len(assigned_trips)
            win_rate = winning_trades / len(trips) if trips else 0

            # Profit Factor: sum of non-assigned net_pnl / abs(sum of assigned net_pnl)
            non_assigned_pnl = sum(_rt_pnl(t) for t in non_assigned)
            assigned_pnl = sum(_rt_pnl(t) for t in assigned_trips)
            if assigned_pnl != 0:
                profit_factor = non_assigned_pnl / abs(assigned_pnl)
            elif non_assigned_pnl > 0:
                profit_factor = float("inf")
            else:
                profit_factor = 0

            # Avg ROC: use recalculated roc field (net_pnl / collateral)
            avg_roc = sum(t.get("roc", 0) for t in trips) / len(trips)

            pnls = [_rt_pnl(t) for t in trips]
            total_pnl = sum(pnls)
            holding_days = [t.get("holding_days", 0) for t in trips if t.get("holding_days")]

            data["overview"] = {
                "total_trades": len(trips),
                "winning_trades": winning_trades,
                "losing_trades": losing_trades,
                "win_rate": win_rate,
                "total_option_pnl": sum(t.get("realized_pnl", 0) for t in trips),
                "total_stock_pnl": sum(t.get("assigned_stock_pnl", 0) for t in trips),
                "total_pnl": total_pnl,
                "avg_winner": non_assigned_pnl / winning_trades if winning_trades else 0,
                "avg_loser": abs(assigned_pnl) / losing_trades if losing_trades else 0,
                "profit_factor": profit_factor,
                "expectancy": total_pnl / len(trips) if trips else 0,
                "avg_holding_days": sum(holding_days) / len(holding_days) if holding_days else 0,
                "avg_roc": avg_roc,
                "avg_annualized_return": sum(t.get("annualized_return", 0) for t in trips) / len(trips),
            }

        # Monthly breakdown (YTD)
        from datetime import date as _date
        current_year = _date.today().year
        # Normalize close_date to YYYY-MM format (handle both YYYY-MM-DD and YYYYMMDD)
        def _norm_month(cd: str) -> str:
            if not cd:
                return ""
            if "-" in cd:
                return cd[:7]
            elif len(cd) >= 6:
                return cd[:4] + "-" + cd[4:6]
            return cd

        month_map: dict[str, list] = {}
        for t in trips:
            cd = _norm_month(t.get("close_date", ""))
            if not cd or int(cd[:4]) != current_year:
                continue
            month_map.setdefault(cd, []).append(t)

        month_names = {
            "01": "Jan", "02": "Feb", "03": "Mar", "04": "Apr",
            "05": "May", "06": "Jun", "07": "Jul", "08": "Aug",
            "09": "Sep", "10": "Oct", "11": "Nov", "12": "Dec",
        }
        data["monthly_breakdown"] = []
        for month_key in sorted(month_map.keys()):
            m_trips = month_map[month_key]
            opt_pnl = sum(t.get("realized_pnl", 0) for t in m_trips)
            stk_pnl = sum(t.get("assigned_stock_pnl", 0) for t in m_trips)
            net = sum(_rt_pnl(t) for t in m_trips)
            data["monthly_breakdown"].append({
                "month": month_key,
                "month_label": month_names.get(month_key[5:], month_key),
                "trade_count": len(m_trips),
                "option_pnl": opt_pnl,
                "stock_pnl": stk_pnl,
                "total_pnl": net,
            })

        # Recompute strategy performance from converted round-trips
        strat_map: dict[str, list] = {}
        for t in trips:
            st = t.get("strategy_type") or "Unknown"
            strat_map.setdefault(st, []).append(t)

        data["strategy_performance"] = []
        for st, st_trips in sorted(strat_map.items()):
            st_pnls = [_rt_pnl(t) for t in st_trips]
            st_wins = [p for p in st_pnls if p >= 0]
            st_losses = [p for p in st_pnls if p < 0]
            st_total = sum(st_pnls)
            st_total_wins = sum(st_wins)
            st_total_losses = abs(sum(st_losses))
            st_days = [t.get("holding_days", 0) for t in st_trips if t.get("holding_days")]
            data["strategy_performance"].append({
                "strategy_type": st,
                "trade_count": len(st_trips),
                "winning_trades": len(st_wins),
                "win_rate": len(st_wins) / len(st_trips) if st_trips else 0,
                "total_pnl": st_total,
                "avg_pnl": st_total / len(st_trips) if st_trips else 0,
                "profit_factor": st_total_wins / st_total_losses if st_total_losses else 0,
                "avg_holding_days": sum(st_days) / len(st_days) if st_days else 0,
            })

        # Recompute DTE breakdown
        dte_map: dict[str, list] = {}
        for t in trips:
            days = t.get("holding_days", 0)
            if days <= 7:
                bucket = "0-7"
            elif days <= 14:
                bucket = "8-14"
            elif days <= 30:
                bucket = "15-30"
            elif days <= 60:
                bucket = "31-60"
            else:
                bucket = "60+"
            dte_map.setdefault(bucket, []).append(t)

        data["dte_breakdown"] = []
        for bucket in ("0-7", "8-14", "15-30", "31-60", "60+"):
            dte_trips = dte_map.get(bucket, [])
            if not dte_trips:
                continue
            dte_pnls = [_rt_pnl(t) for t in dte_trips]
            dte_wins = [p for p in dte_pnls if p >= 0]
            data["dte_breakdown"].append({
                "dte_bucket": bucket,
                "trade_count": len(dte_trips),
                "winning_trades": len(dte_wins),
                "win_rate": len(dte_wins) / len(dte_trips) if dte_trips else 0,
                "total_pnl": sum(dte_pnls),
            })

        # Recompute loss clusters
        loss_map: dict[str, list] = {}
        for t in trips:
            pnl = _rt_pnl(t)
            if pnl < 0:
                underlying = t.get("underlying", "")
                days = t.get("holding_days", 0)
                bucket = "0-7" if days <= 7 else "8-14" if days <= 14 else "15-30" if days <= 30 else "31-60" if days <= 60 else "60+"
                key = f"{underlying}|{bucket}"
                loss_map.setdefault(key, []).append({"pnl": pnl, **t})

        data["loss_clusters"] = []
        for key, loss_trips in sorted(loss_map.items(), key=lambda x: sum(t["pnl"] for t in x[1])):
            underlying, dte = key.split("|", 1)
            data["loss_clusters"].append({
                "cluster_key": key,
                "underlying": underlying,
                "dte_bucket": dte,
                "loss_count": len(loss_trips),
                "total_loss": sum(t["pnl"] for t in loss_trips),
            })

        # Recompute streak info
        sorted_trips = sorted(trips, key=lambda t: (t.get("close_date", ""), t.get("open_date", "")))
        max_consec = 0
        current_streak = 0
        consec = 0
        streak_end = ""
        for t in sorted_trips:
            pnl = _rt_pnl(t)
            if pnl < 0:
                consec += 1
                current_streak = -(consec) if current_streak <= 0 else -1
                if consec > max_consec:
                    max_consec = consec
                    streak_end = t.get("close_date", "")
            else:
                if current_streak < 0:
                    current_streak = 1
                else:
                    current_streak = max(current_streak, 0) + 1
                consec = 0

        data["streak_info"] = {
            "max_consecutive_losses": max_consec,
            "streak_end_date": streak_end,
            "recovery_date": "",
            "recovery_days": 0,
            "current_streak": current_streak,
        }

        # Recompute underlying breakdown
        ul_map: dict[str, list] = {}
        for t in trips:
            ul = t.get("underlying", "")
            ul_map.setdefault(ul, []).append(t)

        data["underlying_breakdown"] = []
        for ul, ul_trips in sorted(ul_map.items(), key=lambda x: sum(_rt_pnl(t) for t in x[1]), reverse=True):
            ul_pnls = [_rt_pnl(t) for t in ul_trips]
            ul_wins = [p for p in ul_pnls if p >= 0]
            data["underlying_breakdown"].append({
                "underlying": ul,
                "trade_count": len(ul_trips),
                "winning_trades": len(ul_wins),
                "win_rate": len(ul_wins) / len(ul_trips) if ul_trips else 0,
                "total_pnl": sum(ul_pnls),
                "avg_pnl": sum(ul_pnls) / len(ul_trips) if ul_trips else 0,
            })

        return data

    @app.callback(
        Output("trade-review-data", "data"),
        Input("main-tabs", "active_tab"),
    )
    def load_trade_review_data(active_tab):
        """Load trade review data when Trade Review tab is selected."""
        if active_tab != "tab-trade-review":
            return no_update
        try:
            data = run_cli("trades")
            return _convert_trades_to_usd(data)
        except CliError:
            return {}

    @app.callback(
        [
            Output("trade-review-data", "data", allow_duplicate=True),
            Output("tr-status", "children", allow_duplicate=True),
            Output("tr-status", "style", allow_duplicate=True),
        ],
        Input("tr-rebuild-btn", "n_clicks"),
        State("tr-account-tabs", "active_tab"),
        prevent_initial_call=True,
    )
    def rebuild_trades(n_clicks, active_account):
        """Rebuild round-trips and reload data."""
        if not n_clicks:
            return no_update, no_update, no_update
        try:
            args: list[str] = ["--rebuild"]
            account = None if active_account == "all" else active_account
            if account:
                args.extend(["--account", account])
            data = run_cli("trades", *args)
            data = _convert_trades_to_usd(data)
            matched = data.get("overview", {}).get("total_trades", 0)
            return data, f"Matched {matched} round-trips", {"color": RISK_COLORS["SAFE"]}
        except CliError as e:
            return no_update, f"Error: {e}", {"color": RISK_COLORS["CRITICAL"]}

    @app.callback(
        [
            Output("tr-card-total", "children"),
            Output("tr-card-win-rate", "children"),
            Output("tr-card-option-pnl", "children"),
            Output("tr-card-stock-pnl", "children"),
            Output("tr-card-pnl", "children"),
            Output("tr-card-profit-factor", "children"),
            Output("tr-card-avg-roc", "children"),
        ],
        Input("tr-filtered-data", "data"),
    )
    def update_trade_overview_cards(data):
        """Update KPI cards from overview data."""
        if not data:
            return "—", "—", "—", "—", "—", "—", "—"
        ov = data.get("overview", {})
        if not ov:
            return "—", "—", "—", "—", "—", "—", "—"

        total = ov.get("total_trades", 0)
        win_rate = ov.get("win_rate", 0) or 0
        option_pnl = ov.get("total_option_pnl", 0) or 0
        stock_pnl = ov.get("total_stock_pnl", 0) or 0
        total_pnl = ov.get("total_pnl", 0) or 0
        pf = ov.get("profit_factor") or 0
        avg_roc = ov.get("avg_roc", 0) or 0

        return (
            str(total),
            f"{win_rate:.1%}" if total else "—",
            f"${option_pnl:,.0f}" if total else "—",
            f"${stock_pnl:,.0f}" if total else "—",
            f"${total_pnl:,.0f}" if total else "—",
            f"{pf:.2f}" if total else "—",
            f"{avg_roc:.1%}" if total else "—",
        )

    @app.callback(
        Output("tr-monthly-table", "children"),
        Input("tr-filtered-data", "data"),
    )
    def update_monthly_table(data):
        """YTD monthly breakdown table."""
        if not data:
            return html.P("No data", className="text-muted")

        months = data.get("monthly_breakdown", [])
        if not months:
            return html.P("No YTD data", className="text-muted")

        header = html.Thead(html.Tr([
            html.Th("Month", style={"textAlign": "left"}),
            html.Th("Trades"),
            html.Th("Option P&L"),
            html.Th("Stock P&L"),
            html.Th("Net P&L"),
        ], style={"backgroundColor": "#253449", "color": "#f8fafc"}))

        rows = []
        for idx, m in enumerate(months):
            bg = BG_CARD if idx % 2 == 0 else "#253449"
            opt = m.get("option_pnl", 0)
            stk = m.get("stock_pnl", 0)
            net = m.get("total_pnl", 0)
            rows.append(html.Tr([
                html.Td(m.get("month_label", ""), style={"textAlign": "left", "color": "#f8fafc", "backgroundColor": bg}),
                html.Td(str(m.get("trade_count", 0)), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"${opt:,.0f}", style={"backgroundColor": bg, "color": RISK_COLORS["SAFE"] if opt >= 0 else RISK_COLORS["CRITICAL"]}),
                html.Td(f"${stk:,.0f}", style={"backgroundColor": bg, "color": RISK_COLORS["SAFE"] if stk >= 0 else RISK_COLORS["CRITICAL"]}),
                html.Td(f"${net:,.0f}", style={"backgroundColor": bg, "color": RISK_COLORS["SAFE"] if net >= 0 else RISK_COLORS["CRITICAL"]}),
            ]))

        return dbc.Table([header, html.Tbody(rows)], bordered=True, className="mb-0",
                         style={"backgroundColor": BG_CARD})

    @app.callback(
        Output("tr-monthly-chart", "figure"),
        Input("tr-filtered-data", "data"),
    )
    def update_monthly_chart(data):
        """YTD monthly P&L stacked bar chart."""
        if not data:
            return go.Figure()

        months = data.get("monthly_breakdown", [])
        if not months:
            return go.Figure()

        labels = [m.get("month_label", "") for m in months]
        opt = [m.get("option_pnl", 0) for m in months]
        stk = [m.get("stock_pnl", 0) for m in months]

        fig = go.Figure()
        fig.add_trace(go.Bar(
            name="Option P&L",
            x=labels,
            y=opt,
            marker_color="#22c55e",
            text=[f"${v:,.0f}" for v in opt],
            textposition="auto",
        ))
        fig.add_trace(go.Bar(
            name="Stock P&L",
            x=labels,
            y=stk,
            marker_color="#f59e0b",
            text=[f"${v:,.0f}" for v in stk],
            textposition="auto",
        ))

        fig.update_layout(
            barmode="stack",
            xaxis_title="Month",
            yaxis_title="P&L ($)",
            paper_bgcolor=BG_CARD,
            plot_bgcolor=BG_CARD,
            font={"color": "#f8fafc"},
            xaxis={"gridcolor": "#334155"},
            yaxis={"gridcolor": "#334155"},
            margin={"l": 40, "r": 20, "t": 20, "b": 40},
            legend={"orientation": "h", "yanchor": "bottom", "y": 1.02, "xanchor": "right", "x": 1},
        )
        return fig

    @app.callback(
        Output("trade-list-data", "data"),
        Input("tr-filtered-data", "data"),
    )
    def update_trades_table_data(data):
        """Populate trades DataTable from review data."""
        if not data:
            return []
        return data.get("round_trips", [])

    @app.callback(
        Output("tr-trades-table", "data"),
        Input("trade-list-data", "data"),
    )
    def render_trades_table(trips):
        """Render trades table rows."""
        if not trips:
            return []
        return trips

    @app.callback(
        Output("tr-stock-table", "children"),
        Input("tr-filtered-data", "data"),
    )
    def render_stock_table(data):
        """Render assigned stock positions grouped by underlying with expandable details."""
        if not data:
            return html.P("No assigned positions", className="text-muted")

        trips = data.get("round_trips", [])
        assigned = [rt for rt in trips if rt.get("close_reason") == "assigned"]
        if not assigned:
            return html.P("No assigned positions", className="text-muted")

        # Group by underlying
        from collections import defaultdict
        grouped = defaultdict(list)
        for rt in assigned:
            grouped[rt.get("underlying", "Unknown")].append(rt)

        # Build accordion items
        items = []
        for underlying in sorted(grouped.keys()):
            positions = grouped[underlying]

            # Calculate totals for the group
            total_shares = sum(p.get("assigned_shares", 0) for p in positions)
            total_stock_pnl = sum(p.get("assigned_stock_pnl", 0) for p in positions)
            total_option_pnl = sum(p.get("realized_pnl", 0) for p in positions)
            total_net_pnl = sum(p.get("net_pnl", 0) for p in positions)

            # Header row for the group
            pnl_color = RISK_COLORS["SAFE"] if total_net_pnl >= 0 else RISK_COLORS["CRITICAL"]
            header = html.Div([
                html.Span(underlying, style={"fontWeight": "bold", "fontSize": "1rem"}),
                html.Span(f" ({len(positions)} positions)", className="text-muted", style={"marginLeft": "8px"}),
                html.Span(f"Shares: {total_shares:,}", style={"marginLeft": "20px", "fontSize": "0.9rem"}),
                html.Span(f"Net P&L: ", style={"marginLeft": "20px", "fontSize": "0.9rem"}),
                html.Span(f"${total_net_pnl:,.0f}", style={"color": pnl_color, "fontSize": "0.9rem", "fontWeight": "bold"}),
            ], style={"display": "flex", "alignItems": "center"})

            # Detail table rows
            detail_rows = []
            for i, p in enumerate(positions):
                bg = BG_CARD if i % 2 == 0 else "#253449"
                row_pnl = p.get("net_pnl", 0)
                row_pnl_color = RISK_COLORS["SAFE"] if row_pnl >= 0 else RISK_COLORS["CRITICAL"]
                stock_pnl = p.get("assigned_stock_pnl", 0)
                stock_pnl_color = RISK_COLORS["SAFE"] if stock_pnl >= 0 else RISK_COLORS["CRITICAL"]

                detail_rows.append(html.Tr([
                    html.Td(p.get("account", ""), style={"backgroundColor": bg, "textAlign": "left"}),
                    html.Td(f"{p.get('right', 'P')}", style={"backgroundColor": bg}),
                    html.Td(f"{p.get('strike', 0):,.0f}", style={"backgroundColor": bg}),
                    html.Td(f"{p.get('assigned_shares', 0):,}", style={"backgroundColor": bg}),
                    html.Td(p.get("close_date", ""), style={"backgroundColor": bg}),
                    html.Td(f"{p.get('current_price', 0):,.2f}" if p.get("current_price") else "—", style={"backgroundColor": bg}),
                    html.Td(f"${stock_pnl:,.0f}", style={"backgroundColor": bg, "color": stock_pnl_color}),
                    html.Td(f"${p.get('realized_pnl', 0):,.0f}", style={"backgroundColor": bg}),
                    html.Td(f"${row_pnl:,.0f}", style={"backgroundColor": bg, "color": row_pnl_color, "fontWeight": "bold"}),
                ]))

            detail_table = dbc.Table(
                [
                    html.Thead(html.Tr([
                        html.Th("Account", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Right", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Strike", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Shares", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Assigned", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Current", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Stock P&L", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Option P&L", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                        html.Th("Net P&L", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                    ])),
                    html.Tbody(detail_rows),
                ],
                bordered=True,
                className="mb-0",
                style={"fontSize": "0.85rem"},
            )

            items.append(dbc.AccordionItem(
                detail_table,
                title=header,
                item_id=underlying,
            ))

        return dbc.Accordion(
            items,
            always_open=True,
            start_collapsed=True,
            flush=True,
            style={"backgroundColor": BG_CARD},
        )

    @app.callback(
        [
            Output("tr-subtab-trades", "style"),
            Output("tr-subtab-strategy", "style"),
            Output("tr-subtab-loss", "style"),
            Output("tr-subtab-optimal-premium", "style"),
        ],
        Input("trade-subtabs", "active_tab"),
    )
    def toggle_trade_subtabs(active_tab):
        """Show/hide sub-tab content based on active sub-tab."""
        show = {"display": "block"}
        hide = {"display": "none"}
        if active_tab == "subtab-trades":
            return show, hide, hide, hide
        elif active_tab == "subtab-strategy":
            return hide, show, hide, hide
        elif active_tab == "subtab-loss":
            return hide, hide, show, hide
        elif active_tab == "subtab-optimal-premium":
            return hide, hide, hide, show
        return show, hide, hide, hide

    @app.callback(
        Output("tr-strategy-table", "children"),
        Input("tr-filtered-data", "data"),
    )
    def update_strategy_table(data):
        """Build strategy performance HTML table."""
        if not data:
            return html.P("No data", className="text-muted")

        strategies = data.get("strategy_performance", [])
        if not strategies:
            return html.P("No strategy data", className="text-muted")

        header = html.Thead(html.Tr(
            [
                html.Th("Strategy", style={"textAlign": "left"}),
                html.Th("Trades"),
                html.Th("Wins"),
                html.Th("Win Rate"),
                html.Th("Total P&L"),
                html.Th("Avg P&L"),
                html.Th("Profit Factor"),
                html.Th("Avg Days"),
            ],
            style={"backgroundColor": "#253449", "color": "#f8fafc"},
        ))

        rows = []
        for idx, s in enumerate(strategies):
            bg = BG_CARD if idx % 2 == 0 else "#253449"
            pnl = s.get("total_pnl", 0)
            pnl_color = RISK_COLORS["SAFE"] if pnl >= 0 else RISK_COLORS["CRITICAL"]
            rows.append(html.Tr([
                html.Td(s.get("strategy_type", ""), style={"textAlign": "left", "color": "#f8fafc", "backgroundColor": bg}),
                html.Td(str(s.get("trade_count", 0)), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(str(s.get("winning_trades", 0)), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"{s.get('win_rate', 0):.0%}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"${pnl:,.0f}", style={"backgroundColor": bg, "color": pnl_color}),
                html.Td(f"${s.get('avg_pnl', 0):,.0f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"{s.get('profit_factor', 0):.2f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"{s.get('avg_holding_days', 0):.1f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
            ]))

        return dbc.Table(
            [header, html.Tbody(rows)],
            bordered=True,
            className="mb-0",
            style={"backgroundColor": BG_CARD},
        )

    @app.callback(
        Output("tr-dte-chart", "figure"),
        Input("tr-filtered-data", "data"),
    )
    def update_dte_chart(data):
        """DTE breakdown bar chart."""
        if not data:
            return go.Figure()

        buckets = data.get("dte_breakdown", [])
        if not buckets:
            return go.Figure()

        labels = [b.get("dte_bucket", "") for b in buckets]
        pnls = [b.get("total_pnl", 0) for b in buckets]
        win_rates = [b.get("win_rate", 0) * 100 for b in buckets]
        counts = [b.get("trade_count", 0) for b in buckets]

        fig = go.Figure()
        fig.add_trace(go.Bar(
            name="P&L",
            x=labels,
            y=pnls,
            marker_color=[RISK_COLORS["SAFE"] if p >= 0 else RISK_COLORS["CRITICAL"] for p in pnls],
            text=[f"${p:,.0f}" for p in pnls],
            textposition="auto",
        ))

        fig.update_layout(
            barmode="group",
            xaxis_title="DTE Bucket",
            yaxis_title="P&L ($)",
            paper_bgcolor=BG_CARD,
            plot_bgcolor=BG_CARD,
            font={"color": "#f8fafc"},
            xaxis={"gridcolor": "#334155"},
            yaxis={"gridcolor": "#334155"},
            margin={"l": 40, "r": 20, "t": 20, "b": 40},
            showlegend=False,
        )
        return fig

    @app.callback(
        Output("tr-loss-clusters", "children"),
        Input("tr-filtered-data", "data"),
    )
    def update_loss_clusters(data):
        """Display loss cluster list."""
        if not data:
            return html.P("No data", className="text-muted")

        clusters = data.get("loss_clusters", [])
        if not clusters:
            return html.P("No loss clusters found", className="text-muted", style={"color": RISK_COLORS["SAFE"]})

        items = []
        for c in clusters:
            key = c.get("cluster_key", "")
            loss_count = c.get("loss_count", 0)
            total_loss = c.get("total_loss", 0)
            underlying = c.get("underlying", "")
            strategy = c.get("strategy_type", "")
            dte = c.get("dte_bucket", "")

            label_parts = [p for p in [underlying, dte, strategy] if p]
            label = " / ".join(label_parts) if label_parts else key

            items.append(html.Div(
                [
                    html.Strong(label, style={"color": "#f8fafc"}),
                    html.Span(f"  -- {loss_count} losses, ${total_loss:,.0f}", style={"color": RISK_COLORS["CRITICAL"]}),
                ],
                className="mb-1 p-2",
                style={"backgroundColor": "#253449", "borderRadius": "4px"},
            ))

        return html.Div(items)

    @app.callback(
        Output("tr-streak-info", "children"),
        Input("tr-filtered-data", "data"),
    )
    def update_streak_info(data):
        """Display streak information."""
        if not data:
            return html.P("No data", className="text-muted")

        streak = data.get("streak_info", {})
        if not streak:
            return html.P("No streak data", className="text-muted")

        max_consec = streak.get("max_consecutive_losses", 0)
        streak_end = streak.get("streak_end_date", "—")
        recovery = streak.get("recovery_date", "—")
        recovery_days = streak.get("recovery_days", 0)
        current = streak.get("current_streak", 0)

        items = [
            _detail_card("Max Consecutive Losses", str(max_consec)),
            _detail_card("Streak Ended", streak_end),
            _detail_card("Recovery Date", recovery if recovery != "—" else "Not recovered"),
            _detail_card("Recovery Days", str(recovery_days) if recovery != "—" else "—"),
            _detail_card("Current Streak", f"{current}" + (" (loss)" if current < 0 else " (win)" if current > 0 else "")),
        ]

        return dbc.Row([dbc.Col(item, width=4) for item in items], className="g-2")

    # --- Optimal Premium Analysis ---

    @app.callback(
        [
            Output("op-underlying-table", "data"),
            Output("op-analysis-data", "data"),
            Output("op-card-recommended-premium", "children"),
            Output("op-card-overall-winrate", "children"),
            Output("op-card-trades-analyzed", "children"),
        ],
        [
            Input("tr-filtered-data", "data"),
            Input("op-dte-filter", "value"),
            Input("op-right-filter", "value"),
            Input("op-target-winrate", "value"),
        ],
    )
    def update_optimal_premium_analysis(data, dte_filter, right_filter, target_wr):
        """Compute and display optimal premium analysis."""
        if not data:
            return [], {}, "—", "—", "—"

        trips = data.get("round_trips", [])
        if not trips:
            return [], {}, "—", "—", "—"

        # Parse target win rate
        try:
            target_win_rate = float(target_wr)
        except (TypeError, ValueError):
            target_win_rate = 0.80

        # Apply DTE filter
        if dte_filter and dte_filter != "all":
            max_dte = int(dte_filter)
            trips = [rt for rt in trips if _compute_dte_at_entry(rt) is not None and _compute_dte_at_entry(rt) <= max_dte]

        # Apply right filter
        if right_filter and right_filter != "all":
            trips = [rt for rt in trips if rt.get("right") == right_filter]

        if not trips:
            return [], {}, "—", "—", "—"

        result = _compute_optimal_premium(trips, target_win_rate)

        # Format table data
        table_data = []
        for row in result["per_underlying"]:
            wr = row["win_rate"]
            pct = row["max_safe_premium_pct"]
            table_data.append({
                "underlying": row["underlying"],
                "right": row["right"],
                "trade_count": row["trade_count"],
                "assigned_count": row["assigned_count"],
                "win_rate": f"{wr:.0%}" if row["trade_count"] >= 5 else f"{wr:.0%}*",
                "max_safe_premium_pct": f"{pct:.2f}%" if pct is not None else "N/A",
                "avg_win_premium": f"${row['avg_win_premium']:.2f}" if row["avg_win_premium"] else "—",
                "avg_assigned_premium": f"${row['avg_assigned_premium']:.2f}" if row["avg_assigned_premium"] else "—",
            })

        # KPI cards
        rec_pct = result["recommended_premium_pct"]
        rec_display = f"{rec_pct:.2f}% of strike" if rec_pct is not None else "N/A"
        wr_display = f"{result['overall_win_rate']:.0%}"
        count_display = str(result["total_trades"])

        return table_data, result, rec_display, wr_display, count_display

    @app.callback(
        Output("op-premium-chart", "figure"),
        Input("op-analysis-data", "data"),
    )
    def update_optimal_premium_chart(analysis_data):
        """Render premium-as-%-of-strike vs assignment risk scatter chart."""
        if not analysis_data:
            return go.Figure()

        trades = analysis_data.get("aggregate_trades", [])
        if not trades:
            return go.Figure()

        recommended_pct = analysis_data.get("recommended_premium_pct")

        not_assigned = [(t["premium_pct"], t["underlying"], t["open_price"], t["strike"]) for t in trades if not t["is_assigned"]]
        assigned = [(t["premium_pct"], t["underlying"], t["open_price"], t["strike"]) for t in trades if t["is_assigned"]]

        fig = go.Figure()

        # Not assigned trades (bottom)
        if not_assigned:
            fig.add_trace(go.Scatter(
                x=[p for p, _, _, _ in not_assigned],
                y=[0.1] * len(not_assigned),
                mode="markers",
                name="Not Assigned",
                marker=dict(color=RISK_COLORS["SAFE"], size=8, opacity=0.6),
                text=[f"{u}<br>Prem: ${op:.2f} / Strike: ${s:.0f}<br>{p:.2f}% of strike" for p, u, op, s in not_assigned],
                hovertemplate="%{text}<extra></extra>",
            ))

        # Assigned trades (top)
        if assigned:
            fig.add_trace(go.Scatter(
                x=[p for p, _, _, _ in assigned],
                y=[0.9] * len(assigned),
                mode="markers",
                name="Assigned",
                marker=dict(color=RISK_COLORS["CRITICAL"], size=8, opacity=0.6),
                text=[f"{u}<br>Prem: ${op:.2f} / Strike: ${s:.0f}<br>{p:.2f}% of strike" for p, u, op, s in assigned],
                hovertemplate="%{text}<extra></extra>",
            ))

        # Max safe premium % line
        if recommended_pct is not None:
            fig.add_vline(
                x=recommended_pct,
                line_dash="dash",
                line_color="#fbbf24",
                annotation_text=f"Max Safe: {recommended_pct:.2f}%",
                annotation_position="top left",
                annotation=dict(font_color="#fbbf24", font_size=12),
            )

        fig.update_layout(
            xaxis_title="Premium as % of Strike",
            yaxis=dict(
                tickvals=[0.1, 0.9],
                ticktext=["Not Assigned", "Assigned"],
                range=[-0.2, 1.2],
            ),
            plot_bgcolor="#1e293b",
            paper_bgcolor=BG_CARD,
            font_color="#f8fafc",
            height=400,
            margin=dict(l=60, r=30, t=30, b=50),
            showlegend=True,
            legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1),
        )

        return fig


def _detail_card(label: str, value: str) -> html.Div:
    """Create a small metric card for detail panel."""
    return html.Div(
        [
            html.Small(label, className="text-muted"),
            html.Br(),
            html.Strong(value, style={"color": "#f8fafc", "fontSize": "1.1rem"}),
        ],
        className="p-2 text-center",
        style={"backgroundColor": BG_CARD, "borderRadius": "4px"},
    )


def _risk_gauge(current_price: float, strike: float, risk_color: str) -> html.Div:
    """Create a visual risk gauge showing price vs strike."""
    if not current_price or not strike:
        return html.Div()

    # Calculate price position as percentage of strike
    pct = (current_price / strike) * 100

    return html.Div(
        [
            html.P("Price vs Strike", className="text-muted mb-2"),
            html.Div(
                [
                    html.Span("ITM", style={"color": RISK_COLORS["CRITICAL"], "width": "20%", "textAlign": "center", "display": "inline-block"}),
                    html.Span("1-5%", style={"color": RISK_COLORS["HIGH"], "width": "20%", "textAlign": "center", "display": "inline-block"}),
                    html.Span("5-10%", style={"color": RISK_COLORS["MODERATE"], "width": "20%", "textAlign": "center", "display": "inline-block"}),
                    html.Span("SAFE", style={"color": RISK_COLORS["SAFE"], "width": "40%", "textAlign": "center", "display": "inline-block"}),
                ],
                className="mb-1",
            ),
            html.Div(
                [
                    html.Div(
                        style={
                            "width": f"{min(pct, 100)}%",
                            "height": "20px",
                            "backgroundColor": risk_color,
                            "borderRadius": "4px",
                        }
                    ),
                    html.Span(
                        f"{pct:.1f}%",
                        className="ml-2",
                        style={"color": "#f8fafc", "position": "relative", "top": "-2px"},
                    ),
                ],
                style={"backgroundColor": "#334155", "borderRadius": "4px", "display": "flex"},
            ),
        ]
    )
