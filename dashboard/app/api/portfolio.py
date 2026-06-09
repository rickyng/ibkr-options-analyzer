from fastapi import APIRouter, Query

from ..services.strategies import query_portfolio_analysis, query_underlying_exposure
from ..services.positions import query_positions_by_expiry_date

router = APIRouter()


@router.get("/risk")
def portfolio_risk(account: str | None = Query(None)):
    analysis = query_portfolio_analysis()
    return {
        "data": {
            "portfolio": analysis.get("portfolio", {}),
            "accounts": analysis.get("accounts", []),
        }
    }


@router.get("/exposure")
def portfolio_exposure(account: str | None = Query(None)):
    exposure = query_underlying_exposure(account=account)
    return {"data": exposure}


@router.get("/expiry-calendar")
def expiry_calendar(account: str | None = Query(None)):
    positions_by_expiry = query_positions_by_expiry_date(account=account)
    return {"data": positions_by_expiry}
