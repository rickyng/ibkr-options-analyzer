#!/usr/bin/env python3
"""Generate formatted text tables for copy-paste into email or messaging apps.

Usage:
    python tools/report.py                    # All accounts, expiry calendar
    python tools/report.py --account No1      # Specific account
    python tools/report.py --type trades      # Trade performance summary
    python tools/report.py --type summary     # Portfolio summary with risk alerts
"""

import argparse
import json
import subprocess
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CLI_PATH = PROJECT_ROOT / "build" / "release" / "ibkr-options-analyzer"


def run_cli(command: str, *args: str) -> dict:
    cmd = [str(CLI_PATH), "--format", "json", "--quiet", command, *args]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Error: {result.stderr.strip()}", file=sys.stderr)
        sys.exit(1)
    stdout = result.stdout.strip()
    if not stdout:
        return {}
    return json.loads(stdout)


def fmt_currency(val: float, currency: str = "USD") -> str:
    if currency == "USD":
        return f"${val:,.0f}" if abs(val) >= 1 else f"${val:.2f}"
    return f"{val:,.2f} {currency}"


def fmt_pct(val: float) -> str:
    return f"{val:+.1f}%" if val != 0 else "0%"


def fmt_dte(dte: int) -> str:
    if dte <= 0:
        return "EXPIRED"
    return f"{dte}d"


def risk_icon(category: str) -> str:
    icons = {"SAFE": ".", "WATCH": "!", "DANGER": "X"}
    return icons.get(category, "?")


# ── Expiry Calendar ──────────────────────────────────────────────────────────

def expiry_calendar(account: str | None = None) -> str:
    args = ["open"]
    if account:
        args.extend(["--account", account])

    data = run_cli("analyze", *args)
    positions_by_bucket = data.get("positions", {})
    summary = data.get("summary", {})

    if not positions_by_bucket:
        return "No open positions found."

    bucket_order = ["W1", "W2", "W3", "W4", "W5+"]
    bucket_labels = {
        "W1": "<7d",
        "W2": "7-14d",
        "W3": "14-21d",
        "W4": "21-28d",
        "W5+": "28d+",
    }

    lines = []
    account_label = account or "All Accounts"
    lines.append(f"Expiry Calendar — {account_label}")
    lines.append(f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    lines.append("")

    for bucket in bucket_order:
        positions = positions_by_bucket.get(bucket, [])
        if not positions:
            continue

        label = bucket_labels.get(bucket, bucket)
        lines.append(f"--- {label} ({len(positions)} positions) ---")

        # Group by expiry date
        by_expiry = defaultdict(list)
        for pos in positions:
            by_expiry[pos.get("expiry", "?")].append(pos)

        for expiry in sorted(by_expiry.keys()):
            positions_at_expiry = by_expiry[expiry]
            # Parse expiry for day of week
            try:
                expiry_dt = datetime.strptime(expiry, "%Y-%m-%d")
                dow = expiry_dt.strftime("%a")
                expiry_display = f"{expiry} ({dow})"
            except ValueError:
                expiry_display = expiry

            lines.append(f"  {expiry_display}")

            # Sort by underlying, then strike
            positions_at_expiry.sort(key=lambda p: (p.get("underlying", ""), p.get("strike", 0)))

            for pos in positions_at_expiry:
                sym = pos.get("underlying", "?")
                right = pos.get("right", "?")
                strike = pos.get("strike", 0)
                qty = abs(int(pos.get("quantity", 0)))
                entry = pos.get("entry_premium", 0)
                cur_price = pos.get("current_price", 0)
                dte = pos.get("days_to_expiry", 0)
                dist = pos.get("distance_from_strike_pct", 0)
                itm = pos.get("in_the_money", False)
                risk = pos.get("risk_category", "?")
                cur = pos.get("currency", "USD")
                acc = pos.get("account", "?")

                right_label = "P" if right == "P" else "C"
                itm_flag = " [ITM]" if itm else ""
                risk_flag = f" [{risk}]" if risk != "SAFE" else ""
                acc_label = f" ({acc})" if not account else ""

                premium = entry * qty * 100
                lines.append(
                    f"    {risk_icon(risk)} {sym} {strike}{right_label} x{qty}"
                    f" | prem {fmt_currency(premium, cur)}"
                    f" | spot {cur_price:.1f}"
                    f" | {fmt_pct(dist)}{itm_flag}{risk_flag}{acc_label}"
                )

        lines.append("")

    # Summary footer
    total = summary.get("total_positions", 0)
    short_puts = summary.get("short_puts", 0)
    short_calls = summary.get("short_calls", 0)
    premium_collected = summary.get("premium_collected", 0)
    lines.append(f"Total: {total} positions ({short_puts} short puts, {short_calls} short calls)")
    if premium_collected:
        lines.append(f"Premium collected: {fmt_currency(premium_collected)}")

    return "\n".join(lines)


# ── Trade Performance ────────────────────────────────────────────────────────

def trade_performance(account: str | None = None) -> str:
    args = []
    if account:
        args.extend(["--account", account])

    data = run_cli("trades", *args)
    overview = data.get("overview", {})

    if not overview:
        return "No trade data found."

    lines = []
    lines.append("Trade Performance Summary")
    lines.append(f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    lines.append("")

    total = overview.get("total_trades", 0)
    wins = overview.get("winning_trades", 0)
    losses = overview.get("losing_trades", 0)
    win_rate = overview.get("win_rate", 0)
    total_pnl = overview.get("total_pnl", 0)
    avg_pnl = overview.get("avg_pnl", 0)
    best = overview.get("best_trade_pnl", 0)
    worst = overview.get("worst_trade_pnl", 0)
    profit_factor = overview.get("profit_factor", 0)
    avg_dte = overview.get("avg_holding_days", 0)

    lines.append(f"  Total trades:  {total}")
    lines.append(f"  Wins / Losses: {wins} / {losses}")
    lines.append(f"  Win rate:      {win_rate:.1f}%")
    lines.append(f"  Total P&L:     {fmt_currency(total_pnl)}")
    lines.append(f"  Avg P&L:       {fmt_currency(avg_pnl)}")
    lines.append(f"  Best trade:    {fmt_currency(best)}")
    lines.append(f"  Worst trade:   {fmt_currency(worst)}")
    lines.append(f"  Profit factor: {profit_factor:.2f}")
    lines.append(f"  Avg hold:      {avg_dte:.0f} days")

    # Strategy breakdown
    strategies = data.get("strategy_performance", [])
    if strategies:
        lines.append("")
        lines.append("--- By Strategy ---")
        for s in strategies:
            strat = s.get("strategy", "?")
            cnt = s.get("count", 0)
            wr = s.get("win_rate", 0)
            pnl = s.get("total_pnl", 0)
            lines.append(f"  {strat}: {cnt} trades, {wr:.0f}% win, {fmt_currency(pnl)}")

    # Top/worst underlyings
    underlyings = data.get("underlying_breakdown", [])
    if underlyings:
        lines.append("")
        lines.append("--- Top 5 By P&L ---")
        sorted_ul = sorted(underlyings, key=lambda u: u.get("total_pnl", 0), reverse=True)
        for u in sorted_ul[:5]:
            sym = u.get("underlying", "?")
            cnt = u.get("count", 0)
            pnl = u.get("total_pnl", 0)
            wr = u.get("win_rate", 0)
            lines.append(f"  {sym}: {fmt_currency(pnl)} ({cnt} trades, {wr:.0f}% win)")

    return "\n".join(lines)


# ── Portfolio Summary ────────────────────────────────────────────────────────

def portfolio_summary(account: str | None = None) -> str:
    args = []
    if account:
        args.extend(["--account", account])

    data = run_cli("analyze", "portfolio", *args)
    if not data:
        return "No portfolio data found."

    lines = []
    lines.append("Portfolio Summary")
    lines.append(f"Updated: {datetime.now().strftime('%Y-%m-%d %H:%M')}")
    lines.append("")

    # Accounts overview
    accounts = data.get("accounts", [])
    if accounts:
        lines.append("--- By Account ---")
        for acc in accounts:
            name = acc.get("account", "?")
            pos_count = acc.get("position_count", 0)
            itm = acc.get("in_the_money_count", 0)
            exposure = acc.get("total_exposure", 0)
            premium = acc.get("premium_collected", 0)
            cur = acc.get("currency", "USD")

            lines.append(
                f"  {name}: {pos_count} pos, {itm} ITM"
                f" | exposure {fmt_currency(exposure, cur)}"
                f" | prem {fmt_currency(premium, cur)}"
            )

    # Risk alerts
    alerts = data.get("risk_alerts", [])
    if alerts:
        lines.append("")
        lines.append("--- Risk Alerts ---")
        for alert in alerts:
            level = alert.get("level", "?")
            msg = alert.get("message", "")
            sym = alert.get("underlying", "")
            label = f"[{level}]" if level else ""
            sym_label = f" {sym}:" if sym else ""
            lines.append(f"  {label}{sym_label} {msg}")

    # Exposure by underlying
    exposure_list = data.get("exposure_by_underlying", [])
    if exposure_list:
        lines.append("")
        lines.append("--- Exposure by Underlying ---")
        sorted_exp = sorted(exposure_list, key=lambda e: e.get("exposure", 0), reverse=True)
        for e in sorted_exp[:10]:
            sym = e.get("underlying", "?")
            exposure = e.get("exposure", 0)
            pos_count = e.get("position_count", 0)
            cur = e.get("currency", "USD")
            lines.append(
                f"  {sym}: {fmt_currency(exposure, cur)} ({pos_count} positions)"
            )

    return "\n".join(lines)


# ── Main ─────────────────────────────────────────────────────────────────────

REPORT_TYPES = {
    "expiry": ("Expiry Calendar (default)", expiry_calendar),
    "trades": ("Trade Performance", trade_performance),
    "summary": ("Portfolio Summary", portfolio_summary),
}


def main():
    parser = argparse.ArgumentParser(
        description="Generate formatted text reports for copy-paste"
    )
    parser.add_argument(
        "--type",
        choices=list(REPORT_TYPES.keys()),
        default="expiry",
        help="Report type (default: expiry)",
    )
    parser.add_argument("--account", help="Filter by account name")
    args = parser.parse_args()

    if not CLI_PATH.exists():
        print(f"CLI not found at {CLI_PATH}. Run 'cmake --build build/release' first.")
        sys.exit(1)

    label, fn = REPORT_TYPES[args.type]
    output = fn(account=args.account)
    print(output)
    print()
    print(f"--- Generated: {datetime.now().strftime('%Y-%m-%d %H:%M')} ---")


if __name__ == "__main__":
    main()
