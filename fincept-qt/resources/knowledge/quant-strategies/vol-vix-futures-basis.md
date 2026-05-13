# VIX Futures Basis Trading

The **VIX futures basis** — the gap between front-month VIX futures and the spot VIX index — is one of the most-documented mean-reverting signals in modern volatility markets. Empirically, when the basis is large (in either direction), **VIX futures prices revert toward spot VIX over the following weeks**.

The strategy: short VIX futures when the basis is sharply positive (contango), buy VIX futures when the basis is sharply negative (backwardation). Kakushadze & Serur (2018) §7.2 give the construction, citing **Simon & Campasano (2014)** as the canonical empirical reference.

## The setup — VIX index vs. VIX futures

The **VIX index** is a measure of expected 30-day S&P 500 volatility, computed from current SPX option prices. It is **not directly tradeable** — there is no spot VIX market.

**VIX futures** are tradeable contracts whose payoff at settlement equals the then-prevailing VIX index value. Front-month futures (UX1) settle in ~1 month; second-month (UX2) in ~2 months; out to UX9 or beyond.

VIX futures prices are determined by the market's expectation of where VIX will be at futures settlement — plus a risk premium for bearing volatility-of-volatility risk.

### The basis definition

```
B_VIX  =  P_UX1 − P_VIX                                  (Eq. 7.1)

Daily roll value:
D  =  B_VIX / T                                          (Eq. 7.2)
```

Where `P_UX1` is the front-month VIX futures price, `P_VIX` is the spot VIX, and `T` is the number of business days until UX1 settlement (assumed ≥ 10).

- **`B_VIX > 0`** — UX1 trades higher than spot VIX. The VIX futures curve is in **contango** at the front end.
- **`B_VIX < 0`** — UX1 trades lower. **Backwardation** at the front end.

In normal market conditions, the futures curve is in contango (VIX futures > spot VIX). In crisis conditions, it flips to backwardation as fear of imminent volatility exceeds expectations of long-term mean reversion.

## The empirical fact

The key empirical finding (Simon & Campasano 2014; Mixon 2007; Nossman & Wilhelmsson 2009):

**VIX futures prices tend to fall when the basis is positive (contango), and rise when the basis is negative (backwardation).**

The basis has **substantial forecasting power for subsequent VIX futures price changes**, even though it has essentially **no forecasting power for spot VIX changes**.

This is critical: you cannot use the basis to predict spot VIX. You can use it to predict how the **futures price will move relative to the spot** — which is what determines the P&L of a VIX futures position.

## The strategy rule

```
Open long UX1 position  if D < −0.10
Close long UX1 position if D > −0.05
Open short UX1 position if D > +0.10
Close short UX1 position if D < +0.05                    (Eq. 7.3)
```

The asymmetric thresholds avoid whipsaws: you require the basis to be sharply against the position to enter, but exit quickly once it moves back toward neutral.

In practice the long-short trade is **mostly the short side**: contango is the dominant regime, so the strategy spends most of its time short VIX futures.

## Why does this work?

Two complementary explanations:

### 1. Volatility risk premium (VRP)

The expected return on selling VIX futures in normal markets is positive because **investors over-pay for VIX exposure** as a hedge against equity market crashes. The contango is the price of that insurance.

When `D > 0.10` (strong contango), the premium is unusually high — a good time to sell. When `D < −0.10` (backwardation), the market is paying you to take the *long* VIX position, which is unusual and suggests the panic is overdone.

### 2. VIX mean-reverting structure

Spot VIX is itself mean-reverting (Heston-style stochastic volatility). When VIX is below its long-run mean and the curve is in contango, the curve correctly anticipates VIX rising — but **the curve usually overshoots**, pricing in too much rise. Sellers of UX1 capture the overshoot.

When VIX is above its long-run mean and the curve is in backwardation (front futures below spot), the curve anticipates VIX falling — but again, often overshoots in the direction of normalisation. Buyers of UX1 capture the bounce.

## Hedging the position

The book notes (§7.2 with footnote 7): a short UX1 position is exposed to a sudden increase in volatility (which typically occurs during equity sell-offs). This risk can be hedged:

```
Short UX1: hedge by SHORTING mini-S&P 500 futures (ES)
                  (or going long during a vol-spike short trade)
Long UX1:  hedge by BUYING mini-S&P 500 futures
```

The hedge ratio is typically estimated via historical regression of VIX futures returns on ES returns. Empirical hedge ratio: roughly **−4 to −6 ES contracts per UX1 short**, varying by regime.

This is the **VIX-equity beta**: VIX rises when equities fall. The hedge captures the mechanical correlation.

## Empirical record

Simon & Campasano (2014) tested this strategy over 2007–2012:

- Average monthly return: ~3–4% on the short-UX1-when-D-positive trade.
- Sharpe: ~0.8 in their sample (gross of costs).
- The strategy was **most profitable in 2010–2012** (post-2008 contango regime with low VIX).
- It **suffered substantially in February 2018** (the "Volmageddon" when XIV liquidated, VIX spiked 200% in a day).

The fundamental risk: **a sudden vol spike during a short position** can crystallise large losses. The 2018 episode is the warning.

## Variants

### UX2 instead of UX1

The second-month future has more time to expiry, slower convergence to spot, and is less exposed to settlement-day dynamics. Some implementations use UX2 for cleaner roll behaviour.

### Curve-slope signal

Instead of UX1 vs. spot, use UX1-vs-UX2 (the front-of-curve slope). Captures the same information but with cleaner futures-only construction (no spot-vs-futures basis risk).

### Hedge-ratio dynamics

The optimal SPX hedge ratio is **regime-dependent**. In calm markets, ratio is ~-4. In stressed markets, can be ~-8 or higher. Dynamic hedging (recomputed weekly) outperforms static.

### VRP overlay

Combine with [[volatility-quant|the broader volatility risk premium]] strategy. Both bet on excess implied vol; the basis trade is the futures version, VRP is the options version.

## When the strategy fails

- **Sudden vol spikes** (Feb 2018, March 2020). The short-UX1 position loses violently as VIX gaps up.
- **Persistent vol regimes**. If VIX stays elevated for months (2008–2009, 2020), the contango/backwardation patterns invert and the signal can be wrong-footed for extended periods.
- **Curve dislocations**. Some VIX futures expiries trade with unusual settlement-day dynamics; the signal at those expiries is noisier.

## The February 2018 lesson

On Feb 5, 2018, VIX spiked from ~17 to ~37 in one trading session. Short-VIX positions were decimated:
- XIV (inverse VIX ETN, structurally short UX1+UX2) **lost 96% in one day** and was liquidated.
- Many systematic short-vol funds had substantial losses.
- The basis trade as described above, if held with leverage, also lost heavily.

The lesson: even with the favourable historical premium, **the trade has fat negative tail risk**. Position sizing must account for the 2018-style event.

## Practical implementation

- **Position size**: small. The strategy has high realised Sharpe but low capacity per dollar of capital because of tail risk.
- **Stop-loss**: mandatory. If VIX rises by N standard deviations in a session, exit. The 2018 episode would have been survivable with a tight stop.
- **Hedge with ES**: always. Unhedged short VIX is a way to blow up.
- **Roll discipline**: when UX1 approaches settlement (T < 10 days), roll to UX2. Don't carry to settlement.

## Common mistakes

- **Treating short VIX as risk-free carry**. It isn't. The 2018 episode and March 2020 prove this.
- **Using leverage**. Short-vol trades blow up under leverage. Cap leverage at 1× (no margin).
- **Ignoring the hedge**. Unhedged short VIX is equivalent to selling deep-OTM puts on the S&P. Always hedge.
- **Selling vol when the curve is already in backwardation**. The signal is the *opposite* — when in backwardation, you should be considering long VIX, not short.
- **Not adjusting for special expiries**. Some VIX futures have unusual settlement dynamics due to SPX option expiries. Avoid trading the last 1–2 days before VIX futures settlement.

## Risk management essentials

- **Hard stop on basis reversal**: if you're short and the basis goes negative, exit.
- **Vol-of-vol regime detection**: when realised VIX vol (vol of VIX) exceeds threshold, reduce position.
- **Counterparty risk**: VIX futures clear at CME; some VIX ETPs are off-exchange. Know what you're trading.
- **Sector hedge for short-vol books**: in addition to ES hedge, some funds hedge with long SPX puts.

## Where to do this in the terminal

- **Derivatives screen** — VIX futures term structure with basis flagged.
- **Surface Analytics** — implied vol surface with VIX expectations overlay.
- **AI Quant Lab** — VIX-basis trading strategy template with hedge-ratio module.
- **Backtesting** — historical VIX basis P&L with February 2018 stress-period flagged.

## See also

- [[volatility-quant|Volatility Trading and the Surface]] — the broader vol framework (covers VRP, implied vs realised, variance swaps)
- [[vol-etn-decay|VIX ETN Decay Trade]] — the structurally-short version via VXX/VXZ
- [[vol-skew-risk-reversal|Vol Skew and Risk Reversals]] — directional vol-of-vol trade
- [[etf-leveraged-decay|Leveraged ETF Decay]] — same principle for equity LETFs

## External references

- Simon, D., Campasano, J. (2014). "The VIX Futures Basis: Evidence and Trading Strategies." *Journal of Derivatives* 21(3), 54–69.
- Mixon, S. (2007). "The Implied Volatility Term Structure of Stock Index Options." *Journal of Empirical Finance* 14(3), 333–354.
- Nossman, M., Wilhelmsson, A. (2009). "Is the VIX Futures Market Able to Predict the VIX Index? A Test of the Expectation Hypothesis." *Journal of Alternative Investments* 12(2), 54–67.
- Whaley, R. (2000). "The Investor Fear Gauge." *Journal of Portfolio Management* 26(3), 12–17.
- Whaley, R. (2009). "Understanding the VIX." *Journal of Portfolio Management* 35(3), 98–105.
- Eraker, B. (2009). "The Volatility Premium." Working paper.
- Zhang, J., Zhu, Y. (2006). "VIX Futures." *Journal of Futures Markets* 26(6), 521–531.
- Lee, S., Wang, C., Yang, J. (2017). "Information Content of VIX Futures Basis." *Journal of Futures Markets*.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §7.2. https://doi.org/10.1007/978-3-030-02792-6
