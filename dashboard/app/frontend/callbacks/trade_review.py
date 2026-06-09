"""Trade Review tab callbacks — raw trade listing, KPI cards, sub-tab toggling."""

from dash import Dash, Output, Input, State, html, no_update
import dash_bootstrap_components as dbc

from ...services.trades import query_all_trades
from ._helpers import _derive_market_from_symbol, _apply_year_filter
from ..layout import RISK_COLORS, BG_CARD, TEXT_MUTED


def register(app: Dash) -> None:
    """Register all trade review tab callbacks with the Dash app."""

    @app.callback(
        Output("tr-filtered-data", "data"),
        [
            Input("trade-review-data", "data"),
            Input("global-account-filter", "value"),
            Input("global-market-filter", "value"),
            Input("global-year-filter", "value"),
        ],
    )
    def apply_tr_filters(data, active_account, market, year):
        """Filter trade review data by account, market, and year."""
        if not data:
            return data

        option_trades = data.get("option_trades", [])
        stock_trades = data.get("stock_trades", [])
        # Exclude currency pairs (USD.HKD, etc.) — they use dot notation
        stock_trades = [t for t in stock_trades if "." not in t.get("symbol", "")]

        overview = data.get("overview", {})
        account = None if active_account == "all" else active_account

        if account:
            option_trades = [t for t in option_trades if t.get("account") == account]
            stock_trades = [t for t in stock_trades if t.get("account") == account]
        if market and market != "All Markets":
            option_trades = [t for t in option_trades if _derive_market_from_symbol(t.get("underlying", "")) == market]
            stock_trades = [t for t in stock_trades if _derive_market_from_symbol(t.get("symbol", "")) == market]
        if year and year != "all":
            option_trades = _apply_year_filter(option_trades, year, "date")
            stock_trades = _apply_year_filter(stock_trades, year, "date")

        # Recompute overview from filtered trades
        net_cash_opts = sum(t.get("net_cash", 0) or 0 for t in option_trades)
        net_cash_stocks = sum(t.get("net_cash", 0) or 0 for t in stock_trades)
        all_nc = [t.get("net_cash", 0) or 0 for t in option_trades + stock_trades]
        total_commissions = sum(abs(t.get("commission", 0) or 0) for t in option_trades + stock_trades)

        return {
            **data,
            "option_trades": option_trades,
            "stock_trades": stock_trades,
            "overview": {
                "total_trades": len(option_trades) + len(stock_trades),
                "total_premium_in": sum(nc for nc in all_nc if nc > 0),
                "total_premium_out": sum(abs(nc) for nc in all_nc if nc < 0),
                "net_premium": net_cash_opts + net_cash_stocks,
                "total_commissions": total_commissions,
            },
        }

    @app.callback(
        Output("trade-review-data", "data"),
        Input("main-tabs", "active_tab"),
    )
    def load_trade_review_data(active_tab):
        """Load trade review data when Trade Review or Summary tab is selected."""
        if active_tab not in ("tab-trade-review", "tab-summary"):
            return no_update
        try:
            return query_all_trades()
        except Exception:
            return {}

    @app.callback(
        [
            Output("tr-card-total", "children"),
            Output("tr-card-win-rate", "children"),
            Output("tr-card-option-pnl", "children"),
            Output("tr-card-stock-pnl", "children"),
            Output("tr-card-pnl", "children"),
            Output("tr-card-profit-factor", "children"),
            Output("tr-card-avg-roc", "children"),
            Output("tr-card-standalone-loss", "children"),
        ],
        Input("tr-filtered-data", "data"),
    )
    def update_trade_overview_cards(data):
        """Update KPI cards from overview data."""
        if not data:
            return "--", "--", "--", "--", "--", "--", "--", "--"
        ov = data.get("overview", {})
        if not ov:
            return "--", "--", "--", "--", "--", "--", "--", "--"

        total = ov.get("total_trades", 0)
        premium_in = ov.get("total_premium_in", 0) or 0
        premium_out = ov.get("total_premium_out", 0) or 0
        net_premium = ov.get("net_premium", 0) or 0
        commissions = ov.get("total_commissions", 0) or 0

        # Compute KPIs from filtered trades
        option_trades = data.get("option_trades", [])
        stock_trades = data.get("stock_trades", [])
        all_trades = option_trades + stock_trades
        wins = sum(1 for t in all_trades if (t.get("net_cash", 0) or 0) > 0)
        win_rate = f"{wins / len(all_trades):.0%}" if all_trades else "--"
        gross_profit = sum(t.get("net_cash", 0) or 0 for t in all_trades if (t.get("net_cash", 0) or 0) > 0)
        gross_loss = abs(sum(t.get("net_cash", 0) or 0 for t in all_trades if (t.get("net_cash", 0) or 0) < 0))
        profit_factor = f"{gross_profit / gross_loss:.2f}" if gross_loss > 0 else "--"
        # Avg ROC: net_cash / (strike * qty * mult) for option trades
        roc_values = []
        for t in option_trades:
            nc = t.get("net_cash", 0) or 0
            strike = t.get("strike", 0) or 0
            qty = abs(t.get("quantity", 0) or 0)
            mult = int(t.get("multiplier") or 100)
            if strike > 0 and qty > 0:
                roc_values.append(nc / (strike * mult * qty) * 100)
        avg_roc = f"{sum(roc_values) / len(roc_values):.1f}%" if roc_values else "--"

        return (
            str(total),
            f"${premium_in:,.0f}" if total else "--",
            f"${premium_out:,.0f}" if total else "--",
            f"${net_premium:,.0f}" if total else "--",
            f"${commissions:,.0f}" if total else "--",
            win_rate,
            profit_factor,
            avg_roc,
        )

    @app.callback(
        Output("tr-trades-table", "data"),
        Input("tr-filtered-data", "data"),
    )
    def render_trades_table(data):
        """Render trades table rows."""
        if not data:
            return []
        return data.get("option_trades", [])

    @app.callback(
        Output("tr-stock-table", "children"),
        Input("tr-filtered-data", "data"),
    )
    def render_stock_table(data):
        """Render stock trades table."""
        if not data:
            return html.P("No stock trades", className="text-muted")

        stock_trades = data.get("stock_trades", [])
        if not stock_trades:
            return html.P("No stock trades", className="text-muted")

        # Build simple table
        header = html.Thead(html.Tr([
            html.Th("Date", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Account", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Symbol", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Description", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Qty", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Price", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Net Cash", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
            html.Th("Type", style={"backgroundColor": "#253449", "color": "#f8fafc"}),
        ]))

        rows = []
        for i, t in enumerate(stock_trades):
            bg = BG_CARD if i % 2 == 0 else "#253449"
            net_cash = t.get("net_cash", 0) or 0
            notes = t.get("notes_codes", "") or ""
            trade_type = "DRIP" if "R" in notes else ""
            net_color = RISK_COLORS["SAFE"] if net_cash >= 0 else RISK_COLORS["CRITICAL"]
            rows.append(html.Tr([
                html.Td(t.get("date", ""), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(t.get("account", ""), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(t.get("symbol", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc", "fontWeight": "bold"}),
                html.Td(t.get("description", ""), style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc", "maxWidth": "200px", "overflow": "hidden", "textOverflow": "ellipsis", "whiteSpace": "nowrap"}),
                html.Td(str(t.get("quantity", 0)), style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"${t.get('trade_price', 0):,.2f}", style={"backgroundColor": bg, "color": "#f8fafc"}),
                html.Td(f"${net_cash:,.2f}", style={"backgroundColor": bg, "color": net_color}),
                html.Td(trade_type, style={"backgroundColor": bg, "color": "#f59e0b" if trade_type == "DRIP" else "#f8fafc"}),
            ]))

        return dbc.Table(
            [header, html.Tbody(rows)],
            bordered=True,
            className="mb-0",
            style={"fontSize": "0.85rem"},
        )

    @app.callback(
        [
            Output("tr-subtab-trades", "style"),
            Output("tr-subtab-strategy", "style"),
        ],
        Input("trade-subtabs", "active_tab"),
    )
    def toggle_trade_subtabs(active_tab):
        """Show/hide sub-tab content based on active sub-tab."""
        show = {"display": "block"}
        hide = {"display": "none"}
        if active_tab == "subtab-trades":
            return show, hide
        elif active_tab == "subtab-strategy":
            return hide, show
        return show, hide
