# Portfolio Review — Three-Account Comparison

**Date:** 2026-05-24
**Perspective:** Cross-account risk synthesis + margin optimization + capital allocation

---

## Executive Summary

| Account | NLV | Margin | Margin/NLV | Coverage | Win Rate | Net Yield | Status |
|---------|-----|--------|------------|----------|----------|-----------|--------|
| **No1** | $4.09M | -$789K | 16.7% | 1.2x | 72.6% | 0.24% | **AMBER** |
| **No2** | $586K | -$376K | 64.1% | 3.1x | 90.5% | 8.3% | **GREEN** |
| **No3** | $171K | -$19K | 10.8% | 46x | 100% | 30.5% | **GREEN** |

**Combined Portfolio:**
- Total NLV: **$4.85M**
- Total Margin: **-$1.18M** (24.3% of combined NLV)
- Combined Coverage: **1.7x** (borderline safe)
- Combined Net Yield: **1.4%**

**Key Insight:** No1 dominates the combined portfolio (84% of NLV) and drives the aggregate risk. No1's margin problem (1.2x coverage) offsets No2 and No3's strong performance.

---

## Account Profiles

### Account No1 — The Problem Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | Diversified global (US, JP, HK) | Concentration in US Tech (79%) |
| Strategy | JP diversification via short puts | Margin-funded, cost exceeds income |
| Margin balance | -$789K | Growing trajectory (+82% in 4 months) |
| Options exposure | $8.9M notional (JP puts) | Assignment = $2-3M on margin |
| Stress test | 4/8 scenarios = margin call | Vulnerable to correlated selloff |

**Diagnosis:** Options income ($55-75K) barely covers margin ($36K rising). JP puts would push cost to $72-96K — exceeding income entirely. Strategy sound in intent (Buffett-style Japan diversification) but funding mechanism (margin at 6.8%) is wrong.

**Prescription:** Reduce margin to -$500K before resuming JP strategy. Sell distressed positions, apply premiums to margin paydown, stop new JP puts.

---

### Account No2 — The Leverage Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | US-focused quality + speculative | Distressed drag (10.4%), TSLA concentration |
| Strategy | Quality entry puts + covered calls | Conservative, well-structured |
| Margin balance | -$376K | High relative to NLV (64.1%) |
| Options exposure | $650K notional | Manageable, quality entries |
| Stress test | 0/9 scenarios = margin call | Safe despite high leverage |

**Diagnosis:** Leverage ratio is very high (64% margin/NLV) but options income ($58-87K) covers margin ($24K) by 3.1x. The risk is leverage amplification, not income shortfall. A 35% tech decline = 55% equity decline relative to NLV.

**Prescription:** Sell distressed positions ($96K) — achieves <50% margin/NLV target in one day. Continue quality put strategy. Monitor leverage ratio, not just coverage.

---

### Account No3 — The Model Account

| Dimension | Value | Risk |
|-----------|-------|------|
| Portfolio type | Concentrated (TSLA 26%, quality) | TSLA drives 42% of VaR |
| Strategy | TSLA strangle + quality puts | Exemplary execution, 100% OTM |
| Margin balance | -$19K | Recently cash-positive (Jan-Feb) |
| Options exposure | $350K notional | High relative to NLV (136%) |
| Stress test | 3/10 scenarios = margin call | Tail risk from TSLA + put assignment |

**Diagnosis:** Best performing account. 100% win rate, 46x coverage, 30.5% net yield. Margin is negligible. The concern is TSLA concentration and high maintenance margin (71% of NLV), not margin cost.

**Prescription:** No margin reduction needed (already at 10.8%). Focus on TSLA concentration (<30%) and monitor LITE/COHR puts ($112K notional).

---

## Margin Analysis — Cross-Account

### Margin Interest YTD

| Month | No1 | No2 | No3 | Combined |
|-------|-----|-----|-----|----------|
| Jan | -$1,642 | -$379 | **+$34** (earned) | -$1,987 |
| Feb | -$1,605 | -$346 | **+$34** (earned) | -$1,917 |
| Mar | -$1,724 | -$572 | -$74 | -$2,370 |
| Apr | -$2,481 | -$1,145 | -$364 | -$3,990 |
| May | -$3,023 | -$1,473 | -$611 | -$5,107 |
| **YTD** | **-$10,475** | **-$3,916** | **-$980** | **-$15,371** |

**Observations:**
- No1: 82% increase from Jan-Mar avg to May ($1,657 → $3,023)
- No2: 5x increase from Jan ($379) to May peak, now reducing
- No3: Was cash-positive Jan-Feb, first borrowing in Mar

### Margin Trajectory

| Account | Jan Implied | May Implied | Current | Trend |
|---------|-------------|-------------|---------|-------|
| No1 | ~$290K | ~$534K | **$789K** | ↑ Accelerating |
| No2 | ~$72K | ~$281K | **$376K** | ↑ Peaked, now stable |
| No3 | +$8K (cash) | ~$116K | **$19K** | ↓ Paid down |

**No3 is actively reducing margin. No1 and No2 are growing.**

### Projected Annual Margin Cost

| Scenario | No1 | No2 | No3 | Combined |
|----------|-----|-----|-----|----------|
| Current run rate | $36K | $24K | $1.2K | **$61K** |
| At sustained balance | $53K | $24K | $1.2K | **$78K** |
| If all puts assign | $190-260K | $49K | $16K | **$255-325K** |

### Options Income vs Margin

| Account | Options Income | Margin Cost | Net | Coverage |
|---------|---------------|-------------|-----|----------|
| No1 | $55-75K | $36K → $72K | $19-39K → negative | 1.2x → 0.8x |
| No2 | $58-87K | $24K | $34-63K | 3.1x |
| No3 | $43-64K | $1.2K | $42-63K | 46x |
| **Combined** | **$156-226K** | **$61K** | **$95-165K** | **2.6x → 1.7x** |

**Combined coverage drops from 2.6x to 1.7x if No1's trajectory continues.**

---

## Options Strategy Assessment

### Win Rate & Expectancy

| Account | Positions | Win Rate | Avg R (Wins) | Avg R (Losses) | Expectancy |
|---------|-----------|----------|--------------|----------------|------------|
| No1 | 62 (puts) | 72.6% | +0.55R | -0.31R | **+0.31R** |
| No2 | 21 | 90.5% | +0.64R | -0.36R | **+0.55R** |
| No3 | 12 | **100%** | +0.93R | N/A | **+0.93R** |
| **Combined** | 95 | 79.2% | +0.64R | -0.32R | **+0.48R** |

**No3 has the best edge. No1's expectancy is dragged down by ITM JP puts.**

### Options Notional / NLV

| Account | Notional | NLV | Ratio | Assessment |
|---------|----------|-----|-------|------------|
| No1 | $8.9M | $4.09M | 2.2x | HIGH — JP puts oversized |
| No2 | $650K | $586K | 1.1x | MODERATE — manageable |
| No3 | $350K | $171K | 2.0x | HIGH — but all OTM |
| **Combined** | **$9.9M** | **$4.85M** | **2.0x** | Elevated aggregate exposure |

### Put Assignment Liability

| Account | Max Assignment | % of NLV | Probability | Impact |
|---------|---------------|----------|-------------|--------|
| No1 | $8.9M (realistic: $2-3M) | 49-73% | ITM JP puts = likely | Margin call if correlated |
| No2 | $410K | 70% | Low (most OTM) | Manageable |
| No3 | $234K | 136% | Very low (all OTM) | Would spike margin |

---

## VaR Analysis — Cross-Account

### Portfolio VaR (95%, 1-month)

| Account | VaR | % of NLV | Leverage Factor | Primary Driver |
|---------|-----|----------|-----------------|----------------|
| No1 | $538K | 11.4% | 1.05x | US Tech concentration |
| No2 | $97K | 16.5% | 1.58x | Leverage amplification |
| No3 | $24K | 14.0% | 1.08x | TSLA volatility |
| **Combined** | **$659K** | **13.6%** | 1.20x | No1 dominates |

**Combined VaR = $659K (13.6% of NLV). No1 contributes 81% of total VaR.**

### Component VaR Drivers

| Account | Top Contributor | VaR Contribution | % of Account VaR |
|---------|-----------------|-----------------|------------------|
| No1 | US Tech Growth | $536K | 99.6% |
| No2 | US Tech Growth | $50K | 51.7% |
| No3 | TSLA | $10K | 41.7% |

**US Tech concentration is the dominant risk across all three accounts.**

---

## Stress Test Summary

### Margin Call Scenarios

| Account | Total Scenarios | Margin Call | Warning | Safe | Failure Rate |
|---------|-----------------|-------------|---------|------|---------------|
| No1 | 8 | 4 | 1 | 3 | **50%** |
| No2 | 9 | 0 | 1 | 8 | **0%** |
| No3 | 10 | 3 | 1 | 6 | **30%** |

**No1 has the highest margin call risk. No2 is safest despite high leverage.**

### Worst-Case Scenario (All Accounts)

| Shock | No1 Impact | No2 Impact | No3 Impact | Combined |
|-------|------------|------------|------------|----------|
| US Tech -35% | -$1.32M equity | -$123K equity | -$17K equity | -$1.46M |
| + All puts assign | +$2-3M margin | +$410K margin | +$234K margin | +$2.6-3.6M margin |
| Post-shock margin | $2.8-3.8M | $786K | $252K | $3.8-4.8M |
| Post-shock coverage | 0.5x | 1.5x | 3.4x | **0.5x combined** |
| **Result** | **MARGIN CALL** | WARNING | MARGIN CALL | **MARGIN CALL** |

**A correlated selloff + simultaneous put assignment would trigger margin calls across all accounts.**

---

## Risk Limits — Cross-Account

### Current vs Limits

| Account | Margin | Hard Ceiling | Warning | Target | Status |
|---------|--------|--------------|---------|--------|--------|
| No1 | -$789K | -$607K | -$404K | -$550K | **ABOVE CEILING** |
| No2 | -$376K | -$920K | -$767K | -$350K | **WITHIN LIMITS** |
| No3 | -$19K | -$462K | -$100K | <$20K | **WITHIN LIMITS** |

**No1 is the only account exceeding its hard ceiling.**

### Suggested Max Margin Cost

| Account | Max Annual Interest | Implied Balance | % of Income |
|---------|--------------------|-----------------|-------------|
| No1 | **$37,400** | -$550K | 68% of $55K |
| No2 | **$39,400** | -$626K | 68% of $58K |
| No3 | **$29,100** | -$462K | 68% of $43K |
| **Combined** | **$106,000** | — | 68% of $156K |

---

## Margin/NLV Reduction Targets

| Account | Current | Target | Gap | Timeline | Primary Action |
|---------|---------|--------|-----|----------|----------------|
| No1 | 16.7% | **<10%** | -$289K | 4-6 weeks | Sell distressed + stop JP puts |
| No2 | 64.1% | **<50%** | -$83K | **1 day** | Sell distressed positions |
| No3 | 10.8% | **<15%** | Achieved | — | Maintain current level |

### Execution Priority

| Priority | Account | Action | Impact |
|----------|---------|--------|--------|
| **1** | No2 | Sell distressed ($96K) | Achieves target in 1 day |
| **2** | No1 | Sell distressed ($144K) | Margin -$789K → -$645K |
| **3** | No1 | Close P $85 puts | Avoid $51K margin increase |
| **4** | No1 | Stop new JP puts | Prevent $100-300K expansion |
| **5** | No1 | Apply premiums to margin | ~$2K/week paydown |
| **6** | No3 | Monitor LITE/COHR | Avoid $112K assignment spike |

**Combined margin reduction: -$289K (No1) + -$96K (No2) = -$385K → Combined margin/NLV drops from 24.3% to 16.5%**

---

## Capital Allocation Recommendations

### Across Accounts

| Recommendation | Rationale |
|----------------|-----------|
| **No1 → No3 transfer** | No3 has 46x coverage, can absorb new positions. No1 at 1.2x cannot. |
| **Shift new put writing to No3** | Best edge (+0.93R), minimal margin cost, 100% win rate. |
| **No2: Maintain, don't expand** | High leverage (64%), but well-covered. Reduce first, then expand. |
| **No1: Reduce, pause strategy** | Margin exceeds limits. Focus on paydown, not expansion. |

### Strategy by Account

| Account | Current Strategy | Recommended Adjustment |
|---------|------------------|----------------------|
| No1 | JP diversification via margin | **PAUSE** — reduce margin first |
| No2 | Quality entry puts | **CONTINUE** — sell distressed, then resume |
| No3 | TSLA strangle + semiconductor | **CONTINUE** — best performing |

---

## Bottom Line — Three-Account Summary

### Risk Ranking

| Rank | Account | Primary Risk | Secondary Risk | Action Urgency |
|------|---------|--------------|----------------|----------------|
| 1 | **No1** | Income shortfall (1.2x → 0.8x) | JP puts assignment | **HIGH — act this week** |
| 2 | **No2** | Leverage amplification (64%) | Distressed drag | **MEDIUM — 1 day to fix** |
| 3 | **No3** | TSLA concentration (26%) | High maintenance margin | **LOW — monitor only** |

### Combined Portfolio Health

| Metric | Value | Threshold | Status |
|--------|-------|-----------|--------|
| Combined Margin/NLV | 24.3% | <20% | YELLOW |
| Combined Coverage | 1.7x | >1.5x | YELLOW |
| Combined Net Yield | 1.4% | >3% | RED |
| VaR as % of NLV | 13.6% | <15% | GREEN |
| Stress test failure rate | 37% | <20% | RED |

**Overall: YELLOW — No1's problems drag down otherwise healthy No2 and No3.**

### Immediate Actions (This Week)

| # | Action | Account | Impact |
|---|--------|---------|--------|
| 1 | Sell all distressed positions | No1 + No2 | -$240K margin |
| 2 | Close P $85 puts before May 27 | No1 | Avoid $51K margin |
| 3 | Sell 732 HK (penny stock) | No2 | -$7K margin |
| 4 | Stop all new JP put writing | No1 | Prevent expansion |
| 5 | Apply options premiums to margin | All | ~$5K/week paydown |

**After Week 1: Combined margin -$1.18M → -$0.93M (19.2% of NLV) — closer to target.**

---

*Generated from quantitative risk manager assessment across three accounts.*
*Data as of 2026-05-24 with live options positions from database.*
*FX rates: USD/JPY = 159.15, USD/HKD = 7.8366, USD/CAD = 1.3801*