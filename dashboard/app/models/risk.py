from pydantic import BaseModel


class UnderlyingRisk(BaseModel):
    profit: float
    loss: float


class RiskyPosition(BaseModel):
    underlying: str
    strategy_type: str
    max_loss: float | str
    strike: float


class RiskSummary(BaseModel):
    margin_pct: int
    total_positions: int
    estimated_profit: float
    estimated_loss: float
    by_underlying: dict[str, UnderlyingRisk]
    top_riskiest: list[RiskyPosition]
