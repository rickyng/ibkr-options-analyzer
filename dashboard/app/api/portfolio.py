from collections import defaultdict

from fastapi import APIRouter, Query

from ..services.cli import run_cli

router = APIRouter()


@router.get("/risk")
def portfolio_risk(account: str | None = Query(None)):
    args = ["strategy"]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)
    return {
        "data": {
            "portfolio": data.get("portfolio", {}),
            "accounts": data.get("accounts", []),
        }
    }


@router.get("/exposure")
def portfolio_exposure(account: str | None = Query(None)):
    args = ["strategy"]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)
    return {"data": data.get("underlying_exposure", [])}


@router.get("/expiry-calendar")
def expiry_calendar(account: str | None = Query(None)):
    args = ["open"]
    if account:
        args.extend(["--account", account])
    data = run_cli("analyze", *args)

    positions_by_expiry: dict[str, list] = defaultdict(list)
    buckets = data.get("positions", {})
    for _bucket_name, positions in buckets.items():
        for pos in positions:
            expiry = pos.get("expiry", "")
            positions_by_expiry[expiry].append(pos)

    return {"data": dict(positions_by_expiry)}
