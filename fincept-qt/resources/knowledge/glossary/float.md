# Float

The number of shares **actually available to trade** on the open market, excluding restricted shares: insider holdings, employee equity not yet vested, government stakes, lockup-period shares, etc.

```
Float = Shares Outstanding − Restricted Shares − Insider Holdings (locked) − Govt Stakes
```

## Why float matters

[[market-cap|Market cap]] tells you the total equity value; **float tells you how much of it actually trades**. A stock with 1B shares outstanding but only 100M float is functionally a small-cap from a liquidity perspective.

## Worked example — float vs market cap

```
Snowflake (SNOW) at IPO (2020):
  Shares outstanding:  280M
  Public float:        ~28M (only 10% offered)
  Day-1 market cap:    $70B
  Effective tradable cap: ~$7B

This explains the ~100% pop on IPO day:
  280M shares pricing model assumes liquid distribution
  28M actual float meant insufficient supply for institutional demand
```

## Float categories

| Float | Example | Behavior |
|---|---|---|
| **Mega-float** (>1B shares) | AAPL, MSFT | Highly liquid; can absorb large institutional flows |
| **Large** (100M–1B) | Most S&P 500 | Liquid; institutional-grade |
| **Mid** (20–100M) | Russell mid-caps | Tradable; size positions carefully |
| **Small** (5–20M) | Small-cap, post-IPO | Volatile; vulnerable to single-flow moves |
| **Tiny** (<5M) | Some IPOs, OTC, post-spinoff | Squeeze territory; manipulation risk |

## Float and short squeezes

When a stock has a **small float and high short interest**:
```
Short interest as % of float:
  GameStop (Jan 2021):   ~140% (more shorts than float!)
  AMC (Jun 2021):        ~25%
  Volkswagen (Oct 2008): ~13%, but with Porsche corner = effective 100%
```

When the squeeze starts, shorts must buy back shares from a tiny supply — price can rip 100s of percent. **Float-aware short interest is the key metric**, not raw short interest.

## Lockup expirations

After IPO, insiders typically can't sell for 90–180 days (the **lockup period**). Lockup expiration releases new float onto the market — often pressures price downward.

```
IPO date: Day 0
Lockup ends: Day 180

Day 179: $X (with low float)
Day 180: insiders can sell; supply increases
Day 200: price often -10 to -20% as new shares hit market
```

## Index inclusion mechanics

Major indexes (S&P 500, MSCI) use **float-adjusted market cap** to weight constituents. This means:
- Companies with low float get smaller index weight than market cap would suggest
- Index inclusion mechanically attracts ~5–10% of float in passive demand
- Float reductions (buybacks, increased insider holdings) can pressure index weighting

## Pitfalls

- **Confusing float with shares outstanding** — common error in screening
- **Treating market cap as proxy for liquidity** — misses concentration of holdings
- **Ignoring lockup calendars** for recent IPOs — can be a 6-month overhang
- **Founder ownership transitions** — gradual selling vs sudden block sale
- **Stock splits don't change float meaningfully** — both numerator (price) and denominator scale

## Decision rules

- **Float < 20M shares + high short interest** → squeeze risk in either direction
- **Recent IPO with lockup expiring** → expect supply shock
- **Buyback + low float** → mechanical price support; over time, float shrinks
- **Insiders own > 50%** → understand alignment; also understand minority-shareholder leverage
- **Float-adjusted ADV** is the right liquidity number, not raw share volume

## Historical examples

- **Volkswagen 2008** — Porsche cornered ~74%, leaving tiny effective float; squeeze made VW briefly the world's largest market cap
- **GameStop 2021** — short interest > 100% of float; squeeze cost Melvin Capital 53%
- **Snowflake IPO 2020** — only 10% float = lock-up overhang for months
- **Lyft IPO 2019** — pop and crash partly because of low effective float during early trading

## In finterm

Equity Research → Share Data shows float, shares outstanding, and float as % of market cap. Track lockup expirations on recent IPOs in the calendar.
