#!/usr/bin/env python3
"""Margin Reduction Plan Calculator — Account No1."""

# Current state (from portfolio review 2026-05-24)
NLV = 4_090_000  # $4.09M
MARGIN = -789_455  # -$789K
TOTAL_PORTFOLIO = 4_730_000  # $4.73M

# Estimated prices (based on strike/OTM from portfolio review)
PRICES = {
    "NVDA": 220,      # Strike $235, ~7% OTM → $220
    "AAPL": 306,      # Mentioned as $306 in review
    "TSLA": 420,      # Strike $475, 13% OTM → $420
    "META": 610,      # Strike $680, 11% OTM → $610
    "AMZN": 270,      # Strike $290, 8% OTM → $270
    "700.HK": 66.4,   # HK$520 ÷ 7.8366 = $66.4 USD
    "MSFT": 425,      # Strike $465, 10% OTM → $425
    "GOOG": 386,      # Mentioned as $386 in review
    "O": 58,          # Strike $65, ~10% OTM → $58
}

# Current position values (from portfolio review)
CURRENT_VALUES = {
    "NVDA": 1_740_000,
    "AAPL": 633_000,
    "TSLA": 533_000,
    "META": 125_000,
    "AMZN": 107_000,
    "700.HK": 80_000,
    "MSFT": 75_000,
    "GOOG": 62_000,
    "O": 56_000,
    "TSM": 500_000,  # Not being sold
}

# Shares to sell
SHARES_TO_SELL = {
    "NVDA": 1000,
    "AAPL": 400,
    "TSLA": 300,
    "META": 100,
    "AMZN": 100,
    "700.HK": 400,
    "MSFT": 100,
    "GOOG": 100,
    "O": 300,
}

def main():
    print("=" * 60)
    print("MARGIN REDUCTION PLAN — ACCOUNT NO1")
    print("=" * 60)

    # Calculate proceeds
    print("\n## SALES PROCEEDS\n")
    total_proceeds = 0
    for stock, shares in SHARES_TO_SELL.items():
        price = PRICES[stock]
        proceeds = shares * price
        total_proceeds += proceeds
        pct_of_position = (proceeds / CURRENT_VALUES[stock]) * 100
        print(f"  {stock:10} {shares:,} shares @ ${price:>7.2f} = ${proceeds:>12,.0f} ({pct_of_position:.1f}% of position)")

    print(f"\n  {'TOTAL':10} {'':>14} ${total_proceeds:>12,.0f}")

    # Calculate new margin
    print("\n## MARGIN IMPACT\n")
    new_margin = MARGIN + total_proceeds
    margin_reduction_pct = (total_proceeds / abs(MARGIN)) * 100

    print(f"  Current margin:      ${MARGIN:>12,}")
    print(f"  Proceeds applied:    ${total_proceeds:>12,.0f}")
    print(f"  New margin balance:  ${new_margin:>12,}")
    print(f"  Margin reduced by:   {margin_reduction_pct:.1f}%")

    # Calculate new interest cost
    print("\n## INTEREST COST IMPACT\n")
    # IBKR margin rate ~5.5% (benchmark + spread)
    RATE = 0.055
    old_interest = abs(MARGIN) * RATE
    new_interest = abs(new_margin) * RATE
    interest_savings = old_interest - new_interest

    print(f"  Old annual interest:   ${old_interest:>10,.0f} (at {RATE*100:.1f}%)")
    print(f"  New annual interest:   ${new_interest:>10,.0f}")
    print(f"  Annual savings:        ${interest_savings:>10,.0f}")

    # Calculate new NLV (unchanged since selling stock + paying margin doesn't change equity)
    print("\n## NLV IMPACT\n")
    print(f"  NLV before:  ${NLV:>12,} (unchanged — deleveraging doesn't affect equity)")
    print(f"  NLV after:   ${NLV:>12,}")

    # Calculate remaining position values and % of NLV
    print("\n## POSITION PERCENTAGES AFTER SALES\n")
    print(f"  {'Stock':10} {'Remaining':>12} {'% NLV':>8} {'Change':>10}")
    print(f"  {'-'*10} {'-'*12} {'-'*8} {'-'*10}")

    remaining_top4 = 0
    for stock, current_val in CURRENT_VALUES.items():
        if stock in SHARES_TO_SELL:
            proceeds = SHARES_TO_SELL[stock] * PRICES[stock]
            remaining = current_val - proceeds
        else:
            remaining = current_val

        pct_before = (current_val / NLV) * 100
        pct_after = (remaining / NLV) * 100
        change = pct_after - pct_before

        print(f"  {stock:10} ${remaining:>11,.0f} {pct_after:>7.1f}% {change:>9.1f}%")

        # Track top 4
        if stock in ["NVDA", "AAPL", "TSLA", "TSM"]:
            remaining_top4 += remaining

    top4_pct_before = (CURRENT_VALUES["NVDA"] + CURRENT_VALUES["AAPL"] + CURRENT_VALUES["TSLA"] + CURRENT_VALUES["TSM"]) / NLV * 100
    top4_pct_after = remaining_top4 / NLV * 100

    print(f"\n  Top 4 concentration before: {top4_pct_before:.1f}%")
    print(f"  Top 4 concentration after:  {top4_pct_after:.1f}%")

    # Concentration assessment
    print("\n## CONCENTRATION STATUS\n")
    print(f"  {'Metric':30} {'Before':>10} {'After':>10} {'Target':>10} {'Status':>10}")
    print(f"  {'-'*30} {'-'*10} {'-'*10} {'-'*10} {'-'*10}")

    nvda_before = CURRENT_VALUES["NVDA"] / NLV * 100
    nvda_after = (CURRENT_VALUES["NVDA"] - SHARES_TO_SELL["NVDA"] * PRICES["NVDA"]) / NLV * 100
    nvda_status = "✓ OK" if nvda_after <= 20 else "⚠ HIGH"

    tech_before = 79.6
    # Tech after = NVDA + AAPL + TSLA + TSM + META + AMZN remaining
    tech_remaining = (
        (CURRENT_VALUES["NVDA"] - SHARES_TO_SELL["NVDA"] * PRICES["NVDA"]) +
        (CURRENT_VALUES["AAPL"] - SHARES_TO_SELL["AAPL"] * PRICES["AAPL"]) +
        (CURRENT_VALUES["TSLA"] - SHARES_TO_SELL["TSLA"] * PRICES["TSLA"]) +
        CURRENT_VALUES["TSM"] +
        (CURRENT_VALUES["META"] - SHARES_TO_SELL["META"] * PRICES["META"]) +
        (CURRENT_VALUES["AMZN"] - SHARES_TO_SELL["AMZN"] * PRICES["AMZN"])
    )
    tech_after = tech_remaining / NLV * 100
    tech_status = "✓ OK" if tech_after <= 50 else "⚠ HIGH"

    margin_pct_before = abs(MARGIN) / NLV * 100
    margin_pct_after = abs(new_margin) / NLV * 100
    margin_status = "✓ OK" if margin_pct_after <= 10 else "✓ GOOD" if margin_pct_after <= 5 else "⚠ HIGH"

    print(f"  {'NVDA single position':30} {nvda_before:>9.1f}% {nvda_after:>9.1f}% {'≤20%':>10} {nvda_status:>10}")
    print(f"  {'Top 4 concentration':30} {top4_pct_before:>9.1f}% {top4_pct_after:>9.1f}% {'≤40%':>10} {'⚠ STILL HIGH':>10}")
    print(f"  {'US Tech sector':30} {tech_before:>9.1f}% {tech_after:>9.1f}% {'≤50%':>10} {tech_status:>10}")
    print(f"  {'Margin / NLV':30} {margin_pct_before:>9.1f}% {margin_pct_after:>9.1f}% {'≤10%':>10} {margin_status:>10}")

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"""
  Proceeds:           ${total_proceeds:,.0f}
  New margin balance: ${new_margin:,.0f} (from ${MARGIN:,})

  ✓ Margin/NLV drops from {margin_pct_before:.1f}% to {margin_pct_after:.1f}%
  ✓ Interest savings: ${interest_savings:,.0f}/year
  ✓ NVDA concentration drops from {nvda_before:.1f}% to {nvda_after:.1f}%
  ✓ Tech sector drops from {tech_before:.1f}% to {tech_after:.1f}%

  ⚠ Top 4 still at {top4_pct_after:.1f}% (above 40% threshold)
     → Need more NVDA/TSLA sales to reach target
  ⚠ NVDA still at {nvda_after:.1f}% (above 20% threshold)
     → Need to sell another ~{(nvda_after-20)/100*NLV/PRICES['NVDA']:.0f} shares to reach 20%
""")

if __name__ == "__main__":
    main()