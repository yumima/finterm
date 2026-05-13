# Dollar Carry Trade

The **dollar carry** is a tactical-allocation cousin of the cross-sectional FX carry strategy ([[fx-carry-trade|FX Carry Trade]]). Instead of sorting individual currencies into longs and shorts, dollar carry takes a **directional view on the USD as a whole**, governed by a single number — the average forward discount of foreign currencies versus the dollar.

Kakushadze & Serur (2018) cover this in §8.3 in three terse paragraphs. The strategy is much older and richer than that suggests; it became a standalone factor in **Lustig–Roussanov–Verdelhan (2014) "Countercyclical Currency Risk Premia."**

## The signal — one number, every month

For a basket of `N` foreign currencies, compute the cross-sectional average of the forward discount:

```
D̄(t,T) = (1/N) · Σᵢ Dᵢ(t,T)                          (Eq. 8.8)

where Dᵢ(t,T) = sᵢ(t) − fᵢ(t,T) ≈ rᵢ_foreign − r_USD
```

In English: average over your universe how much higher (or lower) foreign short rates are than US short rates.

- If `D̄ > 0` — on average foreign rates exceed US rates. The strategy is: **long all N foreign currency forwards** (equal-weighted).
- If `D̄ < 0` — on average US rates exceed foreign rates. The strategy is: **short all N foreign currency forwards**, i.e., long USD.

That's the entire rule. Horizon T can be 1, 3, 6, or 12 months. Rebalance monthly.

## Why this is a different bet from the HML-carry

[[fx-carry-trade|HML-Carry]] is **cross-sectional** — it goes long *some* currencies and short *others*, neutralising the average dollar move. The strategy makes money if high-rate currencies outperform low-rate ones, regardless of whether the dollar rallies or sells off.

Dollar carry is **time-series and directional** — the entire trade is long-or-short USD. You will make or lose money depending on whether the dollar strengthens or weakens against the rest-of-world basket. The two strategies have low correlation, which is why they get combined in practice.

## The macro intuition the book skips

`D̄(t,T)` is essentially **how much higher rest-of-world short rates are than US rates**. When is that wide and positive?

- When the US economy is *weak* relative to the rest of the world (the Fed has cut more, or other central banks are tighter).
- Empirically: 2002–2007, 2014 emerging-market period, late 2020.

When is it wide and negative?

- When the US is *strong* relative to the rest of the world (Fed hiking faster, others on hold or easing).
- Empirically: 1995–2001, 2014–2016 USD rally, the 2022–2023 hiking cycle.

So the dollar carry signal is a real-time, market-implied measure of relative monetary policy stance. **Lustig–Roussanov–Verdelhan (2014)** documented that the average forward discount predicts USD excess returns: when `D̄` is high, the dollar tends to underperform forwards (i.e., other currencies outperform the dollar), and vice versa.

This means dollar carry is, in effect, **a slow-moving USD value/macro signal**, not just a carry signal. It often works in 2–3 year cycles aligned with central-bank divergence.

## Sizing — what makes this trade tradeable

A few details the book glosses over:

- **Equal-weighting matters.** If you size by ADV or market cap, the JPY and EUR will dominate. Equal-weight gives the smaller currencies (NZD, NOK, SEK) a meaningful share and improves diversification of idiosyncratic FX moves.
- **Quote in USD return space.** The forwards are quoted against the USD; the P&L is in USD. No funding-currency-choice subtlety like in HML-carry.
- **Watch correlation to broad USD index.** Dollar carry is roughly anti-correlated with the DXY when `D̄ < 0` and roughly correlated when `D̄ > 0`. Manage book risk on a DXY-beta basis.

## Where it worked, where it didn't

Honest record:

- **2002–2007.** `D̄` was high (US in long easing cycle). Long foreign baskets paid handsomely.
- **2008 risk-off.** Like HML-carry, dollar carry suffered as the USD rallied violently during the crisis. The signal lagged.
- **2009–2013.** `D̄` was modestly positive on EM, USD broadly weak. Strategy paid.
- **2014–2016.** `D̄` flipped negative on G10 (US tightening, ECB easing). Short-foreign / long-USD leg of the strategy worked.
- **2022.** Probably the cleanest period for the strategy in a decade — `D̄` was sharply negative as the Fed front-loaded hikes, and long-USD against everything (except JPY late in the cycle) paid.

## When the strategy fails — and how it relates to risk

The trade fails when the dollar moves *opposite* to what relative rates predict. The classic version of this is **flight-to-quality in risk-off**: even if `D̄ > 0` (US rates lower), the USD rallies because it's the global reserve currency and capital flees to it. 2008 and March 2020 both did this.

This is why the literature distinguishes "carry crashes" (HML-carry's enemy) from "USD safe-haven episodes" (dollar carry's enemy). They overlap but are not identical.

## Combining with HML-carry

The two strategies are roughly orthogonal — you can run both:

- HML-carry captures the cross-sectional spread.
- Dollar carry captures the time-series USD signal.

A simple combined book: 50/50 risk-weighted. Lustig et al. (2014) show the combined strategy has materially higher Sharpe than either alone in their 1983–2010 sample.

## Risk management essentials

- **Avoid pegged or managed currencies in the basket.** A peg break (TRY 2018, ARS many times) turns a moderate signal into a tail event.
- **Cap position size during VIX spikes.** Like HML-carry, dollar carry has a risk-off tail when investors flee to USD. Halve size when VIX > 25.
- **Use forwards, not spot + borrowed cash.** The book uses forwards because they're cleaner: no separate funding leg, no funding-rate gap risk. Stick with forwards.
- **Beware of late-cycle reversals.** When the Fed pivots, `D̄` can flip sign in a single month and the trade should turn. Don't smooth the signal too aggressively.

## Common mistakes

- **Treating it as a "long carry" strategy in both directions.** It's not — when `D̄ < 0`, you are *short* the carry, *long* the USD. Sign matters.
- **Ignoring funding currency drift.** Over years, the USD-vs-rest-of-world FX move can be enormous and overwhelm the carry signal. The trade is not "set and forget."
- **Backtesting on a sample that ends in 2007.** Same warning as HML-carry. The post-GFC period is what informs real expectations.

## Where to do this in the terminal

- **FX screen** — cross-sectional forward discount monitor; `D̄(t)` plotted as a time series for G10 and EM baskets separately.
- **AI Quant Lab** — dollar-carry template with universe selection (G10 / G10+EM / custom) and horizon choice (1m / 3m / 6m).
- **Backtesting** — runs the strategy with realistic forward-roll mechanics and per-currency transaction costs.

## See also

- [[fx-carry-trade|FX Carry Trade (HML)]] — the cross-sectional cousin; uncorrelated, often combined
- [[fx-momentum-carry-combo|FX Momentum + Carry Combo]] — minimum-variance blend that includes dollar carry
- [[trend-following|Trend Following]] — currency trend signals that often align with the dollar carry direction
- [[strategy-patterns|Strategy Patterns]] — carry as one of the five archetypes

## External references

- Lustig, H., Roussanov, N., Verdelhan, A. (2014). "Countercyclical Currency Risk Premia." *Journal of Financial Economics* 111(3), 527–553.
- Lustig, H., Roussanov, N., Verdelhan, A. (2011). "Common Risk Factors in Currency Markets." *Review of Financial Studies* 24(11), 3731–3777.
- Cooper, I., Priestley, R. (2008). "Time-Varying Risk Premia and the Output Gap." *Review of Financial Studies* 22(7), 2801–2833.
- Joslin, S., Konchitchki, Y. (2018). "Interest Rate Volatility, the Yield Curve, and the Macroeconomy." *Journal of Financial Economics* 128(2), 344–362.
- Verdelhan, A. (2018). "The Share of Systematic Variation in Bilateral Exchange Rates." *Journal of Finance* 73(1), 375–418.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §8.3. https://doi.org/10.1007/978-3-030-02792-6
