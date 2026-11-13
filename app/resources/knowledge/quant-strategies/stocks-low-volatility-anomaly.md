# Low-Volatility Anomaly (BAB)

The **low-volatility anomaly** is one of the most counterintuitive findings in equity markets: **low-volatility (and low-beta) stocks earn higher risk-adjusted returns than high-volatility (and high-beta) stocks**. This violates the CAPM prediction that higher risk should compensate with higher expected return, and has been documented over decades and across countries. **Frazzini & Pedersen (2014) "Betting Against Beta"** is the modern canonical reference.

Kakushadze & Serur (2018) §3.4 give the construction in one paragraph. The deeper story — why a violation of CAPM has persisted for 50+ years — connects to **leverage constraints**, **lottery preferences**, and **benchmark-driven institutional behaviour**.

## The empirical fact

Sort US stocks each month by trailing realised volatility (or beta). Form portfolios.

Over 1972–2014 (Frazzini-Pedersen sample):
- **Low-vol portfolio** earned ~1% per month with vol ~3.5%/month — Sharpe ~0.85.
- **High-vol portfolio** earned ~0.6% per month with vol ~7%/month — Sharpe ~0.30.
- A long-low-vol / short-high-vol portfolio (the "BAB factor") earned **~0.8% per month with Sharpe ~0.78**.

This is roughly **3× the Sharpe of the market itself**. And it goes in the wrong direction for CAPM — which predicts the opposite.

## The signal

Kakushadze & Serur §3.4:

```
σ_i  =  historical volatility of stock i over a lookback window (3–12 months)

Long the bottom decile by σ_i  (low-vol stocks).
Short the top decile by σ_i  (high-vol stocks).
```

Alternative formulations use **beta** to the market (`β_i`) instead of total volatility. Results are similar.

## Why does this exist?

The persistence of this anomaly for 50+ years is itself remarkable. Three explanations, all probably correct in part:

### 1. Leverage aversion (Frazzini & Pedersen 2014)

Many investors are **constrained from using leverage** — pensions, mutual funds, retail. They want higher returns but can't borrow to buy more low-beta stocks. So they instead bid up high-beta stocks (which give them "leverage in a long-only wrapper").

The result: high-beta stocks become overpriced, low-beta stocks become underpriced. The premium is the gap.

**Frazzini & Pedersen** model this formally and show it generates the empirical pattern. They also show the BAB factor performs poorly during funding crises (when even unconstrained investors face leverage constraints), which is consistent with the leverage-aversion theory.

### 2. Lottery preferences (Bali, Brown, Cakici, Whitelaw 2011)

Investors over-pay for "lottery" stocks — those with high volatility, positive skewness, and the chance of a big payoff. Kumar (2009) showed retail investors disproportionately hold lottery-like stocks; Bali et al. (2011) showed these stocks systematically underperform.

This is the same story as [[commodity-skewness-premium|the commodity skewness premium]] — investors over-pay for tail-upside exposure across asset classes.

### 3. Benchmark-relative incentives

Active managers are evaluated on **tracking error** to a benchmark. Holding low-beta stocks creates tracking error when the market rallies (you'll underperform). So managers tilt toward high-beta to keep up — even when that's not the highest-Sharpe choice.

Baker, Bradley, Wurgler (2011) "Benchmarks as Limits to Arbitrage" formalised this. The benchmark-relative incentive prevents managers from rotating into the low-vol trade even when they recognise the opportunity.

## Variants

### BAB (Betting Against Beta) — Frazzini-Pedersen

The most-cited construction. Define:
```
z_i = rank of β_i (cross-sectional)
w_i = z_i − z̄        (demean)
```

Long-leg weights `w_L_i = (negative w_i, scaled so the leg has β = 1)`.
Short-leg weights `w_H_i = (positive w_i, scaled so the leg has β = 1)`.

The BAB factor is **leverage-adjusted**: the long leg is levered up to beta = 1, the short leg is delevered to beta = 1. This makes the long and short legs equally risky on a beta basis.

### Idiosyncratic volatility version (Ang, Hodrick, Xing, Zhang 2006, 2009)

Sort by **idiosyncratic volatility** — the residual vol from a Fama-French factor model. Same direction: low-IVol outperforms high-IVol. Has the advantage of stripping out the size/value/momentum components and isolating the pure vol effect.

### Quality-adjusted (Asness, Frazzini, Pedersen 2019)

Combine low-vol with quality (profitability, growth stability, payout). The "Quality-Minus-Junk" (QMJ) factor and the low-vol factor are positively correlated and additive.

## When the strategy works — and fails

Low-vol has been a remarkably stable strategy. But it has failure modes:

- **Bull market accelerations.** When the market rallies hard, high-beta stocks lead. 1999–2000 (tech bubble), 2009 (post-Lehman recovery), 2020 Q2–Q4 (post-COVID rally). BAB suffered in each.
- **Funding crises.** When leveraged carry trades unwind, even low-vol stocks sell off because they're held in leveraged structures. October 2008, March 2020. BAB lost during these despite its long-run track record.
- **Crowded BAB regimes.** Post-2014, with many "min-vol" ETFs launched (USMV, SPLV, EEMV, ACWV), the BAB premium decayed. By 2018, the factor was earning closer to Sharpe 0.3 net than the 0.7 documented in academic samples.

## The 2008 and 2020 episodes

**2008**: BAB lost ~30% from peak to trough as financial-sector stocks (typically high-beta) crashed alongside the broader market. The high-vol short leg, paradoxically, *also* crashed, so the long-short trade was less hurt than expected — but still down materially.

**2020 March**: Sharp drawdown for BAB. Low-vol "defensive" stocks (utilities, staples) did not provide their usual cushion because the COVID selloff was simultaneously a liquidity crisis. By Q3 2020, BAB recovered modestly; the post-March rally favored high-beta stocks.

## Practical implementation

- **Lookback window**: 3–12 months of daily returns. Shorter is more responsive; longer is more stable.
- **Universe filter**: stick to liquid stocks (top 80% of market cap). Micro-caps have noisy vol estimates.
- **Rebalance frequency**: monthly is standard. Quarterly reduces turnover but introduces stale-signal risk.
- **Beta vs. vol**: in practice, beta-based BAB and vol-based low-vol have ~85% correlation. Either works. Beta has the leverage-adjustment property; vol is simpler.
- **Sector neutralisation**: optional. Without it, BAB tilts toward defensive sectors (utilities, staples). With it, the factor captures stock-level low-vol effects within sectors.

## Common mistakes

- **Using too short a lookback.** Volatility is noisy. 1-month lookback gives jittery signals; 3–6 months is the sweet spot.
- **Equal-weighting long and short without leverage adjustment.** Without scaling each leg to equal beta, the long-low-beta leg has less market exposure than the short-high-beta leg, contaminating the signal with a directional market bet.
- **Ignoring borrow costs.** High-vol short-leg stocks (especially distressed ones) have high borrow costs that eat short-leg returns.
- **Treating BAB as defensive.** It's positively correlated with the market in normal times and especially during high-vol periods. It's not a hedge.

## Risk management essentials

- **Cap leverage on the long leg.** The Frazzini-Pedersen construction levers the long leg up to beta = 1. In a fund context, total leverage must be capped to avoid funding-shock vulnerability.
- **Monitor funding-stress signals.** When LIBOR-OIS or TED spread blows out, BAB historically suffers. Reduce exposure preemptively.
- **Sector caps.** Without limits, BAB ends up long utilities, staples, healthcare and short small-cap biotech, energy, financials. Diversify across the long-leg sectors.
- **Pair with momentum or quality**: standalone BAB has drawdown years; the combined factor portfolio is much smoother.

## Where to do this in the terminal

- **AI Quant Lab** — low-vol / BAB strategy template with beta vs. vol signal toggle.
- **Equity Research** — single-stock beta and idiosyncratic-volatility metrics.
- **Backtesting** — historical BAB P&L with leverage analysis and funding-shock overlays.

## See also

- [[stocks-multifactor|Multifactor Portfolio]] — low-vol combined with value, momentum, quality
- [[stocks-residual-momentum|Residual Momentum]] — idiosyncratic-vol-related cousin factor
- [[fi-bond-factors|Bond Factor Investing]] — the same low-risk anomaly in bonds
- [[strategy-patterns|Strategy Patterns]] — BAB as one of the canonical premia

## External references

- Frazzini, A., Pedersen, L. (2014). "Betting Against Beta." *Journal of Financial Economics* 111(1), 1–25.
- Ang, A., Hodrick, R., Xing, Y., Zhang, X. (2006). "The Cross-Section of Volatility and Expected Returns." *Journal of Finance* 61(1), 259–299.
- Ang, A., Hodrick, R., Xing, Y., Zhang, X. (2009). "High Idiosyncratic Volatility and Low Returns: International and Further U.S. Evidence." *Journal of Financial Economics* 91(1), 1–23.
- Baker, M., Bradley, B., Wurgler, J. (2011). "Benchmarks as Limits to Arbitrage: Understanding the Low-Volatility Anomaly." *Financial Analysts Journal* 67(1), 40–54.
- Bali, T., Brown, S., Cakici, N. (2011). "Maxing Out: Stocks as Lotteries and the Cross-Section of Expected Returns." *Journal of Financial Economics* 99(2), 427–446.
- Kumar, A. (2009). "Who Gambles in the Stock Market?" *Journal of Finance* 64(4), 1889–1933.
- Black, F. (1972). "Capital Market Equilibrium with Restricted Borrowing." *Journal of Business* 45(3), 444–455.
- Blitz, D., van Vliet, P. (2007). "The Volatility Effect: Lower Risk Without Lower Return." *Journal of Portfolio Management* 34(1), 102–113.
- Asness, C., Frazzini, A., Pedersen, L. (2019). "Quality Minus Junk." *Review of Accounting Studies* 24(1), 34–112.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §3.4. https://doi.org/10.1007/978-3-030-02792-6
