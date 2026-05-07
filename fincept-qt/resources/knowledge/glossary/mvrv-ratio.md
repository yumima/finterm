The **MVRV ratio (Market Value to Realized Value)** is a Bitcoin (and broader crypto) on-chain valuation metric that compares the current market capitalization to the "realized capitalization" — an estimate of what all coins in circulation actually cost their current holders.

MVRV reveals whether Bitcoin is trading above or below the aggregate cost basis of all on-chain holders, making it one of the most historically reliable cycle timing tools in crypto.

## Formula

```
MVRV = Market Cap / Realized Cap

Market Cap     = Current Price × Circulating Supply
Realized Cap   = Sum of (each coin × price when it last moved on-chain)
```

Realized cap approximates total "money invested" in the network, since it values each coin at the price when it was last transacted (a proxy for the holder's cost basis).

## Worked example

```
BTC price: $65,000
Circulating supply: 19.7M BTC
Market Cap: 19.7M × $65,000 = $1.28T

Realized Cap (example): $450B
  (reflects that many coins were acquired at lower prices historically)

MVRV = $1.28T / $0.45T = 2.84
```

An MVRV of 2.84 means the average holder is sitting on ~184% unrealized profit.

## Historical MVRV thresholds

| MVRV Range | Historical Signal | Action |
|---|---|---|
| < 1.0 | Below realized cap; holders at loss | Historically strong buy zone |
| 1.0–2.0 | Fair to undervalued | Accumulation range |
| 2.0–3.5 | Elevated; mid-cycle | Hold / reduce risk |
| 3.5–5.0 | Historically overvalued | Distribution zone |
| > 5.0 | Extreme greed; near cycle top | Historically sells signals |

Historical MVRV peaks: 2017 top (~4.8), 2021 top (~3.96).
Historical MVRV troughs: 2018 bear market bottom (~0.4), March 2020 COVID (~0.7).

## MVRV Z-score

A normalized variant that accounts for standard deviation of the spread:

```
MVRV Z-score = (Market Cap − Realized Cap) / Std Dev of Market Cap
```

Z-score > 7 has historically coincided with cycle tops; < 0 with cycle bottoms.

## Limitations

- MVRV assumes coins last moved = coins acquired at that price, which is imprecise (coins can move for non-economic reasons like wallet migrations).
- Works best for Bitcoin; less reliable for altcoins with different supply and exchange dynamics.
- Long-term holders in profit don't necessarily sell; MVRV is a sentiment indicator, not a timing mechanism.
- Exchanges holding customer funds distort realized cap if large batches move at current prices.

## Pitfalls

- MVRV at historical "sell levels" can stay elevated for months during a strong bull run — using it for precise exit timing requires other confirming signals.
- Do not compare MVRV levels across different cryptocurrencies directly; each has its own historical distribution.
- Declining MVRV during price appreciation means new buyers are paying more than old holders — a sign of distribution, not accumulation.

See also: [[hash-rate|Hash Rate]], [[staking-yield|Staking Yield]], [[funding-rate|Funding Rate]], [[market-cap|Market Cap]].
