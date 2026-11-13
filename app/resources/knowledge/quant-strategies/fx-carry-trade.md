# FX Carry Trade

The carry trade is the simplest cross-asset strategy that still has its own academic literature: **borrow in a low-interest-rate currency, invest in a high-interest-rate currency, take the rate differential as your return**. That it works on average is one of the most-documented empirical anomalies in international finance. That it crashes when it crashes is the other half of the story — and the reason the trade has not been arbitraged away.

This entry covers the Kakushadze–Serur (2018) §8.2–8.4 treatment, expanded with the economic intuition the book leaves out, plus the empirical evidence on when carry pays and when it gives back years of return in a quarter.

## What the trade is, in one paragraph

If 1-month USD rates are 4% and 1-month JPY rates are 0%, then either:
- The yen will *appreciate* by enough to wipe out the 4% gap (this is what theory — **Uncovered Interest Rate Parity, UIRP** — predicts); or
- It will not, and a USD holder who borrowed yen pockets the difference.

UIRP says the first; the data says the second, on average, in developed markets, between roughly 1976 and 2007. This is the "forward premium puzzle" (Fama 1984; Hansen & Hodrick 1980), and the carry trade is the strategy that monetises it.

## The math, written so it actually makes sense

### Uncovered Interest Rate Parity (the textbook claim)

```
(1 + r_d) = E_t[ S(t+T) ] / S(t)  ·  (1 + r_f)        (Eq. 8.4)
```

Reading this in English: *the* expected return on holding the domestic currency equals the expected return on holding the foreign currency once you've converted at the future spot rate. `S(t)` is the spot FX rate (units of domestic per 1 unit foreign), `r_d` and `r_f` are domestic and foreign risk-free rates over horizon `T`.

Rearrange and you get the UIRP forecast of the future spot:
```
E_t[ S(t+T) ] = S(t) · (1 + r_d) / (1 + r_f)
```

If `r_d > r_f`, UIRP says the domestic currency must depreciate (`S` rises = more domestic units per foreign unit). The data says it doesn't, on average.

### Covered Interest Rate Parity (the no-arbitrage identity)

The *forward* FX rate `F(t,T)` is locked at trade time:
```
F(t,T) = S(t) · (1 + r_d) / (1 + r_f)                  (Eq. 8.5)
```

This is **CIRP**, and unlike UIRP, it holds to a few basis points in liquid currency pairs because violating it would be a true arbitrage. CIRP failures during 2008 and 2016+ are themselves a separate trade (see Du–Tepper–Verdelhan 2018 on the post-crisis CIP basis).

### The trader's signal: the forward discount

Take logs of CIRP:
```
D(t,T) = s(t) − f(t,T) = ln(1+r_f) − ln(1+r_d) ≈ r_f − r_d   (Eq. 8.6, 8.7)
```

So the **forward discount** is approximately the *foreign-minus-domestic* rate. The carry signal is just that rate gap, no model needed.

### The cross-sectional rule (high-minus-low)

For a basket of currencies `i = 1, ..., N`:

```
1. Each rebalance (typically monthly), compute D_i for every currency.
2. Sort. Long the top quantile (high foreign rates).
3. Short the bottom quantile (low foreign rates).
4. Equal-weight the legs. Hold one month. Roll.
```

This is the **HML-Carry** factor of Lustig–Roussanov–Verdelhan (2011). They built it on G10 + EM currencies and showed a long-run Sharpe of roughly 0.6–0.9 (varies by sample window) before transaction costs.

## Why does this work? Three competing stories

The carry premium is one of the most-modelled puzzles in international finance. The three live explanations:

### 1. Peso problem / crash risk (Brunnermeier–Nagel–Pedersen 2008)

Carry trades earn small positive returns most months and occasional large negative returns during global risk-off events. The unconditional Sharpe looks great because the disaster has only happened a handful of times (1998, 2008, August 2015, March 2020). You are being paid to provide insurance against currency crashes — and the price of that insurance happens to be persistent.

Test: carry returns are negatively skewed and load on the VIX. They do. This is empirically well established.

### 2. Time-varying risk premia (Lustig–Verdelhan 2007)

High-rate currencies have higher consumption-growth betas — they pay less when global consumption growth disappoints. Investors demand a premium to hold them. Carry is compensation for systematic macro risk.

### 3. Limits to arbitrage / slow-moving capital

Even if carry profits are not pure risk premia, the institutional friction of running a leveraged short-yen / long-AUD position (margin, counterparty, accounting volatility) keeps the trade away from balance-sheet-constrained players. The premium is what you charge for being willing to wear that risk on your book.

You don't have to pick one. They all probably explain a piece of it.

## Variants the book covers

| Strategy | What's different | Book section |
|---|---|---|
| **Single-pair carry** | One currency vs USD, hold the forward; pure UIRP failure trade | §8.2 (Eq. 8.5) |
| **HML-Carry (cross-sectional)** | Long top quantile / short bottom by forward discount; equal weight | §8.2 end |
| **Dollar Carry** | Long *all* foreign currency forwards when avg D̄(t,T) > 0; short all when < 0; bets on USD vs. rest-of-world | §8.3 (Eq. 8.8) |
| **Momentum + Carry combo** | Minimum-variance blend of FX trend signal and carry; weights from `w₁ = (σ₂² − σ₁σ₂ρ)/(σ₁² + σ₂² − 2σ₁σ₂ρ)` | §8.4 (Eq. 8.15–8.16) |

The momentum + carry combo formula is just **minimum-variance combination of two return streams**, and it works because carry and FX momentum are roughly *uncorrelated* — diversifying across them raises the Sharpe of the combined book (Asness–Moskowitz–Pedersen 2013 "Value and Momentum Everywhere" applies the same idea across asset classes).

## Where it works (and where it broke)

- **G10 currencies, 1976 to mid-2007.** Carry was a quietly excellent diversifier. Sharpe ~0.7 net of costs in Lustig–Verdelhan's sample.
- **Aug 2008 to Mar 2009.** The trade gave back 4–5 years of returns in 7 months as JPY and CHF rallied violently against AUD, NZD, MXN, ZAR. This is the canonical "carry crash."
- **2009–2015.** Recovered, but at lower Sharpe (~0.3–0.4 net), as G10 rate dispersion collapsed under unified ZIRP. There was essentially nothing to long against the funding currencies.
- **EM-funded carry, 2015 onward.** Still has spreads, but China-funded carry, TRY/USD blowups, and broad EM currency drawdowns have made it a much harder trade. The persistent premium is now mostly in countries where convertibility risk is itself the constraint.
- **2022 USD spike.** Long-USD carry against everything except CHF worked exceptionally; the Dollar Carry strategy (§8.3) had a strong year.

The empirically *honest* statement: carry pays a premium for tail risk, that premium shrinks when the tail comes home, and the developed-market spread that drove the trade pre-GFC is mostly gone. The trade is alive but smaller.

## Risk management essentials

The crash-risk profile means a naïve unhedged carry book is *not* a buy-and-hold strategy. Live operators do some combination of:

- **VIX or vol filter.** Brunnermeier–Nagel–Pedersen show carry crashes are predicted by spikes in VIX and FX-implied vol. A simple "exit when VIX > 25" rule cut the 2008 drawdown roughly in half historically (Daniel–Hodrick–Lu 2017 documents variants).
- **Put-option overlay on the long leg.** Buy short-dated OTM puts on AUD/JPY (or whatever the marquee long is) to cap the crash. Cost is real — typically 1–2% of notional per year — and eats much of the unconditional carry premium. The question is whether you'd rather have low-vol carry minus insurance, or high-vol carry plus the occasional 20% drawdown.
- **Hard position limits per pair.** Crashes are correlated across pairs. Don't size each pair against its own historical vol — size against the book's beta to USD-funded G10 risk-off.
- **Avoid pegs that can break.** ARS, EGP, TRY before its float — the rate looks great until it doesn't. The Soros 1992 GBP-ERM trade is a carry trade on the *other* side of a peg break.

## Common mistakes

- **Equating UIRP failure with arbitrage.** It isn't arbitrage. It's a risk premium. Treat it like one — size it as a fraction of risk budget, not "as much as we can borrow."
- **Ignoring transaction costs in EM.** Bid/ask on TRY/USD or ZAR/JPY forwards can be 10–30 bps round-trip. On a strategy that earns 50–100 bps a month, half your edge is in the spread. The book's strategy assumes "ignoring transaction costs"; in practice you cannot.
- **Calibrating on the 1976–2007 sample.** Almost every textbook number for "carry Sharpe" comes from that window. The post-GFC sample tells a different story. Walk forward.
- **Not adjusting for capital controls.** Some "high rates" are high because money can't get in or out freely. The forward discount embeds that. INR, CNY one-month NDF forwards are not the same instrument as a G10 forward.

## Where to do this in the terminal

- **FX screen** — view live forward discounts and the cross-sectional ranking for a G10 + EM universe.
- **AI Quant Lab** — the carry-strategy template lets you choose universe, quantile, rebalance frequency, and overlay a VIX filter or volatility scaling.
- **Backtesting** — runs the strategy with realistic forward-roll mechanics and per-currency transaction costs.

## See also

- [[trend-following|Trend Following (TSMOM)]] — the natural diversifier; carry and trend are nearly uncorrelated, hence the §8.4 combo
- [[strategy-patterns|Strategy Patterns]] — carry as one of the five archetypes (mean reversion, trend, carry, vol, stat-arb)
- [[volatility-quant|Volatility Trading]] — the VIX filter that gates carry exposure lives here
- [[portfolio-construction|Portfolio Construction]] — the min-variance blending math used in the carry + momentum combo

## External references

- Fama, E. (1984). "Forward and Spot Exchange Rates." *Journal of Monetary Economics* 14(3), 319–338.
- Hansen, L., Hodrick, R. (1980). "Forward Exchange Rates as Optimal Predictors of Future Spot Rates." *Journal of Political Economy* 88(5), 829–853.
- Lustig, H., Verdelhan, A. (2007). "The Cross-Section of Foreign Currency Risk Premia and Consumption Growth Risk." *American Economic Review* 97(1), 89–117.
- Lustig, H., Roussanov, N., Verdelhan, A. (2011). "Common Risk Factors in Currency Markets." *Review of Financial Studies* 24(11), 3731–3777.
- Brunnermeier, M., Nagel, S., Pedersen, L. (2008). "Carry Trades and Currency Crashes." *NBER Macroeconomics Annual* 23, 313–347.
- Burnside, C., Eichenbaum, M., Rebelo, S. (2011). "Carry Trade and Momentum in Currency Markets." *Annual Review of Financial Economics* 3, 511–535.
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Du, W., Tepper, A., Verdelhan, A. (2018). "Deviations from Covered Interest Rate Parity." *Journal of Finance* 73(3), 915–957.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §8.2–8.4. https://doi.org/10.1007/978-3-030-02792-6
