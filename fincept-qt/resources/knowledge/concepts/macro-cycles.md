# Macro Cycles

The economy doesn't move in a straight line. It expands, peaks, contracts, and troughs — and then does it again. This repeating pattern is the **business cycle**, and understanding where you are in it is one of the most powerful inputs to top-down investment decisions.

## The four phases

```
EARLY          MID            LATE           RECESSION
─────────────────────────────────────────────────────
Recovery       Acceleration   Deceleration   Contraction
Rates low      Rates rising   Rates high     Rates falling
Credit loose   Credit tight.  Credit tight   Credit freezing
ing starts     ening begins   or very tight  then easing
```

| Phase | GDP growth | Unemployment | Inflation | Fed policy |
|---|---|---|---|---|
| **Early cycle** | Recovering | Falling fast | Subdued | Easing / just cut |
| **Mid cycle** | Strong | Low | Moderate | Neutral or hiking |
| **Late cycle** | Slowing | Near trough | Rising | Hiking aggressively |
| **Recession** | Negative | Rising | Peaking then falling | Cutting |

The NBER (National Bureau of Economic Research) is the official arbiter of US recessions. Their dating is always backward-looking — they typically declare a recession months after it started.

## The credit cycle: the engine underneath

The business cycle is driven in large part by the **credit cycle** — the expansion and contraction of credit availability.

- **Expansion**: banks ease lending standards; businesses and consumers borrow more; investment rises; asset prices inflate; leverage accumulates
- **Peak and turn**: credit becomes too extended; a trigger event (rising rates, a defaulting institution, an exogenous shock) causes lenders to tighten; borrowers reduce spending; asset prices fall
- **Contraction**: defaults rise; banks provision for losses; lending tightens further (a self-reinforcing loop); the real economy contracts
- **Trough**: credit is tight but stabilizing; policy easing begins; sentiment improves; cycle restarts

Credit cycles typically run longer than a single business cycle — sometimes 15–20 years — with multiple business cycles nested inside them. The 1980s–2008 period was broadly a credit expansion; 2008 was the blowup.

## Leading, lagging, and coincident indicators

Markets are **leading indicators** — they price in expectations 6–12 months ahead. This makes cycle-timing genuinely hard; by the time the data confirms a recession, equities have often already sold off 20–30%.

| Type | Examples | Leads/lags economy by |
|---|---|---|
| **Leading** | Yield curve, ISM new orders, building permits, stock prices, credit spreads | 6–12 months |
| **Coincident** | GDP, payrolls, personal income, industrial production | In real time |
| **Lagging** | Unemployment rate, CPI, commercial loan rates | 3–12 months after |

The Conference Board's LEI (Leading Economic Index) aggregates 10 leading indicators. A sustained decline in LEI is one of the most reliable early recession warnings.

## Sector rotation through the cycle

Different sectors tend to outperform at different cycle phases. This is the basis for Fidelity's widely-cited sector rotation model.

| Cycle phase | Outperforming sectors | Underperforming sectors | Why |
|---|---|---|---|
| **Early** | Financials, Consumer Discretionary, Industrials | Utilities, Consumer Staples | Rate cuts boost loan demand; consumers spend again |
| **Mid** | Technology, Materials, Industrials | Financials (curve flattening) | Strong earnings growth; capex picks up |
| **Late** | Energy, Materials, Health Care | Consumer Discretionary | Inflation rises; input prices matter; consumers cautious |
| **Recession** | Consumer Staples, Utilities, Health Care | Energy, Financials, Industrials | Defensive cash flows; dividend yield replaces growth |

See [[sector-rotation]] for a deeper treatment of this framework and how to implement it.

## The yield curve as cycle clock

The [[yield-curve]] is arguably the most reliable single cycle indicator. The typical sequence:

1. Mid-cycle: curve flattens as Fed hikes short rates
2. Late-cycle: curve inverts (2s10s goes negative)
3. Recession: curve re-steepens as the Fed cuts aggressively and long rates fall in anticipation of weak growth
4. Early-cycle: steep curve; banks begin earning spread again; lending recovers

The re-steepening after a prolonged inversion is often the *actual* trigger for recession — it signals the economy is deteriorating fast enough that the Fed is forced to act.

## The credit spread signal

Investment-grade and high-yield credit spreads (the premium over Treasuries) are powerful real-time cycle monitors. See [[credit-markets]] for detail.

- **Spreads tightening**: credit markets are confident; risk appetite healthy; usually mid-cycle
- **Spreads at historic tights**: late-cycle warning; complacency is high
- **Spreads widening sharply**: stress is building; risk-off
- **Spreads blowing out**: recession / financial stress confirmed; policy response incoming

## How to use cycle analysis in practice

The difficulty is that cycles don't arrive on schedule. The COVID recession of 2020 lasted two months — the shortest on record. The 1991 recession and 2001 recession were mild. The 2008–09 cycle was catastrophically deep because of the credit bubble embedded in it.

Practical rules:

- **Look at multiple cycle indicators together** — no single one is reliable enough
- **Use the yield curve, credit spreads, LEI, and ISM together** as a dashboard
- **Don't try to call the exact turn** — position gradually as evidence builds
- **The market leads, not lags** — by the time it's obvious, much of the move is over
- **Policy matters**: a Fed that pivots quickly compresses cycle downturns; a Fed that stays tight amplifies them

## In finterm

The Economics screen aggregates key cycle indicators: ISM Manufacturing, LEI, credit spreads, and the yield curve. Watch how they evolve together rather than in isolation. The Markets screen shows sector performance, which can confirm or contradict cycle analysis.
