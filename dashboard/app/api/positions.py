from fastapi import APIRouter, Query

from ..services.db import query

router = APIRouter()


@router.get("/underlyings")
def list_underlyings(account: str | None = Query(None)):
    sql = """
        SELECT DISTINCT oo.underlying
        FROM open_options oo
        JOIN accounts a ON a.id = oo.account_id
        WHERE date(oo.expiry) > date('now')
    """
    params: list[str] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    sql += " ORDER BY oo.underlying"
    rows = query(sql, tuple(params))
    return {"data": [r["underlying"] for r in rows]}


@router.get("")
def list_positions(account: str | None = Query(None), underlying: str | None = Query(None)):
    sql = """
        SELECT oo.*, a.name as account_name
        FROM open_options oo
        JOIN accounts a ON a.id = oo.account_id
        WHERE date(oo.expiry) > date('now')
    """
    params: list[str] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    if underlying:
        sql += " AND oo.underlying = ?"
        params.append(underlying)
    sql += " ORDER BY oo.expiry, oo.underlying, oo.strike"
    rows = query(sql, tuple(params))
    return {"data": rows}


@router.get("/count")
def count_positions(account: str | None = Query(None)):
    sql = """
        SELECT COUNT(*) as count
        FROM open_options oo
        JOIN accounts a ON a.id = oo.account_id
        WHERE date(oo.expiry) > date('now')
    """
    params: list[str] = []
    if account:
        sql += " AND a.name = ?"
        params.append(account)
    rows = query(sql, tuple(params))
    return {"data": rows[0] if rows else {"count": 0}}
