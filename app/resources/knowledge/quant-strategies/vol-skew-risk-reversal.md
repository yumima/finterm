# Volatility Skew — Long Risk Reversal

The **volatility skew** is the empirical fact that **OTM put options trade at higher implied volatility than OTM call options** at the same distance from the spot. The skew has existed since 1987 (Black-Scholes assumes constant vol; the data has never agreed) and has been one of the most-studied features of the equity options surface.

The **long risk reversal strategy** captures the skew by **buying an OTM call** and **selling an OTM put**, at strikes equidistant from spot. The position has positive delta (it's bullish) but profits from the relative pricing of calls vs. puts — specifically, you collect more premium on the put sale than you pay for the call purchase.

Kakushadze & Serur (2018) §7.5 give this in one paragraph. The strategy is a staple of options trading and a useful piece of any vol surface book.

## The setup

For an underlying at spot `S_0`:

```
OTM call:  strike K_call = S_0 + κ        (κ > 0)
OTM put:   strike K_put  = S_0 − κ

Where κ is the distance from spot (e.g., 5% OTM).

Empirically: IV(K_put) > IV(K_call)        (this is the skew)

Strategy: Buy 1 OTM call. Sell 1 OTM put.
```

The position is approximately delta-neutral at inception (the deltas of equidistant OTM puts and calls roughly cancel — typically the call has delta ~+0.30 and the put has delta ~−0.30). The exposure is to the **skew premium**.

## What the position is

The combined position has:
- **Positive delta**: roughly +0.30 to +0.60 depending on how OTM.
- **Negative vega**: you're net selling vol because the put has more vega than the call.
- **Negative skew exposure**: profits if skew flattens (put IV falls relative to call IV).
- **Premium collected at inception** if the put premium > call premium (the typical case in equities).

So it's **directionally bullish + short skew + short vega**.

## The economic story — why the skew exists

The vol skew in equities is *primarily*:

### 1. Demand for crash protection

Institutional investors (mutual funds, pensions) buy OTM index puts as portfolio insurance. This persistent demand bids up put IV. Single-stock skew is less extreme but follows the same pattern.

### 2. Leverage effects (Black 1976)

When equity prices fall, debt-to-equity ratios rise; the firm becomes more levered; future equity vol increases. This creates a **negative correlation between stock returns and volatility**, which in turn produces a downward-sloping (skewed) implied vol surface.

### 3. Jump risk

Markets occasionally jump downward (1987, 2008, March 2020). The market prices this asymmetric jump risk into puts more aggressively than into calls. The skew is partially compensation for absorbing crash-jump risk.

## The trade — long risk reversal

You profit from the long risk reversal if:

1. **Skew flattens** while the underlying doesn't move much (put IV falls more than call IV) — the short put leg gains more than the long call leg loses.
2. **Underlying rallies** — the long call increases in value; the short put loses value (good for you as the seller).

You lose if:

1. **Underlying falls sharply** — short put losses dominate; long call expires worthless.
2. **Skew steepens** — put IV rises more than call IV; short put loses more than long call gains.
3. **Vol generally rises** — net negative vega.

The book notes: "this is a directional strategy — it loses money if the price of the underlying drops below `K_put − C`, where `K_put` is the strike price of the put, and `C > 0` is the premium of the put minus the premium of the call."

So the **break-even on the downside** is roughly the put strike minus the net premium received. Any drop deeper than that produces a loss.

## When the trade works

The strategy is most profitable in regimes where:

- **Equity market is in a slow bull or sideways range** — neither leg expires deeply ITM.
- **Skew is steep** at trade inception — the premium collected on the put is substantial.
- **Volatility is moderate** — calm enough that nothing gets close to either strike.

Empirically:

- **2017**: extremely low vol, persistent rally. Long risk reversals printed money.
- **2013–2019**: generally profitable in the long-running bull market.

## When the trade fails

- **Sharp equity sell-offs**: the short put leg loses heavily. 2008, March 2020.
- **Vol spikes without directional move**: skew widens; even a flat underlying can hurt.
- **Long, slow drawdowns** (2022): equities grind lower over months; short puts repeatedly get assigned or roll-up.

## Variants

### Cash-secured long risk reversal

Cover the short put with cash equal to `K_put × 100 × N_contracts`. Treats the trade as a synthetic long stock position with a strike at `K_put` and an upside call kicker.

### Wide skew = wide reversal

Use OTM puts at deeper strikes (e.g., 10% OTM) and OTM calls at 5% OTM. Asymmetric strike selection captures more skew but increases downside tail risk.

### Calendar risk reversal

Use a long call at a longer expiry and a short put at a shorter expiry. The short put rolls multiple times against the long call; the long call has more time value remaining when puts are sold.

### Index risk reversal vs. single-stock

Index risk reversals are cleaner (less idiosyncratic news risk). Single-stock risk reversals capture stock-specific skew (event-driven) but require care around earnings.

## Variants — long risk reversal on currencies

The **FX risk reversal** is a measure of the relative pricing of OTM puts vs. OTM calls on currencies. Used as a market-implied sentiment indicator.

```
RR_25Δ  =  IV_25Δ_call  −  IV_25Δ_put
```

Negative RR_25Δ (puts more expensive than calls) suggests downside fear; positive RR suggests upside fear.

The same long-risk-reversal trade in FX is structurally similar: buy OTM call, sell OTM put, collect the skew premium. Works in some FX pairs (USD/JPY in calm periods) and fails in others.

## How this relates to volatility risk premium

The long risk reversal is essentially **selling skew vol premium** on the put leg while **buying flat call vol** on the other leg. It's a more refined version of [[volatility-quant|the simple VRP trade]] which sells ATM straddles.

Compared to a simple short ATM straddle:
- **Long risk reversal** has positive delta, less negative vega, and a directional bull view.
- **Short straddle** is delta-neutral, more negative vega, and is pure short-vol.

For an explicitly bullish view + short skew exposure, the long risk reversal is the cleaner expression.

## Volatility surface dynamics — what to watch

When trading the long risk reversal, the relevant surface quantities to monitor:

| Quantity | What it tells you |
|---|---|
| **25-delta risk reversal** | Cleanest measure of skew steepness |
| **Skew slope** | Steeper = more put-call IV gap = more premium on the trade |
| **ATM IV** | Overall vol level; if ATM IV is high, both legs are expensive |
| **Vol of vol** | Higher vol-of-vol = bigger random P&L; size down |
| **Realised correlation to skew changes** | If realised skew differs from implied, the trade is profitable on the IV-collapse leg |

## Common mistakes

- **Treating the position as delta-neutral when it's not**. The OTM call's delta is usually slightly less than the OTM put's, so the position has a small initial delta. Be aware.
- **Sizing as if the put premium is the maximum loss**. The put leg is **uncovered short** — losses can vastly exceed the premium received. The strike-minus-premium break-even is the relevant downside.
- **Ignoring event risk**. Earnings, FOMC, ECB meetings. The short put leg is exposed to event-driven jumps.
- **Confusing vol skew with vol surface curvature**. Skew is the asymmetry (puts vs. calls); curvature (smile) is symmetric around ATM. Different trades address different phenomena.
- **Buy-and-hold the risk reversal**. The position has time decay (theta). Hold it past 30 days and the call is just bleeding theta against you. Roll or close.

## Risk management essentials

- **Size the short put leg for tail loss**: assume a 10% drop in the underlying when sizing.
- **Stop-loss on the underlying**: exit if underlying drops more than some threshold.
- **Hedge with index puts**: small position in deep-OTM index puts to cover the catastrophic tail.
- **Time decay management**: close before expiry; don't let the position drift.

## Where to do this in the terminal

- **Derivatives screen** — option chain with skew metric visible per strike.
- **Surface Analytics** — implied vol surface with 25-delta risk-reversal column.
- **AI Quant Lab** — long-risk-reversal strategy template with strike selection.
- **Backtesting** — historical risk-reversal P&L with skew-environment overlay.

## See also

- [[volatility-quant|Volatility Trading and the Surface]] — the broader vol surface framework
- [[vol-vix-futures-basis|VIX Futures Basis Trading]] — vol-curve trading
- [[vol-etn-decay|VIX ETN Decay]] — short-vol ETN structure
- [[index-dispersion-trading|Dispersion Trading]] — correlation trading
- [[stocks-implied-volatility-signal|Implied Volatility Signal]] — single-stock IV change strategy

## External references

- Black, F. (1976). "Studies of Stock Price Volatility Changes." Proceedings of the 1976 Business Meeting of the Business and Economics Statistics Section, American Statistical Association.
- Bollen, N., Whaley, R. (2004). "Does Net Buying Pressure Affect the Shape of Implied Volatility Functions?" *Journal of Finance* 59(2), 711–753.
- Bates, D. (1991). "The Crash of '87: Was It Expected? The Evidence from Options Markets." *Journal of Finance* 46(3), 1009–1044.
- Jackwerth, J. (2000). "Recovering Risk Aversion from Option Prices and Realized Returns." *Review of Financial Studies* 13(2), 433–451.
- Doran, J., Krieger, K. (2010). "Implications for Asset Returns in the Implied Volatility Skew." *Financial Analysts Journal* 66(1), 65–76.
- Xing, Y., Zhang, X., Zhao, R. (2010). "What Does the Individual Option Volatility Smirk Tell Us About Future Equity Returns?" *Journal of Financial and Quantitative Analysis* 45(3), 641–662.
- Damghani, B., Kos, A. (2013). "De-arbitraging With a Weak Smile: Application to Skew Risk." *Wilmott Journal*.
- Mixon, S. (2011). "What Does Implied Volatility Skew Measure?" *Journal of Derivatives* 18(4), 9–25.
- Bondarenko, O. (2014). "Why Are Put Options So Expensive?" *Quarterly Journal of Finance* 4(3).
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §7.5. https://doi.org/10.1007/978-3-030-02792-6
