"""Dash callbacks for IBKR Options Dashboard."""

from collections import defaultdict, Counter
from datetime import datetime, date, timedelta

import plotly.graph_objects as go
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
        """Create stacked bar chart of positions grouped by calendar week buckets."""
        if not positions:
            return go.Figure()

        # Calendar week buckets (ISO week offset from current week)
        bucket_labels = ["This Week", "Next Week", "Week 3", "Week 4", "Week 5+"]
        bucket_risks: dict[str, Counter] = {label: Counter() for label in bucket_labels}

        today = date.today()
        today_monday = today - timedelta(days=today.weekday())

        for pos in positions:
            expiry_date = _parse_expiry(pos.get("expiry", ""))
            if not expiry_date:
                continue
            expiry_monday = expiry_date - timedelta(days=expiry_date.weekday())
            week_offset = (expiry_monday - today_monday).days // 7

            if week_offset <= 0: bucket_idx = 0
            elif week_offset == 1: bucket_idx = 1
            elif week_offset == 2: bucket_idx = 2
            elif week_offset == 3: bucket_idx = 3
            else: bucket_idx = 4

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
            xaxis_title="Calendar Week",
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

    # Callback: Update expiry stock table
    @app.callback(
        Output("expiry-stock-table", "children"),
        Input("filtered-positions-data", "data"),
    )
    def update_expiry_stock_table(positions):
        """Create table of positions grouped by stock across calendar week buckets."""
        if not positions:
            return html.P("No position data", className="text-muted")

        bucket_labels = ["This Week", "Next Week", "Week 3", "Week 4", "Week 5+"]

        # Store (strike, risk_category) pairs per stock per bucket
        stock_buckets: dict[str, dict[str, list[tuple[float, str]]]] = {}

        today = date.today()
        today_monday = today - timedelta(days=today.weekday())

        for pos in positions:
            expiry_date = _parse_expiry(pos.get("expiry", ""))
            if not expiry_date:
                continue

            underlying = pos.get("underlying", "")
            strike = pos.get("strike", 0) or 0
            risk = pos.get("risk_category", "SAFE")

            if underlying not in stock_buckets:
                stock_buckets[underlying] = {b: [] for b in bucket_labels}

            expiry_monday = expiry_date - timedelta(days=expiry_date.weekday())
            week_offset = (expiry_monday - today_monday).days // 7

            if week_offset <= 0: bucket_idx = 0
            elif week_offset == 1: bucket_idx = 1
            elif week_offset == 2: bucket_idx = 2
            elif week_offset == 3: bucket_idx = 3
            else: bucket_idx = 4

            stock_buckets[underlying][bucket_labels[bucket_idx]].append((strike, risk))

        if not stock_buckets:
            return html.P("No position data", className="text-muted")

        header_cells = [
            html.Th("Stock", style={"textAlign": "left", "padding": "6px 10px", "color": "#94a3b8", "fontWeight": "600"})
        ]
        for label in bucket_labels:
            header_cells.append(html.Th(label, style={"textAlign": "center", "padding": "6px 10px", "color": "#94a3b8", "fontWeight": "600"}))

        rows = []
        for idx, stock in enumerate(sorted(stock_buckets.keys())):
            row_bg = "#1e293b" if idx % 2 == 0 else "#253449"
            cells = [
                html.Td(stock, style={"textAlign": "left", "padding": "6px 10px", "color": "#f8fafc", "backgroundColor": row_bg})
            ]
            for label in bucket_labels:
                entries = stock_buckets[stock][label]
                if entries:
                    parts = []
                    for strike, risk in sorted(entries, key=lambda x: x[0]):
                        color = RISK_COLORS.get(risk, TEXT_MUTED)
                        parts.append(html.Span(f"${strike:.0f}", style={"color": color}))
                    # Join with comma separator
                    strike_spans = []
                    for i, span in enumerate(parts):
                        strike_spans.append(span)
                        if i < len(parts) - 1:
                            strike_spans.append(html.Span(", ", style={"color": "#475569"}))
                    cells.append(html.Td(strike_spans, style={"textAlign": "center", "padding": "6px 10px", "backgroundColor": row_bg}))
                else:
                    cells.append(html.Td("—", style={"textAlign": "center", "padding": "6px 10px", "color": "#475569", "backgroundColor": row_bg}))
            rows.append(html.Tr(cells))

        return dbc.Table(
            [html.Thead(html.Tr(header_cells)), html.Tbody(rows)],
            bordered=False,
            size="sm",
            style={"backgroundColor": "transparent"},
        )

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
        try:
            import_csv()
        except CliError as e:
            return (
                f"Import error: {e}",
                {"color": RISK_COLORS["CRITICAL"]},
                no_update, no_update, no_update, no_update, no_update,
            )

        # Refresh all dashboard data
        portfolio = _fetch_portfolio_risk()
        exposure = _fetch_exposure()
        calendar = _fetch_expiry_calendar()
        positions = _flatten_calendar(calendar)
        accounts = _fetch_accounts()

        return (
            f"Updated {len(updated_accounts)}: {', '.join(updated_accounts)}",
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
        Input("portfolio-review-data", "data"),
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
        Input("portfolio-review-data", "data"),
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
        Input("portfolio-review-data", "data"),
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
        Input("portfolio-review-data", "data"),
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
        Input("portfolio-review-data", "data"),
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
            ("W1", "Week 1 (0-7 days)", RISK_COLORS["CRITICAL"]),
            ("W2", "Week 2 (8-14 days)", RISK_COLORS["HIGH"]),
            ("W3", "Week 3 (15-21 days)", RISK_COLORS["MODERATE"]),
            ("W4", "Week 4 (22-28 days)", RISK_COLORS["MODERATE"]),
            ("W5+", "Week 5+ (29+ days)", RISK_COLORS["SAFE"]),
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
            Output("screener-data", "data", allow_duplicate=True),
            Output("screener-status", "children", allow_duplicate=True),
            Output("screener-status", "style", allow_duplicate=True),
        ],
        Input("run-screener-cache-btn", "n_clicks"),
        [
            State("sc-iv-percentile", "value"),
            State("sc-premium-yield", "value"),
            State("sc-min-dte", "value"),
            State("sc-max-dte", "value"),
            State("sc-otm-buffer", "value"),
        ],
        prevent_initial_call=True,
    )
    def run_screener_cache_only(n_clicks, iv_pctl, prem_yield, min_dte, max_dte, otm_buf):
        """Run screener using cached data only with parameter overrides."""
        if not n_clicks:
            return no_update, no_update, no_update

        args = ["screener", "--cache-only"]
        if iv_pctl is not None:
            args.extend(["--min-iv-percentile", str(iv_pctl)])
        if prem_yield is not None:
            args.extend(["--min-premium-yield", str(prem_yield)])
        if min_dte is not None:
            args.extend(["--min-dte", str(min_dte)])
        if max_dte is not None:
            args.extend(["--max-dte", str(max_dte)])
        if otm_buf is not None:
            args.extend(["--otm-buffer", str(otm_buf)])

        try:
            data = run_cli("analyze", *args)
            d = data.get("data", {})
            scanned = d.get("total_scanned", 0)
            passed = d.get("passed_filter", 0)
            errors = d.get("errors", [])
            status = f"(Cache) Scanned {scanned}, {passed} passed"
            if errors:
                status += f", {len(errors)} no cache"
            return data, status, {"color": "#60a5fa"}  # blue for cache mode
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

    @app.callback(
        Output("trade-review-data", "data"),
        Input("main-tabs", "active_tab"),
    )
    def load_trade_review_data(active_tab):
        """Load trade review data when Trade Review tab is selected."""
        if active_tab != "tab-trade-review":
            return no_update
        try:
            return run_cli("trades")
        except CliError:
            return {}

    @app.callback(
        [
            Output("trade-review-data", "data", allow_duplicate=True),
            Output("trade-list-data", "data", allow_duplicate=True),
            Output("tr-status", "children", allow_duplicate=True),
            Output("tr-status", "style", allow_duplicate=True),
        ],
        Input("tr-rebuild-btn", "n_clicks"),
        State("tr-account-filter", "value"),
        prevent_initial_call=True,
    )
    def rebuild_trades(n_clicks, account):
        """Rebuild round-trips and reload data."""
        if not n_clicks:
            return no_update, no_update, no_update, no_update
        try:
            args: list[str] = ["--rebuild"]
            if account:
                args.extend(["--account", account])
            data = run_cli("trades", *args)
            trips = data.get("round_trips", [])
            matched = data.get("overview", {}).get("total_trades", 0)
            return data, trips, f"Matched {matched} round-trips", {"color": RISK_COLORS["SAFE"]}
        except CliError as e:
            return no_update, no_update, f"Error: {e}", {"color": RISK_COLORS["CRITICAL"]}

    @app.callback(
        [
            Output("tr-card-total", "children"),
            Output("tr-card-win-rate", "children"),
            Output("tr-card-pnl", "children"),
            Output("tr-card-profit-factor", "children"),
            Output("tr-card-avg-roc", "children"),
            Output("tr-card-expectancy", "children"),
        ],
        Input("trade-review-data", "data"),
    )
    def update_trade_overview_cards(data):
        """Update KPI cards from overview data."""
        if not data:
            return "—", "—", "—", "—", "—", "—"
        ov = data.get("overview", {})
        if not ov:
            return "—", "—", "—", "—", "—", "—"

        total = ov.get("total_trades", 0)
        win_rate = ov.get("win_rate", 0)
        pnl = ov.get("total_pnl", 0)
        pf = ov.get("profit_factor", 0)
        avg_roc = ov.get("avg_roc", 0)
        expectancy = ov.get("expectancy", 0)

        pnl_color = RISK_COLORS["SAFE"] if pnl >= 0 else RISK_COLORS["CRITICAL"]

        return (
            str(total),
            f"{win_rate:.1%}" if total else "—",
            f"${pnl:,.0f}" if total else "—",
            f"{pf:.2f}" if total else "—",
            f"{avg_roc:.1%}" if total else "—",
            f"${expectancy:,.2f}" if total else "—",
        )

    @app.callback(
        Output("trade-list-data", "data"),
        Input("trade-review-data", "data"),
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
        [
            Output("tr-subtab-trades", "style"),
            Output("tr-subtab-strategy", "style"),
            Output("tr-subtab-loss", "style"),
        ],
        Input("trade-subtabs", "active_tab"),
    )
    def toggle_trade_subtabs(active_tab):
        """Show/hide sub-tab content based on active sub-tab."""
        show = {"display": "block"}
        hide = {"display": "none"}
        if active_tab == "subtab-trades":
            return show, hide, hide
        elif active_tab == "subtab-strategy":
            return hide, show, hide
        elif active_tab == "subtab-loss":
            return hide, hide, show
        return show, hide, hide

    @app.callback(
        Output("tr-strategy-table", "children"),
        Input("trade-review-data", "data"),
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
        Input("trade-review-data", "data"),
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
        Input("trade-review-data", "data"),
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
        Input("trade-review-data", "data"),
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
