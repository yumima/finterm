# How to Read a Volatility Surface

The volatility surface is a three-dimensional representation of implied volatility across all strikes and expirations for a given underlying. It tells you what options market participants collectively expect — not just "how volatile" the stock is, but *which scenarios* the market is most worried about and how that fear changes over time. Reading it well is a genuine edge in options trading.

## When to Use This

Use this when deciding whether options are cheap or expensive for a specific purpose, when assessing tail risk for a specific direction or time horizon, and when managing existing options positions where understanding the vol surface evolution helps determine the trade's path.

---

## Step-by-Step Process

### Step 1: Understand the Three Dimensions

The volatility surface has:
- **X-axis**: Strike price (often expressed as delta or moneyness — percentage OTM)
- **Y-axis**: Expiration date (days to expiry or calendar months)
- **Z-axis (height)**: Implied volatility at each strike/expiration combination

You'll see it as either a 3D surface plot or a 2D heatmap. The key is to look at the *shape* — not just individual points.

### Step 2: Read the Term Structure (Time Dimension)

Term structure is how IV changes as expiration moves further out:

**Normal (upward-sloping) term structure:**
- Short-dated IV < Long-dated IV
- Markets are calm in the near term but uncertain long-term
- Typical in quiet, non-event-driven environments

**Inverted (downward-sloping) term structure:**
- Short-dated IV > Long-dated IV
- Immediate risk is elevated; markets expect near-term volatility to resolve
- Common before earnings, FOMC meetings, or during acute stress events

**Flat term structure:**
- Uniform IV across all expirations
- Relatively rare; can indicate a persistent elevated uncertainty environment

**Why it matters:** If you're selling short-dated options to collect premium, a steeply normal term structure is favorable — you collect high premium on near-term options and the longer-dated contracts are cheaper to buy as hedges. An inverted term structure means near-term options are the most expensive — appropriate if you're buying protection for a near-term event, less appropriate for routine premium selling.

### Step 3: Read the Skew (Strike Dimension)

Skew describes how IV changes across strikes at a fixed expiration. Most assets have **negative skew** (also called a "put skew" or "left skew"):

- OTM puts carry higher IV than OTM calls at the same distance from the money
- This reflects demand for downside protection — investors pay more for crash insurance than for upside lottery tickets

**Measuring skew:**
- Simple: IV of 25-delta put minus IV of 25-delta call
- Positive result = put skew (most common in equities, indices)
- Negative result = call skew (common in commodities like natural gas, in crypto during bull runs)

**Implied skew tells you what the market fears:**
- **Steep put skew in equity indices**: market is paying heavily for crash protection — risk-off sentiment
- **Steep call skew** (as in commodities or crypto): market is paying for upside surprise — supply disruption fear or FOMO
- **Flat skew**: symmetric uncertainty; both directions equally uncertain

### Step 4: Find the ATM IV for Baseline Comparison

The at-the-money (ATM) implied volatility is your baseline for the current "fair price" of options. Compare this to:

- **Realized volatility (historical vol)**: if IV > realized vol by a large margin, options are expensive and selling premium may be attractive. If IV ≈ realized vol, there's less inherent edge in either direction.
- **IV Rank / IV Percentile**: where is the current ATM IV relative to the past 52 weeks? IV Rank above 60 = elevated; below 30 = compressed.

### Step 5: Identify the Wings

"Wings" refer to the deep OTM options — both puts and calls — that sit at the extremes of the surface. Wing IV is elevated when:
- **Deep OTM puts are elevated**: market is pricing in crash/fat-tail risk
- **Deep OTM calls are elevated**: market is pricing in short squeeze or M&A premium

For equity indexes, deep OTM puts are almost always elevated (the "implied crash premium"). This is why index put protection is expensive in calm markets — you're paying for insurance that is always somewhat in demand.

### Step 6: Apply What You See to a Trade

| Surface observation | What it suggests |
|---|---|
| Inverted term structure with upcoming earnings | Buy near-dated straddle; short-dated premium is expensive but the event may justify it |
| Flat, compressed surface (low IV rank) | Buy options rather than sell; premium is cheap |
| Steep put skew with no obvious macro event | Selling put spreads may be attractive if you disagree with the tail fear |
| Elevated wing vol across the surface | Avoid selling far OTM options; the market is pricing tail risk it may know something about |
| Normal term structure, moderate skew | Standard environment; iron condor or calendar spread are well-suited |

---

## Common Mistakes

- **Reading only ATM IV and ignoring the shape.** The shape — term structure and skew — often contains more information than the ATM level.
- **Selling the most expensive part of the surface without understanding why.** Steep put skew sometimes reflects genuine risk (event risk, macro stress) that the options market is pricing correctly.
- **Ignoring term structure when choosing expiration.** Selling 7-day options in an inverted term structure means you're selling when short-dated vol is the highest — it looks attractive but may reflect a real near-term event risk.
- **Treating the surface as static.** The surface changes daily, sometimes dramatically around events. A position that was well-structured when placed can become exposed when the surface shifts.

---

## What Success Looks Like

You look at a vol surface and within two minutes can characterize: whether term structure is normal or inverted, where put skew is relative to normal, whether ATM IV is elevated or compressed relative to history, and whether there is any anomalous wing pricing. You use these observations to select strategy type, expiration, and strikes — not just the ATM level in isolation.
