"""Shared UI helper components for dashboard callbacks."""

from dash import html

from ..layout import RISK_COLORS, BG_CARD, TEXT_MUTED


def _detail_card(label: str, value: str) -> html.Div:
    """Create a small metric card for the position detail panel."""
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
    """Create a visual risk gauge showing price vs strike with zone labels."""
    if not current_price or not strike:
        return html.Div()

    pct = (current_price / strike) * 100

    # Zone definitions: label, color, width percentage
    zones = [
        {"label": "ITM", "color": RISK_COLORS["CRITICAL"], "width": "20%"},
        {"label": "1-5%", "color": RISK_COLORS["HIGH"], "width": "15%"},
        {"label": "5-10%", "color": RISK_COLORS["MODERATE"], "width": "15%"},
        {"label": "SAFE", "color": RISK_COLORS["SAFE"], "width": "50%"},
    ]

    zone_divs = []
    for z in zones:
        zone_divs.append(
            html.Div(
                html.Small(z["label"], style={"color": z["color"], "fontSize": "0.7rem", "fontWeight": "600"}),
                className="risk-gauge-zone",
                style={
                    "width": z["width"],
                    "backgroundColor": f"{z['color']}15",
                    "borderRight": "1px solid #334155",
                },
            )
        )

    # Marker position (clamped to 0-100%)
    marker_left = min(pct / 110 * 100, 100)
    marker = html.Div(className="risk-gauge-marker", style={"left": f"{marker_left}%"})

    return html.Div([
        html.Div([
            html.Span("Price vs Strike", style={"color": TEXT_MUTED, "fontSize": "0.85rem"}),
            html.Span(
                f"${current_price:,.2f} / ${strike:,.2f} = {pct:.1f}%",
                style={"color": risk_color, "fontWeight": "bold", "fontSize": "0.85rem"},
            ),
        ], className="d-flex justify-content-between mb-2"),
        html.Div(
            zone_divs,
            className="risk-gauge-zones",
        ),
        html.Div([marker], style={"position": "relative", "height": "0px"}),
    ])
