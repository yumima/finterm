# FX Momentum + Carry Combo

This is not a third FX strategy — it's the **principled way to combine** the two main ones ([[fx-carry-trade|carry]] and FX time-series momentum) into a single book. The motivation is direct: carry and momentum in currencies have **low correlation**, so a properly-weighted blend has a higher Sharpe than either alone. The math is just two-asset minimum-variance.

Kakushadze & Serur (2018) §8.4 spell out the formulae. The deeper context — that this is a particular instance of a much broader cross-asset-class result — comes from **Asness, Moskowitz, and Pedersen (2013) "Value and Momentum Everywhere"**.

## The two ingredients

**FX Carry signal (R₁)**: the return stream from a cross-sectional HML-carry strategy on G10 (or G10+EM). High-rate currencies long, low-rate short.

**FX Momentum signal (R₂)**: a 12-month-minus-1-month time-series momentum signal on the same FX universe. Long currencies that have been appreciating, short those that have been depreciating. The book references this in §8.1 (with HP-filter smoothing of the raw spot series to reduce noise).

These are run as **separate strategies first**, each producing its own monthly return series.

## The combination math — minimum-variance two-asset

Given the two return series, with sample statistics:

```
σ₁² = Var(R₁)                                          (Eq. 8.9)
σ₂² = Var(R₂)                                          (Eq. 8.10)
ρ  = Cor(R₁, R₂)                                       (Eq. 8.11)
```

The combined return is `R = w₁·R₁ + w₂·R₂` with `w₁ + w₂ = 1`. The minimum-variance weights are:

```
w₁ = (σ₂² − σ₁·σ₂·ρ) / (σ₁² + σ₂² − 2·σ₁·σ₂·ρ)        (Eq. 8.15)
w₂ = (σ₁² − σ₁·σ₂·ρ) / (σ₁² + σ₂² − 2·σ₁·σ₂·ρ)        (Eq. 8.16)
```

These are the unique weights summing to 1 that minimise the variance of `R`. This formula is in every portfolio-theory textbook — it's Markowitz applied to two assets.

If `ρ = 0`, the weights simplify to `w_i ∝ 1/σᵢ²` — risk-parity. If `ρ ≈ 0` and `σ₁ ≈ σ₂`, then `w₁ = w₂ = 0.5`.

## Why does this work — the empirical surprise

**Carry returns and momentum returns are almost uncorrelated** in FX. Asness, Moskowitz, Pedersen (2013) report Sharpe ratios on the order of 0.5–0.8 for both individual strategies in FX, and a correlation between them close to zero.

This is *not* an accident — it shows up across asset classes:

- Equity value × Equity momentum: correlation negative (the famous "value-momentum" diversification).
- FX carry × FX momentum: correlation near zero.
- Commodity carry × commodity momentum: correlation near zero.
- Bond carry × bond momentum: correlation low and time-varying.

The economic story: carry pays when stable macro conditions allow rate differentials to persist; momentum pays when those conditions are changing (a currency on a trend). The two trades earn in different macro states. Blending them earns more uniformly.

## What "minimum-variance" gets you vs. naïve 50/50

If `σ₁ ≈ σ₂` and `ρ ≈ 0`, the optimal weights are roughly 50/50 — and naïve 50/50 is essentially correct.

The formula starts to matter when:

- The strategies have very different volatilities (e.g., EM-FX carry vs. G10 trend, where carry is much higher vol).
- The historical correlation is meaningfully non-zero in a given sample.

**Caveat — the textbook trap.** The formula uses *historical* `σ` and `ρ` as estimates. These are noisy. If you re-estimate every month, the weights will jitter and you'll generate excess turnover for no real benefit. In practice, run a 36-or-60-month rolling estimate, and update weights infrequently (quarterly is plenty).

## Variants

The book hints at richer combinations in footnote 10 (Asness, Moskowitz, Pedersen and others). The standard generalisations:

| Variant | What changes |
|---|---|
| **Risk-parity blend** | Use `wᵢ ∝ 1/σᵢ` instead of min-variance. Ignores correlation. More robust to estimation error. |
| **Equal-Sharpe** | `wᵢ ∝ Sharpe_i / σᵢ`. Tilts weight toward whichever strategy has the higher historical Sharpe. Risky — historical Sharpe is noisy. |
| **Maximum-diversification** | Maximises the ratio of weighted-average vol to portfolio vol. Sensible when correlation > 0. |
| **Three-asset blend** | Add FX value as a third signal (PPP-deviation or real-exchange-rate based). [[fx-carry-trade|HML-carry]] + FX-trend + FX-value is the AMP (2013) "everywhere" combination. |

## Where it works and what to expect

In FX on G10+EM, 1985 to present:

- **Carry alone**: Sharpe ~0.6 net (much lower post-2008).
- **Trend alone**: Sharpe ~0.4 net.
- **Equal-weighted combo**: Sharpe ~0.7–0.8 net, with smaller drawdowns. The 2008 carry crash is partially offset by trend going short the yen-funded crosses.
- **Min-variance combo**: marginal improvement over equal-weight unless one strategy is much higher vol.

The combo's value-add is **drawdown control**, not Sharpe maximisation. The crash months of carry alone (Oct 1998, Aug 2008, Mar 2020) tend to be better-than-flat for the combined book because trend already started reducing exposure.

## Common mistakes

- **Re-estimating weights monthly.** Generates massive turnover, eats the trend signal which is itself slow-moving. Quarterly or semi-annually is plenty.
- **Treating the formula as "the answer."** Min-variance assumes the historical covariance is the true future covariance. It isn't. Use the formula as a sanity check on naïve allocations, not as a precision tool.
- **Forgetting that this combines *signals*, not *positions* directly.** Run each strategy as its own book, generate return streams, then combine. Don't try to combine raw position weights — you'll mix incompatible scaling conventions.
- **Ignoring transaction costs at the combo level.** The two strategies often want to put on offsetting positions (carry wants to short JPY, trend in a yen rally wants to be long JPY). Net the offsetting trades before sending to market.

## Where to do this in the terminal

- **AI Quant Lab** — the FX strategy template includes an explicit "combo" mode that runs both signals and applies any of the four weighting schemes.
- **Backtesting** — plots the realised correlation of carry and momentum returns over rolling windows, so you can see directly what `ρ` is doing in any given regime.
- **Portfolio** — the optimiser module can solve the two-asset min-variance system on any two return streams.

## See also

- [[fx-carry-trade|FX Carry Trade]] — the first ingredient
- [[fx-dollar-carry|Dollar Carry]] — sometimes added as a third leg
- [[trend-following|Trend Following (TSMOM)]] — the second ingredient, in its general form
- [[portfolio-construction|Portfolio Construction]] — Markowitz, risk-parity, and the broader allocation theory this builds on

## External references

- Asness, C., Moskowitz, T., Pedersen, L. (2013). "Value and Momentum Everywhere." *Journal of Finance* 68(3), 929–985.
- Olszweski, P., Zhou, G. (2013). "Strategy Diversification: Combining Momentum and Carry Strategies within a Foreign Exchange Portfolio." *Journal of Derivatives & Hedge Funds* 19(4), 311–320.
- Ahmerkamp, J., Grant, J. (2013). "The Returns to Carry and Momentum Strategies." Working paper, SSRN 2227387.
- Burnside, C., Eichenbaum, M., Rebelo, S. (2011). "Carry Trade and Momentum in Currency Markets." *Annual Review of Financial Economics* 3, 511–535.
- Markowitz, H. (1952). "Portfolio Selection." *Journal of Finance* 7(1), 77–91.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §8.4. https://doi.org/10.1007/978-3-030-02792-6
