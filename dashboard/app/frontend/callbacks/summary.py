"""Summary tab callbacks — KPI cards, per-stock table, P&L chart, NLV pie chart."""

from datetime import datetime, date

import plotly.graph_objects as go
from dash import Dash, Output, Input, html, dash_table

from ...services.db import query
from ...services.holdings import compute_stock_holdings
from ...services.trade_repo import query_dividend_totals, query_dividend_totals_by_symbol, query_interest
from ._helpers import (
    _derive_market_from_symbol, _apply_year_filter,
    _FX_SYMBOLS,
)
from ..layout import RISK_COLORS, BG_CARD, TEXT_MUTED, _table_style


def register(app: Dash) -> None:
    """Register all summary tab callbacks with the Dash app."""

    def _pnl_color(val):
        return RISK_COLORS["SAFE"] if val >= 0 else RISK_COLORS["CRITICAL"]

    def _pnl_str_footer(val):
        return html.Span(f"${val:,.0f}", style={"color": _pnl_color(val)})

    @app.callback(
        Output("sum-combined-data", "data"),
        [
            Input("trade-review-data", "data"),
            Input("positions-data", "data"),
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
            Input("sum-pnl-toggle", "value"),
        ],
    )
    def apply_sum_filters(trade_data, positions, active_account, market, year, pnl_toggle):
        """Combine trade review and open positions data, filtered by account/market/year/pnl-toggle."""
        per_stock: dict[str, dict] = {}

        if trade_data:
            account = None if active_account == "all" else active_account

            # 1. Option trades: group by underlying, sum net_cash
            option_trades = trade_data.get("option_trades", [])
            if account:
                option_trades = [t for t in option_trades if t.get("account") == account]
            if market and market != "All Markets":
                option_trades = [t for t in option_trades if _derive_market_from_symbol(t.get("underlying", "")) == market]
            option_trades = _apply_year_filter(option_trades, year, "date")

            for t in option_trades:
                ul = t.get("underlying", "")
                if ul not in per_stock:
                    per_stock[ul] = {"underlying": ul, "market": _derive_market_from_symbol(ul), "trades": 0, "open_premium": 0.0, "net_premium": 0.0, "trades_list": []}
                per_stock[ul]["trades"] += 1
                per_stock[ul]["net_premium"] += t.get("net_cash", 0) or 0
                per_stock[ul]["trades_list"].append(t)

            # 2. Stock trades: ensure symbols appear in per_stock for row generation
            stock_trades = trade_data.get("stock_trades", [])
            if account:
                stock_trades = [t for t in stock_trades if t.get("account") == account]
            if market and market != "All Markets":
                stock_trades = [t for t in stock_trades if _derive_market_from_symbol(t.get("symbol", "")) == market]
            stock_trades = _apply_year_filter(stock_trades, year, "date")

            for t in stock_trades:
                sym = t.get("symbol", "")
                if sym not in per_stock:
                    per_stock[sym] = {"underlying": sym, "market": _derive_market_from_symbol(sym), "trades": 0, "open_premium": 0.0, "net_premium": 0.0, "trades_list": []}

        # Fetch holdings and realized P&L from DB
        account_h = None if active_account == "all" else active_account
        holdings, realized_pnl_db = compute_stock_holdings(account_h, market, year)

        # Query dividends from DB via trade_repo
        total_dividends, div_by_symbol = query_dividend_totals(account_h, year)

        # Query interest from DB via trade_repo
        total_interest = query_interest(account_h, year)

        # Process open positions (potential P&L)
        if positions:
            filtered_positions = positions
            account = None if active_account == "all" else active_account

            if account:
                filtered_positions = [p for p in filtered_positions if p.get("account_name") == account]
            if market and market != "All Markets":
                filtered_positions = [p for p in filtered_positions if p.get("market") == market]

            for pos in filtered_positions:
                ul = pos.get("underlying", "")
                premium = pos.get("premium", 0) or 0
                quantity = abs(pos.get("quantity", 0) or 0)
                multiplier = int(pos.get("multiplier") or 100)
                open_prem = premium * multiplier * quantity

                if ul not in per_stock:
                    per_stock[ul] = {"underlying": ul, "market": pos.get("market", _derive_market_from_symbol(ul)), "trades": 0, "open_premium": 0.0, "net_premium": 0.0, "trades_list": []}
                per_stock[ul]["open_premium"] += open_prem

        # Add rows for symbols with realized P&L that aren't already in per_stock
        for ul in realized_pnl_db:
            if ul not in per_stock:
                per_stock[ul] = {"underlying": ul, "market": _derive_market_from_symbol(ul), "trades": 0, "open_premium": 0.0, "net_premium": 0.0, "trades_list": []}

        # Build rows sorted by total_pnl descending
        rows = []
        for ul in per_stock.keys():
            d = per_stock[ul]
            # Realized P&L from stock sells (avg-cost method)
            stock_realized = realized_pnl_db.get(ul, 0)
            # Unrealized P&L from current holdings
            stock_unrealized = 0.0
            position = 0
            h = holdings.get(ul)
            if h and h["shares"] > 0:
                position = h["shares"]
                if h["current_price_usd"] > 0:
                    stock_unrealized = (h["current_price_usd"] - h["avg_cost_usd"]) * h["shares"]
            stock_total = stock_realized + stock_unrealized

            # Apply P&L toggle filter
            if pnl_toggle == "realized":
                total = d["net_premium"] + stock_realized
                rows.append({
                    "underlying": ul, "market": d["market"], "trades": d["trades"],
                    "position": position,
                    "open_premium": 0.0,
                    "option_pnl": round(d["net_premium"], 2),
                    "stock_pnl": round(stock_realized, 2),
                    "total_pnl": round(total, 2),
                })
            elif pnl_toggle == "unrealized":
                stock_unr = stock_unrealized + d["open_premium"]
                rows.append({
                    "underlying": ul, "market": d["market"], "trades": 0,
                    "position": position,
                    "open_premium": round(d["open_premium"], 2),
                    "option_pnl": 0.0,
                    "stock_pnl": round(stock_unrealized, 2),
                    "total_pnl": round(stock_unr, 2),
                })
            else:
                total = d["net_premium"] + stock_total + d["open_premium"]
                rows.append({
                    "underlying": ul,
                    "market": d["market"],
                    "trades": d["trades"],
                    "position": position,
                    "open_premium": round(d["open_premium"], 2),
                    "option_pnl": round(d["net_premium"], 2),
                    "stock_pnl": round(stock_total, 2),
                    "total_pnl": round(total, 2),
                })

        # Sort by total_pnl descending
        rows.sort(key=lambda r: r["total_pnl"], reverse=True)

        totals = {
            "option_pnl": round(sum(r["option_pnl"] for r in rows), 2),
            "stock_pnl": round(sum(r["stock_pnl"] for r in rows), 2),
            "open_premium": round(sum(r["open_premium"] for r in rows), 2),
        # Dividends and interest are queried separately (not in per_stock rows).
        # They are added to the grand total here, and also added per-row in the
        # table rendering via div_by_symbol. This is not double-counting:
        # the totals dict is the source of truth for the footer, while per-row
        # dividends come from div_by_symbol (a separate DB query).
        "total_pnl": round(sum(r["total_pnl"] for r in rows) + total_dividends + total_interest, 2),
            "dividend_income": round(total_dividends, 2),
            "interest_expense": round(total_interest, 2),
        }

        return {
            "rows": rows,
            "totals": totals,
            "filters": {"account": active_account, "market": market, "year": year},
        }

    @app.callback(
        [
            Output("sum-card-total-pnl", "children"),
            Output("sum-card-option-pnl", "children"),
            Output("sum-card-stock-pnl", "children"),
            Output("sum-card-wheel-pnl", "children"),
            Output("sum-card-dividends", "children"),
            Output("sum-card-interest", "children"),
        ],
        Input("sum-combined-data", "data"),
    )
    def update_sum_cards(data):
        """Update summary tab KPI cards."""
        if not data or not data.get("totals"):
            return "--", "--", "--", "--", "--", "--"
        t = data["totals"]

        def _fmt(val):
            color = RISK_COLORS["SAFE"] if val >= 0 else RISK_COLORS["CRITICAL"]
            return html.Span(f"${val:,.0f}", style={"color": color})

        return _fmt(t["total_pnl"]), _fmt(t["option_pnl"]), _fmt(t["stock_pnl"]), _fmt(t["open_premium"]), _fmt(t["dividend_income"]), _fmt(t["interest_expense"])

    @app.callback(
        Output("sum-per-stock-table", "children"),
        [
            Input("sum-combined-data", "data"),
            Input("sum-include-zero-positions", "value"),
        ],
    )
    def update_sum_table(data, include_zero):
        """Render per-stock P&L summary table with sortable columns."""
        if not data or not data.get("rows"):
            return html.Div("No data", style={"color": "#94a3b8"})

        rows = data["rows"]
        totals = data.get("totals", {})

        # Filter out zero-position rows unless checkbox is checked
        if not include_zero:
            rows = [r for r in rows if r.get("position", 0) != 0]

        # Build dividend lookup from DB (re-query to get per-symbol, respecting year filter)
        div_by_symbol = {}
        try:
            filters = data.get("filters", {})
            year = filters.get("year")
            div_by_symbol = query_dividend_totals_by_symbol(year)
        except Exception:
            pass

        # Enrich rows with dividends and compute total
        table_data = []
        for r in rows:
            ul = r["underlying"]
            div_amt = div_by_symbol.get(ul, 0)
            total = r["total_pnl"] + div_amt
            table_data.append({
                "underlying": ul,
                "market": r.get("market", ""),
                "trades": r.get("trades", 0),
                "position": r.get("position", 0),
                "open_premium": r.get("open_premium", 0),
                "option_pnl": r.get("option_pnl", 0),
                "stock_pnl": r.get("stock_pnl", 0),
                "dividends": div_amt,
                "total_pnl": total,
            })

        # Totals footer row
        interest = totals.get("interest_expense", 0)
        grand_total = totals.get("total_pnl", 0)
        footer_style = {"fontWeight": "bold", "borderTop": "2px solid #475569", "fontSize": "0.85rem"}
        footer_div = html.Div([
            html.Table([
                html.Tbody([
                    html.Tr([
                        html.Td(html.Strong("TOTAL"), style={**footer_style, "width": "15%", "textAlign": "left"}),
                        html.Td("", style={**footer_style, "width": "7%"}),
                        html.Td("", style={**footer_style, "width": "6%"}),
                        html.Td("", style={**footer_style, "width": "8%"}),
                        html.Td(_pnl_str_footer(totals.get("open_premium", 0)), style={**footer_style, "width": "11%"}),
                        html.Td(_pnl_str_footer(totals.get("option_pnl", 0)), style={**footer_style, "width": "11%"}),
                        html.Td(_pnl_str_footer(totals.get("stock_pnl", 0)), style={**footer_style, "width": "11%"}),
                        html.Td(_pnl_str_footer(totals.get("dividend_income", 0)), style={**footer_style, "width": "10%"}),
                        html.Td(_pnl_str_footer(grand_total), style={**footer_style, "width": "11%"}),
                    ]),
                ]),
            ], style={"width": "100%", "borderCollapse": "collapse", "marginTop": "4px"}),
        ], style={"padding": "4px 8px", "backgroundColor": "#1e293b", "borderRadius": "0 0 4px 4px"})

        if interest != 0:
            footer_div.children[0].children[0].children.append(html.Tr([
                html.Td(html.Strong("Margin Interest"), style={"textAlign": "left", "padding": "2px 8px", "color": "#f8fafc", "fontStyle": "italic", "fontSize": "0.85rem"}),
                html.Td("", colSpan=6),
                html.Td(_pnl_str_footer(interest), style={"padding": "2px 8px", "fontSize": "0.85rem"}),
            ]))

        dt = dash_table.DataTable(
            columns=[
                {"name": "Underlying", "id": "underlying"},
                {"name": "Market", "id": "market"},
                {"name": "Trades", "id": "trades", "type": "numeric"},
                {"name": "Position", "id": "position", "type": "numeric", "format": {"specifier": ",.2f"}},
                {"name": "Open Premium", "id": "open_premium", "type": "numeric", "format": {"specifier": "$,.0f"}},
                {"name": "Option Net", "id": "option_pnl", "type": "numeric", "format": {"specifier": "$,.0f"}},
                {"name": "Stock Net", "id": "stock_pnl", "type": "numeric", "format": {"specifier": "$,.0f"}},
                {"name": "Dividends", "id": "dividends", "type": "numeric", "format": {"specifier": "$,.0f"}},
                {"name": "Total P&L", "id": "total_pnl", "type": "numeric", "format": {"specifier": "$,.0f"}},
            ],
            data=table_data,
            sort_action="native",
            sort_by=[{"column_id": "total_pnl", "direction": "desc"}],
            **_table_style(
                style_cell_conditional=[
                    {"if": {"column_id": "underlying"}, "textAlign": "left", "fontWeight": "bold"},
                ],
                style_data_conditional=[
                    {"if": {"row_index": "odd"}, "backgroundColor": "#253449"},
                    {"if": {"column_id": "open_premium", "filter_query": "{open_premium} < 0"}, "color": "#ef4444"},
                    {"if": {"column_id": "open_premium", "filter_query": "{open_premium} > 0"}, "color": "#22c55e"},
                    {"if": {"column_id": "option_pnl", "filter_query": "{option_pnl} < 0"}, "color": "#ef4444"},
                    {"if": {"column_id": "option_pnl", "filter_query": "{option_pnl} > 0"}, "color": "#22c55e"},
                    {"if": {"column_id": "stock_pnl", "filter_query": "{stock_pnl} < 0"}, "color": "#ef4444"},
                    {"if": {"column_id": "stock_pnl", "filter_query": "{stock_pnl} > 0"}, "color": "#22c55e"},
                    {"if": {"column_id": "dividends", "filter_query": "{dividends} < 0"}, "color": "#ef4444"},
                    {"if": {"column_id": "dividends", "filter_query": "{dividends} > 0"}, "color": "#22c55e"},
                    {"if": {"column_id": "total_pnl", "filter_query": "{total_pnl} < 0"}, "color": "#ef4444"},
                    {"if": {"column_id": "total_pnl", "filter_query": "{total_pnl} > 0"}, "color": "#22c55e"},
                ],
            ),
            page_size=50,
        )

        return html.Div([dt, footer_div])

    @app.callback(
        [
            Output("sum-pnl-winners-chart", "figure"),
            Output("sum-pnl-losers-chart", "figure"),
        ],
        [
            Input("sum-combined-data", "data"),
            Input("pnl-chart-include-closed", "value"),
            Input("pnl-chart-log-scale", "value"),
        ],
    )
    def update_pnl_breakdown_chart(data, include_closed, log_scale):
        """Two side-by-side stacked bar charts: Top Winners and Top Losers."""
        if not data or not data.get("rows"):
            return go.Figure(), go.Figure()

        rows = data["rows"]

        # Filter out FX-like symbols
        rows = [r for r in rows if r["underlying"] not in _FX_SYMBOLS]

        # Filter out 0-position tickers unless checkbox is checked
        if not include_closed:
            filters = data.get("filters", {})
            account = filters.get("account")
            market = filters.get("market")
            year = filters.get("year")
            account_h = None if account == "all" else account
            holdings, _ = compute_stock_holdings(account_h, market, year)
            # Keep tickers with stock position only (option positions don't count)
            rows = [r for r in rows if r["underlying"] in holdings]

        if not rows:
            return go.Figure(), go.Figure()

        # Sort by total_pnl descending
        rows_sorted = sorted(rows, key=lambda r: r["total_pnl"], reverse=True)

        winners = [r for r in rows_sorted if r["total_pnl"] >= 0]
        losers = [r for r in rows_sorted if r["total_pnl"] < 0]

        def _make_fig(title, subset, y_color):
            if not subset:
                return go.Figure().update_layout(
                    title=dict(text=title, font=dict(color="#f8fafc", size=14)),
                    plot_bgcolor=BG_CARD, paper_bgcolor=BG_CARD,
                    font_color=TEXT_MUTED, height=600,
                    margin=dict(l=60, r=20, t=40, b=80),
                    xaxis=dict(tickangle=-45),
                )
            tickers = [r["underlying"] for r in subset]
            opts = [r["option_pnl"] for r in subset]
            stocks = [r["stock_pnl"] for r in subset]
            fig = go.Figure()
            fig.add_trace(go.Bar(
                name="Option P/L", x=tickers, y=opts,
                marker_color="#636EFA", marker_line_width=0,
            ))
            fig.add_trace(go.Bar(
                name="Stock P/L", x=tickers, y=stocks,
                marker_color="#FECB52", marker_line_width=0,
            ))
            fig.update_layout(
                title=dict(text=title, font=dict(color="#f8fafc", size=14)),
                barmode="relative",
                plot_bgcolor=BG_CARD,
                paper_bgcolor=BG_CARD,
                font_color=TEXT_MUTED,
                height=600,
                margin=dict(l=60, r=20, t=40, b=80),
                legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1),
                bargap=0.15,
                hovermode="x unified",
                xaxis=dict(tickangle=-45),
                yaxis=dict(title="USD ($)", type="log" if log_scale else "linear", gridcolor="#334155"),
            )
            return fig

        winners_fig = _make_fig(
            f"Top Winners ({len(winners)})", winners, RISK_COLORS["SAFE"],
        )
        losers_fig = _make_fig(
            f"Top Losers ({len(losers)})", losers, RISK_COLORS["CRITICAL"],
        )
        return winners_fig, losers_fig

    @app.callback(
        Output("sum-nlv-pie-chart", "figure"),
        Input("sum-combined-data", "data"),
    )
    def update_nlv_pie_chart(data):
        """Donut chart: Net Liquidation Value by ticker from holdings."""
        if not data or not data.get("totals"):
            return go.Figure()

        # Respect account, market, and year filters from summary tab
        filters = data.get("filters", {})
        account = filters.get("account")
        market = filters.get("market")
        year = filters.get("year")
        account_h = None if account == "all" else account
        holdings, _ = compute_stock_holdings(account_h, market, year)

        tickers, nlvs = [], []
        for symbol, h in holdings.items():
            if h["shares"] > 0 and h["current_price_usd"] > 0:
                nlv = h["current_price_usd"] * h["shares"]
                if nlv > 0:
                    tickers.append(symbol)
                    nlvs.append(nlv)

        if not tickers:
            return go.Figure()

        fig = go.Figure(go.Pie(
            labels=tickers, values=nlvs,
            textinfo="label+percent", textposition="inside",
            hole=0.35,
            marker=dict(line=dict(color=BG_CARD, width=1)),
        ))
        fig.update_layout(
            plot_bgcolor=BG_CARD,
            paper_bgcolor=BG_CARD,
            font_color=TEXT_MUTED,
            height=500,
            margin=dict(l=20, r=20, t=30, b=20),
            legend=dict(orientation="h", yanchor="bottom", y=-0.15, xanchor="center", x=0.5),
            showlegend=True,
        )
        return fig

