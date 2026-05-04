# Credit Spread

The **yield premium of a corporate (or risky) bond over a comparable Treasury** — compensation for default risk, liquidity risk, and any other premium the market demands.

```
Credit Spread = YTM(Corporate) − YTM(Treasury, same maturity)
```

A 10-year corporate bond yielding 6.5% when the 10-year Treasury yields 4.5% has a **200 bp credit spread**.

## Worked example

```
JPMorgan 10-year senior unsecured bond
  Coupon: 5.0%, price 95.50, YTM = 5.65%
10-year Treasury YTM: 4.50%
Credit spread: 5.65 - 4.50 = 115 bps

Interpretation:
  Investor demands 1.15% extra yield for taking JPM's credit risk vs US government.
  Implied default probability (rough):  spread / loss-given-default
                                       = 115 / 60   ≈  1.9% annual default risk
  (real default rate for IG banks is much lower; spread also includes liquidity premium)
```

## Reference spread levels

| Category | Tight (cycle low) | Normal | Wide (recession) |
|---|---|---|---|
| US AAA corporate | 30–50 bps | 60–100 | 150+ |
| US Investment Grade (avg) | 80–120 bps | 130–200 | 350–500+ |
| US High Yield (avg) | 250–350 bps | 400–600 | 800–2,000 |
| US CCC / Distressed | 600–900 bps | 1,000–1,500 | 2,000–4,000+ |
| Emerging market sovereign | 200–350 bps | 350–600 | 800–1,500 |

## Historical extremes

| Event | HY spread peak |
|---|---|
| Russian crisis / LTCM (1998) | 1,200 bps |
| Dot-com bust (2002) | 1,000 bps |
| Global Financial Crisis (Dec 2008) | **2,182 bps** |
| Eurozone crisis (2011) | 800 bps |
| Energy crash (Feb 2016) | 900 bps |
| COVID (Mar 2020) | 1,100 bps |
| Cycle tights (mid-2007, mid-2014, late 2021) | 300–350 bps |

## What a spread really pays you for

Credit spread compensates for:
1. **Expected default losses** — probability × loss given default
2. **Liquidity premium** — corporates trade less than Treasuries
3. **Mark-to-market volatility** — spreads widen even without defaults
4. **Tail risk** — fat-tailed distribution of defaults in stress

For most investment-grade bonds, **expected default losses are a small fraction of the spread**. Most of what you're paid is liquidity + spread-vol risk.

## Spread as a recession indicator

Widening credit spreads (especially HY) often precede equity weakness by weeks-to-months. The HY spread is a better recession indicator than the yield curve in some cycles.

| Signal | Interpretation |
|---|---|
| HY spread widens 100+ bps in a month | Stress building; reduce risk |
| IG spread widens 30+ bps in a month | Caution; check macro context |
| HY spread > 800 bps | Recession likely priced in |
| Spreads at multi-year tights | Late cycle; complacency risk |

## Pitfalls

- **Tight spreads ≠ safe**; they signal complacency, not safety
- **Spread compression in junk + rising rates** — the worst combo (carry shrinks, prices fall)
- **OAS (option-adjusted spread)** strips embedded options for callable bonds; use OAS for cleaner comparison
- **Spreads widen sharply in liquidity events** even for high-quality bonds (March 2020 IG spread blew out)

## Decision rules

- **HY spreads tighten below 350 bps** → consider trimming HY exposure
- **HY spreads widen 200+ bps** → entry opportunity if you have the stomach
- **IG widening alongside curve inversion** → strong combined recession signal
- **Spreads tightening but defaults rising** → market mispricing; eventual repricing

## In finterm

Economics screen tracks IG and HY spreads; FRED's BAMLH0A0HYM2 series is the standard HY benchmark. Watch the *change* in spreads as much as the level.
