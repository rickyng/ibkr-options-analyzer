#!/usr/bin/env python3
"""Account No1 Performance Dashboard — currency summary 2020–2026."""

import plotly.graph_objects as go
from plotly.subplots import make_subplots
import argparse
import webbrowser
import os

# ── Data ──────────────────────────────────────────────────────────────────

YEARS = [2020, 2021, 2022, 2023, 2024, 2025, 2026]

BASE = {
    "Starting Cash":      [0, -637192.60, -1031280.57, -960458.55, -965975.79, -5755.60, -285963.84],
    "Commissions":        [-1825.19, -1413.04, -49.55, -28.89, -144.37, -106.58, -190.96],
    "Deposits":           [438663.90, 141873.50, 108388.50, 0, 0, 0, 0],
    "Dividends":          [3985.80, 14139.32, 17062.61, 16567.63, 20484.45, 23879.22, 6461.64],
    "Broker Interest":    [-2221.32, -11318.52, -26183.49, -64392.24, -54265.34, -20724.47, -7452.06],
    "Trades (Sales)":     [3015749.10, 1452566.16, 544923.82, 50404.72, 1126405.00, 135070.00, 253545.27],
    "Trades (Purchase)":  [-4092012.85, -1995335.99, -571952.18, -5105.00, -131028.89, -418317.62, -549355.74],
    "Other Fees":         [-9.73, 183.42, -5.30, -35.60, -7.16, -6.00, -5.71],
    "Payment In Lieu":    [2103.51, 3645.21, 5742.15, 6836.33, 2587.13, 4125.68, 2078.40],
    "Transaction Fees":   [-95.77, -114.93, 0, 0, 0, 0, 0],
    "Withholding Tax":    [-422.81, -2861.12, -2865.13, -2708.27, -2990.29, -3959.40, -965.27],
    "FX Translation":     [-1107.25, 4548.02, -1933.93, -1367.23, -820.33, -169.08, -1057.89],
    "Ending Cash":        [-637192.60, -1031280.57, -960458.55, -965748.30, -5755.60, -285963.84, -582906.16],
    "Ending Settled Cash": [-652135.83, -1031280.57, -960458.55, -965748.30, -10275.60, -285963.84, -583239.75],
}

HKD = {
    "Starting Cash":      [0, 114989.58, 1269493.48, 886756.35, 896346.41, 953466.14, 1009863.97],
    "Commissions":        [-986.90, -824.28, 0, 0, 0, 0, -63.00],
    "Deposits":           [3400000, 1100000, 850000, 0, 0, 0, 0],
    "Trades (Sales)":     [370911.75, 451421.00, 168.38, 0, 0, 0, 1430.00],
    "Trades (Purchase)":  [-3654192.91, -395200.00, -1269493.48, -40000.00, 0, 0, 0],
    "Transaction Fees":   [-742.36, -892.82, 0, 0, 0, 0, 0],
    "Ending Cash":        [114989.58, 1269493.48, 886756.35, 896346.41, 953466.14, 1009863.97, 1017758.76],
    "Ending Settled Cash": [685380.87, 1269493.48, 886756.35, 896346.41, 953466.14, 1009863.97, 1017758.76],
}


def _fmt(v):
    """Format number with commas, no decimals for large values."""
    if abs(v) >= 1000:
        return f"{v:,.0f}"
    return f"{v:,.2f}"


def build_dashboard() -> go.Figure:
    fig = make_subplots(
        rows=4, cols=2,
        subplot_titles=(
            "Base Currency — Ending Cash Balance",
            "HKD — Ending Cash Balance",
            "Base Currency — Net Trading P&L by Year",
            "Income vs Interest Expense",
            "Cumulative Deposits vs Ending Cash (Base)",
            "Broker Interest Expense Trend",
            "Base Currency — Annual Cash Flow Waterfall",
            "HKD — Annual Cash Flow Waterfall",
        ),
        vertical_spacing=0.07,
        horizontal_spacing=0.12,
    )

    # ── 1. Base ending cash ───────────────────────────────────────────────
    fig.add_trace(go.Scatter(
        x=YEARS, y=BASE["Ending Cash"], mode="lines+markers+text",
        text=[_fmt(v) for v in BASE["Ending Cash"]],
        textposition="bottom center",
        line=dict(color="#e74c3c", width=2.5),
        marker=dict(size=8), name="Ending Cash (Base)",
    ), row=1, col=1)
    fig.add_hline(y=0, line_dash="dash", line_color="gray", row=1, col=1)

    # ── 2. HKD ending cash ────────────────────────────────────────────────
    fig.add_trace(go.Scatter(
        x=YEARS, y=HKD["Ending Cash"], mode="lines+markers+text",
        text=[_fmt(v) for v in HKD["Ending Cash"]],
        textposition="top center",
        line=dict(color="#2ecc71", width=2.5),
        marker=dict(size=8), name="Ending Cash (HKD)",
    ), row=1, col=2)

    # ── 3. Net trading P&L ────────────────────────────────────────────────
    net_trades = [s + p for s, p in zip(BASE["Trades (Sales)"], BASE["Trades (Purchase)"])]
    colors = ["#2ecc71" if v >= 0 else "#e74c3c" for v in net_trades]
    fig.add_trace(go.Bar(
        x=YEARS, y=net_trades,
        marker_color=colors, text=[_fmt(v) for v in net_trades],
        textposition="outside", name="Net Trades (Base)",
    ), row=2, col=1)

    # ── 4. Income vs interest ─────────────────────────────────────────────
    fig.add_trace(go.Bar(
        x=YEARS, y=BASE["Dividends"],
        marker_color="#3498db", name="Dividends",
    ), row=2, col=2)
    fig.add_trace(go.Bar(
        x=YEARS, y=BASE["Payment In Lieu"],
        marker_color="#1abc9c", name="Payment In Lieu",
    ), row=2, col=2)
    fig.add_trace(go.Bar(
        x=YEARS, y=[abs(v) for v in BASE["Broker Interest"]],
        marker_color="#e74c3c", name="Interest Expense",
    ), row=2, col=2)
    fig.update_layout(barmode="group", bargap=0.2)

    # ── 5. Cumulative deposits vs ending cash ─────────────────────────────
    cum_deposits = []
    running = 0
    for d in BASE["Deposits"]:
        running += d
        cum_deposits.append(running)
    fig.add_trace(go.Scatter(
        x=YEARS, y=cum_deposits, mode="lines+markers",
        line=dict(color="#3498db", width=2, dash="dot"),
        marker=dict(size=7), name="Cumulative Deposits",
    ), row=3, col=1)
    fig.add_trace(go.Scatter(
        x=YEARS, y=BASE["Ending Cash"], mode="lines+markers",
        line=dict(color="#e74c3c", width=2),
        marker=dict(size=7), name="Ending Cash",
    ), row=3, col=1)

    # ── 6. Broker interest trend ──────────────────────────────────────────
    fig.add_trace(go.Bar(
        x=YEARS, y=[abs(v) for v in BASE["Broker Interest"]],
        marker_color=["#c0392b" if abs(v) > 30000 else "#e67e22" if abs(v) > 10000 else "#f1c40f"
                       for v in BASE["Broker Interest"]],
        text=[_fmt(abs(v)) for v in BASE["Broker Interest"]],
        textposition="outside",
        name="Interest Expense",
    ), row=3, col=2)

    # ── 7. Base waterfall ─────────────────────────────────────────────────
    base_categories = ["Dividends", "Payment In Lieu", "Trades Net",
                        "Broker Interest", "Commissions", "Withholding Tax", "Other"]
    # Use overall totals
    base_totals = [
        102580.67,    # Dividends
        27118.41,     # Payment In Lieu
        6_578_664.07 - 7_763_108.27,  # Net trades
        -186557.44,   # Broker interest
        -3758.58,     # Commissions
        -16772.29,    # Withholding tax
        113.92 - 210.70 - 1907.69,    # Other (fees + FX)
    ]
    waterfall_colors = ["#2ecc71" if v >= 0 else "#e74c3c" for v in base_totals]
    fig.add_trace(go.Waterfall(
        x=base_categories, y=base_totals,
        measure=["relative"] * len(base_categories) + ["total"],
        decreasing=dict(marker=dict(color="#e74c3c")),
        increasing=dict(marker=dict(color="#2ecc71")),
        totals=dict(marker=dict(color="#3498db")),
        name="Base Currency Flow",
    ), row=4, col=1)

    # ── 8. HKD waterfall ──────────────────────────────────────────────────
    hkd_categories = ["Deposits", "Trades Net", "Commissions", "Fees"]
    hkd_totals = [
        5350000,
        823931.13 - 5358886.39,
        -1874.18,
        -1635.18,
    ]
    fig.add_trace(go.Waterfall(
        x=hkd_categories, y=hkd_totals,
        measure=["relative"] * len(hkd_categories) + ["total"],
        decreasing=dict(marker=dict(color="#e74c3c")),
        increasing=dict(marker=dict(color="#2ecc71")),
        totals=dict(marker=dict(color="#3498db")),
        name="HKD Flow",
    ), row=4, col=2)

    # ── Layout ────────────────────────────────────────────────────────────
    fig.update_layout(
        title=dict(
            text="Account No1 — Performance Dashboard (2020–2026)",
            font=dict(size=22),
        ),
        height=2200,
        showlegend=False,
        template="plotly_white",
        font=dict(size=11),
    )

    # Axis labels
    for r in range(1, 5):
        for c in range(1, 3):
            fig.update_yaxes(tickformat=",.0f", row=r, col=c)

    return fig


def main():
    parser = argparse.ArgumentParser(description="Account No1 Performance Dashboard")
    parser.add_argument("--output", "-o", default="account_no1_dashboard.html",
                        help="Output HTML file path")
    parser.add_argument("--no-open", action="store_true", help="Don't auto-open browser")
    args = parser.parse_args()

    fig = build_dashboard()
    output_path = os.path.abspath(args.output)
    fig.write_html(output_path, include_plotlyjs="cdn")
    print(f"Dashboard saved to: {output_path}")

    if not args.no_open:
        webbrowser.open(f"file://{output_path}")


if __name__ == "__main__":
    main()
