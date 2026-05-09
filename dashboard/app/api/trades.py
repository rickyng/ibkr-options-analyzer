"""Trade review API endpoints."""

from fastapi import APIRouter
from typing import Optional

from ..services.cli import run_cli

router = APIRouter()


def _build_trades_args(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
    strategy: Optional[str] = None,
    underlying: Optional[str] = None,
) -> list[str]:
    """Build CLI args for trades command."""
    args: list[str] = []
    if account:
        args.extend(["--account", account])
    if date_from:
        args.extend(["--date-from", date_from])
    if date_to:
        args.extend(["--date-to", date_to])
    if strategy:
        args.extend(["--strategy", strategy])
    if underlying:
        args.extend(["--underlying", underlying])
    return args


@router.get("/overview")
def get_trades_overview(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
    strategy: Optional[str] = None,
    underlying: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to, strategy, underlying)
    data = run_cli("trades", *args)
    return data.get("overview", {})


@router.get("/list")
def get_trades_list(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
    strategy: Optional[str] = None,
    underlying: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to, strategy, underlying)
    data = run_cli("trades", *args)
    return data.get("round_trips", [])


@router.get("/strategy-performance")
def get_strategy_performance(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to)
    data = run_cli("trades", *args)
    return data.get("strategy_performance", [])


@router.get("/dte-breakdown")
def get_dte_breakdown(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to)
    data = run_cli("trades", *args)
    return data.get("dte_breakdown", [])


@router.get("/underlying-breakdown")
def get_underlying_breakdown(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to)
    data = run_cli("trades", *args)
    return data.get("underlying_breakdown", [])


@router.get("/loss-clusters")
def get_loss_clusters(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to)
    data = run_cli("trades", *args)
    return data.get("loss_clusters", [])


@router.get("/streaks")
def get_streak_info(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to)
    data = run_cli("trades", *args)
    return data.get("streak_info", {})


@router.post("/rebuild")
def rebuild_round_trips(account: Optional[str] = None):
    args: list[str] = ["--rebuild"]
    if account:
        args.extend(["--account", account])
    data = run_cli("trades", *args)
    return {"status": "ok", "matched": data.get("overview", {}).get("total_trades", 0)}


@router.get("/export")
def export_trades_csv(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    args = _build_trades_args(account, date_from, date_to)
    data = run_cli("trades", *args)
    return data.get("round_trips", [])
