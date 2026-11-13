# How to Read an Option Chain

A typical option chain shows 50+ strikes × multiple expirations × ~10 columns each = hundreds of numbers. Most retail traders look at price and skip the rest. Here's how to extract a tradable view in 5 minutes.

## Anatomy of one row

```
                    CALL                                  PUT
Bid    Ask   Last  Vol  OI    IV    Δ     |Strike|  Bid    Ask   Last  Vol   OI    IV    Δ
4.20   4.35  4.28  450  12.4k 28.5  0.45  | 180  | 6.10   6.25  6.18  280   8.2k  29.1  −0.55
```

- **Strike**: the contract's exercise price
- **Bid/Ask/Last**: standard market quote info
- **Vol**: today's volume
- **OI**: outstanding open interest (alive contracts)
- **IV**: implied volatility (in %)
- **Δ**: option delta

## The 5-minute scan

For an option you're considering trading:

### Step 1 — Confirm liquidity
- **OI > 1,000** for hold-to-expiry trades
- **Bid-ask spread < 5% of mid** for entry/exit at fair price
- **Volume > 100 today** confirming the strike is actively used

### Step 2 — Check IV context
- **IV rank**: where is current IV vs the past year? (>70 high; <30 low)
- **IV percentile**: similar concept, percentile-based
- **Skew**: are puts much pricier than calls (typical) or unusually flat?

### Step 3 — Read the term structure
Compare IV across expirations:
- **Front-month IV > back-month** = inverted; signals near-term stress
- **Front-month IV < back-month** = normal contango; calm now

### Step 4 — Identify the relevant Greeks
- **Delta** ≈ probability of finishing ITM
- **Gamma** = delta acceleration; matters for short-dated trades
- **Theta** = daily decay; how much you pay to hold long premium
- **Vega** = sensitivity to IV changes; matters around catalysts

### Step 5 — Define your trade structure
Based on view:
- **Bullish + IV low** → buy calls
- **Bullish + IV high** → bull put spread (sell premium)
- **Bearish + IV low** → buy puts
- **Bearish + IV high** → bear call spread
- **Range-bound + IV high** → iron condor (sell premium both sides)
- **Big move + IV low** → straddle / strangle (buy premium)

## Worked example — pre-earnings AAPL

```
AAPL @ $175, earnings tomorrow
30-day chain:
  $180 call: $4.20 / $4.35 (mid $4.28)
            IV 38%, OI 12k, Vol 450
  $170 put:  $3.80 / $3.95
            IV 36%, OI 9k, Vol 320

Context:
  AAPL avg IV is 25% → IV rank ~85 (very high)
  Earnings = catalyst → IV usually crushes after print

Analysis:
  Buying premium = paying a high IV that will likely crush
  Better idea: sell premium (iron condor or short strangle)

Iron condor structure:
  Sell $180 call ($4.28), buy $185 call ($2.50)  → +$1.78 credit
  Sell $170 put ($3.88), buy $165 put ($2.20)    → +$1.68 credit
  Total credit: $3.46
  Max loss: $5 wide − $3.46 = $1.54
  Risk/reward: ~1:2.2 — collect on calm move
```

## Common chain reading mistakes

- **Looking at "Last" instead of bid/mid** — last is stale on illiquid strikes
- **Ignoring spread** — a "cheap" $0.30 option with $0.20 spread costs you 67% on round-trip
- **Trusting IV without context** — high IV is "expensive" only relative to typical IV
- **Picking a strike by gut** — pick a strike by delta (probability) for cleaner thinking
- **Forgetting weeklies vs monthlies** — different expiration cycles in same chain

## Structured reading order

1. **Underlying price + recent move** — context
2. **Catalyst calendar** — earnings, Fed, etc.
3. **IV rank for the underlying** — vol regime
4. **Liquid strikes by OI** — what's actually tradable
5. **Skew check** — puts vs calls relative pricing
6. **Pick structure** based on view + IV regime
7. **Verify execution math** — credit/debit, max profit, max loss, breakeven

## Decision rules

- **Don't trade a strike** with OI < 100 or spread > 10% of mid
- **Don't buy premium** when IV rank > 70 unless thesis is bigger move than priced
- **Don't sell naked** anything you couldn't sustain a worst-case fill on
- **Always know your max loss** before clicking confirm
- **Default size**: max loss ≤ 1% of account

## In finterm

Derivatives screen displays the option chain with all standard columns. Surface Analytics shows IV across strikes/expirations as a heatmap — fastest way to spot rich/cheap pockets.
