# How to Build a Multi-Leg Options Strategy

Most options traders start with single-leg positions (buy a call, buy a put) and discover they're constantly fighting IV crush, theta decay, and the gap between "directionally right" and "financially right." Multi-leg structures — spreads, straddles, condors — are purpose-built to express specific views on direction, volatility, or both, with defined risk. This playbook walks you from market view to structure to execution.

## When to Use This

Use this when you have a specific view on a stock or market that you want to express through options, and a single-leg position's risk profile doesn't fit your view. You should already understand basic options mechanics (delta, theta, IV) before applying this playbook.

---

## Step-by-Step Process

### Step 1: Define Your View Precisely

Before choosing a structure, write down your view in exact terms. Vague views produce bad structure choices:

| View | Appropriate Structure |
|---|---|
| Stock goes up significantly | Buy call or call debit spread |
| Stock stays above X | Sell put or bull put spread |
| Stock stays in a range | Iron condor or short strangle |
| Stock moves a lot (either direction) | Buy straddle or strangle |
| Stock doesn't move much | Sell straddle or iron condor |
| Specific upside, limited downside risk | Bull call spread |

If you can't express your view in one sentence with a directional/volatility component, stop here.

### Step 2: Assess the IV Environment

Check the IV rank and IV percentile for the underlying. This is the single most important input for choosing whether to buy or sell premium.

- **IV Rank above 50**: volatility is elevated relative to the past year; selling premium (short straddle, iron condor, credit spreads) is generally more attractive
- **IV Rank below 30**: volatility is compressed; buying premium (debit spreads, straddles) is relatively cheaper
- **IV Rank between 30–50**: neutral environment; structure selection should be driven more by the directional view than the volatility view

### Step 3: Choose Your Expiration

Expiration choice determines how much theta decay works for or against you:

- **Short-dated (7–21 days)**: maximum theta decay per day; appropriate for selling premium if you want rapid time decay
- **Medium-term (30–60 days)**: the standard range for most premium-selling strategies; enough time for the trade to work, enough theta to earn meaningful income
- **Long-dated (60–120 days)**: appropriate for directional debit spreads; you're buying time for your thesis to play out

Avoid selling options in the last 7 days before expiration unless it's a very liquid underlying — gamma risk is highest here.

### Step 4: Select Strikes

Strike selection determines the probability of profit and the risk/reward:

**For credit spreads (selling premium):**
- Short strike: typically 25–35 delta (roughly 65–75% probability of expiring worthless)
- Long strike: 3–5 points wide (equities), scaled to underlying price
- Collect at least one-third the width of the spread in credit

**For debit spreads (buying premium):**
- Long strike: near the money or slightly OTM in the direction of your view
- Short strike: at your profit target price
- Pay no more than half the width of the spread in debit for the best risk/reward

**For straddles/strangles:**
- ATM straddle: both strikes at current price
- Strangle: buy OTM call and OTM put 1–2 standard deviations out

### Step 5: Calculate and Verify the Greeks

Before placing the trade, calculate the net position Greeks:

- **Net delta**: are you directionally positioned as intended?
- **Net theta**: positive means time decay earns money each day (selling structures); negative means you're paying daily (buying structures)
- **Net vega**: positive means you profit from IV expansion; negative means you profit from IV contraction
- **Maximum profit and maximum loss**: verify these match your risk plan

### Step 6: Check Liquidity at Each Strike

Multi-leg trades introduce execution risk at every strike. Before placing a limit order on the multi-leg structure:
- Check open interest at each strike — aim for >500 OI
- Check the bid-ask spread — it should be <5% of mid for the entire spread
- Use a net debit/credit limit order for the entire spread rather than legging in

---

## Common Mistakes

- **Choosing a structure before defining the view.** "I want to do an iron condor" is not a market view. The structure follows the view.
- **Selling premium into a low-IV environment.** When IV rank is 20, you're collecting minimal premium for real risk. The setup isn't there.
- **Wide spreads on illiquid options.** The bid-ask spread on a 5-point iron condor with illiquid options can eat half your credit before the trade even moves.
- **Ignoring earnings and events during the position's life.** If there's an earnings report or major event during your expiration window, the volatility risk profile changes completely.

---

## What Success Looks Like

You enter a trade where the structure precisely matches your view (direction, magnitude, timing), the Greeks confirm the intended exposure, maximum loss is within your risk budget, and you know exactly at what price levels you will adjust or exit.
