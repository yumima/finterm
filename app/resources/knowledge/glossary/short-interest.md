**Short interest** is the total number of shares of a stock that have been sold short but not yet covered (bought back) — a measure of bearish sentiment and potential short squeeze fuel.

Short sellers borrow shares, sell them in the market, and must eventually buy them back. Short interest represents the cumulative outstanding "open short" positions at any given time.

## Key metrics

**Short interest (shares):**
```
Total shares currently short = 50,000,000
```

**Short interest ratio (% of float):**
```
Short Interest % = Short Interest Shares / Float Shares × 100
50M short / 200M float = 25% of float short
```

**Days to cover (DTC) — the [[days-to-cover|separate entry]] covers this in full:**
```
DTC = Short Interest / Average Daily Volume
```

## Worked example

```
GME (GameStop) January 2021:
Short interest:   71,200,000 shares
Float:            49,000,000 shares
Short % of float: 145% (more shares short than in float — possible via rehypothecation)
DTC:              ~4 days

Result: Short squeeze drove price from $17 → $483 in 3 weeks
```

## Data sources and frequency

FINRA requires reporting twice monthly (mid-month and end-of-month settlements). Data is typically delayed 2 weeks. Near-real-time short data is available from premium providers (S3 Partners, IHS Markit, Ortex).

## Short interest thresholds

| Short % of Float | Interpretation |
|---|---|
| < 2% | Very low; negligible short-selling activity |
| 2%–5% | Normal baseline; modest bearish sentiment |
| 5%–10% | Elevated; meaningful bearish conviction |
| 10%–20% | High; strong bearish positioning |
| > 20% | Very high; meaningful short squeeze potential |
| > 30% | Extreme; substantial squeeze risk if positive catalyst |

## Short interest as a signal — ambiguous

Short interest is **not a clean directional signal**:
- High short interest can mean sophisticated investors have done research and the stock is genuinely overvalued.
- High short interest can also mean crowded shorts ripe for a squeeze if the thesis is wrong.
- Falling short interest can mean shorts are covering (bullish, squeeze occurring) or that the stock has already fallen (bearish, mission accomplished).

## Synthetic short interest

Short interest doesn't capture all bearish exposure:
- Long put positions.
- Short call positions (especially covered calls reducing effective long exposure).
- Total return swaps and CFDs.
- These synthetic positions don't appear in standard short interest data.

## Pitfalls

- Short interest data is stale (2-week delay); significant covering or building can occur between reports.
- Short interest above 100% of float is possible via re-hypothecation (the same shares being lent multiple times), but actual data accuracy is debated.
- High short interest combined with low DTC (high volume) reduces squeeze risk because shorts can cover quickly.

See also: [[days-to-cover|Days to Cover]], [[float|Float]], [[short-selling|Short Selling]], [[dark-pool|Dark Pool]], [[margin|Margin]].
