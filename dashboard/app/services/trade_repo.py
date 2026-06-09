"""Centralized SQL queries for trades, dividends, interest, and share events.

Single source of truth for all dashboard data access. Callbacks call these
functions instead of writing raw SQL.
"""

from .db import query
from .symbols import _derive_market_from_symbol, _year_sql


def query_dividends(account: str | None = None, market: str | None = None, year: str | None = None) -> list[dict]:
    """Query dividends with standard filters. Returns full dividend rows."""
    sql = (
        "SELECT d.id, a.name AS account, d.symbol, d.description, d.ex_date, d.pay_date, "
        "d.amount, d.tax_withheld, d.currency "
        "FROM dividends d JOIN accounts a ON a.id = d.account_id WHERE 1=1"
    )
    params: list = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    yr_sql, yr_params = _year_sql(year, "d.pay_date")
    sql += yr_sql
    params.extend(yr_params)
    sql += " ORDER BY d.pay_date DESC"

    try:
        dividends = query(sql, tuple(params))
    except Exception:
        return []

    if market and market != "All Markets":
        dividends = [d for d in dividends if _derive_market_from_symbol(d.get("symbol", "")) == market]
    return dividends


def query_dividend_totals(account: str | None = None, year: str | None = None) -> tuple[float, dict[str, float]]:
    """Return (total_net_dividends, {symbol: net_dividend})."""
    sql = "SELECT d.symbol, d.amount, d.tax_withheld FROM dividends d JOIN accounts a ON a.id = d.account_id WHERE 1=1"
    params: list = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    yr_sql, yr_params = _year_sql(year, "d.pay_date")
    sql += yr_sql
    params.extend(yr_params)

    total = 0.0
    by_symbol: dict[str, float] = {}
    try:
        for row in query(sql, tuple(params)):
            net = (row["amount"] or 0) - (row["tax_withheld"] or 0)
            total += net
            by_symbol[row["symbol"]] = by_symbol.get(row["symbol"], 0) + net
    except Exception:
        pass
    return total, by_symbol


def query_dividend_totals_by_symbol(year: str | None = None) -> dict[str, float]:
    """Per-symbol dividend totals (for P&L overview table)."""
    sql = "SELECT d.symbol, SUM(d.amount - d.tax_withheld) as net FROM dividends d WHERE 1=1"
    params: list = []
    yr_sql, yr_params = _year_sql(year, "d.pay_date")
    sql += yr_sql
    params.extend(yr_params)
    sql += " GROUP BY d.symbol"

    result: dict[str, float] = {}
    try:
        for row in query(sql, tuple(params)):
            result[row["symbol"]] = row["net"] or 0
    except Exception:
        pass
    return result


def query_interest(account: str | None = None, year: str | None = None) -> float:
    """Total margin interest."""
    sql = "SELECT i.amount FROM interest_expenses i JOIN accounts a ON a.id = i.account_id WHERE 1=1"
    params: list = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    yr_sql, yr_params = _year_sql(year, "i.date")
    sql += yr_sql
    params.extend(yr_params)

    total = 0.0
    try:
        for row in query(sql, tuple(params)):
            total += row["amount"] or 0
    except Exception:
        pass
    return total


def query_share_events(account: str | None = None, market: str | None = None, year: str | None = None) -> list[dict]:
    """Share events (assignments, called away)."""
    sql = (
        "SELECT se.id, a.name AS account, se.underlying, se.event_date, se.event_type, "
        "se.shares, se.strike, se.premium, se.multiplier, se.split_adjusted "
        "FROM share_events se JOIN accounts a ON a.id = se.account_id WHERE 1=1"
    )
    params: list = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    yr_sql, yr_params = _year_sql(year, "se.event_date")
    sql += yr_sql
    params.extend(yr_params)
    sql += " ORDER BY se.event_date DESC"

    try:
        events = query(sql, tuple(params))
    except Exception:
        return []

    if market and market != "All Markets":
        events = [e for e in events if _derive_market_from_symbol(e.get("underlying", "")) == market]
    return events


def query_stock_sell_trades(account: str | None = None, market: str | None = None, year: str | None = None) -> list[dict]:
    """Realized P&L summary from sell trades (grouped by symbol/account)."""
    sql = (
        "SELECT st.symbol, a.name AS account, "
        "SUM(ABS(st.quantity)) as shares_sold, "
        "SUM(st.net_cash) as total_proceeds "
        "FROM stock_trades st JOIN accounts a ON a.id = st.account_id "
        "WHERE st.quantity < 0 AND st.symbol NOT LIKE '%.%'"
    )
    params: list = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    yr_sql, yr_params = _year_sql(year, "st.trade_date")
    sql += yr_sql
    params.extend(yr_params)
    sql += " GROUP BY st.symbol, a.name"

    try:
        rows = query(sql, tuple(params))
    except Exception:
        return []

    if market and market != "All Markets":
        rows = [r for r in rows if _derive_market_from_symbol(r.get("symbol", "")) == market]
    return rows


def query_open_short_calls() -> dict[str, list]:
    """Cross-reference open short calls by underlying (for wheel status)."""
    info: dict[str, list] = {}
    try:
        for row in query(
            "SELECT underlying, strike, expiry FROM open_options "
            "WHERE quantity < 0 AND right = 'C'"
        ):
            ul = row["underlying"]
            parts = info.setdefault(ul, [])
            parts.append({"strike": row["strike"], "expiry": row["expiry"]})
    except Exception:
        pass
    return info


def query_existing_positions_map() -> dict[str, str]:
    """Cross-reference open options to describe existing positions per underlying."""
    info: dict[str, list[str]] = {}
    try:
        for row in query(
            "SELECT underlying, right, SUM(ABS(quantity)) as cnt "
            "FROM open_options WHERE quantity < 0 GROUP BY underlying, right"
        ):
            ul = row["underlying"]
            parts = info.setdefault(ul, [])
            label = "P" if row["right"] == "P" else "C"
            parts.append(f"{int(row['cnt'])}{label} open")
    except Exception:
        pass
    return {ul: ", ".join(parts) for ul, parts in info.items()}


def query_latest_assignments(underlyings: list[str] | None = None) -> dict[str, list[dict]]:
    """Latest assignment(s) per underlying from trades with notes_codes containing 'A'.

    Returns {underlying: [{right, strike, trade_date, qty}, ...]} — all assignments
    on the most recent assignment date for that underlying.
    Deduplicates by (strike, right) to avoid showing identical badges per account.
    """
    result: dict[str, list[dict]] = {}
    try:
        sql = (
            "SELECT underlying, right, strike, trade_date, SUM(ABS(quantity)) as qty "
            "FROM trades WHERE notes_codes LIKE '%A%'"
        )
        params: list = []
        if underlyings:
            placeholders = ",".join("?" * len(underlyings))
            sql += f" AND underlying IN ({placeholders})"
            params.extend(underlyings)
        sql += " GROUP BY underlying, right, strike, trade_date ORDER BY underlying, trade_date DESC"
        rows = query(sql, tuple(params))
    except Exception:
        return result

    for row in rows:
        ul = row["underlying"]
        td = row["trade_date"]
        if ul not in result:
            result[ul] = []
        # Only include rows from the latest date (first group encountered per underlying)
        if not result[ul] or result[ul][0]["trade_date"] == td:
            result[ul].append({
                "right": row["right"],
                "strike": row["strike"],
                "trade_date": td,
                "qty": row["qty"],
            })
    return result


def query_available_years() -> list[str]:
    """Distinct trade years for year filter dropdown."""
    try:
        rows = query("SELECT DISTINCT substr(trade_date,1,4) AS y FROM trades ORDER BY y DESC")
        return [r["y"] for r in rows if r["y"]]
    except Exception:
        return []
