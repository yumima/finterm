# Implied Volatility (IV)

The volatility that, when plugged into an option pricing model, produces the option's current market price.

It's the market's **forward-looking guess** at how much the underlying will move between now and expiration.

## Worked example — earnings IV spike

```
AAPL stock:                $175
At-the-money 30-day call:  trading at $9.20
Solving Black-Scholes for σ:  IV ≈ 32%

Two days before earnings, same option trades at $11.50 → IV ≈ 42%
The day after earnings, stock barely moves, option drops to $7.30 → IV crashed to 24%
```

A long-call holder who was right about direction (flat) still lost ~$2.20 per share — the *implied vol crush* (42% → 24%) outweighed any small directional move. That's why earnings options are dangerous: you must be right about direction *and* magnitude.

## Where it shows up

- **Option premiums**: high IV → expensive options. Low IV → cheap options.
- **IV rank / IV percentile**: where current IV sits versus its 1-year range. IV rank of 80 means today's IV is higher than 80% of the past year.
- **Volatility surface**: IV plotted across strikes and expirations — usually a "smile" or "skew."

## Typical IV ranges

| Underlying | Low IV | Normal | High IV |
|---|---|---|---|
| SPY (S&P 500 ETF) | 10–14% | 15–22% | 30–50% |
| Single mega-cap (AAPL/MSFT) | 18–24% | 25–35% | 50–80% |
| High-beta tech (TSLA/NVDA) | 30–40% | 45–60% | 80–120% |
| Crypto majors (BTC) | 50–70% | 70–100% | 120%+ |
| Biotech ahead of FDA | — | 60–100% | 200%+ |

## Variants worth knowing

- **At-the-money IV (ATM IV)** — most-cited single number per expiration
- **Skew (smile)** — IV varies by strike; OTM puts usually pricier than calls (crash protection demand)
- **Term structure** — IV across expirations: contango (long IV > short) or backwardation
- **Forward IV** — implied IV between two expirations
- **Volga / vanna / vomma** — second-order Greeks measuring IV sensitivity

## What IV does *not* tell you

**Direction.** High IV means "big moves expected" — not which way. A stock with IV spike ahead of earnings is signaling that the post-print move will be large; selling premium captures the rich vol if you're right that the move is overpriced.

## Practical heuristics

- Before earnings, IV climbs. The day after, IV typically collapses ("vol crush") — bad for long option holders even if direction is right.
- IV that diverges from realized vol is the single biggest input in options strategy choice:
  - **IV >> HV** → favors selling premium
  - **IV << HV** → favors buying premium

## Decision rules

- **IV rank > 70 + thesis the move is overstated** → sell premium (credit spread, iron condor)
- **IV rank < 30 + catalyst expected** → buy premium (long call/put, debit spread)
- **IV term structure inverted** → market expects near-term shock; reduce gamma
- **Skew rich on puts** → crash hedging demand; can sell puts only if you'd own the underlying

## In finterm

The Derivatives screen shows IV per strike. Surface Analytics renders the full IV surface across strikes and expirations — useful for spotting cheap/rich pockets.

## Pair with

- [[volatility]] — the realized counterpart
- [[vix|VIX]] — the headline equity IV index
