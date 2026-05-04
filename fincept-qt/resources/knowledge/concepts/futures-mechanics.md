# How Futures Actually Work

A **futures contract** is a standardized agreement to buy or sell a specific quantity of an asset at a specific price on a specific future date. Unlike forwards, futures trade on exchanges and clear through a central counterparty.

## What "standardized" means

Each contract has a fixed:
- **Underlying** — e.g., 1,000 barrels of WTI crude
- **Contract size / multiplier** — e.g., E-mini S&P at $50 × index
- **Tick size** — minimum price increment
- **Delivery month and last trading day**
- **Settlement type** — physical delivery (oil, grain) or cash (S&P futures)

Always check the contract specs before trading. A "1 contract" position can mean very different notional sizes depending on the product.

## Common contracts and their notional values

| Contract | Symbol | Multiplier | Notional @ typical price | Initial margin |
|---|---|---|---|---|
| E-mini S&P 500 | ES | $50 × index | $5,000 × 5,200 = $260,000 | ~$13,000 (5%) |
| Micro E-mini S&P | MES | $5 × index | $26,000 | ~$1,300 |
| WTI Crude Oil | CL | 1,000 barrels | $78 × 1,000 = $78,000 | ~$6,200 (8%) |
| Micro WTI | MCL | 100 barrels | $7,800 | ~$620 |
| 10-Year T-note | ZN | $100,000 face | ~$110,000 | ~$1,500 (1.4%) |
| Gold | GC | 100 oz | $2,400 × 100 = $240,000 | ~$11,500 |
| Bitcoin | BTC | 5 BTC | $500,000 | ~$80,000 (16%) |

Micro contracts (1/10 size) are how retail traders learn without 5–10× leverage on their first trade.

## Margin — the leverage

You don't pay full notional value to enter a futures position. You post **initial margin** (typically 5–15% of notional) as collateral.

```
Initial Margin    — required to open the position
Maintenance Margin — minimum equity to keep it open
```

If your position moves against you and equity drops below maintenance, you get a **margin call**: post more cash or the broker liquidates. In fast markets this can happen the same day.

### Worked example — margin call

```
Account:           $25,000
Buy 1 E-mini ES:   index 5,200, $260,000 notional
Initial margin:    $13,000
Equity remaining:  $12,000
Maintenance:       $11,800

Next day, ES drops 30 points (-0.58%):
  P&L: -30 × $50 = -$1,500
  Equity: $12,000 - $1,500 = $10,500   ← below maintenance
  → Margin call: post $2,500 or broker closes position
```

## Mark-to-market — daily P&L settlement

Every trading day, your position's gain or loss is **debited or credited to your account in cash**. There's no "paper loss" sitting on your books — losses become real every day at settlement.

This is the single biggest practical difference from holding stock. A leveraged futures position that "would have been fine if I'd held" can still wipe out your account because the margin call comes before the recovery.

## Rolling

A futures contract has a finite life. To maintain exposure, you must **roll** before expiration — close the front-month and open the next contract. Roll cost depends on the [[basis|basis]] and term structure ([[contango]] vs [[backwardation]]).

For long-only commodity exposure, roll cost can dominate spot price changes over time. This is why many commodity ETFs underperform their underlying.

## Tax treatment (US: Section 1256)

Most regulated futures contracts get favorable **60/40 tax treatment**: 60% of gains taxed at long-term capital gains rate, 40% at short-term — *regardless of holding period*. This is materially better than equity short-term gains taxed entirely as ordinary income.

## Position sizing the right way

Don't size by margin used — size by **total notional**. A $50,000 account with $5,000 margin holding a $250,000 oil futures position is taking 5× leverage. A 5% adverse move = 25% of account equity gone.

| Aggression | Notional / equity | Typical use |
|---|---|---|
| Beginner | < 25% | Paper-trading + first live trades |
| Conservative retail | 25–100% | Hedging existing portfolio |
| Moderate retail | 100–300% | Active directional |
| Aggressive | 300–700% | Day-traders / scalpers |
| Professional | varies | Full risk-management infrastructure |

## Decision rules

- **First futures trade** → use micro contracts; sized so daily 1σ move < 1% of account
- **Hold past Fed/CPI day** → either size down or hedge intra-event
- **Holding overnight** → confirm overnight margins (usually higher than intraday)
- **Roll cost > 1%/month** → consider equity proxies or spot ETFs instead

## Famous futures blowups

| Event | Loss | Cause |
|---|---|---|
| Hunt brothers silver corner (1980) | $1B+ | Levered silver corner; CME changed rules |
| Long-Term Capital Management (1998) | $4.6B | Levered convergence trades + Russian default |
| Amaranth (2006) | $6.6B | Single trader, levered nat gas spreads |
| ICAP / Amaranth nat gas (2006) | $6B | Position too big for the market |
| Negative WTI (Apr 2020) | many small accounts | Tank shortage → futures went to −$37 |

## In finterm

The Futures screen shows contract specs, margins, and the term structure curve. Use the curve to assess roll cost before committing to a hold.
