The **funding rate** is a periodic payment exchanged between long and short holders of perpetual futures contracts — a mechanism that keeps the perpetual contract price anchored to the spot price of the underlying asset.

Unlike traditional futures, perpetual contracts have no expiration date. The funding rate compensates for this by periodically transferring value from the "over-crowded" side to the "under-crowded" side, discouraging one-sided positioning.

## How it works

```
If perpetual price > spot price → longs pay shorts (positive funding)
If perpetual price < spot price → shorts pay longs (negative funding)
```

Funding is settled directly between positions, not with the exchange. The rate is recalculated every 8 hours on most exchanges (Binance, Bybit) or continuously (BitMEX hourly).

## Formula

```
Funding Rate = Clamp(Interest Rate + Premium Index, −0.05%, +0.05%)

Interest Rate: fixed platform component (typically 0.01%/8hr)
Premium Index: (Mark Price − Spot Index) / Spot Index
```

The clamp caps funding within exchange-defined limits to prevent extreme rates.

## Worked example

```
BTC spot index:    $65,000
BTC perp mark:     $65,600 (premium of +0.92%)
Calculated funding: +0.083% per 8-hour period
Annualized:         0.083% × 3 × 365 = ~91% annualized

A long position of $100,000 notional pays:
$100,000 × 0.083% = $83 every 8 hours
```

## Funding rate as a sentiment indicator

| Funding Rate (8hr) | Signal |
|---|---|
| > +0.05% | Extreme leverage long; crowded; correction risk |
| +0.01% to +0.05% | Bullish but moderate |
| ~0% | Neutral; balanced positioning |
| −0.01% to −0.05% | Bearish sentiment; shorts dominant |
| < −0.05% | Extreme short positioning; short squeeze risk |

## Cash-and-carry trade (funding arbitrage)

When funding is persistently positive, arbitrageurs profit by:
1. Buying spot BTC.
2. Shorting the perpetual contract in equal size.
3. Collecting funding payments from longs every 8 hours.
4. Net exposure: zero (delta neutral), earning funding yield.

This trade is called a "basis trade" or "cash-and-carry" in crypto and narrows extreme funding rates over time.

## Pitfalls

- High positive funding is a lagging indicator of crowded longs — a correction may have already started by the time you notice extreme readings.
- Negative funding does not always mean "buy" — in a genuine bear market, funding can stay negative for months.
- Funding rates differ significantly between exchanges; arbitrage opportunities exist but require cross-exchange execution.
- Perpetual contracts are not available to U.S. retail investors on most platforms (regulatory restriction).

See also: [[liquidation-price|Liquidation Price]], [[open-interest|Open Interest]], [[basis|Basis]], [[contango|Contango]].
