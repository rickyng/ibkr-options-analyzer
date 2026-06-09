from fastapi import APIRouter, Query

from ..services.db import query

router = APIRouter()


@router.get("")
def list_strategies(account: str | None = Query(None)):
    sql = """
        SELECT ds.*, a.name as account_name
        FROM detected_strategies ds
        JOIN accounts a ON a.id = ds.account_id
        WHERE date(ds.expiry) >= date('now')
    """
    params: list[str] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    sql += " ORDER BY ds.expiry, ds.underlying"
    rows = query(sql, tuple(params))
    return {"data": rows}


@router.get("/by-underlying")
def strategies_by_underlying(account: str | None = Query(None)):
    strategies = list_strategies(account)["data"]
    grouped: dict[str, list] = {}
    for s in strategies:
        grouped.setdefault(s["underlying"], []).append(s)
    return {"data": grouped}


@router.get("/risk")
def risk_summary(margin: int = Query(0)):
    # TODO: implement risk calculation from strategies
    return {"data": {"margin_pct": margin, "total_positions": 0}}
