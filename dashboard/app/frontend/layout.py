"""Dash layout components for IBKR Options Dashboard."""

import dash_bootstrap_components as dbc
from dash import html, dcc, dash_table

# Risk category colors from spec
RISK_COLORS = {
    "SAFE": "#22c55e",
    "MODERATE": "#fbbf24",
    "HIGH": "#f97316",
    "CRITICAL": "#f87171",
    "ITM": "#f87171",
    "NEAR": "#f97316",
    "EXPIRING": "#fbbf24",
}

# Grade colors for screener
GRADE_COLORS = {
    "A": "#22c55e",
    "B": "#84cc16",
    "C": "#fbbf24",
    "D": "#f97316",
}

# Theme colors from spec
BG_PRIMARY = "#0f172a"
BG_CARD = "#1e293b"
TEXT_MUTED = "#94a3b8"


def _summary_card(title: str, value_id: str) -> dbc.Card:
    """Create a summary metric card."""
    return dbc.Card(
        dbc.CardBody(
            [
                html.H6(title, className="text-muted mb-2", style={"fontSize": "0.85rem"}),
                html.H2(
                    id=value_id,
                    children="—",
                    className="mb-0",
                    style={"color": "#f8fafc"},
                ),
            ],
            className="p-3",
        ),
        style={"backgroundColor": BG_CARD, "border": "none"},
    )


def _positions_tab() -> dbc.Tab:
    """The existing positions dashboard content."""
    return dbc.Tab(
        label="Positions",
        tab_id="tab-positions",
        children=[
            # Account filter tabs (populated dynamically)
            dbc.Tabs(
                id="account-tabs",
                children=[
                    dbc.Tab(label="All Accounts", tab_id="all"),
                ],
                active_tab="all",
                className="mb-3",
            ),

            # Market and Risk filters
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Select(
                            id="market-filter",
                            options=[
                                {"label": "All Markets", "value": "All Markets"},
                                {"label": "US", "value": "US"},
                                {"label": "HK", "value": "HK"},
                                {"label": "JP", "value": "JP"},
                            ],
                            value="All Markets",
                            style={"width": "160px"},
                        ),
                        width="auto",
                    ),
                    dbc.Col(
                        dbc.Select(
                            id="risk-filter",
                            options=[
                                {"label": "All Risks", "value": "All Risks"},
                                {"label": "SAFE", "value": "SAFE"},
                                {"label": "MODERATE", "value": "MODERATE"},
                                {"label": "HIGH", "value": "HIGH"},
                                {"label": "CRITICAL", "value": "CRITICAL"},
                            ],
                            value="All Risks",
                            style={"width": "160px"},
                        ),
                        width="auto",
                    ),
                ],
                className="mb-3 g-2",
            ),

            # Summary cards row
            dbc.Row(
                [
                    dbc.Col(_summary_card("Total Positions", "card-total-positions"), width=2),
                    dbc.Col(_summary_card("Max Profit", "card-max-profit"), width=2),
                    dbc.Col(_summary_card("10% Loss", "card-loss-10pct"), width=2),
                    dbc.Col(_summary_card("20% Loss", "card-loss-20pct"), width=2),
                    dbc.Col(_summary_card("Expiring This Week", "card-expiring-soon"), width=2),
                ],
                className="mb-4 justify-content-center",
            ),

            # Charts row
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H5("Expiry Timeline", className="mb-3", style={"color": "#f8fafc"}),
                                    dcc.Graph(id="expiry-timeline-chart"),
                                    html.Div(id="expiry-stock-table", className="mt-3"),
                                ]
                            ),
                            style={"backgroundColor": BG_CARD, "border": "none"},
                        ),
                        md=7,
                    ),
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H5("Top Exposure", className="mb-3", style={"color": "#f8fafc"}),
                                    html.Div(id="exposure-table"),
                                    html.H5("Risk Distribution", className="mb-3 mt-4", style={"color": "#f8fafc"}),
                                    html.Div(id="risk-distribution"),
                                ]
                            ),
                            style={"backgroundColor": BG_CARD, "border": "none"},
                        ),
                        md=5,
                    ),
                ],
                className="mb-4",
            ),

            # Position table
            dbc.Card(
                dbc.CardBody(
                    [
                        html.H5("Positions", className="mb-3", style={"color": "#f8fafc"}),
                        dash_table.DataTable(
                            id="position-table",
                            columns=[
                                {"name": "Underlying", "id": "underlying"},
                                {"name": "Market", "id": "market"},
                                {"name": "Expiry", "id": "expiry"},
                                {"name": "Strike", "id": "strike", "type": "numeric"},
                                {"name": "Right", "id": "right"},
                                {"name": "Qty", "id": "quantity", "type": "numeric"},
                                {"name": "Premium", "id": "premium", "type": "numeric"},
                                {"name": "Distance %", "id": "distance_pct", "type": "numeric"},
                                {"name": "Risk", "id": "risk_badge"},
                                {"name": "Account", "id": "account_name"},
                            ],
                            data=[],
                            row_selectable="single",
                            sort_action="native",
                            sort_by=[{"column_id": "expiry", "direction": "asc"}],
                            style_header={
                                "backgroundColor": BG_CARD,
                                "color": "#f8fafc",
                                "fontWeight": "bold",
                                "border": "1px solid #334155",
                            },
                            style_cell={
                                "backgroundColor": BG_CARD,
                                "color": "#f8fafc",
                                "border": "1px solid #334155",
                                "textAlign": "center",
                            },
                            style_cell_conditional=[
                                {"if": {"column_id": "underlying"}, "textAlign": "left"},
                                {"if": {"column_id": "account_name"}, "textAlign": "left"},
                            ],
                            style_data_conditional=[
                                {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                            ],
                            page_size=20,
                            css=[{"selector": ".dash-spreadsheet", "rule": "font-family: monospace"}],
                        ),
                        html.Div(id="position-detail", className="mt-4"),
                    ]
                ),
                style={"backgroundColor": BG_CARD, "border": "none"},
            ),
        ],
    )


def _portfolio_tab() -> dbc.Tab:
    """Portfolio review tab."""
    return dbc.Tab(
        label="Portfolio",
        tab_id="tab-portfolio",
        children=[
            # Summary cards
            dbc.Row(
                [
                    dbc.Col(_summary_card("Premium Collected", "pf-card-premium"), width=2),
                    dbc.Col(_summary_card("Unrealized P&L", "pf-card-pnl"), width=2),
                    dbc.Col(_summary_card("ITM", "pf-card-itm"), width=2),
                    dbc.Col(_summary_card("Near Money", "pf-card-near"), width=2),
                    dbc.Col(_summary_card("Expiring Soon", "pf-card-expiring"), width=2),
                ],
                className="mb-4 justify-content-center",
            ),

            # Risk alerts section
            dbc.Card(
                dbc.CardBody(
                    [
                        html.H5("Assignment Risk Alerts", className="mb-3", style={"color": "#f8fafc"}),
                        html.Div(id="pf-risk-alerts"),
                    ]
                ),
                style={"backgroundColor": BG_CARD, "border": "none"},
                className="mb-4",
            ),

            # Position details table
            dbc.Card(
                dbc.CardBody(
                    [
                        html.H5("Positions", className="mb-3", style={"color": "#f8fafc"}),
                        dash_table.DataTable(
                            id="pf-position-table",
                            columns=[
                                {"name": "Account", "id": "account"},
                                {"name": "Symbol", "id": "underlying"},
                                {"name": "Strike", "id": "strike", "type": "numeric"},
                                {"name": "Expiry", "id": "expiry"},
                                {"name": "DTE", "id": "dte", "type": "numeric"},
                                {"name": "Entry", "id": "entry_premium", "type": "numeric"},
                                {"name": "P&L", "id": "pnl", "type": "numeric"},
                                {"name": "OTM%", "id": "otm_percent", "type": "numeric"},
                                {"name": "Yield%", "id": "annualized_yield", "type": "numeric"},
                                {"name": "Alert", "id": "risk_alert"},
                            ],
                            data=[],
                            sort_action="native",
                            sort_by=[{"column_id": "risk_alert", "direction": "asc"}],
                            style_header={
                                "backgroundColor": BG_CARD,
                                "color": "#f8fafc",
                                "fontWeight": "bold",
                                "border": "1px solid #334155",
                            },
                            style_cell={
                                "backgroundColor": BG_CARD,
                                "color": "#f8fafc",
                                "border": "1px solid #334155",
                                "textAlign": "center",
                            },
                            style_cell_conditional=[
                                {"if": {"column_id": "account"}, "textAlign": "left"},
                                {"if": {"column_id": "underlying"}, "textAlign": "left"},
                            ],
                            style_data_conditional=[
                                {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                            ],
                            page_size=25,
                            css=[{"selector": ".dash-spreadsheet", "rule": "font-family: monospace"}],
                        ),
                    ]
                ),
                style={"backgroundColor": BG_CARD, "border": "none"},
                className="mb-4",
            ),

            # Two-column bottom section
            dbc.Row(
                [
                    # Loss scenarios
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H5("Loss Scenarios", className="mb-3", style={"color": "#f8fafc"}),
                                    html.Div(id="pf-loss-scenarios"),
                                ]
                            ),
                            style={"backgroundColor": BG_CARD, "border": "none"},
                        ),
                        md=6,
                    ),
                    # Expiration calendar
                    dbc.Col(
                        dbc.Card(
                            dbc.CardBody(
                                [
                                    html.H5("Expiration Calendar", className="mb-3", style={"color": "#f8fafc"}),
                                    html.Div(id="pf-expiry-calendar"),
                                ]
                            ),
                            style={"backgroundColor": BG_CARD, "border": "none"},
                        ),
                        md=6,
                    ),
                ],
                className="mb-4",
            ),
        ],
    )


def _screener_tab() -> dbc.Tab:
    """Screener tab for put-selling opportunities."""
    return dbc.Tab(
        label="Screener",
        tab_id="tab-screener",
        children=[
            # Toolbar
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Button("Run Screener", id="run-screener-btn", color="primary"),
                        width="auto",
                    ),
                    dbc.Col(
                        dbc.Button("Refresh (Cache)", id="run-screener-cache-btn", color="secondary", outline=True),
                        width="auto",
                    ),
                    dbc.Col(
                        html.Span(id="screener-status", className="ms-2", style={"color": TEXT_MUTED}),
                        width="auto",
                        className="d-flex align-items-center",
                    ),
                ],
                className="mb-3",
            ),

            # Parameter controls
            dbc.Card(
                dbc.CardBody(
                    [
                        html.H6("Parameters", className="mb-3", style={"color": "#f8fafc"}),
                        dbc.Row(
                            [
                                dbc.Col(
                                    [
                                        html.Small("IV Pctl Min %", className="text-muted"),
                                        dbc.Input(id="sc-iv-percentile", type="number", value=30, min=0, max=100, step=5, size="sm"),
                                    ],
                                    width=2,
                                ),
                                dbc.Col(
                                    [
                                        html.Small("Yield Min %", className="text-muted"),
                                        dbc.Input(id="sc-premium-yield", type="number", value=0.5, min=0, step=0.1, size="sm"),
                                    ],
                                    width=2,
                                ),
                                dbc.Col(
                                    [
                                        html.Small("Min DTE", className="text-muted"),
                                        dbc.Input(id="sc-min-dte", type="number", value=14, min=1, step=1, size="sm"),
                                    ],
                                    width=2,
                                ),
                                dbc.Col(
                                    [
                                        html.Small("Max DTE", className="text-muted"),
                                        dbc.Input(id="sc-max-dte", type="number", value=30, min=1, step=1, size="sm"),
                                    ],
                                    width=2,
                                ),
                                dbc.Col(
                                    [
                                        html.Small("OTM Buffer %", className="text-muted"),
                                        dbc.Input(id="sc-otm-buffer", type="number", value=10, min=0, step=1, size="sm"),
                                    ],
                                    width=2,
                                ),
                            ],
                            className="g-2",
                        ),
                    ]
                ),
                style={"backgroundColor": "#253449", "border": "1px solid #334155"},
                className="mb-4",
            ),

            # Summary
            dbc.Row(
                [
                    dbc.Col(_summary_card("Scanned", "sc-card-scanned"), width=2),
                    dbc.Col(_summary_card("Passed Filter", "sc-card-passed"), width=2),
                    dbc.Col(_summary_card("Errors", "sc-card-errors"), width=2),
                ],
                className="mb-4 justify-content-center",
            ),

            # Results table
            dbc.Card(
                dbc.CardBody(
                    [
                        html.H5("Put Selling Opportunities", className="mb-3", style={"color": "#f8fafc"}),
                        dash_table.DataTable(
                            id="screener-table",
                            columns=[
                                {"name": "Symbol", "id": "symbol"},
                                {"name": "Price", "id": "current_price", "type": "numeric"},
                                {"name": "IV%", "id": "iv_display", "type": "numeric"},
                                {"name": "Strike", "id": "suggested_strike", "type": "numeric"},
                                {"name": "DTE", "id": "strike_dte", "type": "numeric"},
                                {"name": "Premium", "id": "premium", "type": "numeric"},
                                {"name": "Yield%", "id": "annualized_yield", "type": "numeric"},
                                {"name": "IV Pctl", "id": "iv_percentile", "type": "numeric"},
                                {"name": "Grade", "id": "grade"},
                                {"name": "Existing", "id": "existing"},
                            ],
                            data=[],
                            sort_action="native",
                            sort_by=[{"column_id": "grade", "direction": "asc"}],
                            style_header={
                                "backgroundColor": BG_CARD,
                                "color": "#f8fafc",
                                "fontWeight": "bold",
                                "border": "1px solid #334155",
                            },
                            style_cell={
                                "backgroundColor": BG_CARD,
                                "color": "#f8fafc",
                                "border": "1px solid #334155",
                                "textAlign": "center",
                            },
                            style_cell_conditional=[
                                {"if": {"column_id": "symbol"}, "textAlign": "left"},
                            ],
                            style_data_conditional=[
                                {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                            ],
                            page_size=25,
                            css=[{"selector": ".dash-spreadsheet", "rule": "font-family: monospace"}],
                        ),
                    ]
                ),
                style={"backgroundColor": BG_CARD, "border": "none"},
                className="mb-4",
            ),
        ],
    )


def create_layout() -> dbc.Container:
    """Create the complete dashboard layout."""
    return dbc.Container(
        [
            # Header
            html.H1(
                "IBKR Options Portfolio",
                className="my-4",
                style={"color": "#f8fafc", "fontWeight": "600"},
            ),

            # Account management + Download toolbar
            dbc.Card(
                dbc.CardBody(
                    [
                        dbc.Row(
                            [
                                dbc.Col(
                                    [
                                        html.Span("Accounts:", style={"color": TEXT_MUTED, "marginRight": "8px"}),
                                        dcc.Dropdown(
                                            id="account-select-dropdown",
                                            placeholder="Select to edit...",
                                            clearable=False,
                                            style={"width": "200px", "backgroundColor": BG_PRIMARY},
                                        ),
                                    ],
                                    md=4,
                                    className="d-flex align-items-center",
                                ),
                                dbc.Col(
                                    [
                                        dbc.Button("Download & Import", id="download-import-btn", color="primary", className="me-2"),
                                        dbc.Button("+ Add Account", id="add-account-btn", color="secondary", outline=True),
                                        html.Span(id="download-status", className="ms-2", style={"color": TEXT_MUTED}),
                                    ],
                                    md="auto",
                                    className="d-flex align-items-center",
                                ),
                            ],
                        ),
                        html.Div(
                            id="account-form-container",
                            style={"display": "none"},
                            className="mt-3 pt-3",
                            children=[
                                html.Hr(style={"borderColor": "#334155"}),
                                dbc.Row(
                                    [
                                        dbc.Col(dbc.Input(id="account-name-input", placeholder="Account Name", style={"backgroundColor": BG_PRIMARY, "color": "#f8fafc"}), md=3),
                                        dbc.Col(dbc.Input(id="account-token-input", placeholder="Flex Token", type="password", style={"backgroundColor": BG_PRIMARY, "color": "#f8fafc"}), md=3),
                                        dbc.Col(dbc.Input(id="account-query-id-input", placeholder="Flex Query ID", style={"backgroundColor": BG_PRIMARY, "color": "#f8fafc"}), md=3),
                                        dbc.Col(
                                            dbc.Checklist(id="account-enabled-check", options=[{"label": "Enabled", "value": 1}], value=[1], style={"color": "#f8fafc"}),
                                            md=2,
                                            className="d-flex align-items-center",
                                        ),
                                        dbc.Col(
                                            [
                                                dbc.Button("Save", id="save-account-btn", color="success", className="me-2"),
                                                dbc.Button("Delete", id="delete-account-btn", color="danger", outline=True, className="me-2", style={"display": "none"}),
                                                dbc.Button("Cancel", id="cancel-account-btn", color="secondary", outline=True),
                                            ],
                                            md=1,
                                            className="d-flex align-items-center",
                                        ),
                                    ],
                                ),
                                html.Span(id="account-form-status", className="mt-2", style={"fontSize": "0.85rem"}),
                            ],
                        ),
                    ]
                ),
                style={"backgroundColor": BG_CARD, "border": "none"},
                className="mb-4",
            ),

            # Data stores
            dcc.Store(id="portfolio-data", data={}),
            dcc.Store(id="exposure-data", data=[]),
            dcc.Store(id="calendar-data", data={}),
            dcc.Store(id="positions-data", data=[]),
            dcc.Store(id="filtered-positions-data", data=[]),
            dcc.Store(id="accounts-data", data=[]),
            dcc.Store(id="selected-position", data=None),
            dcc.Store(id="editing-account-id", data=None),
            dcc.Store(id="portfolio-review-data", data={}),
            dcc.Store(id="screener-data", data={}),

            # One-shot interval to trigger initial data load
            dcc.Interval(id="load-trigger", interval=500, max_intervals=1),

            # Main tabs: Positions, Portfolio, Screener
            dbc.Tabs(
                id="main-tabs",
                children=[
                    _positions_tab(),
                    _portfolio_tab(),
                    _screener_tab(),
                ],
                active_tab="tab-positions",
                className="mb-4",
            ),

            # Footer
            html.Div(
                html.P("IBKR Options Analyzer Dashboard", className="text-muted text-center mt-4"),
            ),
        ],
        fluid=True,
        style={"backgroundColor": BG_PRIMARY, "minHeight": "100vh"},
    )
