# How to Size a Leveraged Position

Leverage amplifies both gains and losses — and it introduces a new risk that unleveraged positions don't have: **forced liquidation**. A leveraged position can be correct in direction and still result in total loss if the drawdown on the way to being right exceeds your margin buffer. This playbook gives you a rigorous framework for sizing leveraged positions in futures, margin accounts, and crypto perpetuals.

## When to Use This

Use this any time you're considering a position with more than 1:1 exposure — futures, options, margin equity, CFDs, crypto perpetuals, forex. The specific mechanics differ across instruments, but the principles are identical.

---

## Step-by-Step Process

### Step 1: Calculate Your True Leverage Ratio

Leverage is often quoted in misleading ways. Start with what matters:

```
True leverage = Notional position value / Equity (margin) deployed
```

If you deploy $10,000 to control $50,000 of E-mini futures: 5:1 leverage.
If you buy $50,000 of stock on margin with $25,000 cash: 2:1 leverage.
If you open 3x BTC perpetuals with $10,000: $30,000 notional, 3:1 leverage.

**Know your number before anything else.**

### Step 2: Calculate the Liquidation Price

The liquidation price is the price level at which your margin equity reaches zero (or the maintenance margin threshold). This is the hard ceiling on your trade — if the price reaches it, you're out regardless of your thesis.

**For a long position:**
```
Liquidation price = Entry price × (1 − 1/leverage)
```

Examples:
- 2:1 leverage, entry $100: liquidation at $50 (−50% from entry)
- 5:1 leverage, entry $100: liquidation at $80 (−20% from entry)
- 10:1 leverage, entry $100: liquidation at $90 (−10% from entry)

**For a short position:**
```
Liquidation price = Entry price × (1 + 1/leverage)
```

Crypto exchanges typically apply a maintenance margin that causes liquidation before full equity loss — check the specific threshold for your exchange.

### Step 3: Set Your Stop Well Above the Liquidation Price

The liquidation price is where you are *forced* out. Your stop loss should be where you *choose* to exit based on your thesis being wrong — this should always be before the liquidation level.

**Rule: The liquidation price should be unreachable if your stop is honored.**

If your stop is 8% below entry and your liquidation is 10% below entry, you have only 2% of buffer. A brief wick, a gap open, or a slow fill could take you to liquidation before your stop executes. Build in a buffer of at least 2–3x between your planned stop and your liquidation price.

### Step 4: Apply Kelly-Adjusted Sizing

The Kelly Criterion gives the optimal fraction of capital to allocate to maximize geometric growth:

```
Kelly fraction = W − (1−W)/R
```

Where:
- W = win rate (probability of the trade being profitable)
- R = average win / average loss ratio

If your strategy wins 50% of the time with 2:1 win/loss ratio: Kelly = 0.50 − 0.50/2 = 0.25 (25% of capital)

**For leveraged positions, full Kelly is almost never appropriate.** The distribution of leveraged position outcomes has fat tails — a single large adverse move can cause devastating loss. Use **quarter-Kelly to half-Kelly** as your starting point.

Practical rule: **position your leveraged trade so that hitting your stop costs 1–2% of total portfolio.** This is independent of leverage — it's about absolute dollar loss, not percentage of the margin deployed.

### Step 5: Account for Margin Costs and Funding

Leveraged positions carry costs that eat into return:

- **Margin interest** (equity/futures): typically prime rate + 1–3% annually; computes daily
- **Funding rate** (crypto perpetuals): can be highly variable; a 0.1% daily funding rate is 36% per year
- **Roll costs** (futures): the cost of rolling expiring contracts forward

For trades intended to last more than a few days, calculate the break-even holding cost. A 5:1 leveraged futures position with 8% annual financing cost needs to gain ~40% (8% × 5) before the position itself breaks even on carry. This is often ignored.

### Step 6: Stress-Test the Maximum Loss Scenario

Before entering, run the worst-case scenario explicitly:

- What is the maximum adverse move this asset has made in a 3-day period historically?
- Does that move reach my liquidation price?
- Can I afford that loss in absolute dollar terms?
- If I lose 100% of the margin on this position, does it affect my ability to hold other positions or meet other financial obligations?

If any of these answers create genuine distress, the position is too large.

---

## Common Mistakes

- **Setting leverage before setting stop distance.** The stop distance and liquidation price should determine your leverage, not the other way around.
- **Ignoring intraday volatility that can force liquidation before your thesis plays out.** Even a correct directional call can be liquidated by a temporary adverse spike, especially in crypto or after-hours.
- **Treating margin as free.** Leveraged positions have real financing costs. Holding leveraged positions for weeks or months while waiting for a macro thesis to materialize can be expensive.
- **Using leverage because you're "more confident" on a trade.** Confidence should be expressed through position size, not leverage ratio. Leverage determines your liquidation risk; sizing determines your dollar risk.

---

## What Success Looks Like

Before entering a leveraged position, you have written down: notional size, margin deployed, leverage ratio, liquidation price, planned stop level, distance between stop and liquidation, dollar loss at stop (as % of portfolio), and daily carry cost. The leverage ratio is determined by your stop placement and risk budget — not by how strong your thesis feels.
