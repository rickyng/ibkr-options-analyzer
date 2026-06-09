"""Symbol classification and market/currency helpers.

All monetary values in the DB are now stored in USD (converted at import time
by the C++ CLI). The dashboard reads USD values directly and does NOT perform
currency conversion.
"""


# -- Symbol → market derivation (for display/filtering only) --


def _derive_currency_from_symbol(underlying: str, multiplier: int | None = None) -> str:
    """Derive the original trading currency from symbol pattern.

    NOTE: This is for display purposes only (showing which market a position is in).
    All DB values are already in USD. Return 'USD' for consistency.
    """
    # All values in DB are now USD after schema 3.2.0 migration
    return "USD"


def _derive_market_from_symbol(underlying: str, multiplier: int | None = None) -> str:
    """Derive market region from symbol pattern (for filtering)."""
    if not underlying:
        return "US"
    if underlying.endswith(".T"):
        return "JP"
    symbol_core = underlying.replace(".HK", "").replace(".T", "")
    if symbol_core.isdigit():
        if multiplier is not None and multiplier == 1:
            return "JP"
        return "HK"
    return "US"


# -- Year filter SQL helper --


def _year_sql(year: str | None, column: str = "trade_date") -> tuple[str, list]:
    """Return (sql_fragment, params) to filter a date column by year."""
    if not year or year == "all":
        return "", []
    return f" AND substr({column},1,4) = ?", [year]