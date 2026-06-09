# Covered Call Strategy — Account No1 Top 10 Holdings

**Date:** 2026-05-25
**Objective:** Trim overweight positions via covered call assignment + generate income on all top 10 holdings
**Target:** Top 4 concentration from 83.3% → 40%, NVDA from 42.6% → 20%
**Timeline:** 6-9 months

---

## Current Top 10 Holdings

| # | Symbol | Value | % NLV | Target % | Action | Shares (est.) |
|---|--------|-------|-------|---------|--------|---------------|
| 1 | NVDA | $1.74M | 42.6% | 20% | **Aggressive trim** | ~7,400 |
| 2 | AAPL | $633K | 15.5% | 10% | **Mild trim** | ~2,100 |
| 3 | TSM | $500K | 12.2% | 10% | **Mild trim** | ~1,200 |
| 4 | TSLA | $533K | 13.0% | 10% | **Mild trim** | ~1,300 |
| 5 | META | $125K | 3.1% | 3% | **Income only** | ~205 |
| 6 | AMZN | $107K | 2.6% | 2.5% | **Income only** | ~400 |
| 7 | 700.HK | $80K | 2.0% | 1.0% | **Trim via ATM calls** | ~1,200 |
| 8 | MSFT | $75K | 1.8% | 2% | **Income only** | ~180 |
| 9 | O | $56K | 1.4% | 1.5% | **Income only** | ~1,000 |
| 10 | GOOG | $62K | 1.5% | 2% | **Income only** | ~160 |

**Top 4: 83.3% → Target: 40%**

---

## Phase 1: Aggressive Trim — NVDA (42.6% → 20%)

**Need to sell: ~3,100 shares via covered call assignment over 6-9 months**

### Strike Ladder (ascending as NVDA rallies)

| Month | Strike | Contracts | Shares | Premium Est. | Notes |
|-------|--------|-----------|--------|-------------|-------|
| **May (existing)** | $235 | 10 | 1,000 | $450 | Already placed |
| Jun | $240 | 5 | 500 | $1,750 | 9% OTM |
| Jun | $245 | 5 | 500 | $1,400 | 11% OTM |
| Jul | $250 | 10 | 1,000 | $4,000 | 14% OTM |
| Aug | $255 | 5 | 500 | $2,250 | 16% OTM |
| Sep | $260 | 5 | 500 | $2,250 | 18% OTM |
| Oct | $265 | 5 | 500 | $2,250 | 20% OTM |
| Nov | $270 | 5 | 500 | $2,250 | 23% OTM |
| **Total** | | **50** | **5,000** | **$16,600** | Over-provisioned for flexibility |

**50 contracts cover 5,000 shares.** Target is ~3,100 assigned. Extra contracts expire OTM and are rolled or replaced.

### Roll Rules

| Situation | Action |
|-----------|--------|
| Call expires OTM | Roll to same strike +1 month. Collect new premium. |
| Call approaching ITM (within 3%) | Let assign. This is a trim at a good price. |
| Call deep ITM early | Accept early assignment. Add new calls at higher strikes. |
| NVDA drops >15% | Pause new calls. Let existing expire. Resume when stable. |

---

## Phase 2: Mild Trim — AAPL (15.5% → 10%)

**Need to sell: ~200-300 shares over 6 months**

AAPL has 2 covered calls at $315 (near-ITM) and $317.5 expiring May 29.

| Month | Strike | Contracts | Shares | Premium Est. | Notes |
|-------|--------|-----------|--------|-------------|-------|
| **May (existing)** | $315 | 2 | 200 | $1,260 | Near-ITM, likely assign |
| **May (existing)** | $317.5 | 2 | 200 | $1,240 | Slightly higher |
| Jul | $320 | 2 | 200 | $1,200 | ~5% OTM |
| Sep | $325 | 2 | 200 | $1,100 | ~6% OTM |
| **Total** | | **8** | **800** | **$4,800** | |

If all assign: 800 shares sold → AAPL drops to ~$389K (9.5% of NLV). Target met.

If only half assign: 400 shares sold → AAPL at ~$513K (12.5%). Continue rolling.

### Roll Rules

- Let $315 calls assign if AAPL > $310 (selling at good price)
- If AAPL drops below $290, pause calls. Resume at $300+.

---

## Phase 3: Mild Trim — TSLA (13.0% → 10%)

**Need to sell: ~100-200 shares over 6 months**

TSLA has 1 covered call at $475 expiring May 29.

| Month | Strike | Contracts | Shares | Premium Est. | Notes |
|-------|--------|-----------|--------|-------------|-------|
| **May (existing)** | $475 | 1 | 100 | $1,030 | 13% OTM |
| Jul | $480 | 1 | 100 | $1,000 | 14% OTM |
| Sep | $490 | 1 | 100 | $900 | 17% OTM |
| **Total** | | **3** | **300** | **$2,930** | |

If all assign: 300 shares sold → TSLA at ~$407K (10.0%). Target met.

### Roll Rules

- TSLA is volatile — write calls 12-15% OTM to avoid premature assignment during dips
- If TSLA rallies to $450+, tighten strikes to $470-480

---

## Phase 4: Mild Trim — TSM (12.2% → 10%)

**Need to sell: ~100 shares over 6 months**

TSM has 2 covered calls at $445 and $450 expiring May 29.

| Month | Strike | Contracts | Shares | Premium Est. | Notes |
|-------|--------|-----------|--------|-------------|-------|
| **May (existing)** | $445 | 1 | 100 | $720 | 9% OTM |
| **May (existing)** | $450 | 1 | 100 | $1,100 | 11% OTM |
| Aug | $455 | 1 | 100 | $800 | 13% OTM |
| **Total** | | **4** | **400** | **$3,420** | |

If all assign: 400 shares sold → TSM at ~$324K (7.9%). Below target but fine.

If 200 assign: TSM at ~$412K (10.1%). Close to target.

### Roll Rules

- TSM less volatile than NVDA/TSLA — can write 8-12% OTM
- If geopolitical tension spikes (Taiwan), pause all TSM calls

---

## Phase 5: Income Only — META, AMZN, MSFT, GOOG

**No trimming intent. Write calls 10-15% OTM for pure income.**

### META ($610, ~205 shares)

| Month | Strike | Contracts | Shares | Premium Est. |
|-------|--------|-----------|--------|-------------|
| **May (existing)** | $680 | 1 | 100 | $1,060 |
| Jul | $685 | 1 | 100 | $1,000 |
| Sep | $690 | 1 | 100 | $950 |

### AMZN ($270, ~400 shares)

| Month | Strike | Contracts | Shares | Premium Est. |
|-------|--------|-----------|--------|-------------|
| **May (existing)** | $290 | 2 | 200 | $1,140 |
| Jul | $295 | 2 | 200 | $1,050 |
| Sep | $300 | 2 | 200 | $950 |

### MSFT ($425, ~180 shares)

| Month | Strike | Contracts | Shares | Premium Est. |
|-------|--------|-----------|--------|-------------|
| **May (existing)** | $465 | 2 | 200 | $1,140 |
| Jul | $470 | 1 | 100 | $550 |
| Sep | $475 | 1 | 100 | $500 |

### GOOG ($386, ~160 shares)

| Month | Strike | Contracts | Shares | Premium Est. |
|-------|--------|-----------|--------|-------------|
| Jul | $420 | 1 | 100 | $650 |
| Sep | $425 | 1 | 100 | $600 |

**Income-only total premium: ~$10,690 over 6 months**

---

## Phase 8: Reduce HK to 3% — Covered Call Trim + Position Cleanup

**Current: $232K (4.9%) → Target: $142K (3.0%)**
**Reduction needed: ~$90K**

### Target Allocation at 3%

| Stock | Company | Current | Target (3%) | Reduce By | Moat |
|-------|---------|---------|-------------|-----------|------|
| 700 (Tencent) | Social/gaming/platform | ~$80K | $47K (1.0%) | $33K | Strong — anchor |
| 388 (HKEX) | Monopoly exchange | ~$30K | $33K (0.7%) | **Hold/Add** | Exceptional |
| 1299 (AIA) | Pan-Asian insurance | ~$50K | $24K (0.5%) | $26K | Strong — trim |
| 2318 (Ping An) | Insurance/finTech | ~$30K | $19K (0.4%) | $11K | Moderate — trim |
| 9618 (JD.com) | E-commerce | ~$17K | $19K (0.4%) | **Hold** | Moderate |
| 291 (CR Beer) | Consumer | ~$15K | $0 | **SELL** | Weak |
| 1800 (CCCC) | State infra | ~$10K | $0 | **SELL** | Weak |
| **Total** | | **$232K** | **$142K** | **$90K** | |

### Step 1: Direct Sales — No Moat Positions ($25K)

| Stock | Action | Est. Proceeds | Rationale |
|-------|--------|--------------|-----------|
| 291 (CR Beer) | SELL all shares | ~$15K | No moat, no pricing power |
| 1800 (CCCC) | SELL all shares | ~$10K | State-owned, low ROC |

### Step 2: ATM Covered Calls for Trimming ($65K)

**Critical:** Existing HK covered calls are all deep OTM (Tencent $600 vs ~$530, AIA $92.5 vs ~$80, Ping An $72.5 vs ~$55). They collect premium but won't reduce position size. **To trim to 3%, write ATM or slightly ITM calls** — assignment is the goal.

| Stock | New Calls | Strike (HKD) | Expiry | Shares Called Away | Est. Proceeds | Note |
|-------|-----------|-------------|--------|-------------------|---------------|------|
| 700 (Tencent) | 4-5 | ATM ~530-540 | Monthly Jun/Jul | 400-500 shares | ~$33K | Existing $600 calls are deep OTM — keep for income. New ATM calls ensure assignment. |
| 1299 (AIA) | 2-3 | ATM ~78-80 | Monthly Jun/Jul | ~1,000-1,500 shares | ~$26K | Existing $92.5/$95 calls too far OTM. Write ATM to actually reduce. |
| 2318 (Ping An) | 1-2 | ATM ~55-58 | Monthly Jun/Jul | ~1,000 shares | ~$11K | Existing $72.5 calls won't assign. Write ATM for real trimming. |

### Step 3: Manage Existing HK Calls

| Position | Action | Rationale |
|----------|--------|-----------|
| 700 $600 Jun/Jul (4 contracts) | KEEP — additional income | Premium already collected, deep OTM |
| 1299 $92.5/$95 Jun/Jul (4 contracts) | KEEP — free income | Deep OTM, no conflict with ATM calls |
| 2318 $72.5 Jun/Jul (8 contracts) | REDUCE to 4 contracts | 8 contracts on a trimming position is excessive |
| 388 $480/$490 Jun/Jul (2 contracts) | KEEP — HKEX is hold/add | Premium income |

### Step 4: Roll Rules for HK Calls

| Situation | Action |
|-----------|--------|
| ATM call assigned | Target met — redeploy proceeds to margin paydown |
| ATM call expires OTM (stock dipped) | Roll to same strike +1 month. Collect new premium. |
| Stock rallies sharply | Let assign at higher price — better sale price |
| Broad HK selloff (-10%+) | Pause new calls. Let existing expire. Resume at lower strikes. |

### HK Premium Income Projection

| Source | Calls | 6-Month Premium | Purpose |
|--------|-------|-----------------|---------|
| Existing deep OTM (700/1299/2318/388) | 14 | ~$3,100 | Income |
| New ATM trimming calls | 7-10 | ~$4,500 | Trim + income |
| **HK Total** | **21-24** | **~$7,600** | |

### Timeline

| Week | Action | HK % |
|------|--------|------|
| Week 1 | Sell 291 + 1800. Write ATM calls on 700/1299/2318. | 4.4% |
| Week 4-6 | ATM calls assign as planned. | 3.0% |
| Ongoing | Roll remaining covered calls for income on kept positions. | 3.0% |

---

## Phase 9: Increase JP to 3% — Short Put Accumulation

**Current: $63K (1.3%) → Target: $142K (3.0%)**
**Increase needed: ~$79K**

### Current JP Holdings + ITM Assignments

After June 11 ITM put assignments:

| Stock | Current (est.) | After ITM Assign | Added | Cost |
|-------|---------------|-----------------|-------|------|
| Nintendo (7974) | ~$33K | ~$43K | +$10K | ¥7,500/7,750 assigns |
| MHI (7011) | ~$10K | ~$21K | +$11K | ¥4,000-4,300 assigns |
| Sony (6758) | ~$11K | ~$13K | +$2K | ¥3,700 assigns |
| Mitsubishi (8058) | ~$7K | ~$11K | +$4K | ¥5,250 assigns |
| Itochu (8001) | ~$6K | ~$8K | +$2K | ¥2,050/2,100 assigns |
| MUFG (8306) | ~$4K | ~$4K | — | OTM — no change |
| Nikkei ETF (1321) | $0 | $0 | — | OTM — no change |
| Others (6752/8053/3690) | ~$5K | ~$5K | — | No puts active |
| **Total** | **~$63K (1.3%)** | **~$105K (2.2%)** | **+$33K** | |

**Post-ITM gap to 3%: ~$37K**

### Margin Constraint

| Step | Margin Impact |
|------|---------------|
| Distressed sales | +$144K |
| HK reduction (291+1800+call assignments) | +$90K |
| **New margin: ~-$555K** (near -$550K target) | |

At -$555K margin, capacity exists to deploy ~$37K into JP via short puts.

### Short Puts for Remaining $37K

| Stock | New Puts | Strike (¥) | Expiry | Shares if Assigned | Cost (USD) | Rationale |
|-------|---------|------------|--------|-------------------|------------|-----------|
| MUFG (8306) | 3-4 | ¥2,900 (ATM) | Jul-Aug | 300-400 | $22-29K | Largest JP bank, dividend ~3%, low vol = low margin drag |
| Itochu (8001) | 1 | ¥2,000 (ATM) | Jul-Aug | 100 | ~$13K | Buffett thesis, trading house, already building position |

**Total if both assign: ~$35-42K → reaches 3% target**

### Why MUFG and Itochu

- **MUFG** — lowest margin risk (bank, low vol, 3% dividend offsets margin cost). Already own 200 shares at ¥2,835 avg cost. Adding at ¥2,900 is near cost basis.
- **Itochu** — direct Buffett thesis play (Berkshire owns Japanese sogo shosha). Already own 500+ shares after ITM assignments. Adding 100 more completes the position.

### What NOT to Add

| Stock | Why Skip |
|-------|----------|
| MHI (7011) | Already 400 shares from ITM — target met |
| Nintendo (7974) | Already $43K (near 1% target) |
| Nikkei ETF (1321) | Save for later when margin is lower |
| Mitsubishi (8058) | Already adding via ¥5,250 assignment |

### Target JP Allocation at 3%

| Stock | Value | % Portfolio | Category |
|-------|-------|-------------|----------|
| Nintendo (7974) | ~$43K | 0.9% | Gaming moat, weak-yen beneficiary |
| MUFG (8306) | ~$26K | 0.5% | Bank, dividend, rate play |
| MHI (7011) | ~$21K | 0.4% | Defense, exporter |
| Itochu (8001) | ~$21K | 0.4% | Sogo shosha, Buffett thesis |
| Sony (6758) | ~$13K | 0.3% | Entertainment + sensors |
| Mitsubishi (8058) | ~$11K | 0.2% | Sogo shosha, Buffett thesis |
| Others | ~$5K | 0.1% | 6752/8053/3690 |
| **Total** | **~$142K** | **3.0%** | |

### JP Premium Income

| Source | Puts | Premium Est. |
|--------|------|-------------|
| MUFG puts (3-4 contracts) | ¥2,900 Jul-Aug | ~$1,800 |
| Itochu put (1 contract) | ¥2,000 Jul-Aug | ~$600 |
| Existing OTM puts expiring worthless | June 11 batch | ~$2,000 |
| **JP Total** | | **~$4,400** |

---

## Aggregate Premium Income Projection (Updated)

| Phase | Position | 6-Month Premium | Purpose |
|-------|----------|-----------------|---------|
| 1 | NVDA | $16,600 | Aggressive trim |
| 2 | AAPL | $4,800 | Mild trim |
| 3 | TSLA | $2,930 | Mild trim |
| 4 | TSM | $3,420 | Mild trim |
| 5 | META+AMZN+MSFT+GOOG | $10,690 | Income only |
| 6 | 700.HK (existing OTM) | $3,100 | Income |
| 7 | O | $3,550 | Income only |
| 8 | HK ATM trim calls | $4,500 | Trim to 3% + income |
| 9 | JP short puts | $4,400 | Build to 3% + income |
| **Total** | | **$53,990** | |

**~$54K in premium over 6-9 months.** Covers margin interest (~$38K/yr at -$555K balance) with $16K surplus.

---

## Concentration Milestones (Updated)

| Month | NVDA % | AAPL % | TSLA % | TSM % | Top 4 % | HK % | JP % | Action Focus |
|-------|--------|--------|--------|-------|---------|------|------|-------------|
| May 2026 | 42.6% | 15.5% | 13.0% | 12.2% | 83.3% | 4.9% | 1.3% | NVDA $235, sell 291/1800, write HK ATM calls |
| Jun | 40.0% | 14.0% | 12.5% | 11.5% | 78.0% | 3.5% | 2.2% | HK ATM calls assigning, JP ITM assigns |
| Jul | 37.5% | 13.0% | 12.0% | 11.0% | 73.5% | **3.0%** | 2.5% | HK at target. Write MUFG/Itochu puts |
| Aug | 35.0% | 12.0% | 11.5% | 10.0% | 68.5% | 3.0% | **3.0%** | JP at target. Continue US trims |
| Sep | 32.5% | 11.0% | 10.5% | 9.5% | 63.5% | 3.0% | 3.0% | NVDA $260, AAPL $325 |
| Oct | 30.0% | 10.5% | 10.0% | 9.5% | 60.0% | 3.0% | 3.0% | NVDA $265 |
| Nov | 27.5% | 10.0% | 10.0% | 9.5% | 57.0% | 3.0% | 3.0% | NVDA $270 |
| Dec | 25.0% | 10.0% | 10.0% | 9.5% | 54.5% | 3.0% | 3.0% | NVDA $275+ |
| Q1 2027 | **20.0%** | **10.0%** | **10.0%** | **10.0%** | **40.0%** | **3.0%** | **3.0%** | All targets reached |

---

## Execution Protocol

### Weekly Checklist (Every Friday)

1. Review NVDA, AAPL, TSLA, TSM prices vs open call strikes
2. Check next week's expiries — any ITM or near-ITM?
3. Place new calls for positions that need them
4. Record premium collected

### Expiry Week (Wed-Fri)

1. **Wed:** Check assignment probability for calls expiring this week
2. **Thu:** Decide — let assign or roll? General rule: if strike is within target range, let assign
3. **Fri:** Execute rolls or accept assignment

### Monthly Review

1. Calculate updated % NLV for all top 10 positions
2. Adjust call quantities — reduce as position shrinks
3. Adjust strike selection based on price trend
4. Total premium collected vs projection

---

## Risk Management

### NVDA Drops >20%

- Pause all new NVDA calls
- Let existing calls expire OTM (keep premium)
- Resume writing when price stabilizes
- Use the pause to assess: is NVDA still a hold at lower price?

### Broad Market Correction (-20%+)

- All calls expire OTM (premium kept)
- No forced selling via assignment
- Portfolio declines but less than unleveraged (margin already reduced)
- Wait for recovery, resume calls at lower strikes

### NVDA Rallies Sharply (+20%+)

- Calls go ITM faster — good for trimming
- Add extra calls at higher strikes to accelerate
- Accept assignment at higher prices (better sale price)

### Single-Stock Event (Earnings, Regulation)

- Avoid writing calls the week before earnings
- If unexpected event, roll calls out 1-2 months for safety

---

## What NOT to Do

| Rule | Why |
|------|-----|
| Don't write calls ITM | Guaranteed assignment, no upside participation |
| Don't write calls on 100% of shares | Keep 50%+ uncovered for upside participation |
| Don't roll down to lower strikes | Defeats trimming purpose |
| Don't panic if NVDA drops | Calls expire, premium kept, resume later |
| Don't skip months | Consistency compounds premium income |
| Don't chase volatile stocks with tight strikes | Use wider OTM on TSLA, tighter on TSM/O |

---

## Summary

| Metric | Value |
|--------|-------|
| Total shares to trim (US) | ~4,300 (NVDA 3,100 + AAPL 400 + TSLA 200 + TSM 100) |
| HK reduction | $90K (291/1800 sold + ATM calls on 700/1299/2318) |
| JP increase | $79K (ITM assigns $33K + new puts $37K) |
| Total premium income | ~$54K over 6-9 months |
| Target top 4 concentration | 40% (from 83.3%) |
| Target NVDA concentration | 20% (from 42.6%) |
| Target HK allocation | 3.0% (from 4.9%) |
| Target JP allocation | 3.0% (from 1.3%) |
| Target timeline | 6-9 months |
| Risk level | Low — gradual, flexible, no forced selling |

**This plan turns concentration risk into income generation.** Every month that passes without assignment = free premium. Every month with assignment = progress toward target allocation. HK reduction frees $90K for margin paydown, enabling JP build to 3% within margin constraints.
