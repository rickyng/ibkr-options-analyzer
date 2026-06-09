"""Account Management callbacks — CRUD, download/import, global filters, toast, modal."""

import os
from pathlib import Path

from dash import Dash, Output, Input, State, html, no_update
import dash_bootstrap_components as dbc

from ...services.cli import run_cli, download_flex, import_csv, refresh_market_data, CliError
from ...services.db import query, execute
from ...services.trade_repo import query_available_years
from ._data import _fetch_exposure, _fetch_expiry_calendar, _flatten_calendar
from ..layout import RISK_COLORS, TEXT_MUTED


def _fetch_accounts() -> list:
    """Fetch account list by querying database directly."""
    return query("SELECT id, name, token, query_id, ibkr_account_id, enabled FROM accounts ORDER BY name")


def _clear_account_downloads(account_name: str) -> None:
    """Remove previous CSV downloads for an account (YTD data supersedes old files)."""
    download_dir = Path.home() / ".ibkr-options-analyzer" / "downloads"
    if not download_dir.exists():
        return

    sanitized_name = account_name.replace(" ", "_")
    pattern = f"flex_report_{sanitized_name}_"

    for csv_file in download_dir.glob("*.csv"):
        if csv_file.name.startswith(pattern):
            os.remove(csv_file)


def register(app: Dash) -> None:
    """Register all account management callbacks with the Dash app."""

    # Callback: Load all data on page load (triggered by one-shot interval)
    @app.callback(
        [
            Output("exposure-data", "data"),
            Output("calendar-data", "data"),
            Output("positions-data", "data"),
            Output("accounts-data", "data"),
        ],
        Input("load-trigger", "n_intervals"),
    )
    def load_initial_data(_):
        """Fetch all data on initial page load."""
        exposure = _fetch_exposure()
        calendar = _fetch_expiry_calendar()
        positions = _flatten_calendar(calendar)
        accounts = _fetch_accounts()
        return exposure, calendar, positions, accounts

    # Callback: Populate global account filter from DB
    @app.callback(
        Output("global-account-filter", "options"),
        Input("accounts-data", "data"),
    )
    def update_global_account_filter(accounts):
        """Populate global account filter dropdown."""
        options = [{"label": "All Accounts", "value": "all"}]
        for acc in accounts:
            name = acc.get("name", "")
            options.append({"label": name, "value": name})
        return options

    # Callback: Populate global year filter from DB
    @app.callback(
        [
            Output("global-year-filter", "options"),
            Output("global-year-filter", "value"),
        ],
        Input("accounts-data", "data"),
    )
    def populate_year_filter(accounts):
        """Populate year dropdown from distinct trade years in DB."""
        years = query_available_years()
        options = [{"label": "All Years", "value": "all"}]
        options += [{"label": y, "value": y} for y in years]
        return options, "all"

    # Callback: Populate account editing dropdown from DB
    @app.callback(
        [
            Output("account-select-dropdown", "options"),
            Output("account-select-dropdown", "value"),
        ],
        Input("accounts-data", "data"),
    )
    def update_account_dropdown(accounts):
        """Populate account editing dropdown from database."""
        if not accounts:
            return [], None
        options = [{"label": a.get("name", ""), "value": a.get("name", "")} for a in accounts if a.get("name")]
        return options, None

    # Callback: Display toast when toast-data changes
    @app.callback(
        [
            Output("action-toast", "children"),
            Output("action-toast", "header"),
            Output("action-toast", "icon"),
            Output("action-toast", "is_open"),
        ],
        Input("toast-data", "data"),
    )
    def display_toast(data):
        """Show toast notification from toast-data store."""
        if not data or not data.get("message"):
            return no_update, no_update, no_update, no_update
        return data["message"], data.get("header", ""), data.get("icon", "info"), True

    # Callback: Handle download & import for all accounts
    @app.callback(
        [
            Output("toast-data", "data", allow_duplicate=True),
            Output("exposure-data", "data", allow_duplicate=True),
            Output("calendar-data", "data", allow_duplicate=True),
            Output("positions-data", "data", allow_duplicate=True),
            Output("accounts-data", "data", allow_duplicate=True),
        ],
        Input("download-import-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def handle_download_import(n_clicks):
        """Download Flex reports for all accounts and import, then refresh."""
        if not n_clicks:
            return {}, no_update, no_update, no_update, no_update

        accounts = _fetch_accounts()
        if not accounts:
            return {"header": "Error", "message": "No accounts configured", "icon": "danger"}, no_update, no_update, no_update, no_update

        updated_accounts = []
        errors = []

        for acc in accounts:
            if not acc.get("enabled"):
                continue
            name = acc.get("name", "")
            token = acc.get("token", "")
            query_id = acc.get("query_id", "")
            if not token or not query_id:
                continue

            _clear_account_downloads(name)

            try:
                download_flex(token, query_id, name, force=True)
                updated_accounts.append(name)
            except CliError as e:
                errors.append(f"{name}: {e}")

        if errors:
            return (
                {"header": "Download Error", "message": f"Errors: {', '.join(errors)}", "icon": "danger"},
                no_update, no_update, no_update, no_update,
            )

        import_result_data = {}
        try:
            import_result_data = import_csv()
        except CliError as e:
            return (
                {"header": "Import Error", "message": str(e), "icon": "danger"},
                no_update, no_update, no_update, no_update,
            )

        exposure = _fetch_exposure()
        calendar = _fetch_expiry_calendar()
        positions = _flatten_calendar(calendar)
        accounts = _fetch_accounts()

        trades_in = import_result_data.get("trades_imported", 0)
        pos_in = import_result_data.get("positions_imported", 0)
        msg = f"Updated {len(updated_accounts)}: {', '.join(updated_accounts)}"
        if trades_in or pos_in:
            msg += f" | {trades_in} trades, {pos_in} positions"

        return (
            {"header": "Download Complete", "message": msg, "icon": "success"},
            exposure, calendar, positions, accounts,
        )

    # Callback: Open modal for Add Account
    @app.callback(
        [
            Output("account-modal", "is_open", allow_duplicate=True),
            Output("account-name-input", "value", allow_duplicate=True),
            Output("account-name-input", "disabled", allow_duplicate=True),
            Output("account-token-input", "value", allow_duplicate=True),
            Output("account-query-id-input", "value", allow_duplicate=True),
            Output("account-enabled-check", "value", allow_duplicate=True),
            Output("editing-account-id", "data", allow_duplicate=True),
            Output("delete-account-btn", "style", allow_duplicate=True),
            Output("account-form-status", "children", allow_duplicate=True),
        ],
        Input("add-account-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def show_add_account_form(n_clicks):
        """Open modal with empty form for new account."""
        return True, "", False, "", "", [1], None, {"display": "none"}, ""

    # Callback: Open modal populated when account selected from dropdown
    @app.callback(
        [
            Output("account-modal", "is_open"),
            Output("account-name-input", "value"),
            Output("account-name-input", "disabled"),
            Output("account-token-input", "value"),
            Output("account-query-id-input", "value"),
            Output("account-ibkr-id-input", "value"),
            Output("account-enabled-check", "value"),
            Output("editing-account-id", "data"),
            Output("delete-account-btn", "style"),
            Output("account-form-status", "children"),
        ],
        Input("account-select-dropdown", "value"),
        State("accounts-data", "data"),
        prevent_initial_call=True,
    )
    def show_edit_account_form(selected_name, accounts):
        """Open modal populated with existing account data."""
        if not selected_name or not accounts:
            return no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update

        for acc in accounts:
            if acc.get("name") == selected_name:
                enabled = [1] if acc.get("enabled") else []
                return (
                    True,
                    acc["name"], True,
                    acc["token"], acc["query_id"],
                    acc.get("ibkr_account_id", ""),
                    enabled,
                    acc["id"],
                    {"display": "inline-block"},
                    "",
                )

        return no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update, no_update

    # Callback: Close modal on Cancel
    @app.callback(
        Output("account-modal", "is_open", allow_duplicate=True),
        Input("cancel-account-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def hide_account_modal(n_clicks):
        return False

    # Callback: Save account (create or update)
    @app.callback(
        [
            Output("account-form-status", "children", allow_duplicate=True),
            Output("accounts-data", "data", allow_duplicate=True),
            Output("account-modal", "is_open", allow_duplicate=True),
        ],
        Input("save-account-btn", "n_clicks"),
        State("account-name-input", "value"),
        State("account-token-input", "value"),
        State("account-query-id-input", "value"),
        State("account-ibkr-id-input", "value"),
        State("account-enabled-check", "value"),
        State("editing-account-id", "data"),
        prevent_initial_call=True,
    )
    def save_account(n_clicks, name, token, query_id, ibkr_id, enabled_val, editing_id):
        name = (name or "").strip()
        token = (token or "").strip()
        query_id = (query_id or "").strip()
        ibkr_id = (ibkr_id or "").strip()

        if not n_clicks or not name or not token or not query_id:
            return "Name, Token, and Query ID required", no_update, no_update

        enabled = 1 if (enabled_val and 1 in enabled_val) else 0

        if editing_id:
            execute(
                "UPDATE accounts SET token = ?, query_id = ?, ibkr_account_id = ?, enabled = ?, updated_at = CURRENT_TIMESTAMP WHERE id = ?",
                (token, query_id, ibkr_id, enabled, editing_id),
            )
        else:
            execute(
                "INSERT INTO accounts (name, token, query_id, ibkr_account_id, enabled) VALUES (?, ?, ?, ?, ?)",
                (name, token, query_id, ibkr_id, enabled),
            )

        refreshed = _fetch_accounts()
        return "", refreshed, False

    # Callback: Delete account
    @app.callback(
        [
            Output("account-form-status", "children", allow_duplicate=True),
            Output("accounts-data", "data", allow_duplicate=True),
            Output("account-modal", "is_open", allow_duplicate=True),
        ],
        Input("delete-account-btn", "n_clicks"),
        State("editing-account-id", "data"),
        prevent_initial_call=True,
    )
    def delete_account(n_clicks, account_id):
        if not n_clicks or not account_id:
            return no_update, no_update, no_update

        execute("DELETE FROM accounts WHERE id = ?", (account_id,))

        refreshed = _fetch_accounts()
        return "", refreshed, False

    # Callback: Refresh market prices and earnings dates
    @app.callback(
        [
            Output("toast-data", "data", allow_duplicate=True),
            Output("exposure-data", "data", allow_duplicate=True),
            Output("calendar-data", "data", allow_duplicate=True),
            Output("positions-data", "data", allow_duplicate=True),
        ],
        Input("refresh-market-btn", "n_clicks"),
        prevent_initial_call=True,
    )
    def handle_refresh_market_data(n_clicks):
        """Refresh prices and earnings, then reload position data."""
        if not n_clicks:
            return {}, no_update, no_update, no_update

        try:
            result = refresh_market_data()
            data = result.get("data", {})
            prices = data.get("prices_refreshed", 0)
            earnings = data.get("earnings_refreshed", 0)
            failed = data.get("failed_symbols", [])
            msg = f"{prices} prices, {earnings} earnings refreshed"
            if failed:
                msg += f", {len(failed)} failed"

            exposure = _fetch_exposure()
            calendar = _fetch_expiry_calendar()
            positions = _flatten_calendar(calendar)

            return (
                {"header": "Prices Refreshed", "message": msg, "icon": "success"},
                exposure, calendar, positions,
            )
        except CliError as e:
            return {"header": "Refresh Error", "message": str(e), "icon": "danger"}, no_update, no_update, no_update
