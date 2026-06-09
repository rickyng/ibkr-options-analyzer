"""Price lookup API — reads from cached_prices table.

Prices are populated by the C++ CLI (refresh/analyze commands) which handles
multi-source fallback (Yahoo → Alpha Vantage → synthetic) and caching.
The dashboard does NOT call external APIs directly.
"""

from fastapi import APIRouter

from ..services.db import query_one

router = APIRouter()


@router.get("/{symbol}")
def get_price(symbol: str):
    row = query_one("SELECT price FROM cached_prices WHERE symbol = ?", (symbol,))
    if not row:
        return {"error": f"No cached price for {symbol}. Run CLI refresh first."}
    return {"data": {"symbol": symbol, "price": row["price"]}}


@router.post("/batch")
def batch_prices(symbols: list[str]):
    results = {}
    for sym in symbols:
        row = query_one("SELECT price FROM cached_prices WHERE symbol = ?", (sym,))
        results[sym] = row["price"] if row else None
    return {"data": results}
