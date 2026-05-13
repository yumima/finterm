# Index Volatility Targeting

**Volatility targeting** is the simplest tactical risk-management overlay in finance: dynamically scale exposure to an index to maintain a **constant realised volatility**. When vol rises, reduce exposure; when vol falls, increase it. The strategy has been documented to improve risk-adjusted returns across markets and decades.

Kakushadze & Serur (2018) §6.5 give the construction in two paragraphs. Modern academic work (**Moreira & Muir 2017 "Volatility-Managed Portfolios"**, **Harvey, Hoyle, Korgaonkar, Rattray, Sargaison, Van Hemert 2018**) has made vol targeting one of the most-validated systematic strategies in the contemporary literature.

## The rule

For a risky asset (e.g., the S&P 500 via SPY or futures) with current volatility `σ` and a chosen vol target `σ*`:

```
Risky-asset weight:  w  =  σ* / σ
Risk-free weight:    (1 − w)
```

If actual vol is above target → reduce exposure (less than 100%).
If actual vol is below target → increase exposure (above 100%, leveraged).

Typical implementations:
- **Target vol**: 10–15% annualised (close to historical average S&P vol).
- **Vol estimate**: trailing 21-day realised vol, or implied vol from VIX, or some forecasting model.
- **Maximum leverage** `L`: cap `w` at e.g. 2.0 (200% leverage limit).
- **Rebalance**: daily, weekly, or threshold-triggered.

The book notes: "rebalancing (instead of periodically) can be done based on a preset threshold `κ`, say, only if the percentage change |Δw|/w since the last rebalancing exceeds `κ`."

## Why does this work?

The strategy is **not** a higher-return strategy. It's a **higher-Sharpe** strategy. The mechanism:

### 1. Equity vol is negatively correlated with returns

This is a well-documented stylised fact: when equity vol is high, expected returns are *lower* on average (because the high vol typically coincides with crises, recessions, panic-selling regimes). When vol is low, expected returns are higher (calm periods reflect functioning capital markets).

**Moreira & Muir (2017)** formalised this: a strategy that reduces equity exposure when vol rises **earns the same return with less risk**. The Sharpe improvement is substantial in their sample.

### 2. Vol clustering

Volatility is **persistent**: high-vol days cluster together (GARCH-style behaviour). When realised vol rises, **future vol is likely to remain high** for days-to-weeks. So scaling down exposure on high-vol days protects against a continuing high-vol regime, not just the past one.

### 3. Drawdown control

The most practical benefit. Vol targeting **caps drawdowns** dramatically:
- A buy-and-hold S&P portfolio had a 50%+ drawdown in 2008.
- A 10%-vol-targeted S&P portfolio had a 25-30% drawdown — half as deep.

In Calmar ratio terms, vol targeting more than doubles the drawdown-adjusted return.

## Empirical record

**Harvey et al. (2018) "The Impact of Volatility Targeting"** tested vol targeting on 60+ markets from 1947 to 2017:

- **Equities**: Sharpe improvement of ~0.2 across S&P 500, FTSE, Nikkei, DAX, etc. Drawdown reduction substantial (often 50%).
- **Bonds**: Less improvement (~0.1 Sharpe). Bond vol is more stable; less to gain.
- **Commodities**: Mild improvement, varies by commodity.
- **Currencies**: Modest improvement.

The largest benefits accrue in **markets with strong vol-return negative correlation** (equities, especially equity indices). The smallest benefits in markets where vol is more stable (developed-market currencies, short-end bonds).

## Variations

### Vol target on implied vol (forward-looking)

Instead of trailing realised vol, use implied vol from VIX. This is **forward-looking** and adjusts faster to regime changes:

```
w = σ* / VIX
```

When VIX spikes, exposure cuts immediately, not after several days of realised vol.

Trade-off: VIX can spike on speculation without underlying realised vol moving. False positives.

### Mixed vol estimate

Combine realised and implied:

```
σ_estimate = α · σ_realised + (1 − α) · VIX
```

with `α` typically 0.5. Balances responsiveness and stability.

### Threshold rebalancing

Don't rebalance daily — only when the desired position deviates from the current position by more than `κ` (e.g., 5%). Reduces transaction costs.

### Multiple-horizon vol estimate

Use both short-window (21-day) and long-window (252-day) vol estimates. Long-window stabilises; short-window catches regime shifts.

### Vol-target on a multi-asset portfolio

Apply vol targeting at the **portfolio level** across equity + bond + commodity holdings. Used by managed-volatility funds and "constant-volatility" target-date strategies (Sharpe & Perold 1988 dynamic asset allocation).

## When vol targeting fails

The strategy is **not** a free Sharpe improvement. Failure modes:

### 1. Sudden recoveries after vol spikes

Vol targeting scales down exposure when vol spikes (March 2020). If the market recovers sharply *while vol is still measured-high*, the strategy is **under-allocated to the recovery**. The April–June 2020 rally was so fast that vol-targeted portfolios captured maybe 60–70% of buy-and-hold returns in that window.

This is the classic vol-target failure mode: **whipsaws around vol regime changes**.

### 2. Transaction costs on frequent rebalancing

Daily rebalancing on volatile periods generates substantial turnover. In a regime where realised vol moves 5%+ per day, weight changes accumulate. Threshold-based rebalancing (`κ` rule) mitigates this.

### 3. Slow regime-change adaptation

If you're using 21-day realised vol, you'll lag a regime change by 10 days. A faster window is more responsive but more noisy.

### 4. Inverse-vol-return relationship breaks

The empirical negative vol-return relationship is strong in equities. In some asset classes (commodities sometimes, FX often), the relationship is weaker or even positive. Vol-targeting in those markets adds little Sharpe.

## What's behind the math

The strategy is essentially **constant-Sharpe-target** under the assumption that expected returns are roughly constant. Then:

```
Expected return per unit risk = μ / σ
Target risk = σ*
Target weight = σ* / σ
Then: actual return contribution = w · μ = (σ*/σ) · μ
```

If `μ` were constant, this would just adjust risk while preserving expected return. The **realised Sharpe improvement** comes from `μ` being **lower** when `σ` is high — so by reducing weight when `σ` is high, you avoid the worst returns.

This is the **risk-return-tradeoff regime dependence** that drives vol targeting's empirical success.

## Variants for risk-averse investors

### Downside-vol-only targeting

Use **downside semi-variance** instead of full variance. Reduces exposure only when downside vol rises (not when upside vol rises). Slightly better empirically in equities.

### Trailing-stop vol target

If realised drawdown exceeds X%, reduce exposure regardless of vol level. Catches the cases where vol is "low" but the market is grinding lower.

### CPPI overlay

Combine vol targeting with constant-proportion portfolio insurance (CPPI). When drawdown approaches a floor, exposure goes to zero. More aggressive risk control.

## Common mistakes

- **Wrong vol window choice**. 5-day vol is noisy; 252-day vol is too slow. 21-day is the practitioner default.
- **No leverage cap**. In low-vol regimes (VIX < 10), the strategy wants `w > 1` (leveraged). Without a cap (`L = 2` or `L = 1.5`), you can be aggressively leveraged in regimes where realised vol is about to spike (the classic "calm-before-the-storm" failure).
- **Daily rebalance with high costs**. Threshold rebalancing is better.
- **Ignoring funding cost when w > 1**. The risk-free leverage cost in the formula is real; for retail it's higher than for institutions.

## Risk management essentials

- **Leverage cap**: typically `L = 1.5` or `L = 2.0`. Prevents the strategy from getting blown up in low-vol-then-spike regimes.
- **Vol estimate ensembling**: use multiple horizons.
- **Threshold rebalancing**: 5% threshold typical.
- **Asset-class diversification**: don't run vol-target on a single asset; combine across asset classes.

## Where to do this in the terminal

- **Portfolio** — vol-targeting tactical overlay; user inputs target vol.
- **AI Quant Lab** — vol-targeting template with all variant choices.
- **Backtesting** — historical vol-targeted strategy P&L with regime decomposition.

## See also

- [[volatility-quant|Volatility Trading and the Surface]] — the broader vol context
- [[etf-multi-asset-trend|Multi-Asset Trend Following]] — often combined with vol targeting
- [[index-cash-and-carry|Cash-and-Carry Arbitrage]] — different index strategy
- [[portfolio-construction|Portfolio Construction]] — Markowitz, risk parity, related frameworks
- [[strategy-patterns|Strategy Patterns]] — vol overlays as a risk-management technique

## External references

- Moreira, A., Muir, T. (2017). "Volatility-Managed Portfolios." *Journal of Finance* 72(4), 1611–1644.
- Harvey, C., Hoyle, E., Korgaonkar, R., Rattray, S., Sargaison, M., Van Hemert, O. (2018). "The Impact of Volatility Targeting." *Journal of Portfolio Management* 45(1), 14–33.
- Anderson, R., Bianchi, S., Goldberg, L. (2014). "Determinants of Levered Portfolio Performance." *Financial Analysts Journal* 70(5), 53–72.
- Perchet, R., de Carvalho, R., Heckel, T., Moulin, P. (2014). "Intertemporal Risk Parity: A Constant Volatility Framework for Factor Investing." *Journal of Investment Strategies* 4(1), 19–41.
- Albeverio, S., Steblovskaya, V., Wallbaum, K. (2013). "Investment Instruments with Volatility Target Mechanism." *Quantitative Finance* 13(10), 1519–1528.
- Cooper, T. (2010). "Alpha Generation and Risk Smoothing Using Managed Volatility." Working paper, SSRN 1664823.
- Torricelli, L. (2018). "Volatility Targeting Using Delayed Diffusions." Working paper, SSRN 2902063.
- Zakamulin, V. (2014). "Dynamic Asset Allocation Strategies Based on Unexpected Volatility." *Journal of Alternative Investments* 16(4), 37–50.
- Sharpe, W., Perold, A. (1988). "Dynamic Strategies for Asset Allocation." *Financial Analysts Journal* 44(1), 16–27.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §6.5. https://doi.org/10.1007/978-3-030-02792-6
