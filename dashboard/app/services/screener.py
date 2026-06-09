"""Screener cache reader and trigger — replaces direct run_cli("analyze", "screener").

CLI writes results to cached_screener_results. This service reads from that
table. The screener button click triggers the CLI (to populate the cache),
then this service reads the results.
"""

from __future__ import annotations

from .cli import run_cli, CliError
from .db import query


def query_cached_screener_results() -> dict:
    """Read most recent screener results from cache table."""
    rows = query(
        """SELECT symbol, current_price, iv, iv_percentile, suggested_strike,
                  strike_dte, premium, annualized_yield, grade,
                  has_existing_position, existing_position_detail
           FROM cached_screener_results
           WHERE run_timestamp = (SELECT MAX(run_timestamp) FROM cached_screener_results)
           ORDER BY grade, symbol"""
    )

    results = []
    for r in rows:
        results.append({
            "symbol": r.get("symbol", ""),
            "current_price": r.get("current_price", 0),
            "iv": r.get("iv", 0),
            "iv_percentile": r.get("iv_percentile", 0),
            "suggested_strike": r.get("suggested_strike", 0),
            "strike_dte": r.get("strike_dte", 0),
            "premium": r.get("premium", 0),
            "annualized_yield": r.get("annualized_yield", 0),
            "grade": r.get("grade", "C"),
            "has_existing_position": bool(r.get("has_existing_position", 0)),
            "existing_position_detail": r.get("existing_position_detail", ""),
        })
    return results


def trigger_screener() -> dict:
    """Run CLI analyze screener to populate cache. Returns status info.

    Raises CliError if the CLI fails.
    """
    data = run_cli("analyze", "screener")
    d = data.get("data", {})
    return {
        "total_scanned": d.get("total_scanned", 0),
        "passed_filter": d.get("passed_filter", 0),
        "errors": d.get("errors", []),
    }
