# Position Sizing (Kelly, Fractional Kelly, Vol Targeting)

How much to risk on each trade is the question that determines whether a positive-edge strategy compounds or blows up. Two strategies with identical signals and identical Sharpe can have wildly different long-run wealth paths depending on sizing. Getting it wrong is the single most common reason quant strategies fail in production.

This entry covers **the Kelly criterion derivation, why full Kelly is too aggressive in practice, fractional Kelly (Thorp), and volatility targeting as the practitioner standard**.

## Kelly criterion — derivation

Kelly (1956), "A New Interpretation of Information Rate" (*Bell System Technical Journal* 35), framed the problem as **maximize the expected logarithm of wealth** — equivalently, maximize the geometric growth rate.

For a binary bet with edge p (probability of winning) and payout 1:1:

```
f* = 2p − 1 = p − (1−p)
```

For a continuous bet on a Gaussian return with mean μ and variance σ²:

```
f* = μ / σ²
```

The proof in one line: per-period log return at leverage f is `f·r − f²·σ²/2` (Itô correction). Setting the derivative to zero gives `f* = μ/σ²`.

Two equivalent ways to state the result:

- **At Kelly fraction**, geometric growth rate is maximized.
- **At any fraction > Kelly**, geometric growth rate is *lower* than at Kelly (and at 2·Kelly, growth rate is zero — beyond that, certain ruin).

## Why full Kelly is too aggressive in practice

The Kelly formula assumes you **know** μ and σ exactly. You don't. You estimate them from a backtest of finite length, and the standard error on μ is enormous:

```
SE(μ̂) = σ / √T
```

For a Sharpe-1 strategy with σ=15% over 5 years (T=60 months), the 95% CI on the annualized mean return spans roughly `μ ± 2σ/√5 = μ ± 13%`. Your estimate of μ could be off by enough to put the true Kelly fraction at half (or twice) your computed value.

Plug a too-high μ into the formula and you over-lever. The variance of the geometric growth rate at full Kelly is also huge — even with the *correct* μ and σ, the path-volatility makes full-Kelly intolerable:

- At full Kelly, the realized growth path has a **50% chance of being down 50% at some point** in a long simulation.
- The expected time to reach 2× starting wealth is ~half the time to first drawdown of 50%.

Most actual investors would fire themselves long before harvesting the full-Kelly geometric optimum. Path-pain dominates terminal-wealth optimization in real institutional settings — see [[drawdown-and-risk|drawdowns and path-dependent risk]].

## Fractional Kelly — Thorp's compromise

Ed Thorp, who used the Kelly criterion to size bets at blackjack and later at Princeton-Newport Partners, settled on **half-Kelly** as the practitioner default. The math (Thorp 2006, "The Kelly Criterion in Blackjack, Sports Betting, and the Stock Market"):

- At half-Kelly, geometric growth rate is **3/4 of the full-Kelly maximum**.
- Variance of the growth path is **half** that of full Kelly.
- Drawdowns roughly halved.

The tradeoff is asymmetric: you give up 25% of geometric growth to cut path-pain in half. For any investor with finite tolerance for drawdowns and finite confidence in their parameter estimates, fractional Kelly dominates full Kelly.

Common practitioner fractions:

- **Half Kelly** — Thorp / academic standard.
- **Quarter Kelly** — when parameter uncertainty is high or drawdown tolerance is tight.
- **Tenth Kelly** — discretionary asset managers, where μ is essentially unknown.

López de Prado (2018), *Advances in Financial Machine Learning*, argues that for ML-driven signals where μ is barely identified, fractions well below 0.5 are appropriate.

## Volatility targeting — the practitioner standard

Most institutional systematic shops don't use Kelly at all. They use **vol targeting**: size each position so that the portfolio's *expected* volatility matches a target (e.g., 10% annualized).

```
position_size_t = (vol_target / σ̂_t) · signal_t
```

where σ̂_t is a forward-looking estimate of return volatility (EWMA or GARCH; see [[time-series-basics|time-series basics]]).

Why it dominates Kelly in practice:

- **No μ required.** Vol is far easier to estimate than mean return. σ̂ converges to its true value at rate √T; μ̂ converges at the same rate but the *signal-to-noise* is much worse because the mean is small relative to vol.
- **Path stability.** A vol-targeted strategy's realized volatility stays roughly constant through regime changes — drawdowns are bounded.
- **Mechanical de-risking in vol spikes.** When the market gets violent, you delever automatically. This is exactly what risk management *should* do.
- **Investor-friendly.** A 10%-vol product can be sized to a client's overall portfolio without surprise; a Kelly-sized product cannot.

Empirical evidence: Moreira & Muir (2017, *JF* 72), "Volatility-Managed Portfolios," showed that vol-scaling factor portfolios (HML, UMD, RMW, market) significantly improved Sharpe and reduced drawdowns. Hurst-Ooi-Pedersen (2017) AQR's trend-following paper uses vol targeting as the core sizing rule across 100+ years of data.

The catch: vol targeting **delevers into drawdowns**. If your strategy has a regime where vol spikes but then mean-reverts, you'll have cut exposure precisely when forward returns are highest. This is the cost of path-friendliness — see Cederburg-O'Doherty-Wang-Yan (2020) for the academic critique.

## Risk parity — vol targeting at portfolio level

Asness, Frazzini, Pedersen (2012), "Leverage Aversion and Risk Parity" (*FAJ* 68): construct a portfolio where **each asset contributes equal volatility**, then lever the whole portfolio to a target. Bonds get levered up (low individual vol), equities held at unit weight or slightly under.

The historical Sharpe of vol-balanced multi-asset portfolios (60/40 vol-equivalent versions) has been substantially higher than naive 60/40, with comparable returns and lower drawdowns. This is the same trade as [[low-vol-factor|BAB / betting against beta]] at the portfolio level.

## Practitioner recipe

1. **Estimate forward vol** (EWMA with λ=0.94, or 30-day rolling, or GARCH(1,1)).
2. **Set position size** as `target_vol / σ̂ · signal_strength`.
3. **Cap leverage** at a hard maximum (e.g., 3-5×) to handle low-vol regimes safely.
4. **Floor de-leveraging** so a vol spike doesn't take you completely flat.
5. **Rebalance weekly or daily**, not intra-day — vol updates are noisy at high frequency.
6. **Cost-adjust.** Aggressive vol-targeting generates turnover. Compute net Sharpe after [[transaction-costs|transaction costs]]; reduce target vol or rebalance frequency if costs dominate.

## Common pitfalls

- **Using realized recent vol unchurned.** Last week's vol is a noisy forecast; use a smoothing window long enough to be stable.
- **Vol spike during gaps.** Overnight gaps (earnings, weekend news) can blow out σ̂ even if intraday vol is calm. Use a model that handles this (e.g., vol of squared returns, not just close-to-close).
- **Single-asset Kelly on portfolio.** Kelly across assets requires the covariance matrix, not just diagonal. The portfolio Kelly is `f* = Σ⁻¹ μ`, which is the mean-variance portfolio.
- **Ignoring leverage capacity.** Vol targeting can demand 4× leverage on a bond strategy. Make sure financing is actually available at the rates your backtest assumes.
- **Drawdown sizing.** Some shops add a "drawdown control" overlay (de-lever 50% if MTD < −5%). Backtests of this *must* respect the rule in-sample to avoid look-ahead.

## See also

- [[risk-adjusted-returns|Risk-adjusted returns]] — Sharpe is what Kelly maximizes (under Gaussian)
- [[transaction-costs|Transaction costs]] — turnover from frequent sizing
- [[drawdown-and-risk|Drawdowns and path-dependent risk]] — why full Kelly is intolerable
- [[probability-essentials|Probability essentials]] — parameter-uncertainty argument for fractional Kelly

## External references

- Kelly (1956). "A New Interpretation of Information Rate." *Bell System Technical Journal* 35(4).
- Thorp (2006). "The Kelly Criterion in Blackjack, Sports Betting, and the Stock Market." *Handbook of Asset and Liability Management* (Elsevier).
- MacLean, Thorp, Ziemba (2010). *The Kelly Capital Growth Investment Criterion*. World Scientific.
- Moreira & Muir (2017). "Volatility-Managed Portfolios." *JF* 72(4).
- Asness, Frazzini, Pedersen (2012). "Leverage Aversion and Risk Parity." *FAJ* 68(1).
- Hurst, Ooi, Pedersen (2017). "A Century of Evidence on Trend-Following Investing." AQR.
- López de Prado (2018). *Advances in Financial Machine Learning*. Wiley.
