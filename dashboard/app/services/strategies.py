"""Strategy analysis reads — computes from open_options directly.

Previously read from cached_portfolio_analysis populated by CLI analyze strategy.
Now computes underlying exposure from the open_options table directly.
"""

from __future__ import annotations

import json
from typing import Any

from .db import query, query_one


def query_detected_strategies(
    account: str | None = None,
    underlying: str | None = None,
) -> list[dict[str, Any]]:
    """Read detected strategies with legs and risk metrics from cache."""
    sql = """
        SELECT ds.id, ds.strategy_type, ds.underlying, ds.expiry,
               ds.leg_count, ds.net_premium_usd, ds.max_profit_usd,
               ds.max_loss_usd, ds.breakeven_price_usd,
               ds.risk_level, ds.account_name, ds.currency,
               ds.net_premium, ds.max_profit, ds.max_loss, ds.breakeven_price
        FROM detected_strategies ds
        WHERE 1=1
    """
    params: list[Any] = []
    if account:
        sql += " AND ds.account_name = ?"
        params.append(account)
    if underlying:
        sql += " AND ds.underlying = ?"
        params.append(underlying)
    sql += " ORDER BY ds.expiry, ds.underlying"

    strategies = query(sql, tuple(params))

    # Attach legs for each strategy
    for s in strategies:
        sid = s["id"]
        legs = query(
            """SELECT sl.leg_role, oo.strike, oo.right, oo.quantity,
                      oo.entry_premium, oo.multiplier, oo.currency
               FROM strategy_legs sl
               JOIN open_options oo ON oo.id = sl.option_id
               WHERE sl.strategy_id = ?""",
            (sid,),
        )
        s["legs"] = legs

        # Build risk dict matching CLI JSON format
        max_loss = s.get("max_loss")
        s["risk"] = {
            "breakeven_price": s.get("breakeven_price"),
            "max_profit": s.get("max_profit"),
            "max_loss": max_loss,
            "max_loss_display": "UNLIMITED" if (max_loss is not None and max_loss < 0) else None,
            "risk_level": s.get("risk_level"),
            "net_premium": s.get("net_premium"),
            "currency": s.get("currency", "USD"),
        }

    return strategies


def query_portfolio_analysis() -> dict[str, Any]:
    """Compute portfolio-level analysis from open_options directly."""
    rows = query("""
        SELECT oo.underlying, oo.strike, oo.right, oo.quantity,
               oo.entry_premium, oo.multiplier, oo.expiry,
               a.name AS account_name
        FROM open_options oo
        LEFT JOIN accounts a ON a.id = oo.account_id
        ORDER BY oo.underlying, oo.expiry
    """)

    if not rows:
        return {
            "portfolio": {},
            "accounts": [],
            "underlying_exposure": [],
        }

    # Aggregate by underlying
    exposure_by_underlying: dict[str, dict] = {}
    account_totals: dict[str, dict] = {}

    for r in rows:
        underlying = r["underlying"]
        mult = r.get("multiplier", 100) or 100
        qty = r.get("quantity", 0)
        premium = abs(qty) * r.get("entry_premium", 0) * mult
        strike = r.get("strike", 0)
        right = r.get("right", "P")

        # Max loss for short puts: (strike - premium) * mult * |qty|
        if qty < 0 and right == "P":
            max_loss = (strike - r.get("entry_premium", 0)) * mult * abs(qty)
        elif qty < 0 and right == "C":
            max_loss = 0  # unlimited, represent as 0 for aggregation
        else:
            max_loss = 0

        # Aggregate by underlying
        if underlying not in exposure_by_underlying:
            exposure_by_underlying[underlying] = {
                "underlying": underlying,
                "total_max_loss": 0.0,
                "total_max_profit": 0.0,
                "position_count": 0,
            }
        exposure_by_underlying[underlying]["total_max_profit"] += premium
        exposure_by_underlying[underlying]["total_max_loss"] += max_loss
        exposure_by_underlying[underlying]["position_count"] += 1

        # Aggregate by account
        acct = r.get("account_name") or "Unknown"
        if acct not in account_totals:
            account_totals[acct] = {
                "account_name": acct,
                "strategy_count": 0,
                "total_max_profit": 0.0,
                "total_max_loss": 0.0,
                "positions_expiring_soon": 0,
            }
        account_totals[acct]["strategy_count"] += 1
        account_totals[acct]["total_max_profit"] += premium
        account_totals[acct]["total_max_loss"] += max_loss

    return {
        "portfolio": {
            "total_strategies": len(rows),
            "total_max_profit": sum(e["total_max_profit"] for e in exposure_by_underlying.values()),
            "total_max_loss": sum(e["total_max_loss"] for e in exposure_by_underlying.values()),
            "positions_expiring_soon": 0,
        },
        "accounts": list(account_totals.values()),
        "underlying_exposure": list(exposure_by_underlying.values()),
    }


def query_underlying_exposure(account: str | None = None) -> list[dict[str, Any]]:
    """Compute underlying exposure from open_options directly."""
    analysis = query_portfolio_analysis()
    return analysis.get("underlying_exposure", [])
