"""Stock holdings computation — running average cost basis and realized P&L.

Extracted from callbacks.py for testability and reuse.

All DB values are now in USD (converted at import time by the C++ CLI).
Prices come from cached_prices table (populated by CLI analyze/import).
No external API calls — the CLI owns all price fetching.
"""

from .db import query
from .symbols import _derive_market_from_symbol


def compute_stock_holdings(
    account: str | None, market: str | None, year: str | None = None
) -> tuple[dict[str, dict], dict[str, float]]:
    """Compute current stock holdings and realized P&L from DB.

    Always loads ALL transactions to maintain correct running cost basis.
    When `year` is specified, only realized P&L from sells in that year
    is counted — holdings and unrealized P&L always reflect current state.

    Returns (holdings, realized_pnl):
      holdings = {symbol: {shares, avg_cost_usd, current_price_usd, account, earliest_date}}
      realized_pnl = {symbol: total_realized_pnl_usd}
    """
    # 1. Collect transactions from stock_trades (always all — needed for cost basis)
    txns: list[tuple[str, str, str, float, float, float]] = []
    st_dates: set[tuple[str, str, str]] = set()

    st_sql = (
        "SELECT a.name AS account, st.symbol, st.quantity, st.trade_price, st.trade_date "
        "FROM stock_trades st JOIN accounts a ON a.id = st.account_id "
        "WHERE 1=1 AND st.symbol NOT LIKE '%.%'"
    )
    st_params: list = []
    if account:
        st_sql += " AND a.name = ?"
        st_params.append(account)

    try:
        for row in query(st_sql, tuple(st_params)):
            sym = row["symbol"]
            td = (row["trade_date"] or "")[:10]
            if market and market != "All Markets":
                if _derive_market_from_symbol(sym) != market:
                    continue
            qty = row["quantity"] or 0
            price = row["trade_price"] or 0
            buy_cost = price * qty if qty > 0 else 0
            sell_proceeds = price * abs(qty) if qty < 0 else 0
            txns.append((td, row["account"], sym, qty, buy_cost, sell_proceeds))
            if td:
                st_dates.add((row["account"], sym, td))
    except Exception:
        pass

    # 2. Add share_events for dates NOT in stock_trades (always all — needed for cost basis)
    try:
        se_sql = (
            "SELECT a.name AS account, se.underlying, se.event_date, se.shares, se.strike "
            "FROM share_events se JOIN accounts a ON a.id = se.account_id WHERE 1=1"
        )
        se_params: list = []
        if account:
            se_sql += " AND a.name = ?"
            se_params.append(account)
        for row in query(se_sql, tuple(se_params)):
            sym = row["underlying"]
            td = (row["event_date"] or "")[:10]
            acc = row["account"]
            if (acc, sym, td) in st_dates:
                continue
            if market and market != "All Markets":
                if _derive_market_from_symbol(sym) != market:
                    continue
            shares = row["shares"] or 0
            strike = row["strike"] or 0
            buy_cost = strike * shares if shares > 0 else 0
            txns.append((td, acc, sym, shares, buy_cost, 0))
    except Exception:
        pass

    # 3. Sort chronologically and apply running average cost, tracking realized P&L
    txns.sort(key=lambda t: (t[0], t[1], t[2]))
    raw_holdings: dict[tuple, dict] = {}
    for td, acc, sym, aq, buy_cost, sell_proceeds in txns:
        key = (acc, sym)
        if key not in raw_holdings:
            raw_holdings[key] = {"account": acc, "symbol": sym, "shares": 0.0, "total_cost": 0.0, "realized_pnl": 0.0, "earliest_date": None}
        h = raw_holdings[key]
        if aq > 0:
            h["shares"] += aq
            h["total_cost"] += buy_cost
        elif aq < 0:
            sell_qty = abs(aq)
            if h["shares"] > 0:
                avg = h["total_cost"] / h["shares"]
                # Only count realized P&L for sells in the selected year
                if not year or year == "all" or td[:4] == year:
                    h["realized_pnl"] += sell_proceeds - avg * min(sell_qty, h["shares"])
                h["total_cost"] -= avg * min(sell_qty, h["shares"])
            h["shares"] -= sell_qty
        if td and (h["earliest_date"] is None or td < h["earliest_date"]):
            h["earliest_date"] = td

    # 4. Read prices from cached_prices (populated by CLI, values in USD)
    price_map: dict[str, float] = {}
    try:
        for row in query("SELECT symbol, price FROM cached_prices"):
            price_map[row["symbol"]] = row["price"]
    except Exception:
        pass

    # 5. Build result dicts, aggregating across accounts
    result: dict[str, dict] = {}
    realized: dict[str, float] = {}
    for key, h in raw_holdings.items():
        symbol = h["symbol"]

        # Accumulate realized P&L (already USD from DB)
        rpnl = h["realized_pnl"]
        if rpnl != 0:
            realized[symbol] = realized.get(symbol, 0) + rpnl

        if h["shares"] <= 0:
            continue
        avg_cost = h["total_cost"] / h["shares"] if h["shares"] > 0 else 0
        current_price = price_map.get(symbol, 0)

        # All values are now USD (converted at import by C++ CLI)
        avg_cost_usd = avg_cost
        current_price_usd = current_price

        if symbol in result:
            # Aggregate across accounts
            existing = result[symbol]
            total_shares = existing["shares"] + h["shares"]
            existing["avg_cost_usd"] = (
                (existing["avg_cost_usd"] * existing["shares"] + avg_cost_usd * h["shares"])
                / total_shares
            )
            existing["shares"] = total_shares
            if h["earliest_date"] and (
                existing["earliest_date"] is None or h["earliest_date"] < existing["earliest_date"]
            ):
                existing["earliest_date"] = h["earliest_date"]
        else:
            result[symbol] = {
                "shares": h["shares"],
                "avg_cost_usd": avg_cost_usd,
                "current_price_usd": current_price_usd,
                "account": h["account"],
                "earliest_date": h["earliest_date"],
            }

    return result, realized