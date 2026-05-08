from fastapi import APIRouter, HTTPException

from ..models.account import AccountCreate, AccountUpdate
from ..services.db import query, execute

router = APIRouter(prefix="/api/accounts", tags=["accounts"])


@router.get("")
def list_accounts():
    return query("SELECT id, name, token, query_id, enabled FROM accounts ORDER BY name")


@router.post("", status_code=201)
def create_account(body: AccountCreate):
    existing = query("SELECT id FROM accounts WHERE name = ?", (body.name,))
    if existing:
        raise HTTPException(status_code=409, detail=f"Account '{body.name}' already exists")
    account_id = execute(
        "INSERT INTO accounts (name, token, query_id, enabled) VALUES (?, ?, ?, ?)",
        (body.name, body.token, body.query_id, 1 if body.enabled else 0),
    )
    return {"id": account_id, "name": body.name}


@router.put("/{account_id}")
def update_account(account_id: int, body: AccountUpdate):
    existing = query("SELECT id FROM accounts WHERE id = ?", (account_id,))
    if not existing:
        raise HTTPException(status_code=404, detail="Account not found")

    sets = []
    params = []
    for field, value in body.model_dump(exclude_unset=True).items():
        if field == "enabled":
            sets.append(f"{field} = ?")
            params.append(1 if value else 0)
        else:
            sets.append(f"{field} = ?")
            params.append(value)

    if not sets:
        return existing[0]

    params.append(account_id)
    execute(f"UPDATE accounts SET {', '.join(sets)} WHERE id = ?", tuple(params))
    return query("SELECT id, name, token, query_id, enabled FROM accounts WHERE id = ?", (account_id,))[0]


@router.delete("/{account_id}")
def delete_account(account_id: int):
    existing = query("SELECT id FROM accounts WHERE id = ?", (account_id,))
    if not existing:
        raise HTTPException(status_code=404, detail="Account not found")
    execute("DELETE FROM accounts WHERE id = ?", (account_id,))
    return {"deleted": True}
