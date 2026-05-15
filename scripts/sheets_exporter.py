#!/usr/bin/env python3
"""
Google Sheets exporter for IBKR Options Analyzer.

Reads JSON data from stdin and pushes to a new Google Spreadsheet.
Creates separate tabs for each data section.

Usage:
    ibkr-options-analyzer trades --google-sheet | python scripts/sheets_exporter.py trades
    ibkr-options-analyzer report --google-sheet | python scripts/sheets_exporter.py report

Or with stdin:
    python scripts/sheets_exporter.py trades < data.json
"""

import argparse
import json
import os
import sys
from datetime import datetime
from typing import Any

from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build

from sheets_auth import get_credentials, SCOPES

def create_spreadsheet(service: Any, title: str) -> str:
    """Create a new spreadsheet and return its ID."""
    spreadsheet = {
        'properties': {'title': title}
    }
    result = service.spreadsheets().create(body=spreadsheet).execute()
    return result['spreadsheetId']

def get_spreadsheet_url(spreadsheet_id: str) -> str:
    """Return the URL for a spreadsheet."""
    return f"https://docs.google.com/spreadsheets/d/{spreadsheet_id}/edit"

def json_to_rows(data: dict | list) -> list[list]:
    """
    Convert JSON object or array to rows for Google Sheets.

    For dict: creates header row from keys, then data row from values
    For list of dicts: creates header row, then rows for each item
    For list of primitives: creates single column rows
    """
    if isinstance(data, dict):
        # Single row: header + values
        headers = list(data.keys())
        values = [format_value(v) for v in data.values()]
        return [headers, values]

    elif isinstance(data, list):
        if not data:
            return []

        if isinstance(data[0], dict):
            # Array of objects: header from first item keys, rows for each
            headers = list(data[0].keys())
            rows = [headers]
            for item in data:
                row = [format_value(item.get(k, '')) for k in headers]
                rows.append(row)
            return rows

        else:
            # Array of primitives: single column
            return [[format_value(v)] for v in data]

    else:
        # Single value
        return [[format_value(data)]]

def format_value(val: Any) -> str:
    """Format a value for Google Sheets cell."""
    if val is None:
        return ''
    elif isinstance(val, float):
        if val == float('inf') or val == float('-inf'):
            return 'UNLIMITED'
        return str(val)
    elif isinstance(val, bool):
        return str(val)
    else:
        return str(val)

def create_tab(service: Any, spreadsheet_id: str, tab_name: str, rows: list[list]):
    """
    Create a new tab and populate with data.

    Uses batchUpdate to add sheet, then update to add values.
    """
    if not rows:
        return

    # Find existing sheet IDs to avoid collision
    metadata = service.spreadsheets().get(spreadsheetId=spreadsheet_id).execute()
    existing_names = {s['properties']['title'] for s in metadata['sheets']}

    # Create sheet if it doesn't exist
    if tab_name in existing_names:
        # Find the sheet ID
        sheet_id = None
        for s in metadata['sheets']:
            if s['properties']['title'] == tab_name:
                sheet_id = s['properties']['sheetId']
                break
    else:
        # Create new sheet
        request = {
            'addSheet': {
                'properties': {'title': tab_name}
            }
        }
        response = service.spreadsheets().batchUpdate(
            spreadsheetId=spreadsheet_id,
            body={'requests': [request]}
        ).execute()
        sheet_id = response['replies'][0]['addSheet']['properties']['sheetId']

    # Calculate range based on data size
    num_rows = len(rows)
    num_cols = max(len(row) for row in rows) if rows else 0

    # Build range A1 notation (e.g., "Overview!A1:J2")
    end_col = chr(ord('A') + num_cols - 1) if num_cols > 0 else 'A'
    range_notation = f"{tab_name}!A1:{end_col}{num_rows}"

    # Update values
    body = {
        'values': rows,
        'majorDimension': 'ROWS'
    }

    service.spreadsheets().values().update(
        spreadsheetId=spreadsheet_id,
        range=range_notation,
        valueInputOption='USER_ENTERED',
        body=body
    ).execute()

def export_trades(service: Any, spreadsheet_id: str, data: dict):
    """
    Export trades command data to spreadsheet.

    Creates tabs: Overview, Round_Trips, Strategy_Performance, DTE_Breakdown,
                  Underlying_Breakdown, Loss_Clusters, Streak_Info,
                  Wheel_Overview, Wheel_Cycles
    """
    tabs = [
        ('Overview', data.get('overview')),
        ('Round_Trips', data.get('round_trips')),
        ('Strategy_Performance', data.get('strategy_performance')),
        ('DTE_Breakdown', data.get('dte_breakdown')),
        ('Underlying_Breakdown', data.get('underlying_breakdown')),
        ('Loss_Clusters', data.get('loss_clusters')),
        ('Streak_Info', data.get('streak_info')),
        ('Wheel_Overview', data.get('wheel_overview')),
        ('Wheel_Cycles', data.get('wheel_cycles')),
    ]

    for tab_name, tab_data in tabs:
        if tab_data is not None:
            rows = json_to_rows(tab_data)
            if rows:
                create_tab(service, spreadsheet_id, tab_name, rows)

def export_report(service: Any, spreadsheet_id: str, data: dict):
    """
    Export report command data to spreadsheet.

    Creates tabs: Positions, Risk_Summaries, Underlying_Exposure
    """
    # The JSON output from report command is wrapped in a 'data' field
    # or returned directly from JsonOutput::report()

    # Check structure - may be direct or nested
    positions = data.get('positions', data.get('data', {}).get('positions'))
    risk_summaries = data.get('risk_summaries', data.get('data', {}).get('risk_summaries'))
    exposures = data.get('underlying_exposures', data.get('data', {}).get('underlying_exposures', data.get('exposures')))

    tabs = [
        ('Positions', positions),
        ('Risk_Summaries', risk_summaries),
        ('Underlying_Exposure', exposures),
    ]

    for tab_name, tab_data in tabs:
        if tab_data is not None:
            rows = json_to_rows(tab_data)
            if rows:
                create_tab(service, spreadsheet_id, tab_name, rows)

def main():
    parser = argparse.ArgumentParser(description="Export IBKR analysis to Google Sheets")
    parser.add_argument('command', choices=['trades', 'report'],
                        help='Command type (trades or report)')
    parser.add_argument('--stdin', action='store_true', default=True,
                        help='Read JSON from stdin (default)')
    parser.add_argument('--title', type=str, default=None,
                        help='Custom spreadsheet title')

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

    # Get credentials
    creds = get_credentials()
    if not creds:
        print("Error: Authentication failed", file=sys.stderr)
        print("Run: python scripts/sheets_auth.py login", file=sys.stderr)
        return 1

    # Build service
    service = build('sheets', 'v4', credentials=creds)

    # Create spreadsheet with timestamped title
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M')
    default_title = f"IBKR {args.command.capitalize()} Report {timestamp}"
    title = args.title or default_title

    try:
        spreadsheet_id = create_spreadsheet(service, title)
    except Exception as e:
        print(f"Error: Failed to create spreadsheet: {e}", file=sys.stderr)
        return 1

    # Export data based on command type
    try:
        if args.command == 'trades':
            export_trades(service, spreadsheet_id, data)
        elif args.command == 'report':
            export_report(service, spreadsheet_id, data)
    except Exception as e:
        print(f"Error: Failed to export data: {e}", file=sys.stderr)
        return 1

    # Print URL for C++ to display
    url = get_spreadsheet_url(spreadsheet_id)
    print(url)

    return 0

if __name__ == '__main__':
    sys.exit(main())