"""Trade review API endpoints."""

from fastapi import APIRouter
from typing import Optional

from ..services.trades import (
    query_trades_overview,
    query_option_trades,
    query_trades_by_strategy,
    query_trades_by_dte,
    query_trades_by_underlying,
    query_loss_clusters,
    query_streak_info,
)

router = APIRouter()


@router.get("/overview")
def get_trades_overview(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
    underlying: Optional[str] = None,
):
    return query_trades_overview(account=account, date_from=date_from,
                                 date_to=date_to, underlying=underlying)


@router.get("/list")
def get_trades_list(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
    underlying: Optional[str] = None,
):
    return query_option_trades(account=account, date_from=date_from,
                               date_to=date_to, underlying=underlying)


@router.get("/strategy-performance")
def get_strategy_performance(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    return query_trades_by_strategy(account=account, date_from=date_from, date_to=date_to)


@router.get("/dte-breakdown")
def get_dte_breakdown(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    return query_trades_by_dte(account=account, date_from=date_from, date_to=date_to)


@router.get("/underlying-breakdown")
def get_underlying_breakdown(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    return query_trades_by_underlying(account=account, date_from=date_from, date_to=date_to)


@router.get("/loss-clusters")
def get_loss_clusters(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    return query_loss_clusters(account=account, date_from=date_from, date_to=date_to)


@router.get("/streaks")
def get_streak_info(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    return query_streak_info(account=account, date_from=date_from, date_to=date_to)


@router.get("/export")
def export_trades_csv(
    account: Optional[str] = None,
    date_from: Optional[str] = None,
    date_to: Optional[str] = None,
):
    return query_option_trades(account=account, date_from=date_from, date_to=date_to)
