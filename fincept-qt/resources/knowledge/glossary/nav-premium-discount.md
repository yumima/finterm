An ETF **NAV premium or discount** is the difference between an ETF's market price (what you pay on the exchange) and its net asset value (NAV) — the per-share fair value of the underlying holdings.

When an ETF trades above NAV it is at a **premium**; when it trades below NAV it is at a **discount**.

## Formula

```
Premium/Discount (%) = (Market Price − NAV) / NAV × 100
```

## Worked example

```
ETF market price:        $201.30
ETF NAV (end of day):    $200.00
Premium:                 (201.30 − 200.00) / 200.00 = +0.65%

ETF market price:        $198.50
ETF NAV:                 $200.00
Discount:                (198.50 − 200.00) / 200.00 = −0.75%
```

## Why premiums and discounts exist

For standard stock ETFs (SPY, QQQ), premiums/discounts are minimal (typically < 0.05%) because arbitrage by [[authorized-participant|authorized participants]] eliminates deviations almost immediately.

Larger premiums/discounts occur in:

| ETF Type | Typical Premium/Discount | Reason |
|---|---|---|
| Large-cap stock ETFs | ±0.01–0.05% | Instant AP arbitrage |
| Bond ETFs | ±0.10–0.30% | Less liquid underlying bonds |
| International ETFs | ±0.20–0.50% | Market hours don't overlap |
| Frontier/emerging markets | ±0.50–2.0%+ | Illiquid underlying markets |
| Closed-end funds | ±2–20%+ | No AP mechanism; fixed share count |
| Bitcoin ETFs (early 2024) | +0.5–2.0% | New product, demand surge |

## Closed-end funds: a special case

Closed-end funds (CEFs) have fixed share counts and no AP mechanism, so they can trade at persistent large discounts:

```
A CEF with $100M in municipal bonds
ETF market price: $9.00/share
NAV: $10.00/share → Trading at −10% discount

Investor buying at discount gets $10 of assets for $9 (potential value unlock)
```

Buying CEFs at large discounts is a classic value-oriented strategy when the discount is historically wide.

## Intraday NAV (iNAV)

During trading hours, ETF providers publish an intraday indicative NAV every 15 seconds. The iNAV allows investors to assess premiums/discounts in real time, not just at end of day. During market stress, the iNAV itself becomes uncertain if underlying prices are stale.

## Pitfalls

- End-of-day NAV for international ETFs is calculated at local market close — the ETF may have traded for 10 hours since then, making the premium/discount number stale and misleading.
- For bond ETFs, the "true" NAV is uncertain because individual bonds are infrequently priced — the ETF market price can actually be more accurate than the reported NAV.
- Paying a large premium means you're overpaying for assets — you own less of the underlying per dollar invested.

See also: [[authorized-participant|Authorized Participant]], [[tracking-error|Tracking Error]], [[expense-ratio|Expense Ratio]], [[liquidity|Liquidity]].
