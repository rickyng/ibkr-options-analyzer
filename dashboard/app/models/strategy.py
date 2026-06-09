from datetime import date
from uuid import UUID

from pydantic import BaseModel


class StrategyLeg(BaseModel):
    option_id: UUID
    leg_role: str
    strike: float
    right: str
    quantity: float


class Strategy(BaseModel):
    id: UUID
    account_id: UUID
    strategy_type: str
    underlying: str
    expiry: date
    legs: list[StrategyLeg]
    net_premium: float
    max_profit: float
    max_loss: float | str
    breakeven_price: float
    risk_level: str
    confidence: float
