# The Yield Curve

A line plot of **interest rates against time-to-maturity** for bonds of similar credit quality (most commonly US Treasuries).

```
3M    1Y    2Y    5Y    10Y    30Y
4.5%  4.4%  4.2%  4.1%  4.3%   4.6%
```

## The four canonical shapes

- **Normal (upward sloping)**: long rates higher than short rates. Standard during expansions.
- **Steep**: long rates much higher than short — often early-cycle or after a Fed cutting campaign.
- **Flat**: short and long rates similar — usually a transitional state.
- **Inverted (downward sloping)**: short rates *higher* than long rates. Historically a recession warning.

```
NORMAL                   FLAT                INVERTED
 30Y  ╭                                        3M  ╭
 10Y  │                  ─────                 1Y  │
  5Y  │                                        2Y  │
  2Y  │                                        5Y  ╰
  3M  │                                       10Y  ╰
                                              30Y  ╰
```

## Why inversion matters

Every US recession in the past 50 years was preceded by an inverted yield curve (specifically, **2-year vs. 10-year Treasury yields**, often called "**2s10s**"). The lead time is variable — typically 6 to 24 months between inversion and recession start.

The mechanism: short rates reflect current Fed policy; long rates reflect long-run growth/inflation expectations plus a term premium. When investors expect future growth and inflation to be *lower* than today's policy rate, they accept lower long yields — inverting the curve.

It's not infallible (a "soft landing" with inversion-but-no-recession remains debated), but it's a stronger signal than most.

## Historical inversions and recessions

| Inversion start | Inversion duration | Recession start | Lag |
|---|---|---|---|
| Aug 1978 | 17 mo | Jan 1980 | 17 mo |
| Sep 1980 | 11 mo | Jul 1981 | 10 mo |
| Dec 1988 | 13 mo | Jul 1990 | 19 mo |
| Jun 1998 | 1 mo | (no recession) | — |
| Feb 2000 | 11 mo | Mar 2001 | 13 mo |
| Dec 2005 | 18 mo | Dec 2007 | 24 mo |
| Aug 2019 | 4 mo | (COVID Mar 2020 — exogenous) | 7 mo |
| Jul 2022 | ~2 yr | recession debated | — |

## Useful spreads to watch

| Spread | What it tells you |
|---|---|
| **2s10s** (10Y − 2Y) | Most-cited recession signal |
| **3M-10Y** | Preferred by the NY Fed model |
| **5s30s** (30Y − 5Y) | Long-end inflation expectations |
| **TIPS spread** (nominal − real) | Implied inflation expectations |
| **2y forward 1y** | Market's view of rates 2 years out |
| **OIS curve** | Cleaner Fed expectations curve |

## The curve and asset prices

- **Banks** earn the spread between borrowing short (deposits) and lending long (mortgages). A flatter or inverted curve squeezes net interest margin. SVB collapsed in 2023 partly because they bought long bonds at low rates and were funded by deposits that suddenly cost more.
- **Long-duration assets** (growth stocks, long bonds) suffer most when long rates rise. A 2% → 4% rise in 10y can cut a 10-year zero's price by ~17%.
- **Defensive sectors** (utilities, REITs) are often called "bond proxies" — sensitive to long rates.
- **Gold and gold miners** often rally when real rates fall.

## Decision rules

- **Curve inverts and stays inverted > 6 months** → recession risk material; reduce equity beta
- **Curve un-inverts (re-steepens) after long inversion** → often the *trigger* for recession; consider defensive positioning
- **Steep curve + positive growth indicators** → reflation / early-cycle; cyclicals favored
- **Flat curve at high absolute rates** → late-cycle squeeze; bank margins compressing

## In finterm

The Economics screen plots the live curve and 2s10s history. Gov Data has the raw Treasury par yield series. Watch the *change* in the curve as much as the level.
