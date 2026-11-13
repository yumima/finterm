# Open Interest (OI)

The total number of derivatives contracts (options or futures) that are **currently open** — neither closed by an offsetting trade nor expired.

## Worked example — option chain row

```
SPY 500 Call, Exp 2026-06-19
  Bid:    $4.20
  Ask:    $4.35
  Last:   $4.28
  Volume:  12,450      ← contracts traded today
  OI:     186,330      ← outstanding contracts not yet closed
  IV:     14.2%
```

Today, 12,450 contracts changed hands; 186,330 contracts are alive (open). High OI → liquid strike → tight bid-ask, easy fills. If OI were 50, you'd be the only counterparty — slippage would be brutal.

## Volume vs. open interest

A common confusion:

- **Volume** = how many contracts changed hands today
- **Open interest** = how many contracts are still alive at end-of-day

Buying a contract from someone who already owned it (closing their position) is volume but reduces OI. Buying a *newly created* contract is volume that *increases* OI.

## OI flow signals

| Price | OI | Interpretation |
|---|---|---|
| ↑ | ↑ | New money long; bullish conviction |
| ↑ | ↓ | Short covering; bullish but weak |
| ↓ | ↑ | New money short; bearish conviction |
| ↓ | ↓ | Long liquidation; bearish but weak |

## Where it matters

- **Liquidity**: high-OI strikes have tight bid-ask spreads and easy fills. Low-OI strikes can leave you stuck.
- **Max pain**: a folk theory that price tends to settle at the strike where total option OI loses the most money at expiration. Real or coincidence — debate it.
- **Futures rollovers**: as front-month futures approach expiration, OI shifts to the next month. The day OI inverts often marks the operational rollover.
- **GEX (Gamma Exposure)** — derived from put/call OI; some traders use it to gauge dealer hedging dynamics.

## OI thresholds for retail traders

| OI | Tradability |
|---|---|
| < 50 | Untradable; mid-prices are fiction |
| 50–500 | OK for small size only |
| 500–5,000 | Reasonable for typical retail |
| 5,000+ | Liquid; fills near mid |
| 50,000+ | Institutional-grade liquidity |

## Variants worth knowing

- **Total OI** — sum across all strikes and expirations
- **Weekly vs Monthly OI** — weeklies often have less OI per strike than third-Friday monthlies
- **Put/Call OI ratio** — sentiment proxy; readings >1.0 indicate more bearish positioning
- **OI by strike** — heatmap-friendly view; concentration spikes act as price magnets

## Decision rules

- **Hold-to-expiration position** → require OI > 1,000 per strike
- **Short-dated trade** → require OI > 500 per strike
- **Delta-hedging short option** → require OI > 5,000 (you'll need to roll cleanly)
- **Watch for OI shifts on the day** to detect institutional positioning changes

## In finterm

The Futures and Derivatives screens both display OI columns alongside volume. For options, focus on OI when picking strikes you intend to hold through expiration — execution on a 5-OI contract is often impossible at the displayed mid.
