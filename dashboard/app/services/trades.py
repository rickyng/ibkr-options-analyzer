"""Trade history queries — reads from trades + stock_trades tables.

All DB values are now in USD (converted at import time by the C++ CLI).
No currency conversion is performed in Python.
"""

from __future__ import annotations

from typing import Any

from .db import query


def query_option_trades(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
    underlying: str | None = None,
) -> list[dict[str, Any]]:
    """Query option trades (values already in USD from DB)."""
    sql = """
        SELECT t.trade_date AS date, t.symbol, t.underlying, t.right, t.strike,
               t.expiry, t.quantity, t.trade_price, t.proceeds, t.commission,
               t.net_cash, t.multiplier, t.notes_codes,
               COALESCE(a.name, '') AS account
        FROM trades t LEFT JOIN accounts a ON a.id = t.account_id
        WHERE 1=1
    """
    params: list[Any] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    if date_from:
        sql += " AND t.trade_date >= ?"
        params.append(date_from)
    if date_to:
        sql += " AND t.trade_date <= ?"
        params.append(date_to)
    if underlying:
        sql += " AND t.underlying = ?"
        params.append(underlying)
    sql += " ORDER BY t.trade_date DESC"

    rows = query(sql, tuple(params))
    for r in rows:
        r["currency"] = "USD"
    return rows


def query_stock_trades_list(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
    underlying: str | None = None,
) -> list[dict[str, Any]]:
    """Query stock trades (values already in USD from DB)."""
    sql = """
        SELECT st.trade_date AS date, st.symbol, st.description, st.quantity,
               st.trade_price, st.proceeds, st.commission, st.net_cash,
               st.notes_codes, COALESCE(a.name, '') AS account
        FROM stock_trades st LEFT JOIN accounts a ON a.id = st.account_id
        WHERE 1=1
    """
    params: list[Any] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    if date_from:
        sql += " AND st.trade_date >= ?"
        params.append(date_from)
    if date_to:
        sql += " AND st.trade_date <= ?"
        params.append(date_to)
    if underlying:
        sql += " AND st.symbol = ?"
        params.append(underlying)
    sql += " ORDER BY st.trade_date DESC"

    rows = query(sql, tuple(params))
    for r in rows:
        r["currency"] = "USD"
    return rows


def query_trades_overview(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
    underlying: str | None = None,
) -> dict[str, Any]:
    """Aggregate overview stats from option trades."""
    trades = query_option_trades(account=account, date_from=date_from,
                                 date_to=date_to, underlying=underlying)
    total_trades = len(trades)
    total_premium_in = 0.0
    total_premium_out = 0.0
    total_commissions = 0.0

    for t in trades:
        net_cash = t.get("net_cash", 0) or 0
        commission = t.get("commission", 0) or 0
        total_commissions += commission
        if net_cash > 0:
            total_premium_in += net_cash
        else:
            total_premium_out += abs(net_cash)

    return {
        "total_trades": total_trades,
        "total_premium_in": round(total_premium_in, 2),
        "total_premium_out": round(total_premium_out, 2),
        "net_premium": round(total_premium_in - total_premium_out, 2),
        "total_commissions": round(total_commissions, 2),
    }


def query_all_trades(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
    underlying: str | None = None,
) -> dict[str, Any]:
    """Return the combined dict that trade_review.py expects."""
    return {
        "overview": query_trades_overview(account=account, date_from=date_from,
                                          date_to=date_to, underlying=underlying),
        "option_trades": query_option_trades(account=account, date_from=date_from,
                                             date_to=date_to, underlying=underlying),
        "stock_trades": query_stock_trades_list(account=account, date_from=date_from,
                                                date_to=date_to, underlying=underlying),
    }


def query_trades_by_strategy(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
) -> list[dict[str, Any]]:
    """Group option trades by underlying + right for strategy performance."""
    trades = query_option_trades(account=account, date_from=date_from, date_to=date_to)
    groups: dict[str, dict] = {}
    for t in trades:
        key = f"{t.get('underlying', '')} {t.get('right', '')}"
        if key not in groups:
            groups[key] = {"underlying": t.get("underlying", ""), "right": t.get("right", ""),
                           "count": 0, "total_pnl": 0.0}
        groups[key]["count"] += 1
        groups[key]["total_pnl"] += t.get("net_cash", 0) or 0
    return list(groups.values())


def query_trades_by_dte(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
) -> list[dict[str, Any]]:
    """Group option trades by DTE bucket."""
    from datetime import date as dt
    trades = query_option_trades(account=account, date_from=date_from, date_to=date_to)
    buckets = {"0-7": [], "8-30": [], "31-60": [], "61-90": [], "90+": []}
    for t in trades:
        exp_str = t.get("expiry", "")
        try:
            exp = dt.fromisoformat(exp_str)
            trade_d = dt.fromisoformat(t.get("date", "")[:10])
            dte = (exp - trade_d).days
        except (ValueError, TypeError):
            dte = 999
        if dte <= 7:
            buckets["0-7"].append(t)
        elif dte <= 30:
            buckets["8-30"].append(t)
        elif dte <= 60:
            buckets["31-60"].append(t)
        elif dte <= 90:
            buckets["61-90"].append(t)
        else:
            buckets["90+"].append(t)
    return [{"bucket": k, "count": len(v),
             "total_pnl": round(sum(t.get("net_cash", 0) or 0 for t in v), 2)}
            for k, v in buckets.items()]


def query_trades_by_underlying(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
) -> list[dict[str, Any]]:
    """Group option trades by underlying."""
    trades = query_option_trades(account=account, date_from=date_from, date_to=date_to)
    groups: dict[str, dict] = {}
    for t in trades:
        ul = t.get("underlying", "")
        if ul not in groups:
            groups[ul] = {"underlying": ul, "count": 0, "total_pnl": 0.0}
        groups[ul]["count"] += 1
        groups[ul]["total_pnl"] += t.get("net_cash", 0) or 0
    return list(groups.values())


def query_loss_clusters(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
) -> list[dict[str, Any]]:
    """Identify consecutive loss sequences."""
    trades = sorted(
        query_option_trades(account=account, date_from=date_from, date_to=date_to),
        key=lambda t: t.get("date", ""),
    )
    clusters = []
    current: list[dict] = []
    for t in trades:
        if (t.get("net_cash", 0) or 0) < 0:
            current.append(t)
        else:
            if len(current) >= 2:
                clusters.append({
                    "count": len(current),
                    "total_loss": round(sum(x.get("net_cash", 0) or 0 for x in current), 2),
                    "start_date": current[0].get("date", ""),
                    "end_date": current[-1].get("date", ""),
                })
            current = []
    if len(current) >= 2:
        clusters.append({
            "count": len(current),
            "total_loss": round(sum(x.get("net_cash", 0) or 0 for x in current), 2),
            "start_date": current[0].get("date", ""),
            "end_date": current[-1].get("date", ""),
        })
    return clusters


def query_streak_info(
    account: str | None = None,
    date_from: str | None = None,
    date_to: str | None = None,
) -> dict[str, Any]:
    """Win/loss streak statistics."""
    trades = sorted(
        query_option_trades(account=account, date_from=date_from, date_to=date_to),
        key=lambda t: t.get("date", ""),
    )
    wins = sum(1 for t in trades if (t.get("net_cash", 0) or 0) > 0)
    losses = sum(1 for t in trades if (t.get("net_cash", 0) or 0) < 0)

    # Calculate streaks
    max_win_streak = 0
    max_loss_streak = 0
    cur_win = 0
    cur_loss = 0
    for t in trades:
        nc = t.get("net_cash", 0) or 0
        if nc > 0:
            cur_win += 1
            cur_loss = 0
            max_win_streak = max(max_win_streak, cur_win)
        elif nc < 0:
            cur_loss += 1
            cur_win = 0
            max_loss_streak = max(max_loss_streak, cur_loss)
        else:
            cur_win = 0
            cur_loss = 0

    return {
        "total_wins": wins,
        "total_losses": losses,
        "win_rate": round(wins / (wins + losses), 4) if (wins + losses) > 0 else 0,
        "max_win_streak": max_win_streak,
        "max_loss_streak": max_loss_streak,
    }