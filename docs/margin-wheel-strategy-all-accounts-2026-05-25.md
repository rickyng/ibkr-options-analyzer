# Margin Wheel Strategy — All Accounts

**Date:** 2026-05-25
**Accounts:** No1, No2, No3, WinWin
**Status:** Transition plan from leveraged puts to margin wheel across all accounts

---

## Account Comparison — Current State

| Metric | No1 | No2 | No3 | WinWin |
|--------|-----|-----|-----|--------|
| Portfolio | $4.73M | $924K | $159K | $739K |
| NLV | $4.09M | $586K | $171K | ~$739K |
| Margin balance | -$789K | -$376K | -$19K | ~$0 |
| Margin/NLV | 16.7% | 64.1% | 10.8% | ~0% |
| Open positions | 65 | 21 | 12 | 41 |
| Win rate | 72.6% | 90.5% | 100% | N/A |
| Expectancy | +0.31R | +0.55R | +0.93R | N/A |
| Coverage ratio | 1.2x | 3.1x | 46x | N/A |
| Status | AMBER | GREEN | GREEN | YELLOW |

---

## Strategy Overview

**Core idea:** Use margin as intermittent collateral for cash-secured puts (CSPs). Only pay margin interest when assigned. Wheel out via covered calls. Keep average margin balance near zero instead of carrying persistent negative balances.

**Why this works:** Premium received from selling puts keeps cash near zero. Only the assignment phase creates a negative balance, and covered call premiums offset the short-term interest cost. The average margin balance cycles between $0 and -$100K — costing $3-7K/yr in interest instead of $36-54K/yr.

### Current vs. Target — All Accounts

| Metric | Current (All) | Target (All) |
|--------|--------------|--------------|
| Total margin balance | -$1.18M persistent | ~-$250-350K intermittent |
| Annual interest | ~$60K | $8-16K |
| Gross options income | ~$200-290K | $190-286K |
| **Net options income** | **~$140-230K** | **$172-259K** |
| Total positions | 139 | 32-43 |
| Margin call risk | HIGH (No1) | Near-zero (all) |

---

## How Margin Wheel Works — Cash Flow Mechanics

### The Wheel Cycle

```
┌─────────────────────────────────────────────────────────┐
│                    MARGIN WHEEL CYCLE                     │
│                                                          │
│  1. SELL PUT ──► Premium received ──► Cash UP (near $0)  │
│       │                                                  │
│       ├── OTM Expires ──► Keep premium ──► REPEAT       │
│       │                                                  │
│       └── Assigned ──► Buy stock on margin              │
│                              │                           │
│  2. SELL COVERED CALL ──► Premium received              │
│                              │                           │
│       ├── Expires OTM ──► Keep premium, sell again      │
│       │                                                  │
│       └── Assigned ──► Sell stock ──► Cash UP ──► REPEAT│
└─────────────────────────────────────────────────────────┘
```

### Cash Balance Through One Cycle

| Step | Action | Cash Flow | Running Balance | Interest |
|------|--------|-----------|-----------------|----------|
| Start | — | — | $0 | $0 |
| Sell 10 puts | Receive premium | +$7K | +$7K | $0 |
| 7 puts expire OTM | Keep premium | $0 | +$7K | $0 |
| 3 puts assigned | Buy stock | -$90K | -$83K | Starts |
| Sell 3 covered calls | Receive premium | +$2.5K | -$80.5K | $-455/mo |
| 1-2 months later | Calls assigned | +$95K | +$14.5K | Stops |
| **Cycle complete** | — | **+$14.5K net** | **+$14.5K** | **~$-700 total** |

**Net per cycle: +$14.5K income - $700 interest = +$13.8K net**

### Why Intermittent Margin Costs Almost Nothing

| Approach | Balance Pattern | Annual Interest |
|----------|----------------|-----------------|
| Persistent borrowing | -$789K ━━━━━━━━━━━━━━━━ | -$54K |
| Margin wheel (cycling) | $0 ▔▔╲╱▔▔╲╱▔▔╲╱▔▔ | -$3-7K |
| **Savings** | | **+$47-51K/yr** |

---

## Universal Stock Selection Criteria

Applies to all accounts. Only sell puts on stocks that pass ALL criteria:

| Criteria | Requirement | Why |
|----------|-------------|-----|
| Investment thesis | 3-bullet written thesis | No thesis-less positions |
| Market cap | >$50B | Liquidity, survivability |
| Moat quality | Strong or better | Quality compounders only |
| IV rank | >30 | Sufficient premium to justify risk |
| Bid-ask spread | <$0.10 | Don't overpay to enter/exit |
| Put delta | 0.15-0.30 (OTM) | 70-85% probability of expiring worthless |
| Days to expiry | 30-45 DTE | Balance premium vs. gamma risk |
| Position size | ≤2% of portfolio per put | Account-specific notional limit |

---

## Universal Risk Management Rules

### Hard Limits — All Accounts

| # | Rule | Action if Breached |
|---|------|-------------------|
| 1 | Max margin balance (per account limits below) | Stop selling new puts immediately |
| 2 | Max total notional (per account limits below) | No new positions until existing expire/assign |
| 3 | Max single stock weight | 20% of account |
| 4 | Max puts per stock | 2 contracts |
| 5 | Assignment acceptance | Only if thesis holds + under allocation limit |

### Per-Account Limits

| Parameter | No1 | No2 | No3 | WinWin |
|-----------|-----|-----|-----|--------|
| Max puts open | 10-15 | 6-8 | 4-5 | 12-15 |
| Max notional per put | $74K | $12K | $3.4K | $100K |
| Max total notional | $500K | $150K | $80K | $600K |
| Max ITM puts | 3 | 2 | 2 | 3 |
| Margin warning | >-$150K | >-$250K | >-$50K | >-$150K |
| Margin hard limit | >-$200K | >-$350K | >-$100K | >-$200K |
| Max new puts/week | 3-4 | 2-3 | 2 | 3 |

### Warning Levels — All Accounts

| Signal | Action |
|--------|--------|
| Margin balance > warning level | Pause new puts, let existing expire |
| ITM puts > max | Close weakest, roll others |
| Portfolio drawdown > -10% | Reduce notional by 50% |
| Margin interest/month > budget | Review assignment frequency |
| VIX > 30 | Close all delta > 0.25 puts |

### Roll Rules

| Situation | Action |
|-----------|--------|
| Put going ITM, >14 DTE | Roll out 2 weeks at same strike for credit |
| Put going ITM, <14 DTE | Roll out 3-4 weeks down 1 strike for credit |
| Put deep ITM (>5%) | Close if thesis broken, else accept assignment |
| CC, stock rallying | Roll up and out for credit if profitable |
| CC, stock dropping | Roll down to near-ATM for more premium |

### Assignment Decision Framework

```
Do I want to own this stock at the effective price (strike - premium)?
  ├── YES → Accept assignment → Sell covered call immediately
  └── NO  → Does the thesis still hold?
        ├── YES → Roll out for credit, try again
        └── NO  → Close position, take loss, move on
```

### Stop-Loss Framework

| Loss Level | Action |
|------------|--------|
| -20% of premium collected | Monitor closely |
| -50% of premium collected | Roll or close |
| -100% of premium collected | Close if thesis broken |
| -200% of premium collected | Maximum loss — close immediately |

---

# Account No1 — Primary Income + JP Diversification

## Current Assessment

**Strengths:** Largest account ($4.73M). Diversified across US/HK/JP. NVDA covered calls for rebalancing.

**Weaknesses:** Persistent -$789K margin (16.7% margin/NLV). 65 positions. JP puts adding margin pressure. 72.6% win rate. Options income barely covers margin (1.2x).

### Current vs. Target

| Metric | Current | Target |
|--------|---------|--------|
| Cash | -$789K | ~$0 (cycling) |
| Annual interest | -$36 to -$54K | -$4 to -$7K |
| Gross options income | $55-75K | $70-100K |
| **Net options income** | **$19-39K** | **$62-83K** |
| Net yield | 0.4-0.8% | 1.3-1.8% |
| Positions | 65 | 10-15 |

## Transition Plan

### Step 1: Immediate Sales (This Week)

| # | Action | Cash Freed |
|---|--------|-----------|
| 1 | Sell all distressed (DOCU, PYPL, NNDM, CMRC, PINS, RKT, ONL) | +$144K |
| 2 | Let ARKK, ARKG, FSLY, TDOC covered calls assign | Covered |
| 3 | Sell CR Beer (291.HK) + CCCC (1800.HK) | +$25K |
| 4 | Close VRT, LITE $840, MU $660 ITM puts | Avoid assignment |
| 5 | Close P $85 puts before May 27 earnings | Avoid binary event |

### Step 2: NVDA Rebalancing + Concentration Trim (Week 2)

| # | Action | Cash Freed |
|---|--------|-----------|
| 6 | Let NVDA $235 calls assign (1,000 shares) | +$235K |
| 7 | Trim TSLA ~500 shares | +$200K |
| 8 | Trim AAPL ~300 shares | +$95K |

### Transition Summary

| Stage | Margin Balance |
|-------|---------------|
| Current | -$789K |
| After distressed + HK sales + close ITM puts | -$530K |
| After NVDA assignment + TSLA/AAPL trim | **~$0** |

## Approved Candidates — No1

**Tier 1 — Core Wheel:**

| Stock | Sector | Strike | Notional/Contract | Max |
|-------|--------|--------|-------------------|-----|
| AAPL | Tech | -5% OTM | ~$29K | 2 |
| MSFT | Tech | -5% OTM | ~$42K | 1 |
| GOOG | Tech | -5% OTM | ~$37K | 1 |
| META | Tech | -5% OTM | ~$59K | 1 |
| AMZN | Tech | -5% OTM | ~$27K | 1 |
| NVDA | Semi | -8% OTM | ~$22K | 2 |
| TSM | Semi | -8% OTM | ~$41K | 1 |
| AVGO | Semi | -5% OTM | ~$38K | 1 |

**Tier 2 — Opportunistic (when IV elevated):**

| Stock | Sector | Strike | Notional/Contract | Max |
|-------|--------|--------|-------------------|-----|
| UNH | Healthcare | -5% OTM | ~$33K | 1 |
| V | Financials | -5% OTM | ~$33K | 1 |
| PEP | Consumer | -5% OTM | ~$14K | 1 |
| MCD | Consumer | -5% OTM | ~$27K | 1 |
| WMT | Consumer | -5% OTM | ~$11K | 1 |
| CRWD | Cybersecurity | -10% OTM | ~$31K | 1 |

**Tier 3 — JP Equity Building (see JP section below):**

| Stock | Moat | Strike | Notional/Contract | Max |
|-------|------|--------|-------------------|-----|
| 7974 (Nintendo) | Exceptional | -5% OTM | ~$37K | 1 |
| 6758 (Sony) | Strong | -5% OTM | ~$20K | 1 |
| 8001 (Itochu) | Moderate | -5% OTM | ~$13K | 1 |
| 8306 (MUFG) | Moderate | -5% OTM | ~$18K | 1 |
| 8058 (Mitsubishi) | Moderate | -5% OTM | ~$32K | 1 |

**Banned:** VRT, LITE, MU, P (Everpure), any stock without written thesis

## JP Equity Building via Margin Wheel

### JP Allocation Target

| Level | % of Portfolio | USD Amount | Status |
|-------|---------------|------------|--------|
| Current | 1.3% | $63K | Existing holdings |
| ITM assignments (June 11) | +0.9% | +$33K | Accepting |
| **Phase 3 target** | **5.0%** | **$185K** | Build via wheel |

### JP Position Limits

| Parameter | Limit |
|-----------|-------|
| Max JP puts open | 5 |
| Max JP notional | $200K |
| Max single JP stock | $40K (1% of portfolio) |

### JP Build Schedule

| Month | Action | JP Allocation |
|-------|--------|---------------|
| June 2026 | Accept ITM assignments (~$33K) | ~2.2% |
| July 2026 | Write 2-3 JP puts (Itochu, MUFG) | Target 3% |
| Aug 2026 | Write 2-3 JP puts (Nintendo, Mitsubishi) | Target 4% |
| Sep 2026 | Write 1-2 JP puts (Sony, Nikkei ETF) | Target 5% |

**Do not exceed 5% until US margin wheel runs 3 months successfully.**

## Expected Returns — No1

| Component | Conservative | Target |
|-----------|-------------|--------|
| Put premium (net) | $55K | $65K |
| CC premium | $20K | $30K |
| Margin interest | -$5K | -$4K |
| **Net options income** | **$70K** | **$91K** |
| Equity appreciation | $296K | $370K |
| **Total return** | **$366K** | **$461K** |
| **Total return %** | **9.9%** | **12.5%** |

### Monthly Cash Flow Model

| Phase | Cash Flow | Running Margin |
|-------|-----------|----------------|
| Start | $0 | ~$0 (post-transition) |
| Sell 10-12 puts | +$6-9K premium | ~$0 |
| 7-8 expire OTM | Keep $4-6K | ~$0 |
| 3-4 assigned | Buy stock -$80-120K | -$80-120K |
| Sell CCs on assigned | +$2-4K | -$76-116K |
| CCs assigned (1-2 months) | +$80-120K | ~$0 |
| **Monthly interest** | **-$300-500** | **Minimal** |

---

# Account No2 — Growth + Income

## Current Assessment

**Strengths:** Best put selection quality (90.5% win rate, +0.55R expectancy). Options income covers margin 3.1x. Quality entries: GOOG, MSFT, META, TSLA, TSM. No margin call scenarios.

**Weaknesses:** Leverage ratio very high (64.1% margin/NLV). $96K in distressed positions generating $6K/yr margin drag. DRAM penny stock put, U underwater put. Only 6 covered calls vs 15 puts — incomplete wheel.

### Current vs. Target

| Metric | Current | Target |
|--------|---------|--------|
| Margin/NLV | 64.1% | <30% |
| Annual interest | -$24K | -$2-4K |
| **Net income** | **$34-63K** | **$48-71K** |
| Net yield on NLV | 8.3% | 8.2-12.1% |
| Positions | 21 | 6-8 |

## Transition Plan

### Phase 1: Week 1

| # | Action | Cash Freed | New Margin/NLV |
|---|--------|-----------|----------------|
| 1 | Sell all distressed (ARKW, TDOC, ARKK, PINS, PYPL) | +$96K | 47.8% |
| 2 | Sell 732 HK (penny stock) | +$6.7K | 46.6% |
| 3 | Close DRAM $49 + U $24 puts | Avoid $15K | 46.6% |
| 4 | Let OTM puts expire, collect premium | +$4K | 45.2% |

### Phase 2: Week 2-4

| # | Action | Margin Impact |
|---|--------|---------------|
| 5 | Take ARKW +44% profit | +$44K |
| 6 | Collect remaining premiums | +$3K |
| 7 | Pause new puts until margin/NLV < 40% | — |

**Target: -$223K margin / $586K NLV = 38.1%**

## Approved Candidates — No2

**Core wheel:**

| Stock | Strike Range | Notional/Contract | Max |
|-------|-------------|-------------------|-----|
| GOOG | -5% OTM | ~$35K | 1 |
| MSFT | -5% OTM | ~$38K | 1 |
| META | -5% OTM | ~$56K | 1 |
| AAPL | -5% OTM | ~$30K | 1 |
| AMZN | -5% OTM | ~$27K | 1 |
| AVGO | -5% OTM | ~$36K | 1 |

**Opportunistic:**

| Stock | Strike Range | Notional/Contract | Max |
|-------|-------------|-------------------|-----|
| TSLA | -10% OTM | ~$34K | 1 |
| TSM | -8% OTM | ~$38K | 1 |
| UNH | -5% OTM | ~$33K | 1 |
| PEP | -5% OTM | ~$14K | 1 |
| O | -5% OTM | ~$6K | 2 |

**Banned:** DRAM, U, LITE, COHR

## Expected Returns — No2

| Component | Current | Margin Wheel |
|-----------|---------|-------------|
| Put premium | $52-78K | $35-50K |
| CC premium | $6-9K | $15-25K |
| **Gross income** | **$58-87K** | **$50-75K** |
| Margin interest | -$24K | -$3K |
| **Net income** | **$34-63K** | **$48-71K** |

### Monthly Cash Flow Model

| Phase | Cash Flow | Running Margin |
|-------|-----------|----------------|
| Start | $0 | -$223K (post-transition) |
| Sell 6-8 puts | +$4-6K premium | -$217-219K |
| 5-6 expire OTM | Keep $3-4.5K | -$217-219K |
| 1-2 assigned | Buy stock -$35-70K | -$252-289K |
| Sell CCs | +$1.5-3K | -$249-286K |
| CCs assigned | +$37-72K | -$217-214K |
| **Monthly interest** | **-$150-200** | **Minimal** |

---

# Account No3 — Conservative Compounding

## Current Assessment

**Strengths:** Best-performing account (100% win rate, +0.93R expectancy). Lowest leverage (10.8% margin/NLV). Exceptional coverage (46x). Quality holdings: MSFT, NFLX, PEP.

**Weaknesses:** Very small account ($171K NLV) — limits diversification. TSLA concentration at 26%. LITE + COHR puts ($112K notional = 65% of NLV). High maintenance margin ratio (71% of NLV).

### Current vs. Target

| Metric | Current | Target |
|--------|---------|--------|
| Margin/NLV | 10.8% | <15% |
| Annual interest | -$1.2K | -$0.5-1K |
| **Net income** | **$42-63K** | **$29-47K** |
| Net yield on NLV | 30.5% | 17-27.5% |
| Speculative notional | $233K (136% NLV) | $80K (47% NLV) |
| Positions | 12 | 4-5 |

**No3's 30.5% yield is driven by LITE ($81K notional = 47% of NLV) and COHR ($31K). If LITE assigns, that's 47% of the account in one speculative stock. The margin wheel targets sustainable 17-27% with lower tail risk.**

## Transition Plan

### Phase 1: Clean Up (Week 1)

| # | Action | Impact |
|---|--------|--------|
| 1 | Close LITE $810 put when profitable | Avoid $81K assignment |
| 2 | Close COHR $310 put when profitable | Avoid $31K assignment |
| 3 | Let TSLA strangle expire OTM | Collect $279 premium |
| 4 | Manage P $75 put around earnings | Binary event |
| 5 | Let PYPL calls work (exit strategy) | Clean exit at $57.5 |

**After cleanup: margin stays ~-$19K, speculative notional drops from $233K to ~$60K.**

## Approved Candidates — No3

**Only 4 stocks. Small account needs maximum focus.**

| Stock | Strike Range | Notional/Contract | Max |
|-------|-------------|-------------------|-----|
| MSFT | -5% OTM | ~$38K | 1 |
| NFLX | -5% OTM | ~$8K | 2 |
| PEP | -5% OTM | ~$14K | 1 |
| TLT | -3% OTM | ~$8K | 2 |

**Banned:** LITE, COHR, DRAM, IREN, CRWV, any stock >30% vol

### TSLA Strangle — Keep Running

No3 already has a wheel on TSLA:

| Component | Position | Status |
|-----------|----------|--------|
| Own 100 shares | $42K | Base position |
| Short $460 call | Covered | Collect premium |
| Short $360 put | Cash-secured | Buy more if drops |

**Monthly TSLA wheel target:** $270/month = $3,240/year (1.9% yield on NLV from one stock)

## Expected Returns — No3

| Component | Current | Margin Wheel |
|-----------|---------|-------------|
| Put premium | $38-58K | $20-30K |
| CC premium | $4-6K | $10-18K |
| **Gross income** | **$43-64K** | **$30-48K** |
| Margin interest | -$1.2K | -$0.5-1K |
| **Net income** | **$42-63K** | **$29-47K** |

---

# WinWin Account — AI Infrastructure Thesis

## Current Assessment

**Strengths:** Coherent AI infrastructure thesis across all 39 puts. Quality anchors: GOOG, MSFT. Conservative NVDA entries ($180 strike, 18% OTM). 2 covered calls on NVDA/TSM.

**Weaknesses:** 39 positions — too many to monitor. LITE: 7 puts, $350K notional (47% of portfolio). INTC: 4 puts (no moat). SNDK $1000 ITM. $850K max liability vs $739K portfolio. IREN, CRWV, APLD — speculative.

### Current vs. Target

| Metric | Current | Target |
|--------|---------|--------|
| Positions | 39 | 12-15 |
| Max liability | $850K | $600K |
| LITE notional | $350K | $150K |
| Net income | ~$41-61K | $35-58K |

## Transition Plan

### Phase 1: Close Problem Positions (Week 1)

| # | Position | Action | Rationale |
|---|----------|--------|-----------|
| 1 | LITE $500 Jul 02 (ITM) | Close or roll down | ITM, assignment incoming |
| 2 | SNDK $1000 Jun 05 (ITM) | Close or roll down | ITM, $100K assignment |
| 3 | INTC $80 × 4 | Close all 4 | No moat, thesis-less |
| 4 | IREN $40 × 2 | Close both | Crypto mining |
| 5 | CRWV $85 | Close | Too speculative |
| 6 | APLD $30 × 2 | Close both | Datacenter REIT |
| 7 | DRAM $40 | Close | Penny stock |

**Positions closed: 13 of 39. Avoid ~$200K in speculative assignments.**

### Phase 2: Reduce LITE Concentration (Week 2)

Close LITE $500 Jun 05 and $500 Jul 17. Keep LITE $560 Jun 12, $500 Jun 18, $500 Jun 26.

**After: 4 LITE puts instead of 7. Notional drops from $350K to ~$200K.**

### Phase 3: Consolidate to 12-15 Positions (Week 2-4)

| Keep | Position | Notional | Thesis |
|------|----------|----------|--------|
| ✓ | NVDA $180 × 2 | $36K | AI compute |
| ✓ | AVGO $300 | $30K | AI networking |
| ✓ | MU $360 + $380 | $74K | AI memory |
| ✓ | QCOM $150 + $140 | $29K | Mobile/AI edge |
| ✓ | SMH $400 | $40K | Broad semi ETF |
| ✓ | GOOG $350 + $340 | $69K | Quality compounder |
| ✓ | MSFT $360 | $36K | Quality compounder |
| ✓ | BE $170 × 2 + $130 | $47K | Datacenter power |
| ✓ | COHR $250 | $25K | Optical |
| ✓ | LITE $560 + $500 × 3 | $200K | Optical/AI |
| ✓ | AMD $300 | $30K | AI compute |
| ✓ | TSLA $350 + $330 | $68K | Quality entry |
| ✓ | TSM $300 | $30K | Foundry |
| ✓ | PLTR $115 | $12K | AI/data |
| ✓ | NVDA $250 CC + TSM $480 CC | — | Income |

**After: ~17 positions (down from 39). Notional ~$700K (down from $850K).**

## Approved Candidates — WinWin

**Tier 1 — AI Infrastructure Core:**

| Stock | Sector | Max Puts | Max Notional |
|-------|--------|----------|-------------|
| NVDA | Compute | 2 | $36K |
| AVGO | Networking | 1 | $30K |
| AMD | Compute | 1 | $30K |
| MU | Memory | 2 | $72K |
| QCOM | Edge/Mobile | 2 | $30K |
| SMH | Broad Semi ETF | 1 | $40K |
| GOOG | Cloud/Quality | 2 | $70K |
| MSFT | Cloud/Quality | 1 | $36K |

**Tier 2 — Thesis-Supported Speculative:**

| Stock | Sector | Max Puts | Max Notional |
|-------|--------|----------|-------------|
| LITE | Optical | 3 | $150K |
| COHR | Optical | 1 | $25K |
| BE | Power | 2 | $34K |
| TSLA | EV/Quality | 2 | $70K |
| TSM | Foundry | 1 | $30K |
| PLTR | AI/Data | 1 | $12K |

**Banned:** INTC, IREN, CRWV, APLD, DRAM, SNDK $1000, any position without AI or quality thesis

## Expected Returns — WinWin

| Component | Current (39 puts) | Margin Wheel (12-15) |
|-----------|-------------------|---------------------|
| Put premium | ~$40-60K | $30-45K |
| CC premium | ~$1K | $10-18K |
| **Gross income** | **~$41-61K** | **$40-63K** |
| Margin interest | Unknown | -$2-5K |
| **Net income** | **~$41-61K** | **$35-58K** |

---

# Cross-Account Coordination

## Allocation by Account

| Account | Role | Focus | Max Notional |
|---------|------|-------|-------------|
| No1 | Primary income + JP diversification | Quality wheel + JP build | $500K |
| No2 | Growth + income | Quality wheel (tech-heavy) | $150K |
| No3 | Conservative compounding | Tight wheel (MSFT/NFLX/PEP/TLT) | $80K |
| WinWin | AI infrastructure thesis | Semiconductor wheel | $600K |

## LITE Concentration — Cross-Account Risk

**LITE is a $5-6B optical component company. $677K notional across 4 accounts = unintentional 9%+ concentration.**

| Account | LITE Puts | Notional | Action |
|---------|-----------|----------|--------|
| No1 | $800 + $840 | $164K | Close, no thesis |
| No2 | $820 | $82K | Close when profitable |
| No3 | $810 | $81K | Close when profitable |
| WinWin | $500-$560 × 7 | $350K | Reduce to 3 puts ($150K) |
| **Total** | **11 puts** | **$677K** | **→ 3 puts ($150K) in WinWin only** |

## Cross-Account Risk Limits

| Risk | Limit | Current | Target |
|------|-------|---------|--------|
| Total open puts | 45 | 139 | 32-43 |
| Total notional | $1.5M | ~$2.7M | ~$1.3M |
| Single stock (all accounts) | $200K max | LITE $677K | LITE $150K |
| Semiconductor sector | 40% of notional | ~60% | <40% |

---

# Transition Timeline — All Accounts

## Week 1 (Immediate)

| Day | Account | Action |
|-----|---------|--------|
| Mon | All | Sell distressed positions across all accounts |
| Mon | No2 | Close DRAM + U puts |
| Mon | WinWin | Close INTC × 4, IREN × 2, CRWV, APLD × 2, DRAM |
| Tue | No3 | Close LITE $810 + COHR $310 when profitable |
| Wed | WinWin | Close LITE $500 Jul 02 (ITM), roll SNDK $1000 down |
| Thu-Fri | No1 | Sell distressed, close VRT/LITE/MU ITM puts |

## Week 2-3

| Week | Account | Action |
|------|---------|--------|
| W2 | No1 | NVDA calls assign, trim TSLA + AAPL |
| W2 | No2 | Take ARKW profit, collect premiums |
| W2 | WinWin | Close 3 more LITE puts, reduce to 4 |
| W3 | All | Collect OTM premiums, pay down margin |

## Week 4+ (Steady State)

| Week | Account | Action |
|------|---------|--------|
| W4 | No1 | Write first 3-4 Tier 1 puts |
| W4 | No2 | Write first 3-4 quality puts |
| W4 | No3 | Write 2-3 tight puts (MSFT, NFLX, PEP) |
| W4 | WinWin | Write 2-3 AI infrastructure puts (Tier 1) |
| Ongoing | All | Monthly cycle: sell puts → monitor → expire/assign → sell CCs → repeat |

## Per-Account Transition Summary

| Account | Key Action | Margin Impact | Timeline |
|---------|-----------|---------------|----------|
| No1 | Sell distressed + NVDA assign + trim | -$789K → ~$0 | 2-3 weeks |
| No2 | Sell distressed + ARKW profit | -$376K → -$223K | 1-2 weeks |
| No3 | Close LITE + COHR | -$19K → -$19K | 1 week |
| WinWin | Close 13 speculative + reduce LITE | ~$0 → ~$0 | 2 weeks |

---

# Monitoring Dashboard

## Per-Account Targets (Post-Transition)

| Metric | No1 | No2 | No3 | WinWin |
|--------|-----|-----|-----|--------|
| Margin balance | ~$0 cycling | -$200-250K | -$15-30K | ~$0 cycling |
| Margin/NLV | <5% | <40% | <15% | <5% |
| Max puts | 10-15 | 6-8 | 4-5 | 12-15 |
| Max notional | $500K | $150K | $80K | $600K |
| Win rate target | >75% | >80% | >85% | >75% |
| Coverage ratio | >10x | >5x | >20x | >10x |
| Net income target | $62-83K | $48-71K | $29-47K | $35-58K |

## Aggregate Targets

| Metric | Current | Target |
|--------|---------|--------|
| Total net options income | ~$140-230K | $172-259K |
| Total margin interest | ~$60K/yr | $8-16K/yr |
| Total positions | 139 | 32-43 |
| Total notional | ~$2.7M | ~$1.3M |
| Accounts at risk | 1 (No1 AMBER) | 0 (all GREEN) |

## Monthly Review Checklist

### Per Account

- [ ] Margin balance within limits
- [ ] Total notional within limits
- [ ] All ITM puts reviewed — roll, close, or accept?
- [ ] Each assigned position has covered call
- [ ] Net income vs. target
- [ ] Win rate and R-multiple tracked
- [ ] No banned positions opened

### Cross-Account

- [ ] No single stock >$200K notional across all accounts
- [ ] LITE exposure: WinWin only, max 3 puts ($150K)
- [ ] Semiconductor sector <40% of aggregate notional
- [ ] Total margin interest <$1.5K/month
- [ ] No duplicate speculative positions across accounts

### Quarterly Review

- [ ] Annualized return vs. benchmark (S&P 500)
- [ ] Win rate by account and by tier
- [ ] Average hold time for assigned positions
- [ ] Interest cost as % of gross income
- [ ] Update approved candidate lists
- [ ] Stress test: 25% market drop impact on current positions

---

## Appendix: Thesis Template

Required for every put sold (3-bullet minimum):

```
STOCK: [TICKER]
STRIKE: $XXX | DTE: XX | PREMIUM: $X.XX | DELTA: 0.XX

Thesis:
1. [Moat / competitive advantage — why this business wins]
2. [Catalyst or valuation support — why now]
3. [Exit plan — covered call target or assignment acceptance criteria]

Max loss: [2% of account NLV]
Assignment target: [Add to position / wheel out / hold]
```

**Example — AAPL $300 put (No1):**

```
STOCK: AAPL
STRIKE: $300 | DTE: 35 | PREMIUM: $1.25 | DELTA: 0.22

Thesis:
1. Strongest moat in tech — ecosystem lock-in, services revenue growing 15%/yr
2. Trading at 28x forward P/E — reasonable for 12-15% earnings growth
3. If assigned at $300, sell $320 covered call (6.7% above assignment)

Max loss: 2% of portfolio = $74K
Assignment target: Wheel out via covered calls
```

---

*Margin wheel strategy for all accounts — No1, No2, No3, WinWin.*
*Review and update quarterly or after significant market events.*

*Companion documents:*
- *portfolio-review-account-no1-2026-05-24.md*
- *portfolio-review-account-no2-2026-05-24.md*
- *portfolio-review-account-no3-2026-05-24.md*
- *portfolio-review-winwin-2026-05-24.md*
