**Breakeven inflation** is the inflation rate at which a TIPS investor and a nominal Treasury investor earn the same total return — it is the market's implied forecast of future CPI inflation.

Breakeven inflation is derived directly from bond prices rather than surveys or models, making it one of the cleanest real-time inflation expectations measures available.

## Formula

```
Breakeven Inflation = Nominal Treasury Yield − TIPS Real Yield
```

For example, if the 10-year Treasury yields 4.50% and the 10-year TIPS yields 1.80%:

```
Breakeven = 4.50% − 1.80% = 2.70%
```

Investors are collectively indifferent between the two securities if CPI averages exactly 2.70% per year over the next 10 years.

## Worked example

```
10yr Treasury yield:   4.50%
10yr TIPS real yield:  1.80%
10yr breakeven:        2.70%

If CPI averages 3.00% → TIPS wins by ~30 bps/year
If CPI averages 2.20% → Nominal Treasury wins by ~50 bps/year
```

## Breakeven as a Fed watch signal

The Fed targets 2% PCE inflation. Market breakevens give real-time signals:

| Breakeven Level | Signal | Fed Response |
|---|---|---|
| < 1.5% | Deflation risk | Easier policy (QE, rate cuts) |
| 1.5%–2.5% | Anchored expectations | Neutral; monitoring |
| 2.5%–3.0% | Elevated expectations | Hawkish tilt |
| > 3.0% | Unanchored expectations | Aggressive tightening |

## Limitations

Breakeven inflation is not a pure inflation forecast. It includes:
1. **Inflation risk premium** — compensation for uncertainty (typically +20–40 bps).
2. **Liquidity premium** — TIPS are less liquid than Treasuries; TIPS yield is slightly higher → breakeven slightly too low (typically -10 to -20 bps).

These effects partially offset; researchers often estimate the "true" inflation expectation is the breakeven ±30 bps.

## Where to find it

- The Federal Reserve Bank of St. Louis (FRED) publishes the 5-year, 10-year, and 5y5y forward breakeven series.
- Ticker proxies: `^TNX` (10yr nominal) minus `^TYX` (30yr); or dedicated FRED series T10YIE (10yr breakeven).

## Pitfalls

- A falling breakeven during a crisis often reflects a TIPS liquidity squeeze (investors sell TIPS for cash), not genuine deflation expectations.
- The 5y5y forward breakeven (5-year expectations starting 5 years from now) is cleaner because it strips out near-term inflation noise.
- Breakevens are priced off CPI-U; PCE (the Fed's preferred measure) tends to run 20–30 bps below CPI.

See also: [[tips|TIPS]], [[real-interest-rate|Real Interest Rate]], [[inflation-cpi|Inflation / CPI]], [[federal-funds-rate|Federal Funds Rate]], [[forward-guidance|Forward Guidance]].
