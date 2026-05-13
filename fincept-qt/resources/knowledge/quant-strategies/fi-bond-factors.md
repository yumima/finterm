# Bond Factor Investing — Low-Risk, Value, Carry

For three decades, **equity factor investing** (value, size, momentum, low-vol, quality) has been the dominant systematic framework for stock selection. The natural question — does it work in bonds? — has a slow, qualified, but ultimately affirmative answer. **Houweling & van Vundert (2017)** is the most-cited synthesis. Kakushadze & Serur (2018) §5.9–§5.11 cover the three workhorse factors: **low-risk**, **value**, and **carry** in bonds.

This entry treats them as a single integrated chapter because in practice they're run as a multi-factor portfolio, not as standalone signals.

## Factor 1 — Low-Risk

**The low-risk anomaly in bonds**: lower-rated and shorter-duration bonds tend to outperform higher-rated and longer-duration bonds **on a risk-adjusted basis**. This is a direct analogue of the equity "low-volatility anomaly" (Ang, Hodrick, Xing, Zhang 2006; Frazzini & Pedersen 2014 "Betting Against Beta").

The book's construction (§5.9, following Houweling & van Vundert 2017):

```
1. Within Investment Grade (AAA through A−):
   take the bottom decile by maturity.

2. Within High Yield (BB+ through B−):
   take the bottom decile by maturity.
```

The economic interpretation: shorter-duration bonds have lower interest-rate risk per unit of credit spread, and **the credit spread per unit of duration is highest at the short end**. So the strategy buys bonds with the best yield-per-unit-risk ratio.

### Why does it work?

Three competing explanations:

1. **Leverage aversion** (Frazzini & Pedersen 2014). Investors who can't or won't use leverage buy higher-risk securities to reach return targets, bidding up their prices. Lower-risk securities are undervalued by these constrained investors.
2. **Benchmark constraints.** Pension and insurance benchmarks penalise *tracking error*, not absolute risk. Managers stay close to the benchmark by holding long-duration bonds even when short-duration bonds offer better risk-adjusted returns.
3. **Default skew misperception.** High-yield investors over-pay for very long-duration HY bonds because the carry is enormous; the eventual default risk is mispriced.

All three are partly true.

### How it's traded

Annualised excess return of low-risk bonds over the broad investment-grade index has been roughly 50–100 bps with materially lower drawdowns over 1990–2016 (Houweling & van Vundert; De Carvalho et al. 2014). Sharpe gains in the 0.2–0.4 range, which is modest but **stable**.

## Factor 2 — Value

**Value in bonds**: like equity value, the idea is that bonds whose **observed credit spread is wider than a model-predicted "fair" spread** are cheap, and will revert.

The book follows Houweling & van Vundert (2017) closely. The procedure:

```
1. For each bond i, observe its credit spread S_i = bond yield − risk-free rate.

2. Run a cross-sectional regression of S_i on bond credit rating (as dummies)
   and maturity:

   S_i = Σ_r β_r · I_{ir}  +  γ · T_i  +  ε_i              (Eq. 5.37)

   where I_{ir} = 1 if bond i has rating r, 0 otherwise.

3. The fitted value S_i* = S_i − ε_i is the theoretical spread.

4. Define value:
   V_i  =  ln(S_i / S_i*)   or equivalently   ε_i / S_i*    (Eq. 5.38)

5. Long the top decile by V_i (bonds with widest residual spread, i.e., cheapest).
```

The regression captures everything systematic — the average effect of rating and maturity. The **residual `ε_i` is the bond-specific dislocation**: a bond with a 200 bps spread when the model predicts 150 bps is "20-bps cheap per dollar of spread."

### Why does this work?

- **Slow information dissemination.** Corporate-bond markets are less liquid than equity markets; mispricings persist longer.
- **Dealer balance sheet constraints.** Post-2008, dealers can't hold inventory the way they used to, so bonds that need to clear via dealers trade at concessions.
- **Forced selling.** Downgrade events trigger forced selling by mandate-constrained institutional holders; the bonds end up cheap.

### Practical caveats

- **Cheap-for-a-reason.** Some "cheap" bonds are cheap because they have idiosyncratic risk (e.g., upcoming maturity wall, ratings-watch). Use ratings momentum and analyst reports as filters.
- **Liquidity penalty.** The cheapest bonds in a screen are often the most illiquid. Cap turnover, and pre-screen on minimum issue size and trading volume.
- **Sample period sensitivity.** Value-in-bonds underperformed during 2009–2012 as central-bank QE squashed spreads uniformly. The factor is most alive when spreads are dispersed.

## Factor 3 — Carry

**Bond carry** = the return you earn if interest rates **don't move**. It has two components: the yield itself, plus the price appreciation from the bond rolling down a (typically upward-sloping) curve.

The book derives this carefully (§5.11). For a bond at time `t` with maturity `T`, holding from `t` to `t+Δt`:

```
C(t, t+Δt, T)  =  R(t,T) · Δt  +  C_roll(t, t+Δt, T)        (Eq. 5.41)

where  C_roll  ≈  −ModD(t,T) · [R(t, T−Δt) − R(t,T)]         (Eq. 5.42)
```

The first term is the **yield component** — the coupon-equivalent return over time `Δt`.
The second term is the **roll component** — modified duration times the difference in yield between the bond's current maturity bucket and the slightly-shorter bucket it will be in after `Δt` of aging.

In a steep curve, `R(t, T−Δt) < R(t,T)` (shorter maturity has lower yield), so `C_roll > 0` — you make money from aging. In a flat curve, `C_roll ≈ 0`. In an inverted curve, `C_roll < 0` — aging costs you money.

### Trading the carry factor

```
1. For each bond, compute its annualised carry C / Δt.
2. Long the top decile by carry (highest expected return assuming rates don't move).
3. Short the bottom decile.
4. Rebalance monthly or quarterly.
```

### Why it works

This is the bond-market version of the FX carry trade ([[fx-carry-trade|FX Carry]]) and the commodity carry/roll trade ([[commodity-roll-yield|Commodity Roll Yield]]). The unifying explanation across asset classes: **carry compensates for risk that the underlying state variable will move adversely**. In bonds, that's rate-rise risk; in FX, currency-depreciation risk; in commodities, spot-price decline.

Koijen, Moskowitz, Pedersen, Vrugt (2018) "Carry" shows that carry signals predict returns across asset classes, and the bond carry signal has roughly 0.4 Sharpe in isolation over their 1990–2012 sample.

### Practical caveats

- **Carry is correlated with credit risk in corporate bonds.** A high-carry bond is often a high-spread (lower-credit) bond. Make sure your "carry factor" isn't just a credit-quality factor in disguise — control for rating in the construction.
- **Roll-down is curve-shape-dependent.** In flat or inverted curves, the roll component vanishes and carry becomes pure yield. The strategy works less well in those regimes.
- **Reinvestment risk at the rebalance.** Selling a rolled-down bond and buying a new long-end bond exposes you to entry-yield timing.

## The multi-factor combination

In practice, the three factors are not run in isolation. They're combined:

```
Combined score_i  =  (1/3) · [rank(low-risk_i) + rank(value_i) + rank(carry_i)]
```

(or weighted differently if backtesting suggests). Long the top quantile by combined score, short the bottom, equal-weight within each leg.

The three factors have **low pairwise correlations** in bonds:

| Pair | Approx correlation |
|---|---|
| Low-risk × Value | Modest positive (cheap short-dated IG often shows up in both) |
| Low-risk × Carry | Modest negative (low-risk tilts toward short bonds, which have less carry) |
| Value × Carry | Near zero |

This is the same "value-and-momentum-everywhere" pattern (Asness, Moskowitz, Pedersen 2013) translated to bonds. The combined portfolio has higher Sharpe than any single factor.

## Where this fails

- **Liquidity crises** (2008, March 2020). All three factors loaded heavily on bonds that became impossible to trade. Implementation losses dwarfed signal returns for a quarter or two.
- **QE regimes.** Central-bank purchases compress spread dispersion, kill value, flatten carry. 2010–2014 was difficult for bond factor investing.
- **Single-issuer concentration.** Without sector and issuer caps, the combined portfolio can end up with 40% in one sector or 5% in one issuer. Diversification rules matter.

## Risk management essentials

- **Cap single-issuer exposure** at e.g. 1% of book.
- **Sector neutrality**: long-short bond portfolios are normally constructed to be roughly sector-balanced to avoid macro-sector bets.
- **Liquidity filter**: minimum issue size ($200M+), minimum age (issued > 6 months ago), minimum trading volume.
- **Transaction-cost-aware rebalancing**: bond bid-ask is 10–50 bps. Monthly rebalancing without cost models eats most of the alpha.

## Common mistakes

- **Confusing bond carry with credit risk.** A high-carry bond is usually a high-yield bond. Make sure you're picking on residual carry within rating buckets.
- **Equal-weighting across notional, not duration.** Two bonds of the same notional but different durations have different risk contributions. Risk-weight at the duration level.
- **Ignoring bid-ask in backtests.** Bond mid-price backtests look great. Live performance is much worse because of round-trip costs.
- **Using stale credit ratings.** Rating agencies are slow. By the time S&P downgrades, the market has moved. Use market-implied ratings (or trailing-spread quintiles) as a more responsive signal.

## Where to do this in the terminal

- **Bond screen** — credit-spread vs. duration scatter; bonds flagged by factor score.
- **AI Quant Lab** — multi-factor bond-portfolio template with low-risk + value + carry combo; backtest mode shows per-factor and combined returns.
- **Backtesting** — runs the strategy with realistic transaction costs and liquidity filters.

## See also

- [[fi-yield-curve-positioning|Yield Curve Positioning]] — the roll-down mechanic underlying bond carry
- [[fi-curve-spreads|Yield Curve Spreads]] — directional curve trades, complementary to factor approaches
- [[fx-carry-trade|FX Carry Trade]] — the same idea in currencies
- [[commodity-roll-yield|Commodity Roll Yield]] — the same idea in commodities
- [[strategy-patterns|Strategy Patterns]] — factor investing as one of the archetypes

## External references

- Houweling, P., van Vundert, J. (2017). "Factor Investing in the Corporate Bond Market." *Financial Analysts Journal* 73(2), 100–115.
- De Carvalho, R., Dugnolle, P., Lu, X., Moulin, P. (2014). "Low-Risk Anomalies in Global Fixed Income: Evidence from Major Broad Markets." *Journal of Fixed Income* 23(4), 51–70.
- Correia, M., Richardson, S., Tuna, A. (2012). "Value Investing in Credit Markets." *Review of Accounting Studies* 17(3), 572–609.
- Koijen, R., Moskowitz, T., Pedersen, L., Vrugt, E. (2018). "Carry." *Journal of Financial Economics* 127(2), 197–225.
- Frazzini, A., Pedersen, L. (2014). "Betting Against Beta." *Journal of Financial Economics* 111(1), 1–25.
- Beekhuizen, P., Duyvesteyn, J., Martens, M., Zomerdijk, C. (2016). "Carry Investing on the Yield Curve." Working paper, SSRN 2808327.
- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §5.9–5.11. https://doi.org/10.1007/978-3-030-02792-6
