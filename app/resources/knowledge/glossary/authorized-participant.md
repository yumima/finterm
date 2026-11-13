An **authorized participant (AP)** is a large financial institution — typically a broker-dealer or market maker — that has a contract with an ETF issuer granting exclusive rights to create and redeem ETF shares in large blocks called "creation units."

The AP mechanism is what makes ETFs fundamentally different from mutual funds and closed-end funds — it enables near-perfect arbitrage between the ETF market price and its NAV, keeping them tightly aligned.

## The creation process (when ETF trades at a premium)

```
ETF market price > NAV  (premium)

AP action:
1. AP buys the underlying basket of securities in the market
2. AP delivers the basket to the ETF issuer
3. ETF issuer creates new ETF shares (creation unit, typically 50,000 shares)
4. AP receives new ETF shares
5. AP sells ETF shares in the market at the premium price
6. AP pockets the spread (arbitrage profit)
→ Premium collapses as new supply hits the market
```

## The redemption process (when ETF trades at a discount)

```
ETF market price < NAV  (discount)

AP action:
1. AP buys ETF shares cheaply in the market
2. AP delivers ETF shares to the issuer
3. ETF issuer redeems shares and delivers the underlying basket
4. AP sells the underlying basket at higher NAV prices
5. AP pockets the spread
→ Discount collapses as ETF shares are removed from supply
```

## Why this matters for investors

The AP mechanism provides three key benefits:
1. **Price efficiency**: ETF market price stays very close to NAV.
2. **Tax efficiency**: In-kind redemptions (for equity ETFs) don't trigger capital gains distributions.
3. **Scalability**: New shares are created on demand, so ETFs don't cap at a fixed size.

## Which institutions are APs?

Major APs include Goldman Sachs, Citadel, Jane Street, Virtu Financial, and Morgan Stanley. They are not paid by the ETF issuer; they profit from arbitrage spreads.

## AP mechanism limits

The AP mechanism can fail or slow during:

| Scenario | Issue |
|---|---|
| Bond market stress | Underlying bonds are illiquid; AP cannot source basket easily |
| Foreign market hours | Underlying market is closed; creation/redemption is delayed |
| Market dislocation | Bid-ask spreads on underlying so wide that arbitrage is too costly |
| New ETF types (Bitcoin) | Physical Bitcoin custody creates delays and costs |

During the March 2020 COVID shock, some bond ETFs (HYG, LQD) traded at significant discounts because AP arbitrage slowed — the ETF market price became a more accurate real-time price than the stale bond NAV.

## Pitfalls

- The AP mechanism requires *liquid* underlying markets to function. In illiquid market conditions, premiums and discounts can persist.
- Retail investors cannot directly participate in creation/redemption — only APs can; minimum creation units are typically 25,000–100,000 shares (~$2–10M+).
- Concentration risk: if a small number of APs dominate a particular ETF's arbitrage, temporary withdrawal (e.g., during a crisis) can disrupt NAV convergence.

See also: [[nav-premium-discount|NAV Premium/Discount]], [[tracking-error|Tracking Error]], [[expense-ratio|Expense Ratio]], [[liquidity|Liquidity]].
