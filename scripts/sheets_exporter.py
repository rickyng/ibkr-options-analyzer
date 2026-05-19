#!/usr/bin/env python3
"""
Google Sheets exporter for IBKR Options Analyzer.

Uses a persistent spreadsheet (ID stored in ~/.ibkr-options-analyzer/google/spreadsheet_id.txt).
Layout matches the web UI: Dashboard summary + detail tabs.

Usage:
    ibkr-options-analyzer trades --google-sheet
"""

import argparse
import json
import sys
import urllib.request
import urllib.error
from pathlib import Path
from typing import Any
from datetime import date, datetime

from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

from sheets_auth import get_credentials

# ── Config ──────────────────────────────────────────────────────

CONFIG_DIR = Path.home() / ".ibkr-options-analyzer" / "google"
SPREADSHEET_ID_FILE = CONFIG_DIR / "spreadsheet_id.txt"
SPREADSHEET_TITLE = "IBKR Options Analyzer"

# FX rates for currency conversion
FX_RATES = {"USD": 1.0, "JPY": 0.0067, "HKD": 0.13, "EUR": 1.08, "GBP": 1.25}

# Contract multiplier by market (JP options have multiplier of 1)
CONTRACT_MULTIPLIER = {"US": 100, "HK": 100, "JP": 1}

YAHOO_URL = "https://query1.finance.yahoo.com/v8/finance/chart/{symbol}"

# ── Format Types ────────────────────────────────────────────────

CURRENCY = "currency"
PERCENT = "percent"
NUMBER = "number"
TEXT = "text"

NUMBER_FORMATS = {
    CURRENCY: {"type": "CURRENCY", "pattern": "$#,##0"},
    PERCENT: {"type": "PERCENT", "pattern": "0.0%"},
    NUMBER: {"type": "NUMBER", "pattern": "#,##0"},
    TEXT: None,
}

# ── Column Schemas (key columns first, matching UI) ─────────────

TRADES_COLS = [
    ("account", TEXT),
    ("underlying", TEXT),
    ("right", TEXT),
    ("strike", CURRENCY),
    ("expiry", TEXT),
    ("quantity", NUMBER),
    ("open_date", TEXT),
    ("close_date", TEXT),
    ("close_reason", TEXT),
    ("net_premium", CURRENCY, True),
    ("realized_pnl", CURRENCY, True),
    ("roc", PERCENT),
    ("holding_days", NUMBER),
    ("strategy_type", TEXT),
]

WHEEL_COLS = [
    ("account", TEXT),
    ("underlying", TEXT),
    ("cycle_status", TEXT),
    ("total_pnl", CURRENCY, True),
    ("option_pnl", CURRENCY, True),
    ("stock_pnl", CURRENCY, True),
    ("put_strike", CURRENCY),
    ("call_strike", CURRENCY),
    ("put_premium", CURRENCY),
    ("call_premium", CURRENCY),
    ("quantity", NUMBER),
    ("put_assigned_date", TEXT),
    ("call_close_date", TEXT),
]

STRATEGY_COLS = [
    ("strategy_type", TEXT),
    ("trade_count", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
    ("avg_pnl", CURRENCY),
    ("profit_factor", NUMBER),
]

MONTHLY_COLS = [
    ("month", TEXT),
    ("trade_count", NUMBER),
    ("winning_trades", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
    ("avg_pnl", CURRENCY),
]

UNDERLYING_COLS = [
    ("underlying", TEXT),
    ("trade_count", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
    ("avg_pnl", CURRENCY),
]

ACCOUNT_COLS = [
    ("account", TEXT),
    ("trades", NUMBER),
    ("winning", NUMBER),
    ("win_rate", PERCENT),
    ("option_pnl", CURRENCY, True),
    ("stock_pnl", CURRENCY, True),
    ("total_pnl", CURRENCY, True),
]


# ── Helpers ─────────────────────────────────────────────────────

def col_letter(idx: int) -> str:
    result = ""
    while idx >= 0:
        result = chr(idx % 26 + ord("A")) + result
        idx = idx // 26 - 1
    return result


def fetch_price(symbol: str) -> float | None:
    """Fetch current price from Yahoo Finance using stdlib."""
    try:
        url = YAHOO_URL.format(symbol=symbol) + "?range=1d&interval=1d"
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode())
            return data["chart"]["result"][0]["meta"].get("regularMarketPrice")
    except Exception:
        pass
    return None


def currency_for_symbol(underlying: str, multiplier: int | None = None) -> str:
    if underlying.endswith(".T"):
        return "JPY"
    core = underlying.replace(".HK", "").replace(".T", "")
    if core.isdigit():
        # JP stocks have multiplier=1, HK stocks have multiplier=100
        if multiplier is not None and multiplier == 1:
            return "JPY"
        return "HKD"
    return "USD"


def market_for_symbol(underlying: str, multiplier: int | None = None) -> str:
    if not underlying:
        return "US"
    if underlying.endswith(".T"):
        return "JP"
    core = underlying.replace(".HK", "").replace(".T", "")
    if core.isdigit():
        if multiplier is not None and multiplier == 1:
            return "JP"
        return "HK"
    return "US"


def convert_to_usd(amount: float, currency: str) -> float:
    return amount * FX_RATES.get(currency, 1.0)


def rt_pnl(rt: dict) -> float:
    """Effective P&L for a round-trip, preferring net_pnl over realized_pnl."""
    if "net_pnl" in rt:
        return rt["net_pnl"]
    return rt.get("realized_pnl", 0) or 0


def convert_trades_to_usd(data: dict):
    """Convert all P&L values to USD (matches web UI _convert_trades_to_usd)."""
    trips = data.get("round_trips", [])
    if not trips:
        return

    # Derive actual multiplier from each round-trip
    for rt in trips:
        qty = abs(rt.get("quantity", 0) or 0)
        op = abs(rt.get("open_price", 0) or 0)
        np_val = abs(rt.get("net_premium", 0) or 0)
        if qty > 0 and op > 0:
            rt["_multiplier"] = round(np_val / (qty * op))
        else:
            rt_mult = rt.get("_multiplier", 100)
            market = market_for_symbol(rt.get("underlying", ""), int(rt_mult))
            rt["_multiplier"] = CONTRACT_MULTIPLIER.get(market, 100)

    # Recalculate P&L for assigned trades
    assigned = [rt for rt in trips if rt.get("close_reason") == "assigned"]
    prices = {}
    if assigned:
        underlyings = {rt.get("underlying", "") for rt in assigned if rt.get("underlying")}
        for sym in underlyings:
            p = fetch_price(sym)
            if p is not None:
                prices[sym] = p

    for rt in assigned:
        underlying = rt.get("underlying", "")
        strike = rt.get("strike", 0) or 0
        right = rt.get("right", "P")
        qty = rt.get("quantity", 1) or 1
        open_price = rt.get("open_price", 0) or 0
        commission = rt.get("commission", 0) or 0
        multiplier = rt.get("_multiplier", 100)
        currency = currency_for_symbol(underlying, int(multiplier))

        option_pnl = qty * multiplier * open_price - commission
        rt["realized_pnl"] = convert_to_usd(option_pnl, currency)
        rt["close_price"] = 0

        current_price = prices.get(underlying)
        if current_price is not None:
            if right == "P":
                stock_pnl = (current_price - strike) * qty * multiplier
            else:
                stock_pnl = (strike - current_price) * qty * multiplier
            rt["assigned_stock_pnl"] = convert_to_usd(stock_pnl, currency)
            rt["current_price"] = current_price
        else:
            rt["assigned_stock_pnl"] = 0

        rt["net_pnl"] = rt["realized_pnl"] + rt.get("assigned_stock_pnl", 0)
        rt["assigned_shares"] = int(qty * multiplier)
        collateral = strike * qty * multiplier
        if collateral > 0:
            rt["roc"] = (rt["net_pnl"] / collateral) * 100

    # Convert non-assigned P&L
    for rt in trips:
        if rt.get("close_reason") != "assigned":
            underlying = rt.get("underlying", "")
            rt_mult = rt.get("_multiplier", 100)
            currency = currency_for_symbol(underlying, int(rt_mult))
            for key in ("realized_pnl", "net_premium"):
                if key in rt:
                    rt[key] = convert_to_usd(rt[key] or 0, currency)
        rt.pop("_multiplier", None)

    # Recompute overview
    if trips:
        non_assigned = [t for t in trips if t.get("close_reason") != "assigned"]
        assigned_trips = [t for t in trips if t.get("close_reason") == "assigned"]
        winning = len(non_assigned)
        losing = len(assigned_trips)
        non_assigned_pnl = sum(rt_pnl(t) for t in non_assigned)
        assigned_pnl = sum(rt_pnl(t) for t in assigned_trips)
        total_pnl = non_assigned_pnl + assigned_pnl

        if assigned_pnl != 0:
            profit_factor = non_assigned_pnl / abs(assigned_pnl)
        elif non_assigned_pnl > 0:
            profit_factor = float("inf")
        else:
            profit_factor = 0

        pnls = [rt_pnl(t) for t in trips]
        holding_days = [t.get("holding_days", 0) for t in trips if t.get("holding_days")]
        avg_roc = sum(t.get("roc", 0) or 0 for t in trips) / len(trips)

        data["overview"] = {
            "total_trades": len(trips),
            "winning_trades": winning,
            "losing_trades": losing,
            "win_rate": winning / len(trips) if trips else 0,
            "total_option_pnl": sum(t.get("realized_pnl", 0) or 0 for t in trips),
            "total_stock_pnl": sum(t.get("assigned_stock_pnl", 0) or 0 for t in trips),
            "total_pnl": total_pnl,
            "avg_winner": non_assigned_pnl / winning if winning else 0,
            "avg_loser": abs(assigned_pnl) / losing if losing else 0,
            "profit_factor": profit_factor,
            "expectancy": total_pnl / len(trips) if trips else 0,
            "avg_holding_days": sum(holding_days) / len(holding_days) if holding_days else 0,
            "avg_roc": avg_roc,
            "avg_annualized_return": sum(t.get("annualized_return", 0) or 0 for t in trips) / len(trips),
        }

    # Recompute strategy performance
    strat_map = {}
    for t in trips:
        st = t.get("strategy_type") or "Unknown"
        strat_map.setdefault(st, []).append(t)
    data["strategy_performance"] = []
    for st, st_trips in sorted(strat_map.items()):
        st_pnls = [rt_pnl(t) for t in st_trips]
        st_wins = [p for p in st_pnls if p >= 0]
        st_losses = [p for p in st_pnls if p < 0]
        st_total = sum(st_pnls)
        st_total_wins = sum(st_wins)
        st_total_losses = abs(sum(st_losses))
        st_days = [t.get("holding_days", 0) for t in st_trips if t.get("holding_days")]
        data["strategy_performance"].append({
            "strategy_type": st,
            "trade_count": len(st_trips),
            "winning_trades": len(st_wins),
            "win_rate": len(st_wins) / len(st_trips) if st_trips else 0,
            "total_pnl": st_total,
            "avg_pnl": st_total / len(st_trips) if st_trips else 0,
            "profit_factor": st_total_wins / st_total_losses if st_total_losses else 0,
            "avg_holding_days": sum(st_days) / len(st_days) if st_days else 0,
        })

    # Recompute monthly breakdown (YTD)
    current_year = date.today().year
    month_names = {
        "01": "Jan", "02": "Feb", "03": "Mar", "04": "Apr",
        "05": "May", "06": "Jun", "07": "Jul", "08": "Aug",
        "09": "Sep", "10": "Oct", "11": "Nov", "12": "Dec",
    }

    def _norm_month(cd):
        if not cd:
            return ""
        if "-" in cd:
            return cd[:7]
        elif len(cd) >= 6:
            return cd[:4] + "-" + cd[4:6]
        return cd

    month_map = {}
    for t in trips:
        cd = _norm_month(t.get("close_date", ""))
        if not cd or int(cd[:4]) != current_year:
            continue
        month_map.setdefault(cd, []).append(t)

    data["monthly_breakdown"] = []
    for month_key in sorted(month_map.keys()):
        m_trips = month_map[month_key]
        opt_pnl = sum(t.get("realized_pnl", 0) or 0 for t in m_trips)
        stk_pnl = sum(t.get("assigned_stock_pnl", 0) or 0 for t in m_trips)
        net = sum(rt_pnl(t) for t in m_trips)
        m_wins = [t for t in m_trips if rt_pnl(t) >= 0]
        data["monthly_breakdown"].append({
            "month": f"{month_names.get(month_key[5:], month_key)} {month_key[:4]}",
            "trade_count": len(m_trips),
            "winning_trades": len(m_wins),
            "win_rate": len(m_wins) / len(m_trips) if m_trips else 0,
            "total_pnl": net,
            "avg_pnl": net / len(m_trips) if m_trips else 0,
        })


def enrich_wheel_cycles(data: dict):
    """Enrich incomplete/stock_held cycles with unrealized stock P&L (matches web UI)."""
    cycles = data.get("wheel_cycles", [])
    if not cycles:
        return

    open_cycles = [c for c in cycles if c.get("cycle_status") in ("incomplete", "stock_held")]
    if not open_cycles:
        return

    # Fetch prices for open underlyings
    underlyings = {c.get("underlying", "") for c in open_cycles}
    prices = {}
    for sym in underlyings:
        if not sym:
            continue
        p = fetch_price(sym)
        if p is not None:
            prices[sym] = p

    # Enrich each open cycle
    for c in open_cycles:
        ul = c.get("underlying", "")
        put_strike = c.get("put_strike", 0) or 0
        qty = c.get("quantity", 1) or 1
        mult = c.get("multiplier", 100) or 100
        price = prices.get(ul)
        currency = currency_for_symbol(ul, int(mult))

        if price is not None:
            unrealized = (price - put_strike) * qty * mult
            c["stock_pnl"] = convert_to_usd(unrealized, currency)
            c["current_price"] = price

        put_prem = c.get("put_premium") or 0
        call_prem = c.get("call_premium") or 0
        c["option_pnl"] = convert_to_usd(put_prem, currency) + convert_to_usd(call_prem, currency)
        c["total_pnl"] = (c.get("stock_pnl") or 0) + c.get("option_pnl", 0)

    # Convert completed cycle P&L too
    for c in cycles:
        if c.get("cycle_status") == "completed":
            ul = c.get("underlying", "")
            mult = c.get("multiplier", 100) or 100
            currency = currency_for_symbol(ul, int(mult))
            c["option_pnl"] = convert_to_usd(c.get("option_pnl") or 0, currency)
            c["stock_pnl"] = convert_to_usd(c.get("stock_pnl") or 0, currency) if c.get("stock_pnl") is not None else None
            c["total_pnl"] = (c.get("stock_pnl") or 0) + c.get("option_pnl", 0)

    # Recalculate overview
    completed = [c for c in cycles if c.get("cycle_status") == "completed"]
    held = [c for c in cycles if c.get("cycle_status") == "stock_held"]
    incomplete = [c for c in cycles if c.get("cycle_status") == "incomplete"]

    data["wheel_overview"] = {
        "total_option_pnl": sum(c.get("option_pnl", 0) or 0 for c in cycles),
        "total_stock_pnl": sum(c.get("stock_pnl", 0) or 0 for c in cycles),
        "total_wheel_pnl": sum(c.get("total_pnl", 0) or 0 for c in cycles),
        "completed_cycles": len(completed),
        "stock_held_cycles": len(held),
        "incomplete_cycles": len(incomplete),
    }


def format_val(v: Any, fmt: str) -> Any:
    if v is None:
        return ""
    if fmt in (CURRENCY, NUMBER, PERCENT):
        try:
            return float(v)
        except (ValueError, TypeError):
            return v
    return str(v)


def rows_from_data(data: Any, columns: list) -> list[list]:
    headers = [col[0] for col in columns]
    if isinstance(data, dict):
        return [headers, [format_val(data.get(c[0]), c[1]) for c in columns]]
    if isinstance(data, list) and data and isinstance(data[0], dict):
        return [headers] + [
            [format_val(item.get(c[0]), c[1]) for c in columns] for item in data
        ]
    return []


def totals_row(rows: list[list], columns: list) -> list[list]:
    if len(rows) < 3:
        return rows
    last = len(rows)
    totals = []
    labelled = False
    for i, col in enumerate(columns):
        if col[1] in (CURRENCY, NUMBER, PERCENT):
            totals.append(f"=SUM({col_letter(i)}2:{col_letter(i)}{last})")
        elif not labelled:
            totals.append(f"TOTAL ({last - 1})")
            labelled = True
        else:
            totals.append("")
    return rows + [totals]


# ── Spreadsheet Management ──────────────────────────────────────

def get_or_create_spreadsheet(service: Any) -> str:
    """Return existing or new spreadsheet ID."""
    if SPREADSHEET_ID_FILE.exists():
        sid = SPREADSHEET_ID_FILE.read_text().strip()
        try:
            service.spreadsheets().get(spreadsheetId=sid).execute()
            return sid
        except HttpError:
            SPREADSHEET_ID_FILE.unlink()

    # Create new
    result = service.spreadsheets().create(
        body={"properties": {"title": SPREADSHEET_TITLE}}
    ).execute()
    sid = result["spreadsheetId"]
    SPREADSHEET_ID_FILE.write_text(sid)
    return sid


def clear_tabs(service: Any, sid: str):
    """Delete all sheets except the first one."""
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    requests = []
    for sheet in meta["sheets"][1:]:  # Keep first sheet
        requests.append({"deleteSheet": {"sheetId": sheet["properties"]["sheetId"]}})
    if requests:
        service.spreadsheets().batchUpdate(spreadsheetId=sid, body={"requests": requests}).execute()


def get_sheet_id(service: Any, sid: str, name: str) -> int | None:
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    for s in meta["sheets"]:
        if s["properties"]["title"] == name:
            return s["properties"]["sheetId"]
    return None


# ── Tab Creation ────────────────────────────────────────────────

def create_tab(service: Any, sid: str, name: str, data: Any, columns: list, totals: bool = False):
    rows = rows_from_data(data, columns)
    if not rows:
        return

    if totals:
        rows = totals_row(rows, columns)

    num_rows = len(rows)
    num_cols = len(columns)

    # Ensure sheet exists
    sheet_id = get_sheet_id(service, sid, name)
    if sheet_id is None:
        result = service.spreadsheets().batchUpdate(
            spreadsheetId=sid,
            body={"requests": [{"addSheet": {"properties": {"title": name}}}]},
        ).execute()
        sheet_id = result["replies"][0]["addSheet"]["properties"]["sheetId"]

    # Write values
    service.spreadsheets().values().update(
        spreadsheetId=sid,
        range=f"{name}!A1:{col_letter(num_cols - 1)}{num_rows}",
        valueInputOption="USER_ENTERED",
        body={"values": rows},
    ).execute()

    # Apply formatting
    apply_formatting(service, sid, sheet_id, columns, num_rows, num_cols, totals)


def apply_formatting(service: Any, sid: str, sheet_id: int, columns: list, num_rows: int, num_cols: int, has_totals: bool):
    reqs = []

    # Frozen header
    reqs.append({
        "updateSheetProperties": {
            "properties": {"sheetId": sheet_id, "gridProperties": {"frozenRowCount": 1}},
            "fields": "gridProperties.frozenRowCount",
        }
    })

    # Header style
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": sheet_id, "startRowIndex": 0, "endRowIndex": 1},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.15, "green": 0.20, "blue": 0.29},
                    "textFormat": {"foregroundColor": {"red": 0.97, "green": 0.98, "blue": 0.99}, "bold": True},
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat",
        }
    })

    # Column formats + conditional
    data_end = num_rows - (1 if has_totals else 0)
    for i, col in enumerate(columns):
        fmt = NUMBER_FORMATS.get(col[1])
        if fmt:
            reqs.append({
                "repeatCell": {
                    "range": {"sheetId": sheet_id, "startRowIndex": 1, "endRowIndex": data_end, "startColumnIndex": i, "endColumnIndex": i + 1},
                    "cell": {"userEnteredFormat": {"numberFormat": fmt}},
                    "fields": "userEnteredFormat.numberFormat",
                }
            })

        # Conditional for P&L
        if len(col) > 2 and col[2]:
            rng = {"sheetId": sheet_id, "startRowIndex": 1, "endRowIndex": data_end, "startColumnIndex": i, "endColumnIndex": i + 1}
            reqs.append({
                "addConditionalFormatRule": {
                    "rule": {
                        "ranges": [rng],
                        "booleanRule": {
                            "condition": {"type": "NUMBER_LESS", "values": [{"userEnteredValue": "0"}]},
                            "format": {
                                "backgroundColor": {"red": 1.0, "green": 0.92, "blue": 0.92},
                                "textFormat": {"foregroundColor": {"red": 0.8}},
                            },
                        },
                    },
                    "index": 0,
                }
            })
            reqs.append({
                "addConditionalFormatRule": {
                    "rule": {
                        "ranges": [rng],
                        "booleanRule": {
                            "condition": {"type": "NUMBER_GREATER_THAN_EQ", "values": [{"userEnteredValue": "0"}]},
                            "format": {
                                "backgroundColor": {"red": 0.92, "green": 1.0, "blue": 0.92},
                                "textFormat": {"foregroundColor": {"green": 0.5}},
                            },
                        },
                    },
                    "index": 1,
                }
            })

    # Totals row
    if has_totals and num_rows > 1:
        reqs.append({
            "repeatCell": {
                "range": {"sheetId": sheet_id, "startRowIndex": num_rows - 1, "endRowIndex": num_rows},
                "cell": {
                    "userEnteredFormat": {
                        "backgroundColor": {"red": 0.94, "green": 0.94, "blue": 0.94},
                        "textFormat": {"bold": True},
                        "borders": {"top": {"style": "SOLID", "width": 2}},
                    }
                },
                "fields": "userEnteredFormat",
            }
        })

    # Auto-filter on header row
    if num_rows > 2 and num_cols > 0:
        reqs.append({
            "setBasicFilter": {
                "filter": {
                    "range": {
                        "sheetId": sheet_id,
                        "startRowIndex": 0,
                        "endRowIndex": num_rows,
                        "startColumnIndex": 0,
                        "endColumnIndex": num_cols,
                    }
                }
            }
        })

    if reqs:
        service.spreadsheets().batchUpdate(spreadsheetId=sid, body={"requests": reqs}).execute()


# ── Dashboard Tab (matches web UI cards) ────────────────────────

def compute_underlying_from_cycles(data: dict) -> list[dict]:
    """Aggregate wheel cycle P&L by underlying (matches web UI wheel cycle view)."""
    cycles = data.get("wheel_cycles", [])
    if not cycles:
        return []

    grouped = {}
    for c in cycles:
        ul = c.get("underlying", "Unknown")
        if ul not in grouped:
            grouped[ul] = {"cycles": 0, "winning": 0, "total_pnl": 0.0}
        grouped[ul]["cycles"] += 1
        pnl = c.get("total_pnl") or 0
        grouped[ul]["total_pnl"] += pnl
        if pnl > 0:
            grouped[ul]["winning"] += 1

    result = []
    for ul, stats in grouped.items():
        n = stats["cycles"]
        result.append({
            "underlying": ul,
            "trade_count": n,
            "win_rate": stats["winning"] / n if n > 0 else 0,
            "total_pnl": stats["total_pnl"],
            "avg_pnl": stats["total_pnl"] / n if n > 0 else 0,
        })
    return sorted(result, key=lambda x: x["underlying"])


def compute_account_breakdown(data: dict) -> list[dict]:
    """Aggregate stats by account from round_trips and wheel_cycles."""
    trips = data.get("round_trips", [])
    cycles = data.get("wheel_cycles", [])

    accounts = {}
    for t in trips:
        acc = t.get("account", "Unknown")
        if acc not in accounts:
            accounts[acc] = {"trades": 0, "winning": 0, "option_pnl": 0.0}
        accounts[acc]["trades"] += 1
        if t.get("realized_pnl", 0) > 0:
            accounts[acc]["winning"] += 1
        accounts[acc]["option_pnl"] += t.get("realized_pnl", 0) or 0

    for c in cycles:
        acc = c.get("account", "Unknown")
        if acc not in accounts:
            accounts[acc] = {"trades": 0, "winning": 0, "option_pnl": 0.0, "stock_pnl": 0.0}
        accounts[acc]["stock_pnl"] = accounts[acc].get("stock_pnl", 0.0) + (c.get("stock_pnl") or 0)

    result = []
    for acc, stats in accounts.items():
        total = stats["trades"]
        win_rate = stats["winning"] / total if total > 0 else 0
        stock_pnl = stats.get("stock_pnl", 0.0)
        total_pnl = stats["option_pnl"] + stock_pnl
        result.append({
            "account": acc,
            "trades": total,
            "winning": stats["winning"],
            "win_rate": win_rate,
            "option_pnl": stats["option_pnl"],
            "stock_pnl": stock_pnl,
            "total_pnl": total_pnl,
        })
    return sorted(result, key=lambda x: x["account"])


def create_dashboard_tab(service: Any, sid: str, data: dict):
    """Summary tab: KPI cards + Account breakdown + Market info."""
    ov = data.get("overview", {})
    wheel = data.get("wheel_overview", {})
    trips = data.get("round_trips", [])
    cycles = data.get("wheel_cycles", [])

    # --- KPI Row (row 1-2) ---
    kpi_labels = ["Trades", "Win Rate", "Option P&L", "Stock P&L", "Wheel P&L", "Profit Factor", "Avg ROC"]
    kpi_values = [
        ov.get("total_trades", 0) or 0,
        ov.get("win_rate", 0) or 0,
        ov.get("total_option_pnl", wheel.get("total_option_pnl", 0)) or 0,
        ov.get("total_stock_pnl", wheel.get("total_stock_pnl", 0)) or 0,
        wheel.get("total_wheel_pnl", ov.get("total_pnl", 0)) or 0,
        ov.get("profit_factor", 0) or 0,
        ov.get("avg_roc", 0) or 0,
    ]
    kpi_formats = [NUMBER, PERCENT, CURRENCY, CURRENCY, CURRENCY, NUMBER, PERCENT]

    # --- Market Info (row 3) ---
    underlyings = set(t.get("underlying", "") for t in trips)
    markets = {"US": 0, "JP": 0, "HK": 0, "Other": 0}
    for u in underlyings:
        if u.endswith(".T"):
            markets["JP"] += 1
        elif u.isdigit() or u.endswith(".HK"):
            markets["HK"] += 1
        elif u:
            markets["US"] += 1
    market_labels = ["Underlyings", "US Markets", "JP Markets", "HK Markets", "Wheel Cycles", "Completed", "Held"]
    market_values = [
        len(underlyings),
        markets["US"],
        markets["JP"],
        markets["HK"],
        wheel.get("completed_cycles", 0) + wheel.get("stock_held_cycles", 0) + wheel.get("incomplete_cycles", 0),
        wheel.get("completed_cycles", 0) or 0,
        (wheel.get("stock_held_cycles", 0) or 0) + (wheel.get("incomplete_cycles", 0) or 0),
    ]
    market_formats = [NUMBER, NUMBER, NUMBER, NUMBER, NUMBER, NUMBER, NUMBER]

    # --- Account Breakdown (row 5+) ---
    account_data = compute_account_breakdown(data)

    # Rename first sheet to Dashboard
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    first_id = meta["sheets"][0]["properties"]["sheetId"]
    service.spreadsheets().batchUpdate(
        spreadsheetId=sid,
        body={"requests": [{"updateSheetProperties": {"properties": {"sheetId": first_id, "title": "Dashboard"}, "fields": "title"}}]},
    ).execute()

    # Write all sections
    rows = []
    rows.append(kpi_labels)  # row 1
    rows.append([format_val(v, f) for v, f in zip(kpi_values, kpi_formats)])  # row 2
    rows.append(market_labels)  # row 3
    rows.append([format_val(v, f) for v, f in zip(market_values, market_formats)])  # row 4
    rows.append([])  # blank row 5
    rows.append([col[0] for col in ACCOUNT_COLS])  # account header row 6
    for acc in account_data:
        rows.append([format_val(acc.get(c[0]), c[1]) for c in ACCOUNT_COLS])  # account data rows 7+

    service.spreadsheets().values().update(
        spreadsheetId=sid,
        range=f"Dashboard!A1:G{len(rows)}",
        valueInputOption="USER_ENTERED",
        body={"values": rows},
    ).execute()

    # Formatting requests
    reqs = []

    # KPI header (row 1)
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": first_id, "startRowIndex": 0, "endRowIndex": 1},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.15, "green": 0.20, "blue": 0.29},
                    "textFormat": {"foregroundColor": {"red": 0.97, "green": 0.98, "blue": 0.99}, "bold": True, "fontSize": 11},
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat",
        }
    })

    # KPI values (row 2)
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": first_id, "startRowIndex": 1, "endRowIndex": 2},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.12, "green": 0.16, "blue": 0.23},
                    "textFormat": {"foregroundColor": {"red": 1.0, "green": 0.85, "blue": 0.13}, "bold": True, "fontSize": 12},
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat",
        }
    })

    # Per-column number format for KPI row
    for i, fmt in enumerate(kpi_formats):
        nf = NUMBER_FORMATS.get(fmt)
        if nf:
            reqs.append({
                "repeatCell": {
                    "range": {"sheetId": first_id, "startRowIndex": 1, "endRowIndex": 2, "startColumnIndex": i, "endColumnIndex": i + 1},
                    "cell": {"userEnteredFormat": {"numberFormat": nf}},
                    "fields": "userEnteredFormat.numberFormat",
                }
            })

    # Market header (row 3) - lighter gray
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": first_id, "startRowIndex": 2, "endRowIndex": 3},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.25, "green": 0.30, "blue": 0.38},
                    "textFormat": {"foregroundColor": {"red": 0.85, "green": 0.88, "blue": 0.92}, "bold": True, "fontSize": 10},
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat",
        }
    })

    # Market values (row 4)
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": first_id, "startRowIndex": 3, "endRowIndex": 4},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.20, "green": 0.25, "blue": 0.32},
                    "textFormat": {"foregroundColor": {"red": 0.75, "green": 0.80, "blue": 0.85}, "fontSize": 10},
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat",
        }
    })

    # Account table header (row 6)
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": first_id, "startRowIndex": 5, "endRowIndex": 6},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.15, "green": 0.20, "blue": 0.29},
                    "textFormat": {"foregroundColor": {"red": 0.97, "green": 0.98, "blue": 0.99}, "bold": True},
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat",
        }
    })

    # Account table columns (format + conditional)
    account_start = 6
    account_end = len(rows)
    for i, col in enumerate(ACCOUNT_COLS):
        fmt = NUMBER_FORMATS.get(col[1])
        if fmt:
            reqs.append({
                "repeatCell": {
                    "range": {"sheetId": first_id, "startRowIndex": account_start, "endRowIndex": account_end, "startColumnIndex": i, "endColumnIndex": i + 1},
                    "cell": {"userEnteredFormat": {"numberFormat": fmt}},
                    "fields": "userEnteredFormat.numberFormat",
                }
            })
        if len(col) > 2 and col[2]:
            rng = {"sheetId": first_id, "startRowIndex": account_start, "endRowIndex": account_end, "startColumnIndex": i, "endColumnIndex": i + 1}
            reqs.append({
                "addConditionalFormatRule": {
                    "rule": {
                        "ranges": [rng],
                        "booleanRule": {
                            "condition": {"type": "NUMBER_LESS", "values": [{"userEnteredValue": "0"}]},
                            "format": {
                                "backgroundColor": {"red": 1.0, "green": 0.92, "blue": 0.92},
                                "textFormat": {"foregroundColor": {"red": 0.8}},
                            },
                        },
                    },
                    "index": 0,
                }
            })
            reqs.append({
                "addConditionalFormatRule": {
                    "rule": {
                        "ranges": [rng],
                        "booleanRule": {
                            "condition": {"type": "NUMBER_GREATER_THAN_EQ", "values": [{"userEnteredValue": "0"}]},
                            "format": {
                                "backgroundColor": {"red": 0.92, "green": 1.0, "blue": 0.92},
                                "textFormat": {"foregroundColor": {"green": 0.5}},
                            },
                        },
                    },
                    "index": 1,
                }
            })

    # Freeze top 5 rows (KPI + Market headers)
    reqs.append({
        "updateSheetProperties": {
            "properties": {"sheetId": first_id, "gridProperties": {"frozenRowCount": 5}},
            "fields": "gridProperties.frozenRowCount",
        }
    })

    service.spreadsheets().batchUpdate(spreadsheetId=sid, body={"requests": reqs}).execute()


# ── Export ──────────────────────────────────────────────────────

def export_trades(service: Any, sid: str, data: dict):
    """Export trades data matching web UI layout."""
    convert_trades_to_usd(data)
    enrich_wheel_cycles(data)
    create_dashboard_tab(service, sid, data)

    underlying_data = compute_underlying_from_cycles(data) or data.get("underlying_breakdown")
    tabs = [
        ("Trades", data.get("round_trips"), TRADES_COLS, True),
        ("Wheel Cycles", data.get("wheel_cycles"), WHEEL_COLS, True),
        ("Strategy", data.get("strategy_performance"), STRATEGY_COLS, True),
        ("Monthly", data.get("monthly_breakdown"), MONTHLY_COLS, True),
        ("Underlying", underlying_data, UNDERLYING_COLS, True),
    ]

    for name, tab_data, cols, totals in tabs:
        if tab_data:
            create_tab(service, sid, name, tab_data, cols, totals)


def export_report(service: Any, sid: str, data: dict):
    """Export report data."""
    positions = data.get("positions", data.get("data", {}).get("positions"))
    risk = data.get("risk_summaries", data.get("data", {}).get("risk_summaries"))
    exposures = data.get("underlying_exposures", data.get("data", {}).get("underlying_exposures", data.get("exposures")))

    # Simple dashboard for report
    rows = [["Positions", "Risk Summaries", "Exposures"], [len(positions or []), len(risk or []), len(exposures or [])]]
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    first_id = meta["sheets"][0]["properties"]["sheetId"]
    service.spreadsheets().batchUpdate(
        spreadsheetId=sid,
        body={"requests": [{"updateSheetProperties": {"properties": {"sheetId": first_id, "title": "Dashboard"}, "fields": "title"}}]},
    ).execute()
    service.spreadsheets().values().update(
        spreadsheetId=sid,
        range="Dashboard!A1:C2",
        valueInputOption="USER_ENTERED",
        body={"values": rows},
    ).execute()

    for name, tab_data in [("Positions", positions), ("Risk", risk), ("Exposures", exposures)]:
        if tab_data:
            create_raw_tab(service, sid, name, tab_data)


def create_raw_tab(service: Any, sid: str, name: str, data: Any):
    if isinstance(data, dict):
        headers, rows = list(data.keys()), [str(v) for v in data.values()]
    elif isinstance(data, list) and data and isinstance(data[0], dict):
        headers = list(data[0].keys())
        rows = [[str(item.get(k, "")) for k in headers] for item in data]
    else:
        return
    if not rows:
        return

    sheet_id = get_sheet_id(service, sid, name)
    if sheet_id is None:
        result = service.spreadsheets().batchUpdate(
            spreadsheetId=sid,
            body={"requests": [{"addSheet": {"properties": {"title": name}}}]},
        ).execute()
        sheet_id = result["replies"][0]["addSheet"]["properties"]["sheetId"]

    service.spreadsheets().values().update(
        spreadsheetId=sid,
        range=f"{name}!A1:{col_letter(len(headers) - 1)}{len(rows) + 1}",
        valueInputOption="USER_ENTERED",
        body={"values": [headers] + rows},
    ).execute()


# ── Main ────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Export IBKR analysis to Google Sheets")
    parser.add_argument("command", choices=["trades", "report"])
    args = parser.parse_args()

    try:
        data = json.loads(sys.stdin.read())
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON: {e}", file=sys.stderr)
        return 1

    creds = get_credentials()
    if not creds:
        print("Error: Run scripts/sheets_auth.py login", file=sys.stderr)
        return 1

    service = build("sheets", "v4", credentials=creds)
    sid = get_or_create_spreadsheet(service)
    clear_tabs(service, sid)

    try:
        if args.command == "trades":
            export_trades(service, sid, data)
        else:
            export_report(service, sid, data)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    url = f"https://docs.google.com/spreadsheets/d/{sid}/edit"
    print(url)
    return 0


if __name__ == "__main__":
    sys.exit(main())