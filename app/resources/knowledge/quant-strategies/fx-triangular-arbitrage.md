# FX Triangular Arbitrage

Triangular arbitrage is the **textbook example of a real arbitrage in financial markets**: if quoted rates between three currencies don't agree, walking around the triangle nets you a risk-free profit. It is also the textbook example of an arbitrage that **doesn't survive at retail latency** — by the time a human notices, the opportunity has been taken by an HFT.

Kakushadze & Serur (2018) §8.5 give a compact, correct mathematical treatment. This entry expands the micro-structure context, the realistic latency requirements, and the cousins of the trade that *are* tradeable at slower speeds.

## The setup

Take three currencies A, B, C. You can convert directly A↔B, B↔C, A↔C. The market quotes each with a bid and an ask (the spread is the dealer's margin):

```
Bid(A → B) = price at which you sell A and receive B
Ask(A → B) = price at which you buy A using B
```

Identity (no arbitrage):
```
Bid(B → A) = 1 / Ask(A → B)
Ask(A → B) = 1 / Bid(B → A)
```

Now consider walking the chain A → B → C → A. Starting with 1 unit of A, you end with:

```
R(A → B → C → A) = Bid(A → B) × Bid(B → C) × [1 / Ask(C → A)]   (Eq. 8.17)
```

- If `R > 1` — you have made a riskless profit by walking the chain.
- If `R < 1` — try the reverse chain A → C → B → A. If that's > 1, take that direction. If both are < 1, no opportunity.

Because the bid-ask spreads themselves drag `R` below 1, even small mispricings vanish into the spread. The trade only exists when **`R > 1 + total round-trip transaction cost`**, which in major-currency spot FX means the gap must be 2–3 basis points or more.

## Why it exists at all

In an idealised continuous market, triangular arbitrage cannot exist. In reality:

- **Different liquidity providers** post quotes asynchronously.
- **Different venues** (EBS, Reuters, single-bank platforms) have different inventory and latency.
- **News events** create momentary dislocations as quote engines re-price at different speeds.
- **Cross-currency vs. major routing.** EUR/USD and USD/JPY are the most liquid; EUR/JPY is a derived cross. The cross will sometimes lag the majors by milliseconds.

So the opportunity is real, but it lives at the **microsecond-to-millisecond timescale** in major pairs and at the second-to-minute timescale in less-liquid crosses.

## Why retail can't do this in major pairs

The latency budget for triangular arbitrage in EUR/USD/JPY:

| Stage | Time |
|---|---|
| Detect mispricing | Quote stream → trading engine, ≤ 100 µs |
| Decision | Strategy logic, ≤ 50 µs |
| Order to venue | Network round-trip, 100–500 µs colocated, ≥ 50 ms retail |
| Order matched | Variable |
| All three legs filled | Depends on liquidity |

If your retail platform has 20–100 ms round-trip latency and quote updates of ~50–100 ms, the opportunity is gone before your second leg fires. The classic failure: you execute leg one, the price moves, your second-leg quote is stale, you complete the chain at a *loss*.

Akram, Rime, Sarno (2008) "Arbitrage in the Foreign Exchange Market: Turning on the Microscope" documents this. They find triangular arbitrage opportunities of meaningful size, but **median lifetime is well under one second** in major pairs.

## Where it's still tradeable — and at what speed

The trade is alive at different speeds in different venues:

- **HFT in major pairs.** Co-located, FPGA-driven, sub-microsecond. This is a real strategy for a small number of firms. Net spreads after costs are tiny but the volume is enormous.
- **Mid-frequency in emerging-market crosses.** Pairs like ZAR/JPY, MXN/JPY, BRL/USD via cross routes can dislocate for **seconds to minutes** during off-peak hours. Tradeable by a sophisticated mid-frequency desk.
- **Multi-currency arbitrage (footnote 11 in the book).** Generalising past 3 currencies — a 4- or 5-currency loop — increases the number of potential opportunities but exponentially increases the latency budget and the leg-risk.

## Leg risk — the trade's real cost

The math assumes simultaneous execution of all three legs. Reality:

- **Leg 1 fills**, locking you into the first conversion.
- **Leg 2 attempt** — by now, the quote has moved. You may not fill at the price you assumed.
- **Leg 3** — same.

If any leg fills off the assumed price, your "riskless" arbitrage has turned into a directional position. This is **leg risk**, and it's the single biggest reason why retail/manual triangular arbitrage almost always loses money in practice even when the static math looks profitable.

Three mitigations:

1. **Atomic execution at one venue.** Some prime brokers offer "all-or-none" multi-leg orders. Use them.
2. **Quote staleness check.** Don't enter unless the underlying quotes on all three pairs are time-stamped within ~10 ms of each other.
3. **Conservative threshold.** Require `R − 1` to exceed `3 × your worst-case slippage estimate`, not just the round-trip spread.

## Variants the book hints at

Footnote 12 references multi-currency arbitrage. The natural generalisations:

- **N-leg arbitrage**: A → B → C → ... → A. More legs, more potential mispricings but compounded latency risk.
- **Cross-venue arbitrage**: same pair, different venues. EUR/USD on EBS vs. CME futures, or vs. a single-bank platform. Different beast — really a venue-microstructure trade.
- **CIP-violation arbitrage**: not a triangle but a spot-forward-rate-differential cycle. After 2008 this opened up across G10 currencies (Du, Tepper, Verdelhan 2018), but only banks with cheap dollar funding could profitably trade it.

## What this section is actually useful for

For most readers, triangular arbitrage is **not a strategy to deploy** but a strategy to *understand*:

- It anchors your intuition about what a "real" arbitrage looks like.
- It explains why FX spreads are tight — HFTs enforce no-arbitrage continuously.
- It calibrates your expectations: if a "free lunch" is advertised, ask why HFTs haven't already eaten it.

## Common mistakes

- **Confusing static arbitrage opportunity with executable arbitrage.** The static math says R > 1; the executable math says R > 1 + slippage + leg risk. Almost never true at retail latency.
- **Trying it manually with three browser tabs.** You will lose money. Reliably. The book politely says "fast market data and trade execution systems are critical" — that is a euphemism.
- **Ignoring the bid-ask asymmetry.** Beginners use mid-prices in their math. You can only trade at the bid (selling) or ask (buying). Always include spreads.

## Where to do this in the terminal

- **FX screen** — live cross-pair monitor; the screen flags potential triangular-arb conditions visually but does not place orders automatically.
- **Backtesting** — runs the triangular-arb strategy with realistic bid-ask spreads and latency assumptions, so you can see *why* it loses at retail speed.

## See also

- [[fx-carry-trade|FX Carry Trade]] — a real-premium FX strategy, the opposite end of the latency spectrum
- [[pairs-trading|Pairs Trading]] — also a "spread" trade but at much slower timescales
- [[strategy-patterns|Strategy Patterns]] — where arbitrage fits among the five archetypes

## External references

- Akram, Q., Rime, D., Sarno, L. (2008). "Arbitrage in the Foreign Exchange Market: Turning on the Microscope." *Journal of International Economics* 76(2), 237–253.
- Aiba, Y., Hatano, N., Takayasu, H., Marumo, K., Shimizu, T. (2002). "Triangular Arbitrage as an Interaction Among Foreign Exchange Rates." *Physica A* 310(3–4), 467–479.
- Fenn, D., Howison, S., McDonald, M., Williams, S., Johnson, N. (2009). "The Mirage of Triangular Arbitrage in the Spot Foreign Exchange Market." *International Journal of Theoretical and Applied Finance* 12(8), 1105–1123.
- Du, W., Tepper, A., Verdelhan, A. (2018). "Deviations from Covered Interest Rate Parity." *Journal of Finance* 73(3), 915–957.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §8.5. https://doi.org/10.1007/978-3-030-02792-6
