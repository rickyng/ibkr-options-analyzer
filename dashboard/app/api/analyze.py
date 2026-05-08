from fastapi import APIRouter, Query

from ..services.cli import run_cli

router = APIRouter()


@router.get("/open")
def analyze_open(account: str | None = Query(None), underlying: str | None = Query(None)):
    args = ["open"]
    if account:
        args.extend(["--account", account])
    if underlying:
        args.extend(["--underlying", underlying])
    data = run_cli("analyze", *args)
    return {"data": data}


@router.get("/strategy")
def analyze_strategy(account: str | None = Query(None), underlying: str | None = Query(None)):
    args = ["strategy"]
    if account:
        args.extend(["--account", account])
    if underlying:
        args.extend(["--underlying", underlying])
    data = run_cli("analyze", *args)
    return {"data": data}


@router.get("/impact/{underlying}")
def analyze_impact(underlying: str, account: str | None = Query(None)):
    args = ["impact", "--underlying", underlying]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)
    return {"data": data}
