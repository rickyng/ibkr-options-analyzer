"""Summary sub-tab callbacks — toggle, options ledger, stocks ledger, dividends ledger."""

from datetime import datetime, date

from dash import Dash, Output, Input, html
import dash_bootstrap_components as dbc

from ...services.db import query
from ...services.holdings import compute_stock_holdings
from ...services.trade_repo import query_share_events, query_dividends, query_stock_sell_trades
from ._helpers import _derive_market_from_symbol, _apply_year_filter
from ..layout import RISK_COLORS, BG_CARD, TEXT_MUTED


def register(app: Dash) -> None:
    """Register all summary sub-tab callbacks with the Dash app."""

    @app.callback(
        [
            Output("sum-pnl-analysis-content", "style"),
            Output("sum-options-content", "style"),
            Output("sum-stocks-content", "style"),
            Output("sum-dividends-content", "style"),
        ],
        Input("sum-subtabs", "active_tab"),
    )
    def toggle_sum_subtabs(active_tab):
        """Toggle visibility between summary sub-tabs."""
        show = {"display": "block"}
        hide = {"display": "none"}
        states = [hide, hide, hide, hide]
        tab_map = {
            "pnl-analysis": 0,
            "options-ledger": 1,
            "stocks-ledger": 2,
            "dividends-ledger": 3,
        }
        idx = tab_map.get(active_tab, 0)
        states[idx] = show
        return states[0], states[1], states[2], states[3]

    # --- Options ledger sub-tab callbacks ---

    @app.callback(
        [
            Output("sum-opt-card-total", "children"),
            Output("sum-opt-card-premium", "children"),
            Output("sum-opt-card-pnl", "children"),
            Output("sum-opt-card-winrate", "children"),
            Output("sum-opt-card-assign-rate", "children"),
        ],
        [
            Input("trade-review-data", "data"),
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
        ],
    )
    def update_options_kpi_cards(trade_data, active_account, market, year):
        """Update Options sub-tab KPI cards."""
        if not trade_data:
            return "--", "--", "--", "--", "--"

        option_trades = trade_data.get("option_trades", [])
        account = None if active_account == "all" else active_account

        if account:
            option_trades = [t for t in option_trades if t.get("account") == account]
        if market and market != "All Markets":
            option_trades = [t for t in option_trades if _derive_market_from_symbol(t.get("underlying", "")) == market]
        option_trades = _apply_year_filter(option_trades, year, "date")

        total = len(option_trades)
        if total == 0:
            return "0", "$0", "$0", "--", "--"

        total_premium = sum(t.get("trade_price", 0) * abs(t.get("quantity", 0)) * t.get("multiplier", 100) for t in option_trades if t.get("trade_price"))
        total_pnl = sum(t.get("net_cash", 0) or 0 for t in option_trades)
        winners = sum(1 for t in option_trades if (t.get("net_cash", 0) or 0) > 0)
        assigned = sum(1 for t in option_trades if "A" in (t.get("notes_codes", "") or ""))

        win_rate = f"{winners / total:.0%}"
        assign_rate = f"{assigned / total:.0%}"

        pnl_color = RISK_COLORS["SAFE"] if total_pnl >= 0 else RISK_COLORS["CRITICAL"]

        return (
            str(total),
            f"${total_premium:,.0f}",
            html.Span(f"${total_pnl:,.0f}", style={"color": pnl_color}),
            win_rate,
            assign_rate,
        )

    @app.callback(
        Output("sum-options-table", "data"),
        [
            Input("trade-review-data", "data"),
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
        ],
    )
    def update_options_table(trade_data, active_account, market, year):
        """Populate options trades DataTable."""
        if not trade_data:
            return []

        option_trades = trade_data.get("option_trades", [])
        account = None if active_account == "all" else active_account

        if account:
            option_trades = [t for t in option_trades if t.get("account") == account]
        if market and market != "All Markets":
            option_trades = [t for t in option_trades if _derive_market_from_symbol(t.get("underlying", "")) == market]
        option_trades = _apply_year_filter(option_trades, year, "date")

        rows = []
        for t in option_trades:
            rows.append({
                "account": t.get("account", ""),
                "underlying": t.get("underlying", ""),
                "right": t.get("right", ""),
                "strike": t.get("strike", 0),
                "expiry": t.get("expiry", ""),
                "quantity": t.get("quantity", 0),
                "open_date": t.get("date", ""),
                "close_date": "",
                "net_premium": t.get("net_cash", 0),
                "realized_pnl": t.get("net_cash", 0),
                "roc": 0,
                "holding_days": 0,
                "close_reason": "",
            })

        return rows

    @app.callback(
        Output("sum-options-symbol-table", "children"),
        [
            Input("trade-review-data", "data"),
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
        ],
    )
    def update_options_symbol_table(trade_data, active_account, market, year):
        """Options aggregated by underlying: trades, premium, P&L, win rate."""
        if not trade_data:
            return html.P("No data", className="text-muted")

        option_trades = trade_data.get("option_trades", [])
        account = None if active_account == "all" else active_account

        if account:
            option_trades = [t for t in option_trades if t.get("account") == account]
        if market and market != "All Markets":
            option_trades = [t for t in option_trades if _derive_market_from_symbol(t.get("underlying", "")) == market]
        option_trades = _apply_year_filter(option_trades, year, "date")

        if not option_trades:
            return html.P("No trades", className="text-muted")

        # Aggregate by underlying
        sym_data: dict[str, dict] = {}
        for t in option_trades:
            ul = t.get("underlying", "")
            if ul not in sym_data:
                sym_data[ul] = {"count": 0, "wins": 0, "premium": 0.0, "pnl": 0.0}
            d = sym_data[ul]
            d["count"] += 1
            pnl = t.get("net_cash", 0) or 0
            d["pnl"] += pnl
            d["premium"] += t.get("trade_price", 0) * abs(t.get("quantity", 0)) * t.get("multiplier", 100) if t.get("trade_price") else 0
            if pnl > 0:
                d["wins"] += 1

        rows_html = []
        for i, (ul, d) in enumerate(sorted(sym_data.items(), key=lambda x: x[1]["pnl"], reverse=True)):
            bg = BG_CARD if i % 2 == 0 else "#253449"
            wr = d["wins"] / d["count"] * 100 if d["count"] else 0
            pnl_color = RISK_COLORS["SAFE"] if d["pnl"] >= 0 else RISK_COLORS["CRITICAL"]
            rows_html.append(html.Tr([
                html.Td(ul, style={"backgroundColor": bg, "textAlign": "left", "padding": "4px 8px", "fontWeight": "bold"}),
                html.Td(str(d["count"]), style={"backgroundColor": bg, "padding": "4px 8px"}),
                html.Td(f"${d['premium']:,.0f}", style={"backgroundColor": bg, "padding": "4px 8px"}),
                html.Td(f"${d['pnl']:,.0f}", style={"backgroundColor": bg, "padding": "4px 8px", "color": pnl_color}),
                html.Td(f"{wr:.0f}%", style={"backgroundColor": bg, "padding": "4px 8px"}),
            ]))

        header = html.Thead(html.Tr([
            html.Th("Symbol", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc", "padding": "4px 8px"}),
            html.Th("Trades", style={"backgroundColor": "#253449", "color": "#f8fafc", "padding": "4px 8px"}),
            html.Th("Premium", style={"backgroundColor": "#253449", "color": "#f8fafc", "padding": "4px 8px"}),
            html.Th("P&L", style={"backgroundColor": "#253449", "color": "#f8fafc", "padding": "4px 8px"}),
            html.Th("Win Rate", style={"backgroundColor": "#253449", "color": "#f8fafc", "padding": "4px 8px"}),
        ]))

        return dbc.Table([header, html.Tbody(rows_html)], bordered=True, className="mb-0",
                         style={"fontSize": "0.85rem"})

    # --- Stocks ledger sub-tab callbacks ---

    @app.callback(
        [
            Output("sum-stk-card-positions", "children"),
            Output("sum-stk-card-shares", "children"),
            Output("sum-stk-card-cost", "children"),
            Output("sum-stk-card-upnl", "children"),
            Output("sum-stk-card-realized", "children"),
            Output("sum-stocks-events-table", "children"),
            Output("sum-stocks-holdings-table", "data"),
            Output("sum-stocks-realized-table", "data"),
        ],
        [
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
        ],
    )
    def update_stocks_ledger(active_account, market, year):
        """Populate Stocks sub-tab with share events, current holdings, and realized P&L."""
        account = None if active_account == "all" else active_account

        # Fetch share events via trade_repo
        events = query_share_events(account, market, year)

        # Build events table (empty message if no events)
        if not events:
            events_table = html.P("No share events found", className="text-muted")
        else:
            header = html.Thead(html.Tr([
                html.Th("Account", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Symbol", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Event Date", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Event Type", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Shares", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Strike Price", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Premium Credit", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
                html.Th("Split Adj", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            ]))
            event_rows = []
            for i, e in enumerate(events):
                bg = BG_CARD if i % 2 == 0 else "#253449"
                event_type_label = "Assigned Put" if e.get("event_type") == "put_assigned" else "Called Away"
                split_adj = "Yes" if e.get("split_adjusted") else "No"
                event_rows.append(html.Tr([
                    html.Td(e.get("account", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc"}),
                    html.Td(e.get("underlying", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc"}),
                    html.Td(e.get("event_date", ""), style={"backgroundColor": bg, "color": "#f8fafc"}),
                    html.Td(event_type_label, style={"backgroundColor": bg, "color": "#f8fafc"}),
                    html.Td(str(e.get("shares", 0)), style={"backgroundColor": bg, "color": "#f8fafc"}),
                    html.Td(f"${e.get('strike', 0):,.2f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                    html.Td(f"${e.get('premium', 0):,.2f}", style={"backgroundColor": bg, "color": RISK_COLORS["SAFE"]}),
                    html.Td(split_adj, style={"backgroundColor": bg, "color": "#f8fafc"}),
                ]))
            events_table = dbc.Table(
                [header, html.Tbody(event_rows)],
                bordered=True,
                className="mb-0",
                style={"backgroundColor": BG_CARD, "fontSize": "0.85rem"},
            )

        # Compute current holdings and realized P&L
        holdings, realized_pnl = compute_stock_holdings(account, market, year)

        # Build holdings table rows
        holdings_rows = []
        for symbol, h in holdings.items():
            unrealized_pnl = (h["current_price_usd"] - h["avg_cost_usd"]) * h["shares"] if h["current_price_usd"] else 0

            # Days held
            days_held = 0
            if h["earliest_date"]:
                try:
                    earliest = datetime.strptime(h["earliest_date"][:10], "%Y-%m-%d").date()
                    days_held = (date.today() - earliest).days
                except (ValueError, TypeError):
                    pass

            holdings_rows.append({
                "symbol": symbol,
                "account": h["account"],
                "total_shares": h["shares"],
                "avg_cost": round(h["avg_cost_usd"], 2),
                "current_price": round(h["current_price_usd"], 2) if h["current_price_usd"] else 0,
                "unrealized_pnl": round(unrealized_pnl, 0),
                "days_held": days_held,
            })

        # Build realized P&L table from sell trades via trade_repo
        realized_rows = []
        sell_trades = query_stock_sell_trades(account, market, year)
        for row in sell_trades:
            sym = row["symbol"]
            shares_sold = row["shares_sold"] or 0
            total_proceeds = row["total_proceeds"] or 0
            rpnl = realized_pnl.get(sym, 0)
            avg_sell = total_proceeds / shares_sold if shares_sold > 0 else 0
            # Cost basis for sold shares = proceeds - realized P&L
            cost_basis = total_proceeds - rpnl
            avg_cost_val = cost_basis / shares_sold if shares_sold > 0 else 0
            return_pct = (rpnl / abs(cost_basis) * 100) if abs(cost_basis) > 0.01 else 0
            realized_rows.append({
                "symbol": sym,
                "account": row["account"],
                "shares_sold": shares_sold,
                "avg_cost": round(avg_cost_val, 2),
                "avg_sell": round(avg_sell, 2),
                "realized_pnl": round(rpnl, 2),
                "return_pct": round(return_pct, 1),
            })

        # Compute KPIs
        total_positions = len(holdings_rows)
        total_shares = sum(h["total_shares"] for h in holdings_rows)
        total_cost = sum(h["avg_cost"] * h["total_shares"] for h in holdings_rows)
        total_upnl = sum(h["unrealized_pnl"] for h in holdings_rows)
        upnl_color = RISK_COLORS["SAFE"] if total_upnl >= 0 else RISK_COLORS["CRITICAL"]
        total_rpnl = sum(r["realized_pnl"] for r in realized_rows)
        rpnl_color = RISK_COLORS["SAFE"] if total_rpnl >= 0 else RISK_COLORS["CRITICAL"]

        return (
            str(total_positions),
            f"{total_shares:,.0f}",
            f"${total_cost:,.0f}",
            html.Span(f"${total_upnl:,.0f}", style={"color": upnl_color}),
            html.Span(f"${total_rpnl:,.0f}", style={"color": rpnl_color}),
            events_table,
            holdings_rows,
            realized_rows,
        )

    # --- Dividends ledger sub-tab callbacks ---

    @app.callback(
        [
            Output("sum-div-card-total", "children"),
            Output("sum-div-card-tax", "children"),
            Output("sum-div-card-net", "children"),
            Output("sum-div-card-symbols", "children"),
            Output("sum-dividends-detail-table", "children"),
            Output("sum-dividends-symbol-table", "children"),
        ],
        [
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
        ],
    )
    def update_dividends_ledger(active_account, market, year):
        """Populate Dividends sub-tab."""
        account = None if active_account == "all" else active_account

        # Query dividends via trade_repo
        dividends = query_dividends(account, market, year)

        if not dividends:
            return "$0", "$0", "$0", "0", html.P("No dividends found", className="text-muted"), html.P("No data", className="text-muted")

        # KPIs
        total_div = sum(d.get("amount", 0) or 0 for d in dividends)
        total_tax = sum(d.get("tax_withheld", 0) or 0 for d in dividends)
        net_div = total_div - total_tax
        symbols = len(set(d.get("symbol", "") for d in dividends))

        # Detail table
        header = html.Thead(html.Tr([
            html.Th("Account", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Symbol", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Description", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Ex-Date", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Pay Date", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Amount", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Tax", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Net", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Currency", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
        ]))

        detail_rows = []
        for i, d in enumerate(dividends):
            bg = BG_CARD if i % 2 == 0 else "#253449"
            amount = d.get("amount", 0) or 0
            tax = d.get("tax_withheld", 0) or 0
            net = amount - tax
            detail_rows.append(html.Tr([
                html.Td(d.get("account", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(d.get("symbol", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(d.get("description", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc", "maxWidth": "200px", "overflow": "hidden", "textOverflow": "ellipsis", "whiteSpace": "nowrap"}),
                html.Td(d.get("ex_date", ""), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(d.get("pay_date", ""), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"${amount:,.2f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"${tax:,.2f}", style={"backgroundColor": bg, "color": RISK_COLORS["CRITICAL"] if tax > 0 else "#f8fafc"}),
                html.Td(f"${net:,.2f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(d.get("currency", "USD"), style={"backgroundColor": bg, "color": "#f8fafc"}),
            ]))

        detail_table = dbc.Table(
            [header, html.Tbody(detail_rows)],
            bordered=True,
            className="mb-0",
            style={"backgroundColor": BG_CARD, "fontSize": "0.85rem"},
        )

        # Income by symbol summary
        symbol_totals: dict[str, dict] = {}
        for d in dividends:
            sym = d.get("symbol", "")
            if sym not in symbol_totals:
                symbol_totals[sym] = {"total": 0.0, "count": 0}
            symbol_totals[sym]["total"] += d.get("amount", 0) or 0
            symbol_totals[sym]["count"] += 1

        sym_header = html.Thead(html.Tr([
            html.Th("Symbol", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Total Dividends", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Payment Count", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
        ]))

        sym_rows = []
        for i, (sym, info) in enumerate(sorted(symbol_totals.items(), key=lambda x: x[1]["total"], reverse=True)):
            bg = BG_CARD if i % 2 == 0 else "#253449"
            sym_rows.append(html.Tr([
                html.Td(sym, style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc", "fontWeight": "bold"}),
                html.Td(f"${info['total']:,.2f}", style={"backgroundColor": bg, "color": RISK_COLORS["SAFE"]}),
                html.Td(str(info["count"]), style={"backgroundColor": bg, "color": "#f8fafc"}),
            ]))

        symbol_table = dbc.Table(
            [sym_header, html.Tbody(sym_rows)],
            bordered=True,
            className="mb-0",
            style={"backgroundColor": BG_CARD, "fontSize": "0.85rem"},
        )

        return (
            f"${total_div:,.2f}",
            f"${total_tax:,.2f}",
            f"${net_div:,.2f}",
            str(symbols),
            detail_table,
            symbol_table,
        )
