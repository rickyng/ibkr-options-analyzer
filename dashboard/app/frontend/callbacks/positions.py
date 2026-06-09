"""Positions tab callbacks — filters, charts, tables, detail panel."""

from collections import Counter
from datetime import date

import plotly.graph_objects as go
from dash import Dash, Output, Input, State, html
import dash_bootstrap_components as dbc

from ...services.positions import query_cached_earnings
from ...services.trade_repo import query_latest_assignments
from ._data import _fetch_exposure, _fetch_expiry_calendar, _flatten_calendar, _get_existing_positions_map
from ._helpers import _parse_expiry
from ._ui import _detail_card, _risk_gauge
from ..layout import RISK_COLORS, BG_CARD, TEXT_MUTED


def register(app: Dash) -> None:
    """Register all positions tab callbacks with the Dash app."""

    # Callback: Re-fetch data when global account filter changes
    @app.callback(
        [
            Output("exposure-data", "data", allow_duplicate=True),
            Output("calendar-data", "data", allow_duplicate=True),
            Output("positions-data", "data", allow_duplicate=True),
        ],
        Input("global-account-filter", "value"),
        prevent_initial_call=True,
    )
    def filter_by_account(active_tab):
        """Re-fetch data filtered by account selection."""
        account = None if active_tab == "all" else active_tab
        exposure = _fetch_exposure(account)
        calendar = _fetch_expiry_calendar(account)
        positions = _flatten_calendar(calendar)
        return exposure, calendar, positions

    # Callback: Apply market/risk filters to positions
    @app.callback(
        Output("filtered-positions-data", "data"),
        [
            Input("positions-data", "data"),
            Input("global-market-filter", "value"),
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
            Output("card-loss-current", "children"),
            Output("card-loss-5pct", "children"),
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
        loss_current = 0
        loss_5pct = 0
        expiring_soon = 0

        for pos in positions:
            premium = pos.get("premium", 0) or 0
            quantity = abs(pos.get("quantity", 0) or 0)
            strike = pos.get("strike", 0) or 0
            multiplier = pos.get("multiplier", 100)
            current_price = pos.get("current_price")

            max_profit += premium * multiplier * quantity

            # Current loss using live market price
            if current_price and current_price > 0:
                right = pos.get("right", "P")
                if right == 'P':
                    intrinsic = max(0, strike - current_price)
                else:
                    intrinsic = max(0, current_price - strike)
                loss_current += max(0, intrinsic - premium) * multiplier * quantity
            # 5% loss scenario: market drops 5% below current price
            if pos.get("right", "P") == 'P' and current_price and current_price > 0:
                scenario_price = current_price * 0.95
                loss_5pct += max(0, (strike - scenario_price) - premium) * multiplier * quantity

            # Expiring this week (<= 7 days)
            expiry_date = _parse_expiry(pos.get("expiry", ""))
            if expiry_date:
                days_to_expiry = (expiry_date - date.today()).days
                if 0 <= days_to_expiry <= 7:
                    expiring_soon += 1

        return [
            str(total),
            f"${max_profit:,.0f}",
            f"${loss_current:,.0f}",
            f"${loss_5pct:,.0f}",
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

    @app.callback(
        Output("expiry-calendar-table", "children"),
        Input("filtered-positions-data", "data"),
    )
    def update_expiry_calendar_table(positions):
        """Render expiry calendar: strikes grouped by underlying across DTE buckets."""
        if not positions:
            return html.P("No positions", className="text-muted")

        today = date.today()
        bucket_labels = ["<7d", "7-14d", "14-21d", "21-28d", "28d+"]
        bucket_ranges = [(0, 7), (7, 14), (14, 21), (21, 28), (28, 9999)]
        bucket_colors = {
            "<7d": RISK_COLORS["CRITICAL"],
            "7-14d": RISK_COLORS["HIGH"],
            "14-21d": RISK_COLORS["EXPIRING"],
            "21-28d": RISK_COLORS["MODERATE"],
            "28d+": RISK_COLORS["SAFE"],
        }

        def get_bucket(dte: int) -> str:
            for label, (lo, hi) in zip(bucket_labels, bucket_ranges):
                if lo <= dte < hi:
                    return label
            return bucket_labels[-1]

        # Group: underlying -> bucket -> list of strikes with info
        ul_map: dict[str, dict[str, list]] = {}
        for pos in positions:
            ul = pos.get("underlying", "")
            expiry_str = pos.get("expiry", "")
            expiry_date = _parse_expiry(expiry_str)
            if not expiry_date:
                continue
            dte = (expiry_date - today).days
            bucket = get_bucket(dte)
            ul_map.setdefault(ul, {b: [] for b in bucket_labels})
            strike = pos.get("strike", 0)
            right = pos.get("right", "P")
            qty = abs(pos.get("quantity", 0) or 0)
            risk = pos.get("risk_category", "SAFE")
            ul_map[ul][bucket].append({
                "strike": strike,
                "right": right,
                "qty": qty,
                "risk": risk,
                "expiry": expiry_str,
            })

        if not ul_map:
            return html.P("No positions", className="text-muted")

        # Fetch next earnings dates for all underlyings
        earnings = query_cached_earnings(list(ul_map.keys()))

        # Fetch latest assignments for calendar underlyings
        assignments = query_latest_assignments(list(ul_map.keys()))

        # Build table -- Underlying, Earnings, Assigned, then DTE buckets
        header_cells = [html.Th("Underlying", style={"textAlign": "left", "backgroundColor": "#253449", "color": "#f8fafc", "minWidth": "80px"})]
        header_cells.append(html.Th("Earnings", style={"backgroundColor": "#253449", "color": "#fbbf24", "textAlign": "center", "minWidth": "80px"}))
        header_cells.append(html.Th("Assigned", style={"backgroundColor": "#253449", "color": "#94a3b8", "textAlign": "center", "minWidth": "80px"}))
        for bl in bucket_labels:
            header_cells.append(html.Th(bl, style={"backgroundColor": "#253449", "color": bucket_colors[bl], "textAlign": "center", "minWidth": "100px"}))
        header = html.Thead(html.Tr(header_cells))

        rows = []
        for i, (ul, buckets) in enumerate(sorted(ul_map.items())):
            bg = BG_CARD if i % 2 == 0 else "#253449"
            cells = [html.Td(ul, style={"textAlign": "left", "backgroundColor": bg, "color": "#f8fafc", "fontWeight": "bold"})]

            # Earnings date cell
            edate = earnings.get(ul)
            if edate:
                ed = _parse_expiry(edate)
                if ed:
                    days_to = (ed - today).days
                    urgency = "#ef4444" if days_to <= 14 else "#f97316" if days_to <= 30 else "#fbbf24"
                    cells.append(html.Td(
                        html.Span(f"{edate} ({days_to}d)", style={
                            "backgroundColor": f"{urgency}22",
                            "color": urgency,
                            "padding": "2px 6px",
                            "borderRadius": "3px",
                            "fontSize": "0.78rem",
                            "border": f"1px solid {urgency}44",
                        }),
                        style={"backgroundColor": bg, "textAlign": "center"},
                    ))
                else:
                    cells.append(html.Td("--", style={"backgroundColor": bg, "color": TEXT_MUTED, "textAlign": "center"}))
            else:
                cells.append(html.Td("--", style={"backgroundColor": bg, "color": TEXT_MUTED, "textAlign": "center"}))

            # Latest assignment cell
            ul_assignments = assignments.get(ul)
            if ul_assignments:
                assign_badges = []
                assign_color = "#60a5fa" if ul_assignments[0]["right"] == "C" else "#94a3b8"
                for a in ul_assignments:
                    label = f"{a['strike']:,.0f}{a['right']}"
                    if a["qty"] > 1:
                        label += f"x{int(a['qty'])}"
                    assign_badges.append(html.Span(
                        label,
                        style={
                            "backgroundColor": f"{assign_color}22",
                            "color": assign_color,
                            "padding": "1px 5px",
                            "borderRadius": "3px",
                            "fontSize": "0.78rem",
                            "margin": "1px",
                            "display": "inline-block",
                            "border": f"1px solid {assign_color}44",
                        },
                    ))
                cells.append(html.Td(assign_badges, style={"backgroundColor": bg, "textAlign": "center"}))
            else:
                cells.append(html.Td("--", style={"backgroundColor": bg, "color": TEXT_MUTED, "textAlign": "center"}))

            for bl in bucket_labels:
                strikes = buckets[bl]
                if not strikes:
                    cells.append(html.Td("--", style={"backgroundColor": bg, "color": TEXT_MUTED, "textAlign": "center"}))
                else:
                    # Build strike badges
                    badges = []
                    for s in sorted(strikes, key=lambda x: x["strike"]):
                        right = s["right"]
                        qty = s["qty"]
                        strike_val = s["strike"]
                        risk = s["risk"]
                        color = RISK_COLORS.get(risk, TEXT_MUTED)
                        label = f"{strike_val:,.0f}{right}"
                        if qty > 1:
                            label += f"x{int(qty)}"
                        badges.append(html.Span(
                            label,
                            style={
                                "backgroundColor": f"{color}22",
                                "color": color,
                                "padding": "1px 5px",
                                "borderRadius": "3px",
                                "fontSize": "0.78rem",
                                "margin": "1px",
                                "display": "inline-block",
                                "border": f"1px solid {color}44",
                            },
                        ))
                    cells.append(html.Td(badges, style={"backgroundColor": bg, "textAlign": "center"}))

            rows.append(html.Tr(cells))

        return dbc.Table(
            [header, html.Tbody(rows)],
            bordered=True,
            className="mb-0",
            style={"fontSize": "0.85rem"},
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
            multiplier = pos.get("multiplier", 100)
            exposure_map[underlying]["max_profit"] += premium * multiplier * quantity
            # Max loss depends on position type:
            right = pos.get("right", "P")
            raw_qty = pos.get("quantity", 0) or 0
            if raw_qty < 0 and right == "P":
                # Short put: max loss = (strike - premium) * mult * qty
                exposure_map[underlying]["max_loss"] += (strike - premium) * multiplier * quantity
            elif raw_qty < 0 and right == "C":
                # Short call: unlimited loss — show 0
                pass
            else:
                # Long: max loss = premium paid
                exposure_map[underlying]["max_loss"] += premium * multiplier * quantity

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

        # Calculate computed values (CLI values already in USD)
        strike = pos.get("strike", 0)
        premium = pos.get("premium", 0)
        quantity = abs(pos.get("quantity", 0))
        current_price = pos.get("current_price") or 0
        multiplier = pos.get("multiplier", 100)

        # Breakeven for short put: strike - premium
        breakeven = strike - premium

        # Max profit: premium * multiplier * |qty|
        max_profit = premium * multiplier * quantity

        # Max loss depends on position type
        right = pos.get("right", "P")
        raw_qty = pos.get("quantity", 0) or 0
        if raw_qty < 0 and right == "P":
            max_loss = (strike - premium) * multiplier * quantity
        elif raw_qty < 0 and right == "C":
            max_loss = 0  # unlimited
        else:
            max_loss = premium * multiplier * quantity  # long: premium paid

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
                dbc.Col(_detail_card("Strike", f"${strike:,.2f}"), width=3),
                dbc.Col(_detail_card("Current Price", f"${current_price:,.2f}" if current_price else "N/A"), width=3),
                dbc.Col(_detail_card("Premium", f"${premium:,.2f}"), width=3),
                dbc.Col(_detail_card("Distance", f"{distance_pct:.1f}%"), width=3),
                dbc.Col(_detail_card("Breakeven", f"${breakeven:,.2f}"), width=3),
                dbc.Col(_detail_card("Max Profit", f"${max_profit:,.0f}"), width=3),
                dbc.Col(_detail_card("Max Loss", f"${max_loss:,.0f}"), width=3),
                dbc.Col(_detail_card("Days to Expiry", str(days_to_expiry)), width=3),
            ],
            className="g-2",
        )

        # Risk gauge visualization
        gauge = _risk_gauge(current_price, strike, risk_color)

        return dbc.Card(
            dbc.CardBody(
                [
                    html.H5(
                        f"{pos.get('underlying', '')} {pos.get('expiry', '')} {strike} {pos.get('right', '')}",
                        className="mb-3",
                        style={"color": "#f8fafc"},
                    ),
                    metric_cards,
                    html.Div(gauge, className="mt-4"),
                ]
            ),
            className="detail-panel",
        )
