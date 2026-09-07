# Form 4 forward returns on our own universe — 2026-09-07

The gate the plan set before an ownership flag could become an alert: run the
insider-buy signal over twelve months of our own data and look. Script:
`scripts/sec_form4_bulk.py` (`ingest` then `evaluate`); reproducible from the
five SEC quarterly Insider Transactions Data Sets 2025q2–2026q2 and yfinance
daily closes.

**Window:** trades 2025-07-07 → 2026-06-30 (the newest data set ends Q2 2026).
**Events:** one issuer per trade date per category; 37,442 events, 35,771 priced
(1,671 had no price history — delistings, OTC names, bad symbols).
**Returns:** close on the trade date to close 7 / 30 / 91 calendar days later,
minus SPY over the same dates. Windows not yet elapsed are dropped, so the 3m
column has fewer events than the others.
**Not corrected for:** market cap (the store has none), transaction costs,
overlapping events, or survivorship in the symbol map.

## Result — excess return over SPY, equal-weighted per event

| Category | Events | 1w mean / median / hit | 1m mean / median / hit | 3m mean / median / hit |
|---|---:|---|---|---|
| **Scorable buy** (code P, not a 10 % owner, not a plan) | 5,603 | +3.1 % / +1.2 % / 60 % | **+4.3 % / +1.9 % / 58 %** | +4.7 % / +0.6 % / 51 % |
| — of which **cluster** (≥ 2 owners within 30 d) | 3,078 | +3.2 % / +1.3 % / 61 % | **+5.5 % / +2.6 % / 61 %** | +4.9 % / +0.5 % / 51 % |
| — of which single buyer | 2,525 | +2.9 % / +1.0 % / 59 % | +2.8 % / +0.9 % / 54 % | +4.5 % / +0.6 % / 51 % |
| — larger trade value (top tercile) | 2,527 | +3.9 % / +1.7 % / 64 % | +5.2 % / +2.6 % / 61 % | +6.4 % / +1.4 % / 53 % |
| — smaller trade value (bottom tercile) | 1,868 | +2.3 % / +0.6 % / 56 % | +3.4 % / +1.2 % / 55 % | +2.7 % / −0.2 % / 49 % |
| 10 % owner buy | 3,441 | +2.3 % / +0.9 % / 57 % | +2.5 % / +0.2 % / 51 % | +0.5 % / **−3.3 %** / 42 % |
| 10b5-1 plan buy | 225 | +0.9 % / 0.0 % / 50 % | **−1.6 % / −2.0 %** / 45 % | −3.8 % / −4.4 % / 42 % |
| Sell (code S) | 26,502 | −0.3 % / −0.5 % / 46 % | −0.4 % / −1.1 % / 46 % | −1.0 % / −4.3 % / 42 % |

## What it says

1. **The cluster flag is justified on this universe.** Cluster buys beat single
   buys at one month on mean, median and hit rate (61 % vs 54 %), and the
   effect is front-loaded exactly as the literature says: the one-month median
   is the best of the three windows and the three-month hit rate falls back to
   a coin flip. An alert that fires on the filing and expects the reader to
   act within days is the right shape; a "buy and hold for a quarter" reading
   of the same flag is not supported.
2. **Both exclusions are right.** 10 %-owner buys are the weakest buy category
   and go negative on the median by three months; plan buys are negative from
   one month. Keeping them out of the count is not a loss of signal.
3. **Means are far above medians throughout** — the distribution is skewed by
   small, illiquid names that jump. The medians are the honest headline: a
   scorable buy is worth about +2 % over the index in a month, half the time.
4. **Sells are mildly negative here, not zero.** The literature's "sells carry
   no information" holds for large caps; on a universe dominated by small
   names the median sell is followed by −4 % over three months. It still is
   not scored — a −0.4 % mean at one month is not a signal a reader can use —
   but the wrong-sign check the literature describes (large sells preceding
   *positive* returns) does not show up in this sample.
5. **Value, not size.** Larger-value buys did better than smaller ones. Trade
   value is a proxy for conviction, not for market cap; the small-cap effect
   in the papers is about the issuer's size, which this store cannot see.

## Consequences taken

- The **Insider Buys on Holdings** alert is enabled by default (Settings ›
  Notifications), fires only on scorable buys (10 % owners and plan trades
  excluded), and is worded as a filing, never as a call.
- The alert carries the evidence in its message: "on this universe, a cluster
  buy was followed by a median +2.6 % over the index in a month, 61 % of the
  time (2025-07 → 2026-06)".
- Nothing here changes the three per-stock flags or their thresholds.

## Rerunning

```
python scripts/sec_form4_bulk.py ingest '{"quarters":5}'
python scripts/sec_form4_bulk.py evaluate '{"months":12,"benchmark":"SPY"}'
```

Prices are cached in `form4_history.sqlite`; `"refresh_prices": true`
refetches them. Add a data set and rerun when SEC publishes the next quarter.
