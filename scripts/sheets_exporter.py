#!/usr/bin/env python3
"""
Google Sheets exporter for IBKR Options Analyzer.

Reads JSON from stdin and creates a formatted Google Spreadsheet with
number formatting, conditional highlighting, and frozen headers.

Usage:
    ibkr-options-analyzer trades --google-sheet
    ibkr-options-analyzer report --google-sheet
"""

import argparse
import json
import sys
from datetime import datetime
from typing import Any

from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build

from sheets_auth import get_credentials

# ── Format Types ────────────────────────────────────────────────

CURRENCY = "currency"
PERCENT = "percent"
NUMBER = "number"
TEXT = "text"

NUMBER_FORMATS = {
    CURRENCY: {"type": "CURRENCY", "pattern": "$#,##0"},
    PERCENT: {"type": "PERCENT", "pattern": "0.0%"},
    NUMBER: {"type": "NUMBER", "pattern": "#,##0.##"},
    TEXT: None,
}

# ── Column Schemas ──────────────────────────────────────────────
# (field_name, format_type[, conditional_highlight])

OVERVIEW_COLS = [
    ("total_trades", NUMBER),
    ("winning_trades", NUMBER),
    ("losing_trades", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
    ("avg_winner", CURRENCY),
    ("avg_loser", CURRENCY),
    ("profit_factor", NUMBER),
    ("expectancy", CURRENCY),
    ("avg_holding_days", NUMBER),
    ("avg_roc", PERCENT),
    ("avg_annualized_return", PERCENT),
]

ROUND_TRIPS_COLS = [
    ("underlying", TEXT),
    ("right", TEXT),
    ("strike", CURRENCY),
    ("expiry", TEXT),
    ("open_date", TEXT),
    ("close_date", TEXT),
    ("close_reason", TEXT),
    ("quantity", NUMBER),
    ("net_premium", CURRENCY, True),
    ("realized_pnl", CURRENCY, True),
    ("roc", PERCENT),
    ("annualized_return", PERCENT),
    ("holding_days", NUMBER),
    ("commission", CURRENCY),
    ("open_price", CURRENCY),
    ("strategy_type", TEXT),
]

STRATEGY_COLS = [
    ("strategy_type", TEXT),
    ("trade_count", NUMBER),
    ("winning_trades", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
    ("avg_pnl", CURRENCY),
    ("profit_factor", NUMBER),
    ("avg_holding_days", NUMBER),
]

DTE_COLS = [
    ("dte_bucket", TEXT),
    ("trade_count", NUMBER),
    ("winning_trades", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
]

UNDERLYING_COLS = [
    ("underlying", TEXT),
    ("trade_count", NUMBER),
    ("winning_trades", NUMBER),
    ("win_rate", PERCENT),
    ("total_pnl", CURRENCY, True),
    ("avg_pnl", CURRENCY),
]

WHEEL_OVERVIEW_COLS = [
    ("total_option_pnl", CURRENCY, True),
    ("total_stock_pnl", CURRENCY, True),
    ("total_wheel_pnl", CURRENCY, True),
    ("completed_cycles", NUMBER),
    ("stock_held_cycles", NUMBER),
    ("incomplete_cycles", NUMBER),
]

WHEEL_CYCLES_COLS = [
    ("underlying", TEXT),
    ("account", TEXT),
    ("cycle_status", TEXT),
    ("total_pnl", CURRENCY, True),
    ("option_pnl", CURRENCY, True),
    ("stock_pnl", CURRENCY, True),
    ("put_strike", CURRENCY),
    ("call_strike", CURRENCY),
    ("put_premium", CURRENCY),
    ("call_premium", CURRENCY),
    ("quantity", NUMBER),
    ("multiplier", NUMBER),
    ("put_assigned_date", TEXT),
    ("call_close_date", TEXT),
    ("call_close_reason", TEXT),
]


# ── Helpers ─────────────────────────────────────────────────────

def col_letter(idx: int) -> str:
    """Convert zero-based column index to A1 letter."""
    result = ""
    while idx >= 0:
        result = chr(idx % 26 + ord("A")) + result
        idx = idx // 26 - 1
    return result


def format_cell(val: Any, fmt: str) -> Any:
    """Convert a JSON value to the right Python type for Sheets."""
    if val is None:
        return ""
    if fmt in (CURRENCY, NUMBER, PERCENT):
        try:
            return float(val)
        except (ValueError, TypeError):
            return val
    return str(val)


def data_to_rows(data: Any, columns: list) -> list[list]:
    """Convert JSON data to rows using column schema for ordering."""
    headers = [col[0] for col in columns]

    if isinstance(data, dict):
        return [headers, [format_cell(data.get(c[0]), c[1]) for c in columns]]

    if isinstance(data, list) and data and isinstance(data[0], dict):
        rows = [headers]
        for item in data:
            rows.append([format_cell(item.get(c[0]), c[1]) for c in columns])
        return rows

    return [headers]


def add_totals_row(rows: list[list], columns: list) -> list[list]:
    """Append a totals row with SUM formulas for numeric columns."""
    if len(rows) < 3:  # header + at least 2 data rows
        return rows

    last_data = len(rows)
    totals = []
    labelled = False

    for i, col in enumerate(columns):
        if col[1] in (CURRENCY, NUMBER, PERCENT):
            letter = col_letter(i)
            totals.append(f"=SUM({letter}2:{letter}{last_data})")
        elif not labelled:
            totals.append(f"TOTAL ({last_data - 1} rows)")
            labelled = True
        else:
            totals.append("")

    rows.append(totals)
    return rows


# ── Sheets API ──────────────────────────────────────────────────

def create_spreadsheet(service: Any, title: str) -> str:
    result = service.spreadsheets().create(
        body={"properties": {"title": title}}
    ).execute()
    return result["spreadsheetId"]


def get_sheet_id(service: Any, sid: str, tab_name: str) -> int | None:
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    for sheet in meta["sheets"]:
        if sheet["properties"]["title"] == tab_name:
            return sheet["properties"]["sheetId"]
    return None


def create_tab(
    service: Any, sid: str, tab_name: str,
    data: Any, columns: list, totals: bool = False,
):
    """Create a tab, write data, and apply formatting."""
    rows = data_to_rows(data, columns)
    if not rows:
        return

    if totals:
        rows = add_totals_row(rows, columns)

    num_rows = len(rows)
    num_cols = len(columns)

    # Ensure sheet exists
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    existing = {s["properties"]["title"] for s in meta["sheets"]}
    if tab_name not in existing:
        service.spreadsheets().batchUpdate(
            spreadsheetId=sid,
            body={"requests": [{"addSheet": {"properties": {"title": tab_name}}}]},
        ).execute()

    # Write values
    end_col = col_letter(num_cols - 1)
    service.spreadsheets().values().update(
        spreadsheetId=sid,
        range=f"{tab_name}!A1:{end_col}{num_rows}",
        valueInputOption="USER_ENTERED",
        body={"values": rows, "majorDimension": "ROWS"},
    ).execute()

    # Apply formatting
    sheet_id = get_sheet_id(service, sid, tab_name)
    if sheet_id is not None:
        apply_formatting(service, sid, sheet_id, columns, num_rows, totals)


def apply_formatting(
    service: Any, sid: str, sheet_id: int,
    columns: list, num_rows: int, has_totals: bool,
):
    """Apply frozen header, header style, number formats, conditional highlighting."""
    reqs = []

    # Frozen header row
    reqs.append({
        "updateSheetProperties": {
            "properties": {
                "sheetId": sheet_id,
                "gridProperties": {"frozenRowCount": 1},
            },
            "fields": "gridProperties.frozenRowCount",
        }
    })

    # Header styling (dark background, white bold text)
    reqs.append({
        "repeatCell": {
            "range": {"sheetId": sheet_id, "startRowIndex": 0, "endRowIndex": 1},
            "cell": {
                "userEnteredFormat": {
                    "backgroundColor": {"red": 0.15, "green": 0.20, "blue": 0.29},
                    "textFormat": {
                        "foregroundColor": {"red": 0.97, "green": 0.98, "blue": 0.99},
                        "bold": True,
                        "fontSize": 10,
                    },
                    "horizontalAlignment": "CENTER",
                }
            },
            "fields": "userEnteredFormat(backgroundColor,textFormat,horizontalAlignment)",
        }
    })

    # Per-column number format + conditional highlighting
    data_end = num_rows - (1 if has_totals else 0)
    for col_idx, col_def in enumerate(columns):
        fmt_type = col_def[1]
        nf = NUMBER_FORMATS.get(fmt_type)
        if nf is None:
            continue

        # Number format
        reqs.append({
            "repeatCell": {
                "range": {
                    "sheetId": sheet_id,
                    "startRowIndex": 1,
                    "endRowIndex": data_end,
                    "startColumnIndex": col_idx,
                    "endColumnIndex": col_idx + 1,
                },
                "cell": {"userEnteredFormat": {"numberFormat": nf}},
                "fields": "userEnteredFormat.numberFormat",
            }
        })

        # Conditional highlighting for P&L columns
        if len(col_def) > 2 and col_def[2]:
            rng = {
                "sheetId": sheet_id,
                "startRowIndex": 1,
                "startColumnIndex": col_idx,
                "endColumnIndex": col_idx + 1,
            }
            # Red for negative
            reqs.append({
                "addConditionalFormatRule": {
                    "rule": {
                        "ranges": [rng],
                        "booleanRule": {
                            "condition": {
                                "type": "NUMBER_LESS_THAN",
                                "values": [{"userEnteredValue": "0"}],
                            },
                            "format": {
                                "backgroundColor": {"red": 1.0, "green": 0.92, "blue": 0.92},
                                "textFormat": {"foregroundColor": {"red": 0.8, "green": 0.0, "blue": 0.0}},
                            },
                        },
                    },
                    "index": 0,
                }
            })
            # Green for positive/zero
            reqs.append({
                "addConditionalFormatRule": {
                    "rule": {
                        "ranges": [rng],
                        "booleanRule": {
                            "condition": {
                                "type": "NUMBER_GREATER_THAN_OR_EQUAL",
                                "values": [{"userEnteredValue": "0"}],
                            },
                            "format": {
                                "backgroundColor": {"red": 0.92, "green": 1.0, "blue": 0.92},
                                "textFormat": {"foregroundColor": {"red": 0.0, "green": 0.5, "blue": 0.0}},
                            },
                        },
                    },
                    "index": 1,
                }
            })

    # Totals row styling
    if has_totals and num_rows > 1:
        reqs.append({
            "repeatCell": {
                "range": {
                    "sheetId": sheet_id,
                    "startRowIndex": num_rows - 1,
                    "endRowIndex": num_rows,
                },
                "cell": {
                    "userEnteredFormat": {
                        "backgroundColor": {"red": 0.94, "green": 0.94, "blue": 0.94},
                        "textFormat": {"bold": True},
                        "borders": {
                            "top": {"style": "SOLID", "width": 2},
                        },
                    }
                },
                "fields": "userEnteredFormat(backgroundColor,textFormat,borders.top)",
            }
        })

    if reqs:
        service.spreadsheets().batchUpdate(
            spreadsheetId=sid, body={"requests": reqs}
        ).execute()


# ── Export Commands ─────────────────────────────────────────────

def export_trades(service: Any, sid: str, data: dict):
    """Export trades command data with formatted tabs."""
    tabs = [
        ("Overview", data.get("overview"), OVERVIEW_COLS, False),
        ("Round_Trips", data.get("round_trips"), ROUND_TRIPS_COLS, True),
        ("Strategy", data.get("strategy_performance"), STRATEGY_COLS, True),
        ("DTE_Breakdown", data.get("dte_breakdown"), DTE_COLS, True),
        ("Underlying", data.get("underlying_breakdown"), UNDERLYING_COLS, True),
        ("Wheel_Overview", data.get("wheel_overview"), WHEEL_OVERVIEW_COLS, False),
        ("Wheel_Cycles", data.get("wheel_cycles"), WHEEL_CYCLES_COLS, True),
    ]

    # Optional tabs that may not exist
    for key in ("loss_clusters", "streak_info"):
        if data.get(key):
            tabs.append((key.replace("_", " ").title(), data[key], None, False))

    for tab_name, tab_data, columns, totals in tabs:
        if tab_data is None:
            continue
        if columns:
            create_tab(service, sid, tab_name, tab_data, columns, totals)
        else:
            # Fallback: raw json_to_rows without formatting
            _create_raw_tab(service, sid, tab_name, tab_data)


def export_report(service: Any, sid: str, data: dict):
    """Export report command data."""
    positions = data.get("positions", data.get("data", {}).get("positions"))
    risk = data.get("risk_summaries", data.get("data", {}).get("risk_summaries"))
    exposures = data.get(
        "underlying_exposures",
        data.get("data", {}).get("underlying_exposures", data.get("exposures")),
    )

    for tab_name, tab_data in [
        ("Positions", positions),
        ("Risk_Summaries", risk),
        ("Underlying_Exposure", exposures),
    ]:
        if tab_data is not None:
            _create_raw_tab(service, sid, tab_name, tab_data)


def _create_raw_tab(service: Any, sid: str, tab_name: str, data: Any):
    """Create a tab with basic formatting (no column schema)."""
    if isinstance(data, dict):
        headers = list(data.keys())
        rows = [headers, [str(v) for v in data.values()]]
    elif isinstance(data, list) and data and isinstance(data[0], dict):
        headers = list(data[0].keys())
        rows = [headers] + [[str(item.get(k, "")) for k in headers] for item in data]
    else:
        return

    if not rows:
        return

    # Ensure sheet exists
    meta = service.spreadsheets().get(spreadsheetId=sid).execute()
    existing = {s["properties"]["title"] for s in meta["sheets"]}
    if tab_name not in existing:
        service.spreadsheets().batchUpdate(
            spreadsheetId=sid,
            body={"requests": [{"addSheet": {"properties": {"title": tab_name}}}]},
        ).execute()

    num_cols = max(len(r) for r in rows)
    end_col = col_letter(num_cols - 1)
    service.spreadsheets().values().update(
        spreadsheetId=sid,
        range=f"{tab_name}!A1:{end_col}{len(rows)}",
        valueInputOption="USER_ENTERED",
        body={"values": rows, "majorDimension": "ROWS"},
    ).execute()


# ── Main ────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Export IBKR analysis to Google Sheets")
    parser.add_argument(
        "command", choices=["trades", "report"], help="Data type to export"
    )
    parser.add_argument("--title", type=str, default=None, help="Custom spreadsheet title")
    args = parser.parse_args()

    # Read JSON from stdin
    try:
        json_text = sys.stdin.read()
        if not json_text.strip():
            print("Error: No input received from stdin", file=sys.stderr)
            return 1
        data = json.loads(json_text)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON input: {e}", file=sys.stderr)
        return 1

    # Authenticate
    creds = get_credentials()
    if not creds:
        print("Error: Authentication failed", file=sys.stderr)
        print("Run: python scripts/sheets_auth.py login", file=sys.stderr)
        return 1

    service = build("sheets", "v4", credentials=creds)

    # Create spreadsheet
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M")
    title = args.title or f"IBKR {args.command.capitalize()} {timestamp}"

    try:
        sid = create_spreadsheet(service, title)
    except Exception as e:
        print(f"Error: Failed to create spreadsheet: {e}", file=sys.stderr)
        return 1

    # Export
    try:
        if args.command == "trades":
            export_trades(service, sid, data)
        elif args.command == "report":
            export_report(service, sid, data)
    except Exception as e:
        print(f"Error: Failed to export data: {e}", file=sys.stderr)
        return 1

    # Print URL for C++ to display
    url = f"https://docs.google.com/spreadsheets/d/{sid}/edit"
    print(url)
    return 0


if __name__ == "__main__":
    sys.exit(main())
