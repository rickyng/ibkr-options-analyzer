from pathlib import Path

from pydantic_settings import BaseSettings

_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent


class Settings(BaseSettings):
    db_path: Path = Path.home() / ".ibkr-options-analyzer" / "data.db"
    cli_path: Path = _PROJECT_ROOT / "build" / "release" / "ibkr-options-analyzer"
    api_host: str = "127.0.0.1"
    api_port: int = 8001

    model_config = {"env_prefix": "IBKR_"}


settings = Settings()
