from fastapi import APIRouter, Query

from ..services.cli import run_cli
from ..services.positions import (
    query_open_positions_enriched,
    query_positions_by_expiry_bucket,
    query_positions_summary,
)
from ..services.strategies import (
    query_detected_strategies,
    query_portfolio_analysis,
    query_underlying_exposure,
)

router = APIRouter()


@router.get("/open")
def analyze_open(account: str | None = Query(None), underlying: str | None = Query(None)):
    positions = query_open_positions_enriched(account=account, underlying=underlying)
    summary = query_positions_summary(account=account)
    buckets = query_positions_by_expiry_bucket(account=account)
    return {
        "data": {
            "status": "ok",
            "count": len(positions),
            "summary": summary,
            "positions": buckets,
        }
    }


@router.get("/strategy")
def analyze_strategy(account: str | None = Query(None), underlying: str | None = Query(None)):
    strategies = query_detected_strategies(account=account, underlying=underlying)
    portfolio = query_portfolio_analysis()
    return {
        "data": {
            "status": "ok",
            "count": len(strategies),
            "strategies": strategies,
            "portfolio": portfolio.get("portfolio", {}),
            "accounts": portfolio.get("accounts", []),
            "underlying_exposure": portfolio.get("underlying_exposure", []),
        }
    }


@router.get("/impact/{underlying}")
def analyze_impact(underlying: str, account: str | None = Query(None)):
    args = ["impact", "--underlying", underlying]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)
    return {"data": data}
