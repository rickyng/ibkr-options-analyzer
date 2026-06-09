"""Shared CLI data accessors — exposure, calendar, flatten, existing positions.

Consolidates functions that were duplicated between account_mgmt.py and
positions.py, plus the existing-positions lookup shared with screener.py.
"""

from collections import defaultdict

from ...services.db import query as db_query
from ...services.positions import (
    query_positions_by_expiry_date,
    query_positions_by_expiry_bucket,
    query_open_positions_enriched,
)
from ...services.strategies import query_underlying_exposure
from ...services.trade_repo import query_existing_positions_map
from ._helpers import _derive_currency_from_symbol, _derive_market_from_symbol


def _fetch_exposure(account: str | None = None) -> list:
    """Fetch underlying exposure from cached portfolio analysis."""
    return query_underlying_exposure(account=account)


def _fetch_expiry_calendar(account: str | None = None) -> dict:
    """Fetch positions grouped by expiry date."""
    return query_positions_by_expiry_date(account=account)


def _fetch_expiry_buckets(account: str | None = None) -> dict:
    """Fetch positions grouped by W1-W5+ buckets."""
    return query_positions_by_expiry_bucket(account=account)


def _flatten_calendar(calendar: dict) -> list:
    """Flatten calendar data into a flat position list with enriched fields."""
    positions = []
    for expiry, items in calendar.items():
        for pos in items:
            underlying = pos.get("underlying", "")
            mult = pos.get("multiplier", 100) or 100
            currency = pos.get("currency") or _derive_currency_from_symbol(underlying, mult)
            market = pos.get("market") or _derive_market_from_symbol(underlying, mult)
            positions.append({
                "underlying": underlying,
                "expiry": expiry,
                "strike": pos.get("strike", 0),
                "right": pos.get("right", ""),
                "quantity": pos.get("quantity", 0),
                "premium": pos.get("entry_premium", 0),
                "distance_pct": pos.get("distance_from_strike_pct", 0),
                "risk_category": pos.get("risk_category", "SAFE"),
                "current_price": pos.get("current_price"),
                "account_name": pos.get("account", "") or pos.get("account_name", ""),
                "currency": currency,
                "market": market,
                "multiplier": mult,
            })
    return positions


def _get_existing_positions_map() -> dict[str, str]:
    """Cross-reference open options to describe existing positions per underlying."""
    return query_existing_positions_map()
