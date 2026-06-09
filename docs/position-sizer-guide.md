# Position Sizer — Usage & Interpretation Guide

## What It Does

Calculates how many option contracts you can safely write per account using three constraints:

1. **Risk Budget** — how much you can afford to lose (R-multiple framework)
2. **Margin Capacity** — how much borrowing room you have left
3. **Concentration Limit** — max exposure to a single name (20% of NLV)

The recommended contract count is the **minimum of all three** — your binding constraint.

## Quick Start

```bash
# Full portfolio scan — all accounts
python3 tools/position_sizer.py

# Single account
python3 tools/position_sizer.py --account No1

# Size a specific trade
python3 tools/position_sizer.py --account WinWin --ticker TSM --strike 180 --premium 6.00 --stock-price 195

# Custom risk tolerance (2% per trade instead of account default)
python3 tools/position_sizer.py --account No3 --risk-pct 0.02

# JP puts (1329, 1321 — multiplier is 1, not 100)
python3 tools/position_sizer.py --account No1 --ticker 1321 --strike 25000 --premium 500 --multiplier 1
```

## CLI Arguments

| Argument | Required | Description |
|----------|----------|-------------|
| `--account` | No | Account name: `No1`, `No2`, `No3`, `WinWin`. Default: all accounts |
| `--ticker` | No | Underlying symbol (e.g. `NVDA`, `TSM`) |
| `--strike` | No | Strike price of the put |
| `--premium` | No | Premium **per share** (standard options quote). e.g. $4.50 means $450/contract |
| `--stock-price` | No | Current stock price. Used for concentration limit |
| `--risk-pct` | No | Override account's default risk % per trade (e.g. `0.02` for 2%) |
| `--multiplier` | No | Contract multiplier. Default `100` (US options). Use `1` for JP 1321 |
| `-v` / `--verbose` | No | Show detailed calculations |

## Reading the Output

### Section 1: Account Health

```
  NLV:           $   4,090,000
  Margin Debt:   $    -789,000
  Margin Ceiling:$    -607,000
  Headroom:      $    -182,000
  Coverage:              1.8x
  Net Yield:            0.71%
```

| Line | Meaning |
|------|---------|
| **NLV** | Net liquidation value — what you'd receive if you liquidated everything today |
| **Margin Debt** | Amount borrowed from IBKR (negative = you owe) |
| **Margin Ceiling** | Your self-imposed maximum debt (hard limit, no new positions above this) |
| **Headroom** | Ceiling minus current debt. **Negative = limit breached** |
| **Coverage** | Options income / margin interest. Below 1.5x means interest is eating your returns |
| **Net Yield** | (Income - Interest) / NLV. The real return after margin costs |

### Section 2: Kelly Criterion

```
  Win Rate:       72.6%
  Full Kelly:     64.1%
  Half Kelly:     32.1%  (recommended)
  Expectancy/R:   $463.49
```

| Line | Meaning |
|------|---------|
| **Win Rate** | Historical % of short puts that expired OTM (premium kept) |
| **Full Kelly** | Mathematically optimal bet size as % of bankroll. Too aggressive in practice |
| **Half Kelly** | What professionals actually use. Caps volatility while preserving most of the edge |
| **Expectancy/R** | Average dollar profit per trade (wins minus losses averaged across all trades) |

**Interpretation:**
- Full Kelly > 25% → strategy has strong positive edge but is volatile
- Half Kelly is the practical ceiling — never bet more than this
- If Kelly is 0% or negative, the strategy loses money on average — stop trading it

### Section 3: Position Sizing

```
  Max contracts (risk budget):    3
  Max contracts (margin):         0
  Max contracts (concentration): 60
  ─────────────────────────────────
  RECOMMENDED:                    0 contracts
```

| Line | Formula | Meaning |
|------|---------|---------|
| **Risk budget** | `1R budget / max loss per contract` | How many contracts your stop-loss can cover |
| **Margin** | `headroom / (notional × 25%)` | How many contracts your margin allows (25% Reg T) |
| **Concentration** | `20% NLV / position value` | How many contracts before you breach single-name limit |
| **RECOMMENDED** | `min(risk, margin, concentration)` | Your answer. The binding constraint wins |

### Status Icons

| Icon | Status | What it means |
|------|--------|---------------|
| `[+]` | FREE | Healthy headroom. Can open new positions |
| `[~]` | TIGHT | Headroom < 20% of ceiling. Proceed with caution |
| `[!]` | FROZEN | Ceiling breached. No new positions allowed |

### Quick Reference Table

```
  Account        Status    1R Budget  Max Contracts   Kelly½
  Account No1    FROZEN   $   40,900              0   32.1%
  Account No3    FREE     $    5,130              1   50.0%
  WinWin         FREE     $   18,475              1   40.6%
```

One line per account. **Max Contracts** is the takeaway — how many new puts you can write right now.

## How Constraints Work

### Risk Budget (R-Multiple)

1R = your max acceptable loss per trade, defined as a % of NLV.

| Account | NLV | Risk % | 1R Budget |
|---------|-----|--------|-----------|
| No1 | $4.09M | 1.0% | $40,900 |
| No2 | $586K | 2.0% | $11,720 |
| No3 | $171K | 3.0% | $5,130 |
| WinWin | $739K | 2.5% | $18,475 |

No1 uses 1% (conservative) because it's already over-leveraged. No3 uses 3% (aggressive) because it has 42x margin coverage and 100% win rate.

### Margin Capacity

Short puts require ~25% margin of notional value (Reg T). The calculator checks if you have enough headroom to absorb the margin hit of new positions.

When status is FROZEN (headroom ≤ 0), the calculator hard-stops at 0 contracts regardless of other constraints.

### Concentration Limit

Single-name positions capped at 20% of NLV (25% for WinWin). Prevents over-concentration in one stock.

## Account Defaults

These are pre-loaded from the May 24, 2026 portfolio review. Edit `ACCOUNTS` dict in `tools/position_sizer.py` to update.

| Parameter | No1 | No2 | No3 | WinWin |
|-----------|-----|-----|-----|--------|
| NLV | $4.09M | $586K | $171K | $739K |
| Margin Debt | -$789K | -$376K | -$19K | $0 |
| Margin Ceiling | -$607K | -$400K | -$50K | -$100K |
| Win Rate | 72.6% | 90.5% | 100% | 85% |
| Avg Premium | $723 | $3,467 | $5,429 | $2,500 |
| Avg Loss (as % of premium) | 31% | 36% | 0% | 25% |
| Annual Income | $65K | $72.5K | $51K | $60K |
| Annual Interest | $36K | $24K | $1.2K | $0 |
| Risk per Trade | 1.0% | 2.0% | 3.0% | 2.5% |

## Common Workflows

### "Can I write a new put on NVDA in No1?"

```bash
python3 tools/position_sizer.py --account No1 --ticker NVDA --strike 120 --premium 4.50 --stock-price 135
```

Output: `RECOMMENDED: 0 contracts` — No1 is FROZEN until margin drops below -$607K.

### "How much room does No3 have?"

```bash
python3 tools/position_sizer.py --account No3
```

Output: `RECOMMENDED: 1 contracts` — room for one standard-size position.

### "What if I use a tighter stop?"

```bash
python3 tools/position_sizer.py --account WinWin --risk-pct 0.01
```

This overrides WinWin's default 2.5% risk down to 1%, showing how sizing changes with a tighter 1R definition.

### "Size a JP 1321 put"

```bash
python3 tools/position_sizer.py --account No1 --ticker 1321 --strike 25000 --premium 500 --multiplier 1
```

JP 1321 options use multiplier 1 (not 100). Always pass `--multiplier 1` for these.

## Warnings

The calculator flags these conditions:

| Warning | Trigger | Action |
|---------|---------|--------|
| **FROZEN** | Margin ceiling breached | Stop opening positions. Focus on deleveraging. |
| **TIGHT** | Headroom < 20% of ceiling | Limited room. Be selective with new trades. |
| **HIGH INTEREST** | Margin cost > 50% of income | Margin is eating returns. Reduce debt. |
| **High Kelly** | Full Kelly > 25% | Strategy is profitable but volatile. Use half-Kelly. |
