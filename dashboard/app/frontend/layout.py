"""Dash layout components for IBKR Options Dashboard."""

import dash_bootstrap_components as dbc
from dash import html, dcc, dash_table

# Risk category colors
RISK_COLORS = {
    "SAFE": "#22c55e",
    "MODERATE": "#fbbf24",
    "HIGH": "#f97316",
    "CRITICAL": "#f87171",
    "ITM": "#f87171",
    "NEAR": "#f97316",
    "EXPIRING": "#fbbf24",
}

# Theme colors (kept for callback references)
BG_PRIMARY = "#0f172a"
BG_CARD = "#1e293b"
TEXT_MUTED = "#94a3b8"


def _summary_card(title: str, value_id: str) -> dbc.Card:
    """Create a KPI metric card using CSS classes."""
    return dbc.Card(
        dbc.CardBody(
            [
                html.Div(title, className="kpi-label"),
                html.Div(id=value_id, children="--", className="kpi-value"),
            ],
            className="p-3",
        ),
        className="kpi-card",
    )


def _table_style(**overrides) -> dict:
    """Shared DataTable style configuration.

    Returns a dict suitable for spreading into DataTable(**_table_style(...)).
    Pass keyword overrides to replace or add keys (e.g. style_data_conditional).
    """
    config = {
        "style_header": {
            "backgroundColor": "#253449",
            "color": "#f8fafc",
            "fontWeight": "bold",
            "textAlign": "center",
            "border": "1px solid #334155",
        },
        "style_cell": {
            "backgroundColor": BG_CARD,
            "color": "#f8fafc",
            "border": "1px solid #334155",
            "textAlign": "center",
        },
        "style_data_conditional": [
            {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
        ],
    }
    config.update(overrides)
    return config


def _summary_tab() -> dbc.Tab:
    """Summary tab: consolidated P&L across trades and open positions."""
    return dbc.Tab(
        label="Summary",
        tab_id="tab-summary",
        children=[
            # Local filter: P&L toggle (only affects Summary)
            dbc.Row(
                [
                    dbc.Col(
                        dbc.Select(
                            id="sum-pnl-toggle",
                            options=[
                                {"label": "All P&L", "value": "all"},
                                {"label": "Realized Only", "value": "realized"},
                                {"label": "Unrealized Only", "value": "unrealized"},
                            ],
                            value="all",
                            style={"width": "150px"},
                        ),
                        width="auto",
                    ),
                ],
                className="mb-3 g-2",
            ),

            # Sub-tabs: P&L Analysis, Options, Stocks, Dividends
            dbc.Tabs(
                id="sum-subtabs",
                children=[
                    dbc.Tab(label="P&L Analysis", tab_id="pnl-analysis"),
                    dbc.Tab(label="Options", tab_id="options-ledger"),
                    dbc.Tab(label="Stocks", tab_id="stocks-ledger"),
                    dbc.Tab(label="Dividends", tab_id="dividends-ledger"),
                ],
                active_tab="pnl-analysis",
                className="mb-3",
            ),

            # P&L Analysis content (merged overview + charts)
            html.Div(id="sum-pnl-analysis-content", className="subtab-content", children=[
                dcc.Loading(type="circle", children=[
                    # Summary cards row
                    dbc.Row(
                        [
                            dbc.Col(_summary_card("Total P&L", "sum-card-total-pnl"), width=6, lg=2),
                            dbc.Col(_summary_card("Option Net", "sum-card-option-pnl"), width=6, lg=2),
                            dbc.Col(_summary_card("Stock Net", "sum-card-stock-pnl"), width=6, lg=2),
                            dbc.Col(_summary_card("Open Premium", "sum-card-wheel-pnl"), width=6, lg=2),
                            dbc.Col(_summary_card("Dividends", "sum-card-dividends"), width=6, lg=2),
                            dbc.Col(_summary_card("Margin Interest", "sum-card-interest"), width=6, lg=2),
                        ],
                        className="mb-4 justify-content-center",
                    ),

                    # Per-stock P&L table
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.Div([
                                    html.H5("P&L by Underlying", className="card-header-title mb-0"),
                                    dbc.Checkbox(
                                        id="sum-include-zero-positions",
                                        value=False,
                                        label="Include Zero Positions",
                                        style={"color": TEXT_MUTED, "fontSize": "0.85rem"},
                                        className="ms-3",
                                    ),
                                ], className="d-flex align-items-center mb-2"),
                                html.Div(id="sum-per-stock-table"),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                ]),

                # Charts section (from former P&L Chart sub-tab)
                dbc.Card(
                    dbc.CardBody(
                        [
                            html.Div([
                                html.H5("P&L Breakdown by Ticker", className="card-header-title mb-0"),
                                dbc.Checkbox(
                                    id="pnl-chart-include-closed",
                                    value=False,
                                    label="Include Zero Positions",
                                    style={"color": TEXT_MUTED, "fontSize": "0.85rem"},
                                    className="ms-3",
                                ),
                                dbc.Checkbox(
                                    id="pnl-chart-log-scale",
                                    value=False,
                                    label="Log Scale",
                                    style={"color": TEXT_MUTED, "fontSize": "0.85rem"},
                                    className="ms-3",
                                ),
                            ], className="d-flex align-items-center mb-3"),
                            dbc.Row([
                                dbc.Col(
                                    dcc.Graph(id="sum-pnl-winners-chart", style={"height": "600px"}),
                                    md=6,
                                ),
                                dbc.Col(
                                    dcc.Graph(id="sum-pnl-losers-chart", style={"height": "600px"}),
                                    md=6,
                                ),
                            ]),
                        ]
                    ),
                    className="content-card mb-4",
                ),
                dbc.Card(
                    dbc.CardBody(
                        [
                            html.H5("Net Liquidation Value", className="card-header-title"),
                            dcc.Graph(id="sum-nlv-pie-chart", style={"height": "500px"}),
                        ]
                    ),
                    className="content-card mb-4",
                ),
            ]),

            # Options ledger content
            html.Div(id="sum-options-content", className="subtab-content", style={"display": "none"}, children=[
                dcc.Loading(type="circle", children=[
                    dbc.Row(
                        [
                            dbc.Col(_summary_card("Total Trades", "sum-opt-card-total"), width=6, lg=True),
                            dbc.Col(_summary_card("Total Premium", "sum-opt-card-premium"), width=6, lg=True),
                            dbc.Col(_summary_card("Total P&L", "sum-opt-card-pnl"), width=6, lg=True),
                            dbc.Col(_summary_card("Win Rate", "sum-opt-card-winrate"), width=6, lg=True),
                            dbc.Col(_summary_card("Assignment Rate", "sum-opt-card-assign-rate"), width=6, lg=True),
                        ],
                        className="mb-4 justify-content-center",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Options by Symbol", className="card-header-title"),
                                html.Div(id="sum-options-symbol-table"),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Options Trades", className="card-header-title"),
                                dash_table.DataTable(
                                    id="sum-options-table",
                                    columns=[
                                        {"name": "Account", "id": "account"},
                                        {"name": "Underlying", "id": "underlying"},
                                        {"name": "Right", "id": "right"},
                                        {"name": "Strike", "id": "strike", "type": "numeric"},
                                        {"name": "Expiry", "id": "expiry"},
                                        {"name": "Qty", "id": "quantity", "type": "numeric"},
                                        {"name": "Open Date", "id": "open_date"},
                                        {"name": "Close Date", "id": "close_date"},
                                        {"name": "Premium", "id": "net_premium", "type": "numeric"},
                                        {"name": "P&L", "id": "realized_pnl", "type": "numeric"},
                                        {"name": "ROC%", "id": "roc", "type": "numeric"},
                                        {"name": "Days", "id": "holding_days", "type": "numeric"},
                                        {"name": "Reason", "id": "close_reason"},
                                    ],
                                    data=[],
                                    sort_action="native",
                                    sort_by=[{"column_id": "close_date", "direction": "desc"}],
                                    **_table_style(
                                        style_cell_conditional=[
                                            {"if": {"column_id": "account"}, "textAlign": "left"},
                                            {"if": {"column_id": "underlying"}, "textAlign": "left"},
                                        ],
                                        style_data_conditional=[
                                            {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                                            {"if": {"column_id": "realized_pnl", "filter_query": "{realized_pnl} < 0"}, "color": "#ef4444"},
                                            {"if": {"column_id": "realized_pnl", "filter_query": "{realized_pnl} > 0"}, "color": "#22c55e"},
                                        ],
                                    ),
                                    page_size=25,
                                ),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                ]),
            ]),

            # Stocks ledger content
            html.Div(id="sum-stocks-content", className="subtab-content", style={"display": "none"}, children=[
                dcc.Loading(type="circle", children=[
                    dbc.Row(
                        [
                            dbc.Col(_summary_card("Total Positions", "sum-stk-card-positions"), width=6, lg=True),
                            dbc.Col(_summary_card("Total Shares", "sum-stk-card-shares"), width=6, lg=True),
                            dbc.Col(_summary_card("Total Cost Basis", "sum-stk-card-cost"), width=6, lg=True),
                            dbc.Col(_summary_card("Unrealized P&L", "sum-stk-card-upnl"), width=6, lg=True),
                            dbc.Col(_summary_card("Realized P&L", "sum-stk-card-realized"), width=6, lg=True),
                        ],
                        className="mb-4 justify_content-center",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Current Holdings", className="card-header-title"),
                                dash_table.DataTable(
                                    id="sum-stocks-holdings-table",
                                    columns=[
                                        {"name": "Symbol", "id": "symbol"},
                                        {"name": "Account", "id": "account"},
                                        {"name": "Total Shares", "id": "total_shares", "type": "numeric"},
                                        {"name": "Avg Cost", "id": "avg_cost", "type": "numeric"},
                                        {"name": "Current Price", "id": "current_price", "type": "numeric"},
                                        {"name": "Unrealized P&L", "id": "unrealized_pnl", "type": "numeric"},
                                        {"name": "Days Held", "id": "days_held", "type": "numeric"},
                                    ],
                                    data=[],
                                    sort_action="native",
                                    **_table_style(
                                        style_data_conditional=[
                                            {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                                            {"if": {"column_id": "unrealized_pnl", "filter_query": "{unrealized_pnl} < 0"}, "color": "#ef4444"},
                                            {"if": {"column_id": "unrealized_pnl", "filter_query": "{unrealized_pnl} > 0"}, "color": "#22c55e"},
                                        ],
                                    ),
                                    page_size=25,
                                ),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Realized P&L", className="card-header-title"),
                                dash_table.DataTable(
                                    id="sum-stocks-realized-table",
                                    columns=[
                                        {"name": "Symbol", "id": "symbol"},
                                        {"name": "Account", "id": "account"},
                                        {"name": "Shares Sold", "id": "shares_sold", "type": "numeric"},
                                        {"name": "Avg Cost", "id": "avg_cost", "type": "numeric"},
                                        {"name": "Avg Sell", "id": "avg_sell", "type": "numeric"},
                                        {"name": "Realized P&L", "id": "realized_pnl", "type": "numeric"},
                                        {"name": "Return %", "id": "return_pct", "type": "numeric"},
                                    ],
                                    data=[],
                                    sort_action="native",
                                    sort_by=[{"column_id": "realized_pnl", "direction": "desc"}],
                                    **_table_style(
                                        style_data_conditional=[
                                            {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                                            {"if": {"column_id": "realized_pnl", "filter_query": "{realized_pnl} < 0"}, "color": "#ef4444"},
                                            {"if": {"column_id": "realized_pnl", "filter_query": "{realized_pnl} > 0"}, "color": "#22c55e"},
                                        ],
                                    ),
                                    page_size=25,
                                ),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Share Events", className="card-header-title"),
                                html.Div(id="sum-stocks-events-table"),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                ]),
            ]),

            # Dividends ledger content
            html.Div(id="sum-dividends-content", className="subtab-content", style={"display": "none"}, children=[
                dcc.Loading(type="circle", children=[
                    dbc.Row(
                        [
                            dbc.Col(_summary_card("Total Dividends", "sum-div-card-total"), width=6, md=3),
                            dbc.Col(_summary_card("Tax Withheld", "sum-div-card-tax"), width=6, md=3),
                            dbc.Col(_summary_card("Net Dividends", "sum-div-card-net"), width=6, md=3),
                            dbc.Col(_summary_card("Symbols", "sum-div-card-symbols"), width=6, md=3),
                        ],
                        className="mb-4 justify-content-center",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Dividend Income by Symbol", className="card-header-title"),
                                html.Div(id="sum-dividends-symbol-table"),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                    dbc.Card(
                        dbc.CardBody(
                            [
                                html.H5("Dividends", className="card-header-title"),
                                html.Div(id="sum-dividends-detail-table"),
                            ]
                        ),
                        className="content-card mb-4",
                    ),
                ]),
            ]),
        ],
    )


def _positions_tab() -> dbc.Tab:
    """The existing positions dashboard content."""
    return dbc.Tab(
        label="Positions",
        tab_id="tab-positions",
        children=[
            # Local filter: Risk only (market is global)
            dbc.Row(
                [
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
                    dbc.Col(_summary_card("Total Positions", "card-total-positions"), width=6, lg=True),
                    dbc.Col(_summary_card("Max Profit", "card-max-profit"), width=6, lg=True),
                    dbc.Col(_summary_card("Current Loss", "card-loss-current"), width=6, lg=True),
                    dbc.Col(_summary_card("5% Loss", "card-loss-5pct"), width=6, lg=True),
                    dbc.Col(_summary_card("Expiring This Week", "card-expiring-soon"), width=6, lg=True),
                ],
                className="mb-4 justify-content-center",
            ),

            dcc.Loading(type="circle", children=[
                # Charts row
                dbc.Row(
                    [
                        dbc.Col(
                            dbc.Card(
                                dbc.CardBody(
                                    [
                                        html.H5("Expiry Timeline", className="card-header-title"),
                                        dcc.Graph(id="expiry-timeline-chart"),
                                    ]
                                ),
                                className="content-card",
                            ),
                            md=7,
                        ),
                        dbc.Col(
                            dbc.Card(
                                dbc.CardBody(
                                    [
                                        html.H5("Top Exposure", className="card-header-title"),
                                        html.Div(id="exposure-table"),
                                        html.H5("Risk Distribution", className="card-header-title mt-4"),
                                        html.Div(id="risk-distribution"),
                                    ]
                                ),
                                className="content-card",
                            ),
                            md=5,
                        ),
                    ],
                    className="mb-4",
                ),

                # Expiry Calendar
                dbc.Card(
                    dbc.CardBody(
                        [
                            html.H5("Expiry Calendar", className="card-header-title"),
                            html.Div(id="expiry-calendar-table"),
                        ]
                    ),
                    className="content-card mb-4",
                ),

                # Position table
                dbc.Card(
                    dbc.CardBody(
                        [
                            html.H5("Positions", className="card-header-title"),
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
                                **_table_style(
                                    style_cell_conditional=[
                                        {"if": {"column_id": "underlying"}, "textAlign": "left"},
                                        {"if": {"column_id": "account_name"}, "textAlign": "left"},
                                    ],
                                ),
                                page_size=20,
                            ),
                            html.Div(id="position-detail", className="mt-4"),
                        ]
                    ),
                    className="content-card",
                ),
            ]),
        ],
    )


def _trade_review_tab() -> dbc.Tab:
    """Trade review tab for raw trade listing."""
    return dbc.Tab(
        label="Trade Review",
        tab_id="tab-trade-review",
        children=[
            # Overview KPI cards
            dbc.Row(
                [
                    dbc.Col(_summary_card("Total Trades", "tr-card-total"), width=6, lg=2),
                    dbc.Col(_summary_card("Premium In", "tr-card-win-rate"), width=6, lg=2),
                    dbc.Col(_summary_card("Premium Out", "tr-card-option-pnl"), width=6, lg=2),
                    dbc.Col(_summary_card("Net Premium", "tr-card-stock-pnl"), width=6, lg=2),
                    dbc.Col(_summary_card("Commissions", "tr-card-pnl"), width=6, lg=2),
                    dbc.Col(_summary_card("", "tr-card-profit-factor"), width=1, style={"display": "none"}),
                    dbc.Col(_summary_card("", "tr-card-avg-roc"), width=1, style={"display": "none"}),
                    dbc.Col(_summary_card("", "tr-card-standalone-loss"), width=2, style={"display": "none"}),
                ],
                className="mb-4 justify-content-center",
            ),

            # Sub-tabs: Options, Stocks
            dbc.Tabs(
                id="trade-subtabs",
                children=[
                    dbc.Tab(label="Options", tab_id="subtab-trades"),
                    dbc.Tab(label="Stocks", tab_id="subtab-strategy"),
                ],
                active_tab="subtab-trades",
                className="mb-3",
            ),

            # Options trades sub-tab
            html.Div(
                id="tr-subtab-trades",
                className="subtab-content",
                children=[
                    dcc.Loading(type="circle", children=[
                        html.H6("Options Trades", className="mb-2", style={"color": "#f8fafc"}),
                        dash_table.DataTable(
                            id="tr-trades-table",
                            columns=[
                                {"name": "Date", "id": "date"},
                                {"name": "Account", "id": "account"},
                                {"name": "Underlying", "id": "underlying"},
                                {"name": "Right", "id": "right"},
                                {"name": "Strike", "id": "strike", "type": "numeric"},
                                {"name": "Expiry", "id": "expiry"},
                                {"name": "Qty", "id": "quantity", "type": "numeric"},
                                {"name": "Price", "id": "trade_price", "type": "numeric"},
                                {"name": "Net Cash", "id": "net_cash", "type": "numeric"},
                                {"name": "Codes", "id": "notes_codes"},
                            ],
                            data=[],
                            sort_action="native",
                            sort_by=[{"column_id": "date", "direction": "desc"}],
                            **_table_style(
                                style_cell_conditional=[
                                    {"if": {"column_id": "account"}, "textAlign": "left"},
                                    {"if": {"column_id": "underlying"}, "textAlign": "left"},
                                ],
                            ),
                            page_size=25,
                        ),
                    ]),
                ],
            ),

            # Stock trades sub-tab
            html.Div(
                id="tr-subtab-strategy",
                className="subtab-content",
                style={"display": "none"},
                children=[
                    dcc.Loading(type="circle", children=[
                        html.H6("Stock Trades", className="mb-2", style={"color": "#f8fafc"}),
                        html.Div(id="tr-stock-table"),
                    ]),
                ],
            ),
        ],
    )


def create_layout() -> dbc.Container:
    """Create the complete dashboard layout."""
    return dbc.Container(
        [
            # Compact Header
            html.Div(
                [
                    html.H1("IBKR Options Portfolio"),
                    html.Span("Dashboard", className="header-subtitle"),
                ],
                className="dashboard-header",
            ),

            # Toolbar: Account management + actions + global filters
            dbc.Card(
                dbc.CardBody(
                    [
                        # Row 1: Account editing + action buttons
                        dbc.Row(
                            [
                                dbc.Col(
                                    [
                                        html.Span("Accounts:", style={"color": TEXT_MUTED, "marginRight": "8px"}),
                                        dcc.Dropdown(
                                            id="account-select-dropdown",
                                            placeholder="Select to edit...",
                                            clearable=False,
                                            style={"width": "200px"},
                                        ),
                                    ],
                                    md=4,
                                    className="d-flex align-items-center",
                                ),
                                dbc.Col(
                                    [
                                        dbc.Button("Refresh Prices", id="refresh-market-btn", color="secondary", outline=True, className="me-2"),
                                        dbc.Button("Download & Import", id="download-import-btn", color="primary", className="me-2"),
                                        dbc.Button("+ Add Account", id="add-account-btn", color="secondary", outline=True),
                                    ],
                                    md="auto",
                                    className="d-flex align-items-center",
                                ),
                            ],
                        ),

                        # Row 2: Global filters
                        html.Div(
                            className="global-filter-row mt-3 pt-3",
                            style={"borderTop": "1px solid #334155"},
                            children=[
                                html.Span("View:", style={"color": TEXT_MUTED, "fontSize": "0.85rem"}),
                                dbc.Select(
                                    id="global-account-filter",
                                    options=[{"label": "All Accounts", "value": "all"}],
                                    value="all",
                                    style={"width": "180px"},
                                ),
                                dbc.Select(
                                    id="global-market-filter",
                                    options=[
                                        {"label": "All Markets", "value": "All Markets"},
                                        {"label": "US", "value": "US"},
                                        {"label": "HK", "value": "HK"},
                                        {"label": "JP", "value": "JP"},
                                    ],
                                    value="All Markets",
                                    style={"width": "140px"},
                                ),
                                dbc.Select(
                                    id="global-year-filter",
                                    options=[{"label": "All Years", "value": "all"}],
                                    value="all",
                                    style={"width": "120px"},
                                ),
                            ],
                        ),
                    ]
                ),
                className="toolbar-card mb-4",
            ),

            # Account management modal
            dbc.Modal(
                id="account-modal",
                is_open=False,
                centered=True,
                children=[
                    dbc.ModalHeader(dbc.ModalTitle("Account Configuration")),
                    dbc.ModalBody(
                        dbc.Row(
                            [
                                dbc.Col(dbc.Input(id="account-name-input", placeholder="Account Name"), md=6),
                                dbc.Col(dbc.Input(id="account-ibkr-id-input", placeholder="IBKR Account ID (e.g. U5668308)"), md=6),
                                dbc.Col(dbc.Input(id="account-token-input", placeholder="Flex Token", type="password"), md=6),
                                dbc.Col(dbc.Input(id="account-query-id-input", placeholder="Flex Query ID"), md=6),
                                dbc.Col(
                                    dbc.Checklist(id="account-enabled-check", options=[{"label": "Enabled", "value": 1}], value=[1]),
                                    md=6,
                                    className="d-flex align-items-center",
                                ),
                            ],
                            className="g-3",
                        ),
                    ),
                    dbc.ModalFooter(
                        [
                            html.Span(id="account-form-status", className="me-auto", style={"fontSize": "0.85rem"}),
                            dbc.Button("Delete", id="delete-account-btn", color="danger", outline=True, className="me-2", style={"display": "none"}),
                            dbc.Button("Cancel", id="cancel-account-btn", color="secondary", outline=True, className="me-2"),
                            dbc.Button("Save", id="save-account-btn", color="success"),
                        ],
                    ),
                ],
            ),

            # Toast notification
            dbc.Toast(
                id="action-toast",
                header="",
                duration=4000,
                dismissable=True,
                is_open=False,
                className="action-toast",
            ),
            dcc.Store(id="toast-data", data={}),

            # Data stores
            dcc.Store(id="exposure-data", data=[]),
            dcc.Store(id="calendar-data", data={}),
            dcc.Store(id="positions-data", data=[]),
            dcc.Store(id="filtered-positions-data", data=[]),
            dcc.Store(id="accounts-data", data=[]),
            dcc.Store(id="selected-position", data=None),
            dcc.Store(id="editing-account-id", data=None),
            dcc.Store(id="trade-review-data", data={}),
            dcc.Store(id="tr-filtered-data", data={}),
            dcc.Store(id="sum-combined-data", data={}),

            # One-shot interval to trigger initial data load
            dcc.Interval(id="load-trigger", interval=500, max_intervals=1),

            # Main tabs: Summary, Positions, Trade Review
            dbc.Tabs(
                id="main-tabs",
                children=[
                    _summary_tab(),
                    _positions_tab(),
                    _trade_review_tab(),
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
