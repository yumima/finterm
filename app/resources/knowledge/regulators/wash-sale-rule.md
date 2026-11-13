# IRS Wash Sale Rule

The **wash sale rule** (IRC Section 1091) prevents investors from claiming a tax loss on a security sale if they buy the "substantially identical" security within 30 days before or after the sale. The intent is to prevent investors from harvesting tax losses while maintaining economic exposure to the position.

The rule is enforced by the IRS, not a market regulator. It is purely a tax provision — selling and immediately repurchasing is legal market activity; the wash sale rule simply disallows the loss deduction.

## Who it covers

The wash sale rule applies to:

- Individual investors and taxable accounts
- Corporate accounts
- Different accounts owned by the same taxpayer count — selling in your brokerage account and buying in your IRA within 30 days triggers a wash sale
- Spouses' accounts are treated as a single taxpayer in some interpretations

It does **not** apply (yet) to:
- **Cryptocurrency** — as of 2025, the IRS has not formally extended wash sale rules to crypto (though legislative proposals exist). This creates a tax planning opportunity: crypto losses can be harvested and immediately repurchased.
- Tax-deferred accounts only using losses for deduction (losses inside IRAs are generally not deductible regardless).

## The 61-day window

The "30 days before or after" rule creates a **61-day window** of restriction:

- Sale date
- 30 days before the sale
- 30 days after the sale

If you buy the substantially identical security anywhere in that 61-day window, the loss is disallowed.

## What happens to the disallowed loss

The loss is **not permanently lost** — it is added to the cost basis of the replacement shares. This defers, rather than eliminates, the loss. The holding period of the replacement shares also includes the holding period of the sold shares.

**Example:** You buy 100 shares of AAPL at $200. The stock falls to $150 and you sell, realizing a $50/share loss. You buy back AAPL two weeks later at $155.

- Loss of $50/share is disallowed
- New cost basis in the repurchased shares: $155 + $50 = $205 (the disallowed loss is added)
- When you eventually sell at a gain or loss, the full economic loss will be captured

## Substantially identical

"Substantially identical" is not precisely defined but generally means:

- The exact same stock or bond
- Options to buy the same stock (deep in-the-money calls can qualify)
- Futures contracts on the same security

**Not substantially identical:**
- A different company's stock in the same sector (selling GOOGL and buying META)
- An ETF replacing a single stock (selling Apple stock, buying QQQ — debatable in some edge cases)
- Similar but non-identical ETFs (selling SPY and buying IVV is a gray area the IRS hasn't definitively resolved)

## Why traders care

1. **Tax-loss harvesting** requires careful scheduling — avoiding repurchase for 31+ days, or switching to a similar but not identical position.
2. **Year-end complexity** — selling losers in late December while repurchasing in January can trigger wash sales if the 30-day look-back reaches into December.
3. **Crypto loophole** — as long as wash sale rules don't cover crypto, harvesting BTC or ETH losses and immediately rebuying is a legal tax optimization strategy.
4. **Mutual fund year-end distributions** — selling a fund that is about to distribute a capital gain, then repurchasing after the distribution date, is a wash sale if repurchase is within 30 days.

## Common violations and gotchas

- **IRA repurchase triggers wash sale** — selling at a loss in a taxable account and buying the same security in your IRA within 30 days disallows the loss permanently (not just deferred, because IRAs don't have basis tracking the same way).
- **Dividend reinvestment** — DRIP plans that automatically repurchase shares can inadvertently trigger wash sale rules.
- **Broker reporting is incomplete** — brokers report wash sales within the same account, but cross-account wash sales (brokerage + IRA, or multiple brokerage accounts) are your responsibility to track.
- **Tax year doesn't reset** — December sales with 30-day look-forward carry into January of the next tax year.
