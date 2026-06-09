from fastapi import APIRouter, Query
from fastapi.responses import PlainTextResponse

from ..services.reports import generate_positions_csv, generate_strategies_csv, generate_summary

router = APIRouter()


@router.get("/positions.csv")
def positions_csv(account: str | None = Query(None)):
    csv_text = generate_positions_csv(account=account)
    return PlainTextResponse(content=csv_text, media_type="text/csv")


@router.get("/strategies.csv")
def strategies_csv(account: str | None = Query(None)):
    csv_text = generate_strategies_csv(account=account)
    return PlainTextResponse(content=csv_text, media_type="text/csv")


@router.get("/summary")
def summary(account: str | None = Query(None)):
    return {"data": generate_summary(account=account)}
