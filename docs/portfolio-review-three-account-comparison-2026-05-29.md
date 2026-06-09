# Portfolio Review — Four-Account Comparison

**Date:** 2026-05-29
**Perspective:** Cross-account risk synthesis + margin optimization + capital allocation

---

## Executive Summary

| Account | NLV | Margin | Margin/NLV | Coverage | Open P&L | Win Rate | Status |
|---------|-----|--------|------------|----------|----------|----------|--------|
| **No1** | $4.09M | -$789K | 16.7% | 1.2x | +$3.32M | 72.6% | **AMBER** |
| **No2** | $586K | -$376K | 64.1% | 3.1x | +$4.6K | 90.5% | **GREEN** |
| **No3** | $171K | -$19K | 10.8% | 46x | +$3.8K | 100% | **GREEN** |
| **WinWin** | $739K | TBD | — | — | +$2.9K | ~85% | **GREEN** |

**Combined Portfolio:**
- Total NLV: **$5.59M** (No1 $4.09M + No2 $586K + No3 $171K + WinWin $739K)
- Total Open Options: **211 positions** across 4 accounts
- Total Open P&L: **+$3.33M** (dominated by JP/HK positions in No1)
- US-only Open P&L: **+$42.5K**

**Key Insight:** No1 dominates the combined portfolio (73% of NLV) and drives the aggregate risk. No1's margin problem (1.2x coverage) offsets No2, No3, and WinWin's strong performance. WinWin is a newer account running an AI infrastructure thesis via semiconductor put selling.

---

## Account Profiles

### Account No1 — The Problem Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | Diversified global (US, JP, HK) | Concentration in US Tech (79%) |
| Strategy | JP diversification via short puts | Margin-funded, cost exceeds income |
| Margin balance | -$789K | Growing trajectory (+82% in 4 months) |
| Options exposure | 116 positions | $140M notional (JP puts dominate) |
| Open P&L | +$3.32M | +$3.25M from JP/HK puts, +$16K from US |
| Stress test | 4/8 scenarios = margin call | Vulnerable to correlated selloff |

**Diagnosis:** Options income ($55-75K) barely covers margin ($36K rising). JP puts would push cost to $72-96K — exceeding income entirely. Today's 05-29 expiry has 38 positions expiring — most US puts/calls are profitable.

**Prescription:** Reduce margin to -$500K before resuming JP strategy. Sell distressed positions, apply premiums to margin paydown, stop new JP puts.

---

### Account No2 — The Leverage Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | US-focused quality + speculative | Well-diversified, 24 underlyings |
| Strategy | Quality entry puts + covered calls | Conservative, well-structured |
| Margin balance | -$376K | High relative to NLV (64.1%) |
| Options exposure | 30 positions, $936K notional | Manageable, quality entries |
| Open P&L | +$4.6K | Solid — 14 of 16 puts winning |
| Stress test | 0/9 scenarios = margin call | Safe despite high leverage |

**Diagnosis:** Leverage ratio is high (64% margin/NLV) but options income covers margin ($24K) by 3.1x. The 05-29 expiry has 14 positions — all looking positive. LITE $820P is the largest winner (+$2.4K).

**Prescription:** Continue quality put strategy. Monitor leverage ratio. Focus on reducing distressed holdings.

---

### Account No3 — The Model Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | Concentrated (TSLA 26%, quality) | TSLA drives 42% of VaR |
| Strategy | TSLA strangle + quality puts | Exemplary execution, 100% OTM |
| Margin balance | -$19K | Recently cash-positive |
| Options exposure | 18 positions, $558K notional | High relative to NLV but all OTM |
| Open P&L | +$3.8K | All 7 expiring today are winning |
| Stress test | 3/10 scenarios = margin call | Tail risk from TSLA + put assignment |

**Diagnosis:** Best performing account. 100% win rate, 46x coverage. Today's expiry has 7 positions — all profitable. LITE $810P (+$2.2K) and COHR $310P (+$625) are the big winners. PLTR $120P is the only position at risk (-$57).

**Prescription:** No margin reduction needed. Focus on TSLA concentration (<30%). Continue the disciplined put-selling approach.

---

### WinWin — The AI Infrastructure Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | AI infrastructure thesis (compute + optical + storage + power) | Semiconductor sector concentration |
| Strategy | Systematic OTM put selling on AI/semi names | 43 puts, 4 calls across 23 underlyings |
| Portfolio value | ~$739K | US Quality 47%, US Tech Growth 45% |
| Options exposure | 47 positions, $1.28M notional | 1.7x portfolio value |
| Open P&L | +$2.9K | Positive across most positions |
| Focus | LITE (7), INTC (5), BE (3), SNDK (2), TSM (3) | Deep AI infra thesis |

**Diagnosis:** Thesis-driven options strategy focused on AI infrastructure buildout (compute, optical, storage, power). Heavy LITE exposure (7 puts across all expiries). SNDK $1,000P (Jun 5) and LITE $500P (Jul 2) are ITM. INTC $80 ladder (5 puts) is a turnaround bet on a moatless company. Quality anchors via GOOG and MSFT puts.

**Prescription:** Monitor LITE concentration (7 puts = $351K notional). Manage SNDK $1,000P and LITE $500P (Jul 2) ITM assignments. INTC $85P (Jul 2) is the most at-risk position (-$167).

---

## Today's Expiry — 2026-05-29

**Critical:** 66 positions across all accounts expire today.

### Account No1 — 38 positions expiring

| Category | Positions | Premium Collected | Mark Value | P&L |
|----------|-----------|-------------------|------------|-----|
| US Puts | 22 | $12,077 | $3,381 | **+$8,696** |
| US Calls | 16 | $4,276 | $1,975 | **+$2,301** |
| **Total** | **38** | **$16,353** | **$5,356** | **+$10,997** |

**Key winners:** MU $650P x2 (+$3.3K), LITE $800P (+$2.2K), COHR $320P (+$871), CRWD $600P (+$361)
**Key losers:** VRT $350P (-$717), ADBE $240P (-$144), META 640C Jun 5 exposure

### Account No2 — 14 positions expiring

| Category | Positions | Premium Collected | Mark Value | P&L |
|----------|-----------|-------------------|------------|-----|
| Puts | 9 | $4,442 | $1,010 | **+$3,432** |
| Calls | 5 | $927 | $436 | **+$491** |
| **Total** | **14** | **$5,369** | **$1,446** | **+$3,923** |

**Key winners:** LITE $820P (+$2.4K), COHR $300P (+$465), GOOG $360P (+$147)

### Account No3 — 7 positions expiring

| Category | Positions | Premium Collected | Mark Value | P&L |
|----------|-----------|-------------------|------------|-----|
| Puts | 4 | $3,313 | $417 | **+$2,896** |
| Calls | 3 | $470 | $120 | **+$351** |
| **Total** | **7** | **$3,783** | **$537** | **+$3,247** |

**Key winners:** LITE $810P (+$2.2K), COHR $310P (+$625), TSLA $360P (+$160)

### WinWin — 7 positions expiring

| Category | Positions | Premium Collected | Mark Value | P&L |
|----------|-----------|-------------------|------------|-----|
| Puts | 5 | $572 | $13 | **+$559** |
| Calls | 2 | $298 | $5 | **+$293** |
| **Total** | **7** | **$870** | **$18** | **+$852** |

**Key winners:** SNDK $600P (+$99), CRWV $85P (+$111), IREN $40P (+$97)

### Combined Today's Expiry Summary

| Account | Expiring | P&L | Premium Retained |
|---------|----------|-----|-----------------|
| No1 | 38 | +$11.0K | 67% of premium |
| No2 | 14 | +$3.9K | 73% of premium |
| No3 | 7 | +$3.2K | 86% of premium |
| WinWin | 7 | +$0.9K | 98% of premium |
| **Total** | **66** | **+$19.0K** | **71% of premium** |

---

## Margin Analysis — Cross-Account

### Margin Interest YTD

| Month | No1 | No2 | No3 | Combined |
|-------|-----|-----|-----|----------|
| Jan | -$1,642 | -$379 | **+$34** | -$1,987 |
| Feb | -$1,605 | -$346 | **+$34** | -$1,917 |
| Mar | -$1,724 | -$572 | -$74 | -$2,370 |
| Apr | -$2,481 | -$1,145 | -$364 | -$3,990 |
| May (to 5/24) | -$3,023 | -$1,473 | -$611 | -$5,107 |
| **YTD** | **-$10,475** | **-$3,916** | **-$980** | **-$15,371** |

**No3 is actively reducing margin. No1 and No2 are growing.**

### Options Income vs Margin

| Account | Options Income | Margin Cost | Net | Coverage |
|---------|---------------|-------------|-----|----------|
| No1 | $55-75K | $36K → $72K | $19-39K → negative | 1.2x → 0.8x |
| No2 | $58-87K | $24K | $34-63K | 3.1x |
| No3 | $43-64K | $1.2K | $42-63K | 46x |
| **Combined** | **$156-226K** | **$61K** | **$95-165K** | **2.6x → 1.7x** |

---

## Options Strategy Assessment

### Win Rate & Expectancy

| Account | Positions | Win Rate | Open P&L | Assessment |
|---------|-----------|----------|----------|------------|
| No1 | 116 (69P/47C) | ~73% (puts) | +$3.32M | JP puts dominate P&L |
| No2 | 30 (16P/14C) | ~90% | +$4.6K | Consistent, well-managed |
| No3 | 18 (11P/7C) | ~100% | +$3.8K | Best edge |
| WinWin | 47 (43P/4C) | ~85% | +$2.9K | AI infra thesis, concentrated |
| **Combined** | **211** | **~80%** | **+$3.33M** | Positive expectancy |

### Options Notional / NLV

| Account | Notional | NLV | Ratio | Assessment |
|---------|----------|-----|-------|------------|
| No1 | $145.6M (JP-dominated) | $4.09M | 35.6x | EXTREME — JP puts oversized |
| No2 | $936K | $586K | 1.6x | MODERATE — manageable |
| No3 | $558K | $171K | 3.3x | HIGH — but all OTM |
| WinWin | $1.28M | $739K | 1.7x | MODERATE — thesis-driven |
| **US Only** | **$3.5M** | — | — | Reasonable across accounts |

---

## Risk Ranking

| Rank | Account | Primary Risk | Secondary Risk | Action Urgency |
|------|---------|--------------|----------------|----------------|
| 1 | **No1** | Income shortfall (1.2x → 0.8x) | JP puts assignment | **HIGH — act this week** |
| 2 | **No2** | Leverage amplification (64%) | Distressed drag | **MEDIUM — monitor** |
| 3 | **No3** | TSLA concentration (26%) | High maintenance margin | **LOW — monitor only** |
| 4 | **WinWin** | LITE concentration (7 puts) | Sector concentration | **LOW — monitor** |

---

## Cross-Account Position Overlap

### Concentration by Underlying (All Accounts Combined)

| Underlying | Accounts | Total Positions | Combined Notional | Risk |
|------------|----------|-----------------|-------------------|------|
| **LITE** | 1, 2, 3, WinWin | 11 puts | $701K | **HIGH** — 4 accounts exposed |
| **COHR** | 1, 2, 3, WinWin | 5 puts | $148K | MODERATE — 4 accounts |
| **TSLA** | 1, 2, 3, WinWin | 8 positions | $373K | MODERATE — well-distributed |
| **P (Pandora)** | 1, 2, 3 | 6 puts | $78K | LOW — consistent quality play |
| **PLTR** | 1, 2, 3, WinWin | 5 puts | $83K | LOW — spread across accounts |
| **NVDA** | 1, 3, WinWin | 9 positions | $280K | MODERATE — core holding |
| **TSM** | 1, 2, WinWin | 5 positions | $395K | MODERATE |
| **INTC** | WinWin | 5 puts | $40.5K | WinWin only |
| **BE** | WinWin | 3 puts | $47K | WinWin only |

**LITE is the most concentrated cross-account risk.** 11 put positions across all 4 accounts with $701K combined notional. If LITE drops sharply, all accounts are simultaneously impacted.

---

## Combined Portfolio Health

| Metric | Value | Threshold | Status |
|--------|-------|-----------|--------|
| Combined Margin/NLV | 24.3% | <20% | YELLOW |
| Combined Coverage | 1.7x | >1.5x | YELLOW |
| Combined Net Yield | 1.4% | >3% | RED |
| Open P&L | +$3.33M | Positive | GREEN |
| Today's expiring P&L | +$19K | Positive | GREEN |
| Cross-account LITE risk | $701K notional | <$500K | YELLOW |
| Stress test failure rate | 37% | <20% | RED |

**Overall: YELLOW — No1's problems drag down otherwise healthy No2 and No3.**

---

## Immediate Actions

| # | Action | Account | Impact |
|---|--------|---------|--------|
| 1 | **Let 66 positions expire today** — collect +$19K premium | All | +$19K income |
| 2 | **Sell remaining distressed positions** | No1 + No2 | -$240K margin |
| 3 | **Apply today's premiums to margin** | No1 | ~$2K paydown |
| 4 | **Stop all new JP put writing** | No1 | Prevent expansion |
| 5 | **Monitor LITE assignment risk** | All | $701K cross-account |
| 6 | **Monitor INTC $85P (Jul 2)** | WinWin | -$167 mark loss |

**After today's expiry: 145 positions remaining (down from 211). Next major expiry: June 5.**

---

*Generated from quantitative risk manager assessment across four accounts.*
*Data as of 2026-05-29 with live options positions from database.*
*FX rates: USD/JPY = 159.15, USD/HKD = 7.8366, USD/CAD = 1.3801*
