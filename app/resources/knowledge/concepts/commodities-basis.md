# Commodities Basis Trading

In commodity markets, the same physical good can trade at different prices depending on **where**, **when**, and **in what form** it is to be delivered. Basis is the difference between these prices — and understanding it is essential for anyone trading commodity futures, hedging physical exposure, or analyzing commodity ETFs and funds.

## What is basis?

**Basis** is most commonly defined as:

```
Basis = Spot price (cash price) − Futures price
```

Or alternatively (especially in grains and agricultural markets):

```
Basis = Local cash price − Nearest futures contract price
```

The basis is almost never zero, because:

- **Storage costs**: holding physical goods requires warehousing, insurance, and financing
- **Transportation costs**: delivering to the futures contract's delivery point has a cost
- **Quality/grade differences**: local cash prices reflect local grades; futures prices reflect deliverable grades
- **Convenience yield**: holding physical inventory has an option value (you can use it immediately); this premium compresses basis in tight markets

## Contango vs. backwardation revisited

The relationship between spot and forward prices defines the **term structure** of the commodity. See [[futures-mechanics]] for the mechanical treatment; here's the economic interpretation:

**Contango** (futures > spot): Storage costs dominate. The market is saying: "If you want to buy this commodity later, you'll pay more — to cover the cost of storing it until then." This is the "normal" state for most storable commodities with ample supply.

**Backwardation** (futures < spot): Convenience yield dominates. Physical supply is tight; buyers need it now and are willing to pay a premium for immediate delivery. Backwardation typically signals a supply squeeze or strong current demand.

For investors, this distinction is critical because it determines whether holding a long futures position earns or costs the **roll yield**.

## Roll yield: the silent return component

Commodity investors who can't take physical delivery must roll their futures contracts before expiration — selling the near contract and buying the next one. The economics of this roll depend entirely on the curve shape:

**Roll in contango** (most common in normal conditions):

```
Sell near contract (cheaper) → Buy far contract (more expensive)
→ You buy high, effectively losing value each roll
→ Roll yield is negative
```

**Roll in backwardation** (tight supply conditions):

```
Sell near contract (expensive) → Buy far contract (cheaper)
→ You sell high and buy low on each roll
→ Roll yield is positive
```

This explains why commodity ETFs that hold front-month futures (**USO** for oil, **UNG** for natural gas) dramatically underperform spot prices in contango markets. USO lost more than 75% of its value in 2020 partly due to deeply negative roll yield when oil storage was full.

**Total commodity return** = Spot return + Roll yield + Collateral yield (yield on margin collateral)

In contango, you can be right about the direction of spot prices and still lose money because roll costs eat your return.

## Local basis and the hedger's problem

For producers and commercial buyers (a grain elevator, an oil refinery, an airline), basis risk is a daily operational concern.

A Kansas wheat farmer sells CBOT wheat futures to lock in a price. But the CBOT futures price is for delivery in Chicago. The farmer's actual local cash price in Kansas will differ based on:

- Distance from Chicago delivery point (transportation cost)
- Local supply and demand conditions
- Local storage availability
- Quality differences (protein content, moisture)

The **local basis** (Kansas cash − CBOT near futures) will fluctuate. The farmer has eliminated most price risk (the CBOT futures hedge) but retains **basis risk** — uncertainty about the spread between local prices and futures.

Basis risk is generally much smaller than outright price risk, which is why futures hedging is still useful even if imperfect.

## Basis convergence at delivery

A key feature of futures contracts is that **basis must converge to approximately zero at delivery**. If the futures price is significantly different from the spot price at expiration, arbitrageurs will:

- If futures > spot: buy spot, deliver against futures contract, profit on the spread
- If futures < spot: buy futures, receive delivery of physical, sell at spot price

This cash-and-carry arbitrage enforces convergence. The closer to expiration, the tighter the basis, until they converge on delivery day. This convergence is predictable and forms the foundation for much basis trading.

## Basis trading strategies

**Calendar spread trading**: taking a position in the price difference between two delivery months. A trader who believes supply is tightening (contango will shift toward backwardation) might buy the near contract and sell the deferred contract — profiting if the spread narrows.

```
Long Dec crude, Short Mar crude (a Dec/Mar calendar spread)
If Dec/Mar spread goes from -$2.00 to -$0.50, profit = $1.50/barrel
```

**Location basis trading**: in energy markets, price differentials between locations are liquid and tradeable. WTI crude in Cushing, Oklahoma versus physical crude in Houston reflects pipeline capacity and regional supply-demand. Refiners, pipelines, and traders all actively manage these differentials.

**Quality basis**: in grains, the price differential for higher-protein wheat or non-GMO corn reflects premiums above deliverable grade. Farmers with high-quality crops trade these premiums actively.

## Commodity basis for macro investors

For macro investors who don't trade physical commodities, basis provides signals:

**Backwardation = tight physical market**: a commodity in deep backwardation has genuine spot demand above current supply. This is a supportive fundamental backdrop for long futures positions.

**Contango = ample supply / weak demand**: deeply contango markets often reflect storage filling up or slack demand. Long futures positions face headwinds from roll cost.

**Spread between grades**: WTI/Brent spread reflects US-global supply differentials. Brent–Dubai reflects Atlantic Basin vs. Middle Eastern crude quality premiums. These spreads tell you about supply logistics globally.

**EIA inventory reports** (weekly for US oil and gas) are the most market-moving short-term data releases for energy basis, as inventory builds and draws directly affect the term structure.

## Common errors with commodity exposure

- **Buying commodity ETFs and expecting them to track spot price** — in contango markets they won't; check the fund's strategy (front-month vs. diversified tenor)
- **Ignoring roll dates** — large positions rolling near the same time cause predictable front-running; the "Goldman Roll" in the early 2000s was extensively arbitraged
- **Treating basis risk as negligible** — in agricultural commodities, local basis can swing by $0.50–$1.50/bushel; this can exceed the hedging gain
- **Confusing backwardation with a bullish signal universally** — some commodities (gold, silver) are almost always in contango because storage and financing costs are low and forward supply is reliable; backwardation in gold is unusual and often signals specific delivery stress

## In finterm

The Futures screen shows the full contract chain for major commodities, letting you visualize the term structure (contango or backwardation) at a glance. Compare the spread between front-month and second-month contracts to the 12-month spread for an overview of curve shape. The roll date is displayed on each contract — size positions accordingly ahead of major roll windows.
