# Understanding Fed Policy

The Federal Reserve is the central bank of the United States. Its monetary policy decisions — interest rate settings, asset purchase programs, and forward guidance — are among the most consequential inputs to asset prices across every market. Understanding how the Fed thinks, decides, and communicates is a prerequisite for serious macro investing.

## The dual mandate

Congress gave the Fed two objectives:

1. **Maximum employment** — keep unemployment as low as possible without generating inflation
2. **Stable prices** — defined in practice as 2% average inflation (PCE, not CPI)

These two objectives create the fundamental tension that drives every Fed decision. Low unemployment often comes with rising wages and prices. Fighting inflation often requires slowing employment. The Fed is always navigating between them.

## Structure: who decides

The **Federal Open Market Committee (FOMC)** sets policy. It consists of:

- 7 Board of Governors (appointed by the President, confirmed by Senate)
- President of the New York Fed (permanent voter)
- 4 rotating presidents of the other 11 regional Reserve Banks

The Chair (currently a political appointment) sets the tone. The Committee meets **8 times per year**, roughly every 6–7 weeks. Emergency meetings can happen between scheduled dates (as they did in March 2020).

## The federal funds rate

The FOMC's primary instrument is the **federal funds rate** — the target rate for overnight lending between banks. The actual tool is the rate paid on reserve balances (IORB), which keeps the effective funds rate within the FOMC's target range.

When you hear "the Fed raised rates 25 basis points," this is what moved. Everything else — mortgage rates, credit card rates, bond yields, discount rates — is influenced by (but not identical to) the funds rate.

```
Decision → funds rate change → bank funding costs → lending rates → investment + consumption → inflation + employment
                               ↓ also                ↓ simultaneously
                         Treasury yields         Asset price discount rates
```

The transmission mechanism is powerful but has **long and variable lags** — Milton Friedman's famous phrase. Rate changes take 12–18+ months to fully flow through the economy.

## The dot plot and forward guidance

At quarterly meetings (March, June, September, December), the FOMC publishes the **Summary of Economic Projections (SEP)**, which includes the dot plot — each member's anonymous forecast for the funds rate at year-end for the next 2–3 years and the longer run.

The dot plot is **not a commitment**; it's a snapshot of current thinking. The dot plot has been wrong by 200+ basis points within 12 months multiple times. Markets move significantly on dot plot shifts because they signal the committee's collective bias.

Forward guidance takes several forms:

- **Calendar-based**: "Rates will remain low through 2023" (COVID era)
- **State-contingent**: "Will not hike until unemployment below X and inflation at Y" (Evans Rule era)
- **Qualitative**: "Patient," "watchful," "data-dependent" — coded language the market decodes over time

## Quantitative easing and tightening

When the funds rate hits zero (the **effective lower bound**), the Fed can't cut further through conventional policy. It then uses **balance sheet policy**:

**Quantitative Easing (QE)**:
- Fed buys Treasuries and mortgage-backed securities (MBS) in the open market
- Payments go to banks as reserves
- Forces investors out of safe assets into riskier ones ("portfolio balance channel")
- Suppresses long-term yields and widens spreads between risk-free and risky assets
- Major programs: QE1 (2008), QE2 (2010), QE3 (2012), COVID QE (2020)

**Quantitative Tightening (QT)**:
- Opposite: Fed allows maturing bonds to run off the balance sheet without reinvesting
- Reduces reserve balances; tightens financial conditions
- Less understood than rate hikes; effect on long yields is debated
- Current pace (2022–): ~$60–95bn/month in runoff

**The Fed's balance sheet** peaked near $9 trillion in 2022 (from $900bn before 2008). QT has brought it down, but the Fed has permanently expanded its footprint in the Treasury market.

## How to read FOMC communications

| Output | When | What to read |
|---|---|---|
| **FOMC statement** | Every meeting | Changes from prior statement are the signal; track word-for-word redlines |
| **Press conference** | Every meeting | Chair's Q&A often reveals more than the statement; listen for subtle shifts |
| **SEP / Dot plot** | Quarterly | Terminal rate, pace of cuts/hikes, inflation/unemployment projections |
| **Meeting minutes** | 3 weeks after | More nuanced; reveals dissents and internal debate |
| **Speeches** | Ongoing | Pre-meeting, used to prepare markets for surprises; Jackson Hole in August is the most important |
| **Beige Book** | 2 weeks before each meeting | Qualitative economic conditions by district; useful for regional signals |

## Market reactions to Fed actions

| Action | Typical market response |
|---|---|
| **Hike > expected** | Dollar rallies; Treasuries sell off; equities fall; EM assets sell off |
| **Hike in-line** | Muted; focus shifts to forward guidance language |
| **Hike but dovish tone** | Rates fall; equities rally; "dovish hike" |
| **Pause at high rates** | Dollar stable; curve steepens slightly; equities often rally (relief) |
| **Cut** | Dollar weakens; equities rally initially; long-end Treasuries may sell off if cut signals economic trouble |
| **Emergency cut** | Risk-off: implies the situation is dire; short-term relief then volatility |
| **QE announcement** | Strong risk-on; credit spreads tighten; equities surge |

## The rate cycle and equities

Rate cycles follow a predictable (if inexact) pattern:

1. **Hike cycle begins**: equities under pressure; multiples compress; growth stocks underperform
2. **Peak rate** ("terminal rate"): uncertainty high; stocks choppy
3. **Pause**: equities often stabilize and rally — the "soft landing" hope phase
4. **First cut**: historically bullish if driven by cycle normalization; bearish if driven by crisis
5. **Rate cutting cycle well underway**: conditions easing; early-cycle assets outperform

The [[yield-curve]] shape tells you a great deal about where the market thinks the Fed is in this sequence. An inverted curve means the market expects rate cuts — which means it expects economic deterioration. See [[macro-cycles]] for the full picture.

## Common Fed-watching mistakes

- **Taking the dot plot literally** — it's a survey, not a schedule
- **Confusing a pause with a pivot** — the Fed pauses long before it cuts
- **Thinking the Fed is the market's friend** — the Fed's mandate is not to support stock prices
- **Ignoring real rates** — what matters is the nominal rate minus inflation
- **Extrapolating the current regime** — the Fed's communication style and reaction function change with leadership and circumstances

## In finterm

The Economics screen shows the current and historical federal funds rate, FOMC meeting dates, and live market-implied rate expectations (the "Fed funds futures implied path"). The Rates panel under Gov Data shows the full yield curve against prior dates, letting you visualize how expectations have shifted.
