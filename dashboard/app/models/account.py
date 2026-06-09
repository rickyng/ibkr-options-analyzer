from pydantic import BaseModel


class AccountCreate(BaseModel):
    name: str
    token: str
    query_id: str
    enabled: bool = True


class AccountUpdate(BaseModel):
    name: str | None = None
    token: str | None = None
    query_id: str | None = None
    enabled: bool | None = None
