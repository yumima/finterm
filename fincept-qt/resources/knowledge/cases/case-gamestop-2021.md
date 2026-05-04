# GameStop January 2021 — Short Squeeze + Gamma Squeeze

In late January 2021, **GameStop (GME)** — a struggling brick-and-mortar video-game retailer — went from $20 to **$483 in three weeks**. The squeeze cost short-sellers ~$20B, briefly halted trading on Robinhood, and triggered Congressional hearings.

It is the canonical case study for **how float dynamics + retail coordination + dealer hedging can break "rational" pricing** for a period.

## The setup (early Jan 2021)

GameStop in fall 2020:
- Mall-based retailer, declining business in the streaming era
- Activist investor Ryan Cohen took a 13% stake (Aug 2020) suggesting digital pivot
- Some retail investors saw this as a turnaround thesis
- **Short interest as % of float: ~140%** — more shorts than shares actually available

A 140%+ short ratio is mathematically possible because shorted shares can themselves be re-loaned and shorted again. But it creates **extraordinary squeeze risk**.

## The catalyst — r/wallstreetbets

A subreddit called r/wallstreetbets (WSB) had been watching GME for months. By late January, several factors aligned:

- **A user, "DeepFuckingValue" (Keith Gill)**, had been posting GME analysis since 2019 with growing conviction
- WSB users started buying both stock and **out-of-the-money call options**
- GME stock began climbing on heavy retail volume
- **Short squeeze** kicked in: shorts had to buy back as price rose
- Then **gamma squeeze** kicked in (next section)

## The gamma squeeze mechanics

When retail bought OTM calls, dealers (market makers) sold them and had to **delta-hedge by buying GME stock**. As price rose:

```
GME @ $20:  Dealer short $50 calls have low delta (~0.1)
            Dealer needs to be long ~10% of strike per call

GME @ $50:  Same call has higher delta (~0.5)
            Dealer needs to be long ~50% of strike per call
            → Dealer buys MORE stock to maintain hedge

GME @ $200: Same call has delta ~0.95
            Dealer needs to be long ~95% of strike per call
            → Even more stock buying
```

This created a **feedback loop**:
1. Retail buys calls
2. Dealers buy stock to hedge
3. Stock price rises
4. Calls become more in-the-money → higher delta
5. Dealers buy MORE stock
6. Price rises faster
7. Goto 4

Combined with shorts being forced to cover, the price went vertical.

## The peak and the crash

- Jan 11, 2021: GME at $20
- Jan 22: $65
- Jan 26: $148
- Jan 27: $347
- Jan 28: **$483 intraday peak**

Then:
- Jan 28: **Robinhood and several other brokers restricted GME buying** (claimed clearing-house margin requirements; controversial)
- Stock plunged: $483 → $194 in one day
- Feb 2: Down to $90
- Feb 19: Back below $40

## The casualties

- **Melvin Capital** (Plotkin) — short ~$1B notional GME; lost 53% of fund value in January, took $2.75B emergency capital from Citadel and Point72
- **Maplelane Capital** — lost ~45%
- **Citron Research (Andrew Left)** — closed his short, stopped publishing short reports for years
- Robinhood — emergency $3.4B capital raise in days; class-action lawsuits; reputation hit

## The retail outcome

- A few retail traders made life-changing money — those who sold near the top
- Many more bought near the top and rode it down
- The narrative became "retail vs hedge funds" — both true and oversimplified

## Concepts illustrated

- [[float|Float]] — squeeze potential is float-relative, not absolute
- [[open-interest|Open Interest]] — option OI was at unprecedented levels for GME
- [[gamma|Gamma]] — dealer short-gamma hedging created the feedback loop
- [[delta|Delta]] — option delta dynamics drove the buying cascade
- [[liquidity|Liquidity]] — even deep-pocketed shorts couldn't exit cleanly

## Lessons

1. **Float-adjusted short interest** is the critical metric, not raw short interest.
2. **Crowded short trades** can be deadly even with thesis intact. Plotkin's GME short *thesis* was probably right (the business was declining); the *trade* still cost him 53%.
3. **Gamma feedback loops** are real — and asymmetric. They don't unwind smoothly.
4. **Retail coordination at scale** is now a market force, especially in low-float names.
5. **Brokers can change the rules mid-game.** Robinhood's restrictions changed the trade dynamics decisively.
6. **Position size for tail outcomes** — Melvin was sized as if 5σ couldn't happen.

## Aftermath

- GameStop did its activist-led pivot under Ryan Cohen as Chairman; mixed results
- The SEC issued a 45-page report (Oct 2021) but no major regulatory changes
- "Meme stock" became a permanent market category
- Several similar squeezes since: AMC (May 2021), BBBY (Aug 2022), various biotech short squeezes
- WSB became part of mainstream financial vocabulary

## Read more

- *The Antisocial Network* by Ben Mezrich (2021) — popular account
- SEC Staff Report (Oct 2021): https://www.sec.gov/files/staff-report-equity-options-market-struction-conditions-early-2021.pdf
