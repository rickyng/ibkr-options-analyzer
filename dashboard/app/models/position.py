from datetime import date
from uuid import UUID

from pydantic import BaseModel


class Position(BaseModel):
    id: UUID
    account_id: UUID
    account_name: str
    symbol: str
    underlying: str
    expiry: date
    strike: float
    right: str
    quantity: float
    multiplier: int = 100
    mark_price: float | None = None
    entry_premium: float | None = None
    current_value: float | None = None
    dte: int
    is_manual: bool = False
    notes: str = ""
