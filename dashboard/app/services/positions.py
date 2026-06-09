"""Position queries with enrichment — replaces run_cli("analyze", "open").

Reads open_options + cached_prices directly from SQLite and computes
DTE, risk categories, calendar buckets, and exposure aggregation in Python.
"""

from __future__ import annotations

from datetime import date, timedelta
from typing import Any

from .db import query
from .symbols import _derive_market_from_symbol


# ---------------------------------------------------------------------------
# Risk thresholds (must match C++ analyze_command.cpp)
# ---------------------------------------------------------------------------

def _risk_category(right: str, strike: float, current_price: float | None) -> str:
    """Categorize risk based on distance from strike to current price."""
    if not current_price or current_price <= 0:
        return "LONG" if right == "C" else "SAFE"
    if right == "P":
        dist_pct = ((current_price - strike) / current_price) * 100.0
        itm = current_price < strike
    else:
        dist_pct = ((strike - current_price) / current_price) * 100.0
        itm = current_price > strike
    if itm or dist_pct <= 1.0:
        return "CRITICAL"
    if dist_pct <= 5.0:
        return "HIGH"
    if dist_pct <= 10.0:
        return "MODERATE"
    return "SAFE"


def _distance_from_strike_pct(right: str, strike: float, current_price: float | None) -> float:
    """Return distance % from strike (positive = OTM)."""
    if not current_price or current_price <= 0:
        return 0.0
    if right == "P":
        return ((current_price - strike) / current_price) * 100.0
    return ((strike - current_price) / current_price) * 100.0


def _in_the_money(right: str, strike: float, current_price: float | None) -> bool:
    if not current_price:
        return False
    if right == "P":
        return current_price < strike
    return current_price > strike


def _duration_bucket(expiry: str) -> str:
    """Compute calendar-week bucket (W1..W5+) matching C++ logic."""
    try:
        exp = date.fromisoformat(expiry)
    except (ValueError, TypeError):
        return "W5+"
    today = date.today()
    # Monday of this week
    today_monday = today - timedelta(days=today.weekday())
    exp_monday = exp - timedelta(days=exp.weekday())
    week_offset = (exp_monday - today_monday).days // 7
    if week_offset <= 0:
        return "W1"
    if week_offset == 1:
        return "W2"
    if week_offset == 2:
        return "W3"
    if week_offset == 3:
        return "W4"
    return "W5+"


def _days_to_expiry(expiry: str) -> int:
    try:
        return (date.fromisoformat(expiry) - date.today()).days
    except (ValueError, TypeError):
        return 999


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def query_open_positions_enriched(
    account: str | None = None,
    underlying: str | None = None,
) -> list[dict[str, Any]]:
    """Query open_options + cached_prices, compute DTE, risk, distance, buckets."""
    sql = """
        SELECT oo.id, oo.account_id, oo.symbol, oo.underlying, oo.expiry,
               oo.strike, oo.right, oo.quantity, oo.entry_premium,
               oo.multiplier, oo.is_manual,
               a.name AS account_name,
               cp.price AS current_price
        FROM open_options oo
        JOIN accounts a ON a.id = oo.account_id
        LEFT JOIN cached_prices cp ON cp.symbol = oo.underlying
        WHERE 1=1
    """
    params: list[Any] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    if underlying:
        sql += " AND oo.underlying = ?"
        params.append(underlying)
    sql += " ORDER BY oo.expiry, oo.underlying, oo.strike"

    rows = query(sql, tuple(params))

    for r in rows:
        cp = r.get("current_price")
        right = r.get("right", "P")
        strike = r.get("strike", 0)
        mult = int(r.get("multiplier") or 100)

        r["days_to_expiry"] = _days_to_expiry(r.get("expiry", ""))
        r["duration_bucket"] = _duration_bucket(r.get("expiry", ""))
        r["risk_category"] = _risk_category(right, strike, cp)
        r["distance_from_strike_pct"] = round(_distance_from_strike_pct(right, strike, cp), 2)
        r["in_the_money"] = _in_the_money(right, strike, cp)
        r["currency"] = "USD"
        r["market"] = _derive_market_from_symbol(r.get("underlying", ""), mult)
        r["account"] = r.get("account_name", "")  # alias for callback compatibility

    return rows


def query_positions_by_expiry_bucket(
    account: str | None = None,
) -> dict[str, list[dict]]:
    """Return {W1: [...], W2: [...], ...} for expiry calendar."""
    positions = query_open_positions_enriched(account=account)
    buckets: dict[str, list[dict]] = {}
    for p in positions:
        bucket = p.get("duration_bucket", "W5+")
        buckets.setdefault(bucket, []).append(p)
    return buckets


def query_positions_by_expiry_date(
    account: str | None = None,
) -> dict[str, list[dict]]:
    """Return {expiry_date: [positions...]} for expiry calendar."""
    positions = query_open_positions_enriched(account=account)
    by_expiry: dict[str, list[dict]] = {}
    for p in positions:
        exp = p.get("expiry", "")
        by_expiry.setdefault(exp, []).append(p)
    return by_expiry


def query_positions_summary(
    account: str | None = None,
) -> dict[str, Any]:
    """Return portfolio summary (counts, premium, max loss, expiring counts)."""
    positions = query_open_positions_enriched(account=account)
    total = len(positions)
    short_puts = sum(1 for p in positions if p.get("right") == "P" and p.get("quantity", 0) < 0)
    short_calls = sum(1 for p in positions if p.get("right") == "C" and p.get("quantity", 0) < 0)
    long_positions = total - short_puts - short_calls

    premium_collected = 0.0
    max_loss = 0.0
    expiring_7 = 0
    expiring_30 = 0

    for p in positions:
        qty = abs(p.get("quantity", 0) or 0)
        premium = p.get("entry_premium", 0) or 0
        strike = p.get("strike", 0) or 0
        mult = p.get("multiplier", 100) or 100
        dte = p.get("days_to_expiry", 999)

        if p.get("quantity", 0) < 0:
            premium_collected += premium * mult * qty
            if p.get("right") == "P":
                max_loss += (strike - premium) * mult * qty

        if 0 <= dte <= 7:
            expiring_7 += 1
        if 0 <= dte <= 30:
            expiring_30 += 1

    return {
        "total": total,
        "short_puts": short_puts,
        "short_calls": short_calls,
        "long_positions": long_positions,
        "premium_collected": round(premium_collected, 2),
        "max_loss": round(max_loss, 2),
        "expiring_7_days": expiring_7,
        "expiring_30_days": expiring_30,
    }


def query_cached_earnings(symbols: list[str]) -> dict[str, str]:
    """Read cached earnings dates from DB. Returns {symbol: next_earnings_date}."""
    if not symbols:
        return {}
    placeholders = ",".join("?" * len(symbols))
    rows = query(
        f"SELECT symbol, next_earnings_date FROM cached_earnings_dates WHERE symbol IN ({placeholders})",
        tuple(symbols),
    )
    return {r["symbol"]: r["next_earnings_date"] for r in rows if r.get("next_earnings_date")}
