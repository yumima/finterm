**Short selling** is the practice of borrowing shares of a stock you don't own, selling them at the current market price, and buying them back later at (hopefully) a lower price to return to the lender — profiting from a price decline.

Short selling is legal, heavily regulated, and serves important market functions: price discovery, fraud exposure (short sellers famously exposed Enron and Wirecard), and liquidity provision. It also carries unique and asymmetric risks not present in long positions.

## Mechanics

```
1. Investor borrows 1,000 TSLA shares from broker (pays borrow fee)
2. Sells 1,000 shares at $250 each → receives $250,000 cash
3. Cash held as collateral with broker
4. TSLA falls to $180
5. Buy 1,000 shares at $180 → pays $180,000
6. Return 1,000 shares to lender
7. Profit = $250,000 − $180,000 − borrow fees = ~$69,500
```

## Asymmetric risk profile

This is the defining characteristic of short selling:

```
Long position (100 shares at $50):
  Maximum loss: $5,000 (stock goes to $0)
  Maximum gain: Unlimited (theoretically)

Short position (100 shares at $50):
  Maximum gain: $5,000 (stock goes to $0)
  Maximum loss: Unlimited (stock can rise to $500, $5,000, indefinitely)
```

Short sellers have unlimited potential losses — a short squeeze can cause catastrophic damage.

## Short borrow cost

Shorting requires borrowing shares. The annual borrow fee varies by availability:

| Borrow Status | Annual Rate | Examples |
|---|---|---|
| Easy to borrow | 0.1–0.5% | Large-cap S&P 500 stocks |
| Moderate difficulty | 1–5% | Midcap, some sector plays |
| Hard to borrow (HTB) | 5–50% | Small-cap momentum, meme stocks |
| Very hard to borrow | 50–300%+ | GME Jan 2021, AMC |

High borrow costs are a headwind that must be factored into the short thesis.

## Short selling regulations

**Regulation SHO** (U.S., 2005):
- Locate rule: Broker must locate shares before allowing a short sale.
- Close-out rule: Fails to deliver must be closed within specific timeframes.

**Uptick rule (Rule 10a-1, suspended 2007 / circuit breaker version reinstated)**:
- Short sales cannot be executed on a "tick down" — price must be above the most recent sale.
- SEC's alternative uptick rule (Rule 201): Triggered when stock falls 10%+ in a day; restricts shorts to bids above current best bid.

## When short selling is most effective

- Company with accounting fraud or irregularities (Enron, Wirecard, Luckin Coffee).
- Business model disruption (Blockbuster vs. Netflix).
- Excessive valuation with deteriorating fundamentals.
- Heavy insider selling.
- Customer concentration or regulatory risk.

## Risks unique to short selling

1. **Short squeeze**: Covering forced by margin calls can push the price dramatically higher.
2. **Unlimited loss**: No cap on how high a stock can go.
3. **Borrow recall**: Lender can recall shares at any time, forcing premature cover.
4. **Dividend obligation**: Short seller owes any dividends paid by the stock.
5. **Carry cost**: Borrow fee and margin interest are ongoing costs regardless of price movement.

## Pitfalls

- "The market can stay irrational longer than you can stay solvent" — correct short theses can be ruinous if the timing is wrong.
- Stocks can double, triple, or rally 10× before collapsing — holding through requires enormous capital and conviction.
- Tax treatment: Short positions are always taxed as short-term capital gains regardless of how long held.

See also: [[short-interest|Short Interest]], [[days-to-cover|Days to Cover]], [[margin|Margin]], [[margin-call|Margin Call]], [[liquidity|Liquidity]].
