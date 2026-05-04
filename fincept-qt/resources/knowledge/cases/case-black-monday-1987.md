# Black Monday — October 19, 1987

The Dow Jones fell **22.6% in a single day** — the largest one-day percentage decline in US stock market history. No major news event triggered it; the crash was driven by **mechanical feedback loops** in then-new "portfolio insurance" hedging strategies.

The case is the canonical study of **how new financial products can interact unexpectedly to create systemic feedback risk**.

## The setup (1987)

The 1980s had been a strong bull market. By August 1987, the Dow was up 44% YTD. Then it began drifting down.

Two new products had become widespread:

### 1. Portfolio Insurance
Marketed to pension funds and institutional investors, "portfolio insurance" promised to protect equity portfolios from large declines using **dynamic hedging with index futures**.

The idea: as the index falls, the strategy progressively sells more S&P 500 futures, replicating a put option synthetically. Theoretically clean.

By late 1987, **~$60–80B** of institutional assets were running portfolio insurance.

### 2. Index Arbitrage
With the introduction of S&P 500 futures (1982), arbitrageurs would exploit price differences between the futures and the underlying basket. Normally a stabilizing force.

## The week before

Wed Oct 14: Dow −3.8%
Thu Oct 15: Dow −2.4%  
Fri Oct 16: Dow −4.6% (worst day in years at that point)

By Friday close, portfolio insurance models were calling for **massive futures sales** at the open Monday.

## Black Monday — the cascade

```
9:30 AM   Sell orders flood the futures market — portfolio insurance kicks in
10:00 AM  S&P futures trading at 5%+ discount to fair value (NYSE specialists can't keep up)
10:30 AM  Index arb: sell stocks, buy futures — but the stock side gets stuck
11:00 AM  Specialists overwhelmed; many stocks halt trading
12:00 PM  More portfolio insurance triggers as price falls; sells more futures
1:00 PM   NYSE specialists running out of capital to absorb selling
2:00 PM   "Air pocket" — buyers disappear; bid-ask spreads explode
3:00 PM   Cascading sell signals all firing at once
4:00 PM   Dow closes at 1738.74 — DOWN 508 POINTS (−22.6%)
```

The S&P 500 fell **−20.5%**. The S&P 500 futures fell **−28.6%** (basis blew out due to market dysfunction).

## The mechanism

The feedback loop was:

```
1. Stocks fall
2. Portfolio insurance models say "sell futures"
3. Futures sales push futures prices down
4. Arb: sell stock, buy futures (to capture spread)
5. Stock selling pushes stock prices down
6. Goto step 2
```

**Each iteration accelerated the next.** Without circuit breakers (which didn't exist), the loop ran for the full trading day.

## What stopped it

- **Tuesday Oct 20 morning**: Fed Chair Alan Greenspan issues a one-sentence statement: *"The Federal Reserve, consistent with its responsibilities as the Nation's central bank, affirmed today its readiness to serve as a source of liquidity to support the economic and financial system."*
- This was code for: **the Fed will lend infinite reserves to banks** to prevent a clearing-house collapse
- Markets stabilized; rallied 5.9% Tuesday and 10.1% Wednesday
- By year-end, the S&P was back near pre-crash levels

## What changed after 1987

- **Circuit breakers** introduced — trading halts on large moves to prevent cascade unwinds
- **Side-by-side trading** between NYSE and futures markets coordinated
- **Portfolio insurance largely abandoned** — strategy didn't survive the test
- **Shadow VIX retroactive computation**: ~150 (highest ever calculated)
- **Stress testing** of clearing systems became a mandatory regulatory practice

## Concepts illustrated

- [[volatility|Volatility]] — single-day moves can dwarf model assumptions
- [[vix|VIX]] — though VIX didn't exist yet, the implied vol that would have been priced was unprecedented
- [[drawdown|Drawdown]] — −22.6% in one day vs typical max DD of −20% over months
- [[liquidity|Liquidity]] — bid-ask spreads exploded; the market literally couldn't absorb the selling

## Lessons

1. **Hedging strategies that work in calm markets can break in stress.** Portfolio insurance assumed continuous markets; it got discrete jumps.
2. **Feedback loops are the mechanism of cascades.** Small triggers + amplification = catastrophe.
3. **New products with widespread adoption = systemic risk** until they've been tested in a crisis.
4. **Liquidity is conditional.** What was a deep market on Friday was a desert on Monday.
5. **Circuit breakers buy time** for human decision-makers to override mechanical loops.

## Modern echoes

The same mechanics have repeated in different forms:

- **August 2007 quant quake** — quant strategies all crowded into similar trades; correlated unwinds
- **Flash Crash May 2010** — algo-driven feedback in 20 minutes
- **August 2015 China shock** — circuit breakers triggered in China; spilled to US
- **Volmageddon Feb 2018** — see [[case-volmageddon-2018]]
- **March 2020 COVID** — see [[case-covid-march-2020]]

The pattern: novel widespread strategy + crowded positioning + trigger event → cascade.

## Read more

- *Brady Commission Report* (Jan 1988) — the definitive postmortem
- *A Demon of Our Own Design* by Richard Bookstaber — broader history of market accidents
