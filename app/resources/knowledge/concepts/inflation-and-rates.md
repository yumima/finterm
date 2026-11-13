# Inflation and Interest Rates

## Inflation in one paragraph

Inflation is the **percent change in the general price level** over time. The standard US measure is **CPI** (Consumer Price Index, published by the BLS monthly). 2% YoY is the Fed's stated target — the "Goldilocks" zone where prices rise slowly enough that consumers and businesses can plan, but fast enough to keep the economy out of deflationary spirals.

Two inflation flavors matter:

- **Headline CPI** — includes food and energy
- **Core CPI** — strips out food and energy (volatile categories), giving a cleaner read on underlying trend

The Fed pays more attention to **Core PCE** (a slightly different basket than CPI, also published monthly), but headlines move on CPI.

## Worked example — real return on cash

```
Scenario A (2020):
  T-bill yield:         0.1%
  CPI:                  1.4%
  Real yield:          −1.3%   ← cash lost real purchasing power

Scenario B (2024):
  T-bill yield:         5.3%
  CPI:                  3.2%
  Real yield:          +2.1%   ← cash compounds in real terms
```

Same nominal asset, completely different economic outcome. **Real rates drive investment decisions; nominal rates drive headlines.**

## How the Fed reacts

The **Federal Open Market Committee (FOMC)** sets the **federal funds rate** — the rate banks charge each other for overnight loans. It's the lever that influences everything else.

- **Inflation too high** → FOMC raises the funds rate → borrowing gets more expensive → demand cools → prices stabilize
- **Inflation too low / unemployment too high** → FOMC cuts → borrowing cheap → demand picks up

The FOMC meets 8 times a year. Statements, dot plots (members' rate forecasts), and the Chair's press conference are major market events.

## Recent rate cycles

| Cycle | Start → end | Pace | Result |
|---|---|---|---|
| Greenspan (2004–06) | 1.0% → 5.25% | 17 hikes (gradual) | Housing bubble + GFC by 2007 |
| Bernanke ZIRP (2008–15) | 5.25% → 0.25% | Crisis cuts | QE + slow recovery |
| Yellen normalization (2015–18) | 0.25% → 2.50% | 9 hikes over 3 years | Late-2018 stock decline |
| Powell pivot (2019) | 2.50% → 1.75% | Pre-emptive cuts | Soft landing-ish |
| COVID emergency (2020) | 1.75% → 0% | Two cuts in 12 days | Massive QE |
| Powell hike cycle (2022–23) | 0% → 5.50% | 11 hikes, +525 bps in 16 mo | Fastest in 40 years |
| Powell pivot (2024+) | 5.50% → ? | Cut cycle started | Ongoing |

## Real vs. nominal rates

```
Real rate ≈ Nominal rate − Expected Inflation
```

A 5% nominal rate is a great deal when inflation is 2% (3% real). It's a *negative* real rate when inflation is 7%. Real rates drive investment decisions; nominal rates drive headlines.

The market-implied inflation expectation comes from the spread between nominal Treasuries and TIPS (Treasury Inflation-Protected Securities) of the same maturity — the "**breakeven inflation rate**."

## What rate hikes do to assets

| Asset | Why rate hikes hurt | Magnitude |
|---|---|---|
| Long bonds | Existing bonds fall as new ones offer higher coupons | A 100bp rise in 10y yields ~ −8% on TLT |
| Growth stocks | Future cash flows discounted more heavily | Tech multiples compress 20–40% |
| Real estate | Mortgage rates rise; cap rates expand | Home prices stall or decline |
| Gold (sometimes) | Higher real rates make the zero-yield asset less attractive | Inverse correlation to real yields |
| EM currencies | Capital flows to USD | Sharp drawdowns common |

What rate hikes **help**:
- Banks (wider net interest margin, eventually)
- Cash and money-market funds (higher yields)
- Insurers with long-tail liabilities (higher discount rates on liabilities)
- Energy + commodities (often correlated with reflation)

## Common Fed-watching mistakes

- **Trusting the dot plot too literally** — Fed projections have been wrong by 200+ bps within a year multiple times
- **Reacting to single-month CPI prints** — three-month annualized is more reliable
- **Conflating "pause" with "pivot"** — the Fed often pauses long before cutting
- **Ignoring real rates** — the most important variable for asset prices

## Decision rules

- **Inflation surprise > 0.2 vs estimate** → bonds typically sell off; rate cuts get pushed back
- **Fed hikes > market expectations** → equities fall, USD rallies
- **Inverted yield curve + Fed cutting** → recession positioning warranted
- **Real rates > 2% sustained** → headwind for long-duration assets; tailwind for cash

## Practical reading

Watch:
- **CPI release day** (BLS, usually mid-month for prior month)
- **PCE release day** (BEA, end of month)
- **NFP / Jobs report** (BLS, first Friday of month) — Fed cares about employment too
- **FOMC decision days** (8/year)
- **2s10s yield curve** ([[yield-curve]])
- **5y5y forward inflation breakevens** for long-run inflation expectations
- **Fed Chair speeches** between meetings — coded signaling

In finterm, the Economics screen aggregates these in one panel.

## Famous inflation episodes

| Period | Peak inflation | Cause | Aftermath |
|---|---|---|---|
| US 1973–82 (Great Inflation) | 14.6% | Oil shocks + loose policy | Volcker hikes to 20%; 1981 recession |
| Weimar Germany 1923 | hyperinflation | War debt + money printing | Currency collapse |
| Zimbabwe 2008 | 89.7 sextillion% | Land reform + money printing | New currency |
| US 2021–23 | 9.1% | COVID supply shock + stimulus | Powell hikes 525 bps; "soft landing" debated |
