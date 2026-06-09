# Short Put Strategy Framework

Watchlist: AAPL, AMZN, AVGO, COHR, COST, GOOG, META, MSFT, NFLX, NVDA, ORCL, PEP, PLTR, PYPL, TLT, TSLA, TSM, UNH

---

## Tier 1: High-Conviction Value Entries (Wide Moat, Lower Vol)

**Stocks:** AAPL, MSFT, COST, GOOG, PEP

**Strategy:** Cash-Secured Short Puts at **15-25% OTM**, 30-60 DTE
- These are "I want to own them at a discount" names
- Target delta ~0.15-0.20 — you collect premium but rarely get assigned
- COST and PEP are defensive — excellent candidates for rolling if tested
- PEP/COST: target strikes near long-term support levels (e.g., 200-day MA)
- Roll or accept assignment confidently — these are buy-and-hold quality

## Tier 2: Growth Premium Harvesting (Moderate Vol)

**Stocks:** AMZN, META, NFLX, ORCL, AVGO, TSM

**Strategy:** Short Puts at **10-15% OTM**, 30-45 DTE
- Higher IV rank means richer premium — sell when IV is elevated
- Target delta ~0.20-0.25
- META/AMZN: sell on earnings-driven IV spikes (post-earnings, not pre)
- AVGO/TSM: semis are cyclical — sell puts when SOX index is oversold, not overbought
- ORCL: cloud transition story — sell puts on sector-wide dips

## Tier 3: Speculative Premium Collection (High Vol / Binary Risk)

**Stocks:** TSLA, NVDA, PLTR, PYPL

**Strategy:** Short Puts at **20-30% OTM**, 21-30 DTE (shorter duration)
- High IV = fat premium, but assignment risk is real
- **TSLA:** Only sell puts when IV rank > 50%. Use wider strikes (25%+ OTM). Never hold through earnings
- **NVDA:** IV crush post-earnings is your friend — sell puts after reports, not before
- **PLTR:** High growth, unproven profitability — treat as "sell premium, don't want assignment"
- **PYPL:** Turnaround story — if assigned you're buying a value trap or a bargain. Size small

## Tier 4: Special Situations

**Stocks:** COHR, UNH, TLT

- **COHR:** Small-cap semi equipment — high beta. Sell far OTM (25%+), short DTE. Only if you understand the AI photonics narrative
- **UNH:** Defensive healthcare but recent political/regulatory overhang. Sell puts when negative news creates IV spikes — fundamentals are solid
- **TLT:** Rate-sensitive. Sell puts when yields spike (prices drop). This is essentially a bet on rates peaking. Use 60-90 DTE since rate moves are slower

---

## Key Screening Criteria

| Criterion | What to Screen |
|-----------|---------------|
| **IV Rank** | Only sell when IV rank > 30 (avoid low-vol environments) |
| **Delta** | 0.15-0.25 for conservative, 0.25-0.35 for aggressive |
| **DTE** | 21-45 DTE optimal (theta decay curve steepens) |
| **Support Proximity** | Strike should be at or below technical support |
| **Earnings Timing** | Avoid holding short puts through earnings (especially Tier 3) |
| **Assignment Willingness** | Only sell puts on stocks you genuinely want to own at the strike |

---

## Earnings Date Awareness

**Critical:** Earnings dates must be incorporated into screening — an upcoming earnings report can gap the stock well past your short put strike.

### Rules by Tier
| Tier | Earnings Rule |
|------|--------------|
| Tier 1 | Avoid holding through earnings unless strike is 20%+ below current price |
| Tier 2 | Close or roll positions 1-2 weeks before earnings. Re-sell post-earnings IV crush |
| Tier 3 | **Never** hold through earnings. Close 5+ days before. Re-enter after IV normalizes |
| Tier 4 | Case-by-case. COHR: close before earnings. UNH: can hold if strike is very conservative. TLT: N/A (no earnings) |

### Implementation
- Fetch upcoming earnings dates for all watchlist tickers (Yahoo Finance `calendarEvents.earnings`)
- Flag any ticker with earnings within the selected DTE window
- Auto-reject or warn if earnings falls before expiration

---

## Moving Average Support Levels (MA20, MA60, MA120)

Moving averages provide dynamic support/resistance levels for strike selection. Use them to identify high-probability entry strikes.

### MA Definitions
| MA | Meaning | Use Case |
|----|---------|----------|
| **MA20** | ~1-month trend | Short-term momentum. If price holds above MA20, trend is intact |
| **MA60** | ~3-month trend | Medium-term support. Good reference for Tier 2-3 strike selection |
| **MA120** | ~6-month trend | Long-term support. Ideal for Tier 1 strike selection — strong confluence level |

### Strike Selection Using MAs
1. **Conservative (Tier 1):** Place strike below MA120 if MA120 > MA60 > MA20 (bullish alignment)
2. **Moderate (Tier 2):** Place strike near or just below MA60
3. **Aggressive (Tier 3):** Place strike below MA20 only when IV rank is very high (>50%)

### MA Confluence Signals
- **Bullish:** MA20 > MA60 > MA120 (all rising) — safer to sell puts, trend supports you
- **Neutral:** MAs crossing or flat — reduce position size, sell further OTM
- **Bearish:** MA20 < MA60 < MA120 — avoid selling puts or go very far OTM with short DTE

### Implementation
- Fetch daily closes for MA20/60/120 calculation (Yahoo Finance or cached price history)
- Display MA levels alongside current price in screener output
- Auto-suggest strike prices based on tier-specific MA rules
- Highlight MA alignment status (bullish/neutral/bearish) per ticker

---

## Suggested Implementation in ScreenerService

Add `strategy_tier` field to screener config per ticker, then adjust screening criteria (delta target, % OTM, DTE range, earnings buffer, MA alignment) based on tier.
