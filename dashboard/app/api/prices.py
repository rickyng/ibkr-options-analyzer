import asyncio

import httpx
from fastapi import APIRouter

router = APIRouter()

YAHOO_CHART_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"


async def _fetch_price(client: httpx.AsyncClient, symbol: str) -> tuple[str, float | None]:
    try:
        resp = await client.get(
            YAHOO_CHART_URL.format(symbol=symbol),
            params={"range": "1d", "interval": "1d"},
            headers={"User-Agent": "Mozilla/5.0"},
        )
        if resp.status_code == 200:
            data = resp.json()
            meta = data["chart"]["result"][0]["meta"]
            return symbol, meta.get("regularMarketPrice")
    except Exception:
        pass
    return symbol, None


@router.get("/{symbol}")
async def get_price(symbol: str):
    async with httpx.AsyncClient() as client:
        resp = await client.get(
            YAHOO_CHART_URL.format(symbol=symbol),
            params={"range": "1d", "interval": "1d"},
            headers={"User-Agent": "Mozilla/5.0"},
        )
        if resp.status_code != 200:
            return {"error": f"Failed to fetch price for {symbol}"}
        data = resp.json()
        result = data.get("chart", {}).get("result", [])
        if not result:
            return {"error": f"No price data for {symbol}"}
        meta = result[0].get("meta", {})
        price = meta.get("regularMarketPrice")
        return {"data": {"symbol": symbol, "price": price}}


@router.post("/batch")
async def batch_prices(symbols: list[str]):
    results: dict[str, float | None] = {}
    async with httpx.AsyncClient() as client:
        tasks = [_fetch_price(client, symbol) for symbol in symbols]
        responses = await asyncio.gather(*tasks)
        for symbol, price in responses:
            results[symbol] = price
    return {"data": results}
