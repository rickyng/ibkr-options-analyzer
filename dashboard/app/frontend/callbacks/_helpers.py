"""Shared helper functions for dashboard callbacks.

Re-exports symbol helpers from services.symbols so callbacks can use
a single import site. Also contains callback-specific utilities.
"""

from datetime import datetime, date

from ...services.symbols import (
    _derive_market_from_symbol,
    _derive_currency_from_symbol,
    _year_sql,
)

# FX-like symbols to exclude from P&L chart
_FX_SYMBOLS = {"USD.HKD", "USD.JPY", "USD.CNH", "EUR.USD", "GBP.USD"}


def _apply_year_filter(items: list, year: str | None, date_field: str = "date") -> list:
    """Filter a list of dicts by year, extracting year from a date field (YYYY-MM-DD)."""
    if not year or year == "all":
        return items
    return [t for t in items if (t.get(date_field) or "")[:4] == year]


def _parse_expiry(expiry_str: str) -> date | None:
    """Parse expiry string in YYYYMMDD or YYYY-MM-DD format."""
    if not expiry_str:
        return None
    for fmt in ("%Y%m%d", "%Y-%m-%d"):
        try:
            return datetime.strptime(expiry_str, fmt).date()
        except ValueError:
            continue
    return None
