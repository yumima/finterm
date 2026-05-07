**Spread to Treasury** is the yield difference between a non-government bond and the U.S. Treasury security of the same (or interpolated) maturity — the purest common measure of the incremental yield an investor receives for taking credit risk.

It isolates the credit and liquidity premium above the risk-free rate, stripping out the interest-rate component that both bonds share.

## Formula

```
Spread to Treasury = Bond Yield − Matching-Maturity Treasury Yield

Interpolation when no exact-maturity Treasury exists:
Benchmark Treasury Yield = Treasury yield at nearest maturities,
                            linearly interpolated to bond's exact maturity
```

## Worked example

```
Corporate bond: 7-year maturity, yield = 5.80%
7-year Treasury yield: 4.40%  (interpolated between 5yr at 4.30% and 10yr at 4.50%)

Spread to Treasury = 5.80% − 4.40% = 140 bps

Interpretation: Investor earns 140 bps above risk-free for:
  1. Credit risk (probability of default × loss given default)
  2. Liquidity premium (corporate bonds less liquid than Treasuries)
  3. Call risk (if bond is callable)
```

## Spread to Treasury in practice

For most plain-vanilla corporate bonds, "spread to Treasury" means the difference between the bond's yield and the on-the-run Treasury of the closest maturity. For bonds with embedded options (calls, puts), **option-adjusted spread (OAS)** is more appropriate.

## Spread to Treasury vs. other spread measures

| Measure | Rate Curve | Adjusts for Options | Use Case |
|---|---|---|---|
| Spread to Treasury | Single point | No | Quick comparison |
| G-spread | Interpolated gov't curve | No | More accurate positioning |
| Z-spread | Full spot rate curve | No | Most precise pricing |
| OAS | Full spot rate curve | Yes | Callable/putable bonds, MBS |
| ASW (Asset Swap) | Swap curve | No | Floating-rate context |

For investment-grade analysis, G-spread (to the full government curve) and OAS are most rigorous. For rough-and-ready comparisons, spread to Treasury is universally understood.

## Spreads by instrument and typical ranges

| Instrument | Typical Spread Range (bps) | Driven by |
|---|---|---|
| Agency bonds (FNMA, FHLMC) | 20–50 | Government support |
| AAA ABS | 30–80 | Structured credit |
| IG corporate (A-rated) | 60–120 | Corporate credit |
| IG corporate (BBB) | 100–200 | BBB credit risk |
| HY bond (BB) | 200–400 | Leveraged credit |
| HY bond (B) | 350–600 | High default risk |
| Leveraged loan (B) | 300–500 (SOFR+) | Floating-rate credit |
| Emerging market (IG) | 100–250 | Sovereign + corporate |
| EM (HY/distressed) | 400–1500+ | Country risk |

## Carry trade context

Fixed income "carry" is essentially spread to Treasury: you earn more than the risk-free rate, hoping the spread doesn't widen (which would cause price loss exceeding carry).

```
Spread to Treasury carry:
  Hold $10M of BBB bonds at 150 bps over Treasury for 1 year
  Spread stay flat: earn $150,000 extra vs Treasuries
  Spread widens 50 bps: price declines ~(0.50% × 5yr duration) = 2.5% → −$250,000 loss
  Net: carry trade lost money despite positive spread
```

## Pitfalls

- Spread to Treasury conflates credit risk and liquidity premium — a spread can widen due to liquidity concerns even when credit quality is unchanged.
- Using on-the-run Treasury for interpolation: Treasury yields can have "kinks" around specific maturities; interpolated benchmarks smooth this out.
- Spread to Treasury is a nominal measure — TIPS yield-based spreads are used for inflation-adjusted comparisons.

See also: [[yield-spread|Yield Spread]], [[credit-spread|Credit Spread]], [[investment-grade|Investment Grade]], [[high-yield-bond|High-Yield Bond]], [[duration|Duration]].
