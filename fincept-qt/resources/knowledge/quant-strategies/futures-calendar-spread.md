# Futures Calendar Spread

A **calendar spread** is a long position in one futures expiry and an offsetting short position in another expiry of the same underlying. The trade isolates the **term structure** — the shape of the futures curve — and removes most of the directional exposure to the underlying's overall price level.

This is the bread-and-butter trade of commodity desks and a tool for systematic curve-shape strategies. Kakushadze & Serur (2018) §10.2 give the construction in one paragraph; this entry expands the term-structure dynamics, the seasonality patterns that drive specific commodity calendar spreads, and the historical drawdowns.

## The basic construction

```
Bull spread:  long near-month future,  short deferred-month future.
              Profits if the near-deferred spread widens.

Bear spread:  short near-month future, long deferred-month future.
              Profits if the spread narrows.
```

The book frames it: "for commodity futures, for the most part, near-month contracts react to supply and demand more than deferred-month contracts. Therefore, if the trader expects low (high) supply and high (low) demand, then the trader can make a bet with a bull (bear) spread."

### Why use a spread instead of an outright?

Three reasons:

1. **Reduces volatility.** The two legs partially offset, removing the systematic price-level move. A 5% spike in oil affects both legs similarly; the spread changes less.
2. **Reduces margin.** Exchanges grant **spread margin** that's typically 30–60% of outright margin, because spread positions have lower max-loss.
3. **Isolates specific information.** If your view is on supply/demand at the front of the curve (e.g., immediate inventory), the calendar spread captures it cleanly without confusing you with broader macro.

## Common calendar spread structures

### Commodity calendar spreads

- **Crude oil**: WTI front-month vs. Cal-strip (12-month average), or Mar vs. June. Captures inventory and refinery-maintenance seasonality.
- **Natural gas**: classic "winter-summer" spread — March vs. April (the most-traded calendar spread in any commodity). The price drops sharply at end of winter heating demand.
- **Gasoline**: summer (driving season) vs. winter blend. Specifications differ; the spread captures real economic seasonality.
- **Heating oil**: similar to nat gas — winter vs. shoulder month.
- **Wheat / Corn / Soy**: pre-harvest vs. post-harvest months. Crop seasonality.
- **Live cattle / Lean hogs**: production-cycle-driven spreads.

### Financial calendar spreads

- **VIX futures**: front-month vs. second-month (see [[vol-vix-futures-basis|VIX basis trading]]).
- **Eurodollar futures**: short-term rate expectation spreads (e.g., Jun vs. Sep contracts).
- **Treasury futures**: 10y vs. 30y, 2y vs. 10y curve-shape spreads (related to [[fi-curve-spreads|yield curve spread]] strategies).
- **Equity index futures**: front vs. back-month spreads — usually flat because S&P futures track spot closely.

## Why spreads in commodities are non-trivial

The futures curve shape encodes information that's not in the spot price alone:

- **Inventory levels**: low inventories → curve in backwardation (front month higher than deferred).
- **Storage capacity**: when storage is full (e.g., 2020 March oil), the front month collapses while deferred stays firm — leading to extreme contango.
- **Seasonality**: predictable demand patterns (heating in winter, gasoline in summer) create structural curve shapes.
- **Roll dynamics**: index funds (USO, GSCI) roll mechanically on fixed dates, creating predictable supply/demand for specific calendar pairs.

A calendar-spread trader bets on **curve shape changes** — independently of the underlying's overall direction.

## Specific empirical patterns

### The natural gas March-April spread

The most-famous calendar spread. Natural gas demand peaks in winter (heating). The April contract is the first "shoulder month" with no winter demand priced in. The March-April spread captures end-of-winter inventory dynamics:

- **In tight years** (cold winter, low storage): March-April spread blows out to wide premia. Famous example: 2014 polar vortex, March-April spread reached $0.85+/MMBtu vs. typical $0.10.
- **In loose years** (mild winter): spread compresses or even goes negative.

The systematic trade: enter the long March / short April position in mid-November (pre-winter), exit in mid-February (pre-March expiry). Direction depends on weather forecast and inventory levels.

### Crude oil contango blowouts

When physical oil storage approaches capacity:

- **March 2020**: WTI front-month went **negative** (to −$37/barrel) as Cushing storage was full and front-month longs couldn't take physical delivery. The May-June spread reached $40+/barrel — a once-in-a-lifetime contango.
- **April 2008**: similar smaller event.

These are tail events that can either generate enormous P&L or destroy positions, depending on which side you're on.

### Gasoline seasonality

Summer gasoline (RBOB) is a different specification than winter gasoline (more oxygenates, different vapour pressure). The June vs. December spread reliably trades seasonally:

- Spring rally as gasoline blenders ramp up summer-grade production.
- Fall compression as winter-grade kicks in.

A systematic trade: long summer / short winter, entered in February, exited in May. Has historically had Sharpe ~0.6 with manageable drawdowns.

## Why calendar spreads can be hard

### Roll exposure

When the near contract approaches expiry, you must roll (close near and open new near). This creates timing risk — the spread may move adversely during the roll.

### Margin treatment changes

Exchanges sometimes change margin treatment. A previously low-margin calendar spread can suddenly require higher margin if the exchange perceives elevated risk (e.g., March 2020 oil).

### Liquidity differences

Front contracts are usually more liquid than deferred. The bid-ask of the spread is dominated by the deferred leg's spread. In thin deferred contracts, the trade can be expensive to put on and unwind.

### Crowded positioning

Calendar spreads in major commodities are watched by every desk. Crowded positioning can lead to short-squeeze-like behaviour when positioning unwinds.

## Variants

### Butterfly spread

Three-month structure: long near + long deferred, short two units of middle. Captures **curvature** of the term structure — the analogue of [[fi-butterflies|bond butterflies]] in commodities.

### Crack spread (refined products vs. crude)

Long gasoline + heating oil, short crude oil — captures the refining margin. Strictly not a calendar spread (different products), but operationally similar (multiple futures contracts).

### Inter-market spread

Long one commodity, short another (e.g., long heating oil, short crude). Captures relative-pricing dynamics. The most famous: **gold-silver ratio**, **WTI-Brent** crude spread.

## Empirical record

- Commodity calendar spreads have historically generated **lower Sharpe than outright commodity trend-following** but with **less correlation to broader macro**. Combined into a multi-strategy book, they diversify the trend-following premium.
- **Seasonal calendar spreads** (nat gas March-April, gasoline summer-winter) have the highest Sharpe ratios.
- **Roll-yield-driven spreads** (front vs. second month in contango) capture the roll premium analogous to [[commodity-roll-yield|commodity roll yield]].

## Risk management essentials

- **Spread risk ≠ outright risk**: just because the spread is lower-volatility doesn't mean lower risk. The March 2020 oil example showed spreads can blow out dramatically.
- **Margin sizing for tail events**: budget for 2–3× normal-time margin requirements in stress periods.
- **Maturity matching**: hold the spread through both contracts' liquid windows. Don't carry into expiry on the front leg.
- **Inventory monitoring**: especially for energy and agricultural spreads, weekly EIA / USDA reports drive spread P&L.
- **Crowded-positioning awareness**: large CTAs and roll-fund-aware desks watch the same spreads; size accordingly.

## Common mistakes

- **Treating calendar spreads as "low-risk."** They're lower-vol but have tail risk (March 2020 oil).
- **Ignoring roll dates.** Index funds roll on fixed dates; the spread can move adversely during roll windows.
- **Not adjusting for contract specifications.** Some commodities have different specifications across expiries (RBOB summer vs. winter blends). Treat them as different products.
- **Calendar spreads in illiquid contracts.** Deferred contracts in less-traded commodities can have 5%+ effective bid-ask. The spread arbitrage gets eaten.

## Where to do this in the terminal

- **Futures screen** — calendar spread monitor; seasonal patterns highlighted.
- **AI Quant Lab** — calendar-spread strategy template with seasonality detection.
- **Backtesting** — historical calendar-spread P&L with seasonality and roll-date analysis.

## See also

- [[commodity-roll-yield|Commodity Roll Yield]] — related curve-shape strategy
- [[vol-vix-futures-basis|VIX Futures Basis Trading]] — calendar spread on VIX
- [[fi-curve-spreads|Yield Curve Spreads]] — bond market analogue
- [[futures-contrarian|Futures Contrarian Trading]] — mean reversion in futures
- [[futures-hedging|Hedging with Futures]] — risk-management framework

## External references

- Bessembinder, H. (1992). "Systematic Risk, Hedging Pressure, and Risk Premiums in Futures Markets." *Review of Financial Studies* 5(4), 637–667.
- Adrangi, B., Chatrath, A., Song, F., Szidarovszky, F. (2006). "Petroleum Spreads and the Term Structure of Futures Prices." *Applied Economics* 38(16), 1917–1929.
- Bernstein, J. (1990). *Jake Bernstein's Seasonal Futures Spreads: High-Probability Seasonal Spreads for Futures Traders*. Wiley.
- Castelino, M., Vora, A. (1984). "Spread Volatility in Commodity Futures: The Length Effect." *Journal of Futures Markets* 4(1), 39–46.
- Dunis, C., Laws, J., Evans, B. (2006). "Trading Futures Spreads." *Applied Financial Economics* 16(12), 903–914.
- Girma, P., Paulson, A. (1998). "Seasonality in Petroleum Futures Spreads." *Journal of Futures Markets* 18(5), 581–598.
- Kawaller, I., Koch, P., Ludan, E. (2002). "Calendar Spreads, Outright Futures Positions and Risk." *Journal of Alternative Investments* 5(3), 59–74.
- Frino, A., McKenzie, M. (2002). "The Pricing of Stock Index Futures Spreads at Contract Expiration." *Journal of Futures Markets* 22(5), 451–469.
- Cole, C., Kastens, T., Hampel, F., Gow, L. (1999). "A Calendar Spread Trading Simulation of Seasonal Processing Spreads."
- Hou, A., Nordén, L. (2018). "VIX Futures Calendar Spreads." *Journal of Futures Markets* 38(7), 822–838.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §10.2. https://doi.org/10.1007/978-3-030-02792-6
