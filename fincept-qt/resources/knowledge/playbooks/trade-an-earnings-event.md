# How to Trade an Earnings Event

Earnings announcements are among the most predictable scheduled events in markets, yet most traders approach them without a systematic framework. The result: buying options that get crushed by IV decay even when directional calls are correct, or sizing into binary events without adjusting for the elevated uncertainty. This playbook covers the full lifecycle of an earnings trade.

## When to Use This

Use this process whenever you are considering a position in a stock that has an earnings announcement within the next two to three weeks, or when you're deciding whether to hold an existing position through earnings.

---

## Step-by-Step Process

### Step 1: Find the Implied Move

Open the option chain for the nearest expiry after the earnings date. The **implied move** is approximately the cost of an at-the-money straddle divided by the stock price.

```
Implied move ≈ (ATM call + ATM put) / stock price
```

If AAPL is at $200 and the nearest weekly straddle costs $14, the implied move is ~7%. This is the options market's consensus estimate of the magnitude (not direction) of the post-earnings move.

**Compare this to historical moves.** Look at the last 8 quarters. What was the actual move? If the stock historically moves 5% on earnings and the options are pricing 10%, implied move is expensive. If it typically moves 8% and options price 6%, options are cheap.

### Step 2: Decide on Direction and Conviction

Before looking at options strategies, ask: do you have a directional view, or just a view on volatility magnitude? These lead to different strategies.

- **Directional + high conviction**: buy calls or puts directly (accepting IV crush risk)
- **Directional + medium conviction**: buy a vertical spread (reduces IV crush exposure, caps max profit)
- **Volatility view (expects move larger than implied)**: buy a straddle or strangle
- **No edge on direction or magnitude**: don't trade the event — there's no reason your directional guess is better than the market's

### Step 3: Size for a Binary Event

Earnings are binary. Regardless of your fundamental analysis, a company can beat and guide up and still fall 10% if expectations were priced for more. A company can miss and rally on relief. Size positions so that your **maximum loss on an adverse outcome is within your normal risk parameters**.

For option buyers: your maximum loss is the premium paid. This should be ≤ 1–2% of portfolio per trade.
For directional stock trades held through earnings: reduce size by 50–75% relative to a normal position. Volatility is 3–5x higher around earnings; your risk per dollar is proportionally higher.

### Step 4: Know Your Post-Earnings Reaction Plan

If you own stock going into earnings:
- **Beats and rallies**: have a pre-defined plan for whether you trim, hold, or add
- **Beats but falls**: understand this is common (buy the rumor, sell the news) and don't panic
- **Miss and falls**: know your stop level in advance; don't let a bad thesis compound

If you own options:
- **IV crush** will reduce the value of your options by 30–60% the morning after earnings, regardless of direction. If you're up 50% because the stock moved your way, take profit on open — the premium value deteriorates rapidly the day after.

### Step 5: Post-Earnings Positioning

The most actionable time to establish or add to a position is **3–5 days after earnings**, once IV has normalized and the market has digested the report. Enter based on thesis, not on the earnings reaction.

---

## Common Mistakes

- **Buying options into earnings and expecting direction to be enough.** Even correct directional calls frequently lose money due to IV crush. You need the move to exceed the implied move to profit on a long straddle.
- **Holding through earnings without acknowledging the binary nature.** "I'm long-term" is not a risk management plan for a 15% overnight gap-down.
- **Using the post-earnings pop/drop as an entry without waiting for stabilization.** The first 30 minutes after an earnings print are the least informative; the reaction over 3–5 days is more actionable.
- **Sizing as if it's a normal trade.** Earnings implied volatility reflects real uncertainty. Treat it like a coin flip at 2x your normal size, not a conviction trade.

---

## What Success Looks Like

You have a clear view on whether current option pricing is rich or cheap relative to historical moves. You have a defined maximum loss going in. You don't panic at the first-day reaction and instead assess whether the underlying thesis was confirmed or refuted. You're ready to act in the days after, not just on the announcement itself.
