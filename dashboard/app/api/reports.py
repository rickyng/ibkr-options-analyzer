from fastapi import APIRouter, Query
from fastapi.responses import PlainTextResponse

from ..services.cli import run_cli

router = APIRouter()


@router.get("/positions.csv")
def positions_csv(account: str | None = Query(None)):
    args = ["--output", "/dev/stdout"]
    if account:
        args.extend(["--account", account])
    result = run_cli("report", "--type", "positions", *args)
    return PlainTextResponse(content=str(result), media_type="text/csv")


@router.get("/strategies.csv")
def strategies_csv(account: str | None = Query(None)):
    args = []
    if account:
        args.extend(["--account", account])
    result = run_cli("report", "--type", "strategies", "--output", "/dev/stdout", *args)
    return PlainTextResponse(content=str(result), media_type="text/csv")


@router.get("/summary")
def summary(account: str | None = Query(None)):
    args = []
    if account:
        args.extend(["--account", account])
    result = run_cli("report", "--type", "summary", *args)
    return {"data": result}
