"""Report generation from DB — replaces run_cli("report").

Generates CSV/text reports by querying SQLite directly.
"""

from __future__ import annotations

import csv
import io
from typing import Any

from .positions import query_open_positions_enriched, query_positions_summary
from .strategies import query_detected_strategies


def generate_positions_csv(account: str | None = None) -> str:
    """Generate positions CSV report."""
    positions = query_open_positions_enriched(account=account)
    if not positions:
        return ""

    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow([
        "Account", "Underlying", "Expiry", "Right", "Strike",
        "Quantity", "Premium", "DTE", "Risk Category",
        "Current Price", "Distance %", "Market",
    ])
    for p in positions:
        writer.writerow([
            p.get("account_name", p.get("account", "")),
            p.get("underlying", ""),
            p.get("expiry", ""),
            p.get("right", ""),
            p.get("strike", 0),
            p.get("quantity", 0),
            p.get("entry_premium", 0),
            p.get("days_to_expiry", 0),
            p.get("risk_category", ""),
            p.get("current_price", ""),
            round(p.get("distance_from_strike_pct", 0), 2),
            p.get("market", ""),
        ])
    return buf.getvalue()


def generate_strategies_csv(account: str | None = None) -> str:
    """Generate strategies CSV report."""
    strategies = query_detected_strategies(account=account)
    if not strategies:
        return ""

    buf = io.StringIO()
    writer = csv.writer(buf)
    writer.writerow([
        "Account", "Type", "Underlying", "Expiry", "Legs",
        "Net Premium", "Max Profit", "Max Loss", "Breakeven", "Risk Level",
    ])
    for s in strategies:
        writer.writerow([
            s.get("account_name", ""),
            s.get("strategy_type", ""),
            s.get("underlying", ""),
            s.get("expiry", ""),
            s.get("leg_count", 0),
            round(s.get("net_premium", 0) or 0, 2),
            round(s.get("max_profit", 0) or 0, 2),
            round(s.get("max_loss", 0) or 0, 2),
            round(s.get("breakeven_price", 0) or 0, 2),
            s.get("risk_level", ""),
        ])
    return buf.getvalue()


def generate_summary(account: str | None = None) -> dict[str, Any]:
    """Generate summary report data."""
    summary = query_positions_summary(account=account)
    strategies = query_detected_strategies(account=account)
    return {
        "status": "ok",
        "position_count": summary.get("total", 0),
        "summary": summary,
        "strategy_count": len(strategies),
    }
