# Pattern Day Trader Rule

The **Pattern Day Trader (PDT) rule** is FINRA Rule 4210, which requires any trader who executes 4 or more day trades within a rolling 5-business-day period — where those trades represent more than 6% of their total trading activity — to maintain a minimum account equity of **$25,000** in their margin account.

A "day trade" is defined as buying and selling (or short selling and covering) the same security in the same trading day. The rule applies only to margin accounts. Cash accounts are not subject to the PDT rule, but cash accounts can only trade with settled funds.

## Who it covers

The PDT rule applies to:

- U.S. retail margin accounts
- U.S. broker-dealers are required to enforce it
- Equity, options, and leveraged ETF day trades all count

It does **not** apply to:
- Futures accounts (separate margin rules apply)
- Forex accounts at forex dealers
- Cash accounts (but settlement delays apply)
- Accounts at non-U.S. brokers (one common workaround)

## The 5-day rolling window

The rule uses a **rolling 5-business-day window**, not a calendar week. This is the most misunderstood aspect:

- If you trade Mon/Tue/Wed of week 1, then trade again on Thursday (which is still within 5 business days of Monday), that's your 4th day trade in 5 days → PDT flag
- The window continuously rolls forward; there's no reset on Monday morning

## What happens when flagged

1. **First flag** — if your account drops below $25,000, the broker issues a "day trading margin call." You have five business days to deposit funds to bring equity to $25,000.
2. **During the call period** — you can still trade but your day trading buying power is restricted to just two times overnight maintenance excess.
3. **If unfunded** — the account is restricted to "closing positions only" for 90 days (no new positions that could be day trades).
4. **At $25K+ permanently** — the restriction lifts; you have day trading buying power of 4× overnight maintenance excess.

## Day trading buying power

PDT accounts with $25K+ equity get **4× buying power** for day trades (vs. the standard 2× for overnight positions). This is separate from Regulation T initial margin (50%) because day trades are expected to be closed by end of day.

If you use more than your day trading buying power, a "day trading margin call" is issued. This is different from a standard maintenance margin call.

## Why traders care

1. **$25K is a hard wall** — strategies requiring multiple intraday entries/exits (scalping, news trading, opening-range breakouts) become impossible without it.
2. **Option day trades count** — buying and selling an options contract on the same day counts as one day trade, the same as a stock.
3. **Pre-market and after-hours** — trades in extended hours sessions that are opened and closed within the same trading day count toward the PDT limit.
4. **Futures are exempt** — day trading ES (S&P 500 futures), NQ (Nasdaq futures), or MES (micro S&P) contracts does not trigger PDT rules, making futures a popular vehicle for undercapitalized day traders.

## Common violations and gotchas

- **The 6% clause creates confusion** — if fewer than 4 out of your total trades are day trades, you may not be flagged even with 4 round trips. But most active traders exceed the 6% threshold quickly.
- **Cash account workaround has costs** — cash accounts avoid PDT but must wait T+1 for settlement (as of May 2024 for equities). Selling and reusing proceeds the same day violates "good faith" settlement rules.
- **Multiple brokers** — PDT is enforced per account, not per person. Maintaining multiple broker accounts each under 4 day trades per 5 days is technically permitted but operationally complex.
- **Offshore brokers** — some retail traders use offshore brokers to avoid PDT restrictions. These accounts lack SIPC protection and carry counterparty risk.
- **Depositing to meet the call** — depositing $25,000 and then withdrawing back below $25,000 within 5 business days triggers a 90-day restriction.
