#!/usr/bin/env python3
"""Position sizing calculator for short put / covered call strategies.

Uses Kelly Criterion adjusted for margin constraints and concentration limits.
Pre-loaded with current account profiles from portfolio review data.

Usage:
    python tools/position_sizer.py                     # All accounts
    python tools/position_sizer.py --account No1       # Single account
    python tools/position_sizer.py --ticker NVDA       # Size for specific ticker
    python tools/position_sizer.py --risk-pct 2        # Custom risk % per trade
"""

import argparse
import math
import sys
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class AccountProfile:
    name: str
    nlv: float                     # Net liquidation value
    margin_debt: float             # Current margin balance (negative)
    margin_ceiling: float          # Max acceptable margin debt (negative)
    win_rate: float                # Historical win rate for short puts (0-1)
    avg_premium: float             # Average premium collected per put
    avg_loss_pct: float            # Average loss as fraction of premium (0-1)
    options_income_annual: float   # Estimated annual options income
    margin_interest_annual: float  # Annualized margin interest cost
    max_single_name_pct: float = 0.20   # Max % of NLV per underlying
    max_sector_pct: float = 0.50        # Max % of NLV per sector
    risk_per_trade_pct: float = 0.02    # Default risk per trade as % of NLV


ACCOUNTS = {
    "No1": AccountProfile(
        name="Account No1",
        nlv=4_090_000,
        margin_debt=-789_000,
        margin_ceiling=-607_000,
        win_rate=0.726,
        avg_premium=723,
        avg_loss_pct=0.31,
        options_income_annual=65_000,
        margin_interest_annual=36_000,
        max_single_name_pct=0.20,
        max_sector_pct=0.50,
        risk_per_trade_pct=0.01,  # Conservative: already over-leveraged
    ),
    "No2": AccountProfile(
        name="Account No2",
        nlv=586_000,
        margin_debt=-376_000,
        margin_ceiling=-400_000,
        win_rate=0.905,
        avg_premium=3_467,
        avg_loss_pct=0.36,
        options_income_annual=72_500,
        margin_interest_annual=24_000,
        max_single_name_pct=0.20,
        max_sector_pct=0.50,
        risk_per_trade_pct=0.02,
    ),
    "No3": AccountProfile(
        name="Account No3",
        nlv=171_000,
        margin_debt=-19_000,
        margin_ceiling=-50_000,
        win_rate=1.0,
        avg_premium=5_429,
        avg_loss_pct=0.0,  # No losses yet
        options_income_annual=51_000,
        margin_interest_annual=1_200,
        max_single_name_pct=0.20,
        max_sector_pct=0.50,
        risk_per_trade_pct=0.03,  # Aggressive: healthy coverage
    ),
    "WinWin": AccountProfile(
        name="WinWin",
        nlv=739_000,
        margin_debt=0,  # Cash account or minimal debt
        margin_ceiling=-100_000,
        win_rate=0.85,  # Estimated from thesis-driven approach
        avg_premium=2_500,
        avg_loss_pct=0.25,
        options_income_annual=60_000,
        margin_interest_annual=0,
        max_single_name_pct=0.25,
        max_sector_pct=0.50,
        risk_per_trade_pct=0.025,
    ),
}

# Typical multipliers
STOCK_MULTIPLIER = 100  # Standard US options
JP_1321_MULTIPLIER = 1  # Nikkei ETF 1321 exception


@dataclass
class SizingResult:
    account: str
    ticker: Optional[str]
    strike: Optional[float]
    premium_per_contract: Optional[float]

    # Core metrics
    kelly_fraction: float           # Full Kelly %
    half_kelly: float               # Half Kelly (recommended)
    risk_budget_1r: float           # 1R in dollars
    margin_headroom: float          # Available margin capacity
    margin_status: str              # FREE / TIGHT / FROZEN

    # Position sizing
    max_contracts_by_risk: int      # Based on risk budget
    max_contracts_by_margin: int    # Based on margin capacity
    max_contracts_by_concentration: int  # Based on concentration limit
    recommended_contracts: int      # Min of all constraints
    max_notional: float             # Max notional exposure

    # Expectancy
    expectancy_per_r: float         # Expected return per 1R risked
    expectancy_annual: float        # Projected annual return

    # Warnings
    warnings: list[str] = field(default_factory=list)


def kelly_criterion(win_rate: float, avg_win: float, avg_loss: float) -> float:
    """Calculate Kelly fraction: K% = W - (1-W)/(W/L) where W/L = avg_win/avg_loss."""
    if avg_loss <= 0:
        return 1.0  # No losses = full Kelly is 100% (but cap in practice)
    w_l_ratio = avg_win / avg_loss
    k = win_rate - ((1 - win_rate) / w_l_ratio)
    return max(0.0, k)


def calculate_expectancy(win_rate: float, avg_win: float, avg_loss: float) -> float:
    """Expected return per trade in R terms."""
    return (win_rate * avg_win) - ((1 - win_rate) * avg_loss)


def margin_headroom(acct: AccountProfile) -> tuple[float, str]:
    """Calculate available margin capacity and status."""
    available = abs(acct.margin_ceiling) - abs(acct.margin_debt)
    if available <= 0:
        return available, "FROZEN"
    elif available < abs(acct.margin_ceiling) * 0.20:
        return available, "TIGHT"
    else:
        return available, "FREE"


def size_position(
    acct: AccountProfile,
    ticker: Optional[str] = None,
    strike: Optional[float] = None,
    premium_per_contract: Optional[float] = None,
    stock_price: Optional[float] = None,
    multiplier: int = STOCK_MULTIPLIER,
    custom_risk_pct: Optional[float] = None,
) -> SizingResult:
    """Calculate position size for a short put trade."""

    risk_pct = custom_risk_pct or acct.risk_per_trade_pct

    # Average win/loss in dollar terms (avg_premium is total per-contract premium)
    avg_win = acct.avg_premium
    avg_loss = acct.avg_premium * (acct.avg_loss_pct if acct.avg_loss_pct > 0 else 0.5)

    # For specific trade, premium_per_contract is per-share quote → convert to per-contract
    if premium_per_contract is not None:
        trade_premium_total = premium_per_contract * multiplier
    else:
        trade_premium_total = avg_win

    # Kelly
    k_full = kelly_criterion(acct.win_rate, avg_win, avg_loss)
    k_half = k_full / 2.0

    # R-multiple framework
    one_r = acct.nlv * risk_pct

    # Max loss per contract (short put assignment)
    # Premium yield assumption: premium ≈ 6% of strike for OTM puts
    premium_yield = 0.06
    if strike is not None:
        notional_per_contract = strike * multiplier
        loss_per_contract = notional_per_contract - trade_premium_total
    else:
        notional_per_contract = trade_premium_total / premium_yield
        loss_per_contract = notional_per_contract - trade_premium_total

    # Sizing by risk budget
    if loss_per_contract > 0:
        contracts_by_risk = max(1, int(one_r / loss_per_contract))
    else:
        contracts_by_risk = 1

    # Sizing by margin
    # Naked short put requires ~25% margin of notional (Reg T)
    margin_requirement_pct = 0.25
    headroom, margin_status = margin_headroom(acct)

    if margin_status == "FREE" and headroom > 0:
        contracts_by_margin = max(1, int(headroom / (notional_per_contract * margin_requirement_pct)))
    elif margin_status == "TIGHT" and headroom > 0:
        # Soft constraint: allow sizing by risk/conc but warn on margin
        contracts_by_margin = max(1, int(headroom / (notional_per_contract * margin_requirement_pct)))
    else:
        contracts_by_margin = 0

    # Sizing by concentration
    if strike is not None and stock_price is not None:
        max_position_value = acct.nlv * acct.max_single_name_pct
        contracts_by_conc = max(1, int(max_position_value / (stock_price * multiplier)))
    else:
        max_position_value = acct.nlv * acct.max_single_name_pct
        contracts_by_conc = max(1, int(max_position_value / notional_per_contract)) if notional_per_contract > 0 else 1

    # Recommended: min of all constraints (floor at 0)
    recommended = max(0, min(contracts_by_risk, contracts_by_margin, contracts_by_conc))
    if margin_status == "FROZEN":
        recommended = 0

    # Expectancy
    exp_per_r = calculate_expectancy(acct.win_rate, avg_win, avg_loss)
    # Annualized: expectancy per trade * trades/year estimate
    trades_per_year = acct.options_income_annual / avg_win if avg_win > 0 else 20
    exp_annual = exp_per_r * trades_per_year

    # Warnings
    warnings = []
    if margin_status == "FROZEN":
        warnings.append("FROZEN: No new positions — margin ceiling exceeded")
    if margin_status == "TIGHT":
        warnings.append("TIGHT: Margin headroom < 20% of ceiling")
    if abs(acct.margin_debt) > 0 and acct.margin_interest_annual > acct.options_income_annual * 0.5:
        warnings.append("HIGH INTEREST: Margin cost > 50% of options income")
    if k_full > 0.25:
        warnings.append(f"High Kelly ({k_full:.1%}) — use half-Kelly for safety")

    return SizingResult(
        account=acct.name,
        ticker=ticker,
        strike=strike,
        premium_per_contract=premium_per_contract,
        kelly_fraction=k_full,
        half_kelly=k_half,
        risk_budget_1r=one_r,
        margin_headroom=headroom,
        margin_status=margin_status,
        max_contracts_by_risk=contracts_by_risk,
        max_contracts_by_margin=contracts_by_margin,
        max_contracts_by_concentration=contracts_by_conc,
        recommended_contracts=recommended,
        max_notional=recommended * notional_per_contract,
        expectancy_per_r=exp_per_r,
        expectancy_annual=exp_annual,
        warnings=warnings,
    )


def print_report(results: list[SizingResult], verbose: bool = False):
    """Print formatted sizing report."""
    print("\n" + "=" * 72)
    print("  POSITION SIZING CALCULATOR — Kelly Criterion + R-Multiple")
    print("=" * 72)

    for r in results:
        acct = ACCOUNTS.get(
            [k for k in ACCOUNTS if ACCOUNTS[k].name == r.account][0]
        )

        status_icon = {"FREE": "+", "TIGHT": "~", "FROZEN": "!"}[r.margin_status]
        print(f"\n{'─' * 72}")
        print(f"  {r.account}  [{status_icon}] Margin: {r.margin_status}")
        print(f"{'─' * 72}")

        # Account health
        coverage = acct.options_income_annual / acct.margin_interest_annual if acct.margin_interest_annual > 0 else float('inf')
        net_yield = (acct.options_income_annual - acct.margin_interest_annual) / acct.nlv * 100

        print(f"  NLV:           ${acct.nlv:>12,.0f}")
        print(f"  Margin Debt:   ${acct.margin_debt:>12,.0f}")
        print(f"  Margin Ceiling:${acct.margin_ceiling:>12,.0f}")
        print(f"  Headroom:      ${r.margin_headroom:>12,.0f}")
        print(f"  Coverage:      {coverage:>11.1f}x (income/interest)")
        print(f"  Net Yield:     {net_yield:>11.2f}%")

        print(f"\n  --- Kelly Criterion ---")
        print(f"  Win Rate:      {acct.win_rate:>11.1%}")
        print(f"  Full Kelly:    {r.kelly_fraction:>11.1%}")
        print(f"  Half Kelly:    {r.half_kelly:>11.1%}  (recommended)")
        print(f"  Expectancy/R:  ${r.expectancy_per_r:>10,.2f}")

        print(f"\n  --- Position Sizing ---")
        print(f"  Risk Budget:   ${r.risk_budget_1r:>12,.0f}  (1R at {acct.risk_per_trade_pct:.1%})")

        if r.ticker and r.strike:
            print(f"  Ticker:        {r.ticker}")
            print(f"  Strike:        ${r.strike:,.2f}")
            if r.premium_per_contract is not None:
                per_share = r.premium_per_contract
                per_contract = per_share * 100
                print(f"  Premium:       ${per_share:.2f}/share (${per_contract:,.0f}/contract)")

        print(f"\n  Max contracts (risk budget):   {r.max_contracts_by_risk:>4}")
        print(f"  Max contracts (margin):         {r.max_contracts_by_margin:>4}")
        print(f"  Max contracts (concentration):  {r.max_contracts_by_concentration:>4}")
        print(f"  ─────────────────────────────────────")
        print(f"  RECOMMENDED:                    {r.recommended_contracts:>4} contracts")
        print(f"  Max notional exposure:  ${r.max_notional:>12,.0f}")

        if r.warnings:
            print(f"\n  *** WARNINGS ***")
            for w in r.warnings:
                print(f"    - {w}")

    # Summary table
    print(f"\n{'=' * 72}")
    print("  QUICK REFERENCE TABLE")
    print(f"{'=' * 72}")
    print(f"  {'Account':<14} {'Status':<8} {'1R Budget':>10} {'Max Contracts':>14} {'Kelly½':>8}")
    print(f"  {'─'*14} {'─'*8} {'─'*10} {'─'*14} {'─'*8}")
    for r in results:
        print(f"  {r.account:<14} {r.margin_status:<8} ${r.risk_budget_1r:>9,.0f} {r.recommended_contracts:>14} {r.half_kelly:>7.1%}")
    print()


def main():
    parser = argparse.ArgumentParser(description="Position sizing calculator for short put strategies")
    parser.add_argument("--account", choices=["No1", "No2", "No3", "WinWin"], help="Single account to analyze")
    parser.add_argument("--ticker", help="Specific ticker to size for")
    parser.add_argument("--strike", type=float, help="Strike price of the put")
    parser.add_argument("--premium", type=float, help="Premium per contract")
    parser.add_argument("--stock-price", type=float, help="Current stock price")
    parser.add_argument("--risk-pct", type=float, help="Risk percentage per trade (e.g. 0.02 for 2%%)")
    parser.add_argument("--multiplier", type=int, default=100, help="Option multiplier (default: 100)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show detailed calculations")
    args = parser.parse_args()

    accounts_to_run = [args.account] if args.account else list(ACCOUNTS.keys())
    results = []

    for acct_key in accounts_to_run:
        acct = ACCOUNTS[acct_key]
        result = size_position(
            acct,
            ticker=args.ticker,
            strike=args.strike,
            premium_per_contract=args.premium,
            stock_price=args.stock_price,
            multiplier=args.multiplier,
            custom_risk_pct=args.risk_pct,
        )
        results.append(result)

    print_report(results, verbose=args.verbose)


if __name__ == "__main__":
    main()
