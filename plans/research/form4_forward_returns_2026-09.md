# Form 4 forward returns on our own universe — 2026-09-07 (revised)

The gate the plan set before an ownership flag could become an alert: run the
insider-buy signal over twelve months of our own data and look. Script:
`scripts/sec_form4_bulk.py` (`ingest` then `evaluate`); reproducible from the
five SEC quarterly Insider Transactions Data Sets 2025q2–2026q2 and yfinance
daily closes.

**Revision.** The first version of this note (same day) anchored returns on
the *trade* date, admitted 4/A amendments, marked clusters with hindsight
(every buy in a run, including the first), and its "top tercile" was the top
45 %. Review caught all four; the definitions below are what the script does
now, and every number was regenerated. The effect is smaller than the first
table said — about half — and the conclusions survive.

**Window:** filings 2025-07-07 → 2026-06-30 (the newest data set ends Q2 2026).
**Events:** one issuer per *filing* date per category (the day a reader could
act; Form 4 is due two business days after the trade and late filings are
common). 29,468 events, 28,093 priced.
**Returns:** close on the filing date to close 7 / 30 / 91 calendar days later,
minus SPY over the same dates. Windows not yet elapsed are dropped.
**Cluster:** a scorable buy with a *different* owner's scorable buy already on
file inside the trailing 30 days. Point in time: the first buy of a run is a
single buy, because at that moment nobody could know a second insider would
follow. This is the definition the alert can honestly claim.
**Not corrected for:** market cap (the store has none), transaction costs,
overlapping events, or survivorship in the symbol map.

## Result — excess return over SPY, equal-weighted per event

| Category | Events | 1w mean / median / hit | 1m mean / median / hit | 3m mean / median / hit |
|---|---:|---|---|---|
| **Scorable buy** (code P, not a 10 % owner, not a plan) | 4,695 | +2.1 % / +0.7 % / 56 % | **+2.8 % / +1.4 % / 56 %** | +3.4 % / +0.2 % / 50 % |
| — of which **cluster** (another owner already on file) | 1,404 | +1.7 % / +0.5 % / 55 % | **+2.9 % / +1.8 % / 59 %** | +3.2 % / −0.2 % / 50 % |
| — of which single buyer | 3,291 | +2.3 % / +0.8 % / 57 % | +2.8 % / +1.2 % / 55 % | +3.6 % / +0.3 % / 51 % |
| — trade value, top tercile | 1,564 | +2.9 % / +1.4 % / 60 % | +4.1 % / +2.2 % / 58 % | +5.0 % / +0.2 % / 51 % |
| — trade value, bottom tercile | 1,566 | +1.4 % / +0.4 % / 53 % | +1.4 % / +0.7 % / 54 % | +1.9 % / −0.3 % / 49 % |
| 10 % owner buy | 2,165 | +2.0 % / +0.6 % / 54 % | +1.6 % / **−0.1 %** / 49 % | +0.9 % / **−3.0 %** / 43 % |
| 10b5-1 plan buy | 192 | +1.0 % / −0.5 % / 47 % | **−1.2 % / −1.6 %** / 44 % | −2.5 % / −2.5 % / 44 % |
| Sell (code S) | 21,041 | −0.2 % / −0.4 % / 47 % | −0.1 % / −0.9 % / 47 % | −0.6 % / −4.0 % / 43 % |

## What it says

1. **The cluster flag is still justified, and the number is smaller.** Judged
   point in time, a cluster buy is followed by a median +1.8 % over the index
   in a month, 59 % of the time — against +1.2 % / 55 % for a single buyer.
   The hindsight version of the same table said +2.6 % / 61 %: half of that
   was the look-ahead, and a further part was the two days between trade and
   filing that nobody could have traded. The effect remains front-loaded — the
   one-month median is the best window, and by three months the hit rate is a
   coin flip.
2. **Both exclusions are right.** 10 %-owner buys are negative on the median
   from one month; plan buys are negative from one month. Keeping them out of
   the count loses no signal.
3. **Means sit well above medians throughout** — small illiquid names that
   jump. The medians are the honest headline: a scorable insider buy is worth
   about +1 % over the index in a month, a little better than half the time.
4. **Sells are mildly negative, not zero.** On a universe dominated by small
   names the median sell is followed by −4 % over three months. Still not
   scored: −0.1 % at one month is not something a reader can use.
5. **Value, not size.** Top-tercile trade values beat bottom-tercile ones on
   every window. Trade value proxies conviction, not market cap; the
   small-cap effect in the papers is about the issuer's size, which this
   store cannot see.

## Consequences taken

- The **Insider Buys on Holdings** alert stays enabled by default, fires only
  on scorable buys, and quotes the row it fired in: "+1.8 % median, 59 %" for
  a cluster, "+1.2 %, 55 %" for a single insider — with the window stated.
- Amendments are excluded from both the daily store and the bulk history, so
  the alert universe and the backtest universe are the same filings.
- Nothing here changes the three per-stock flags or their thresholds.

## Rerunning

```
python scripts/sec_form4_bulk.py ingest '{"quarters":5}'
python scripts/sec_form4_bulk.py evaluate '{"months":12,"benchmark":"SPY"}'
```

Prices are cached in `form4_history.sqlite`; `"refresh_prices": true`
refetches them. Add a data set and rerun when SEC publishes the next quarter.
