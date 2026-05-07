**Correlation** measures the statistical relationship between two variables — in finance, typically the degree to which two assets' returns move together, ranging from −1 (perfect opposite movement) to +1 (perfect identical movement).

Correlation is the mathematical foundation of diversification: assets with low or negative correlation reduce portfolio volatility even when each individual asset is volatile.

## Formula

```
Correlation (ρ) = Cov(X, Y) / (σX × σY)

Where:
Cov(X, Y) = Covariance of X and Y returns
σX, σY = Standard deviations of X and Y

Range: −1.0 ≤ ρ ≤ +1.0
```

## Correlation interpretation

| Correlation | Relationship | Portfolio Benefit |
|---|---|---|
| +1.00 | Perfect positive | No diversification |
| +0.7 to +1.0 | Strong positive | Low diversification |
| +0.3 to +0.7 | Moderate positive | Some diversification |
| −0.3 to +0.3 | Weak / uncorrelated | Good diversification |
| −0.3 to −0.7 | Moderate negative | Strong diversification |
| −0.7 to −1.0 | Strong negative | Maximum diversification |
| −1.00 | Perfect negative | Perfect hedge (can eliminate variance) |

## Worked example — portfolio variance

```
Portfolio: 50% Stock A (σ=20%), 50% Stock B (σ=15%)

Case 1: ρ = +1.0 (perfectly correlated)
Portfolio σ = 0.5×20% + 0.5×15% = 17.5% (no diversification benefit)

Case 2: ρ = 0.0 (uncorrelated)
Portfolio σ = √(0.5²×20%² + 0.5²×15%²) = √(100+56.25) = √156.25 = 12.5%
(diversification reduced vol from 17.5% to 12.5%)

Case 3: ρ = −1.0 (perfectly negative)
Can construct a zero-variance portfolio by optimal weighting
```

## Common asset correlations (approximate, 2000s–2020s)

| Pair | Typical Correlation | Notes |
|---|---|---|
| US large-cap / US mid-cap | +0.90–+0.95 | High; same economy |
| US large-cap / International developed | +0.80–+0.90 | Globalization increased correlation |
| US equities / US bonds | −0.30 to +0.20 | Regime-dependent (negative in deflation) |
| Equities / Gold | −0.10 to +0.20 | Low; gold is crisis hedge |
| Equities / Bitcoin | +0.20 to +0.60 | Rising with institutional adoption |
| US / EM equities | +0.60–+0.80 | High in crisis; lower in normal times |

## Correlation regime change

Equity-bond correlation changed dramatically post-2022:

```
Pre-2022 (low inflation regime):
  US equities / US bonds: ~−0.30 (bonds were diversifiers)
  
Post-2022 (inflationary regime):
  US equities / US bonds: ~+0.20 to +0.40
  Both fell together as rates rose; bonds stopped diversifying
```

Traditional 60/40 portfolios suffered in 2022 precisely because the equity-bond correlation flipped positive.

## Crisis correlation

In market crises, correlations spike toward +1.0 across most risky assets — the "flight to safety" effect. This is why diversification often "fails" when needed most:

```
Normal times: Portfolio of 10 uncorrelated assets
Crisis: All correlations spike to +0.8
Portfolio now nearly as volatile as its individual components
```

## Pitfalls

- Historical correlation is not stable — it shifts with market regime. Using a 1990s correlation matrix for a 2024 portfolio is dangerous.
- Correlation measures linear relationships; non-linear dependencies (tail co-movements) require copula analysis or stress testing.
- "Diversification across non-correlated strategies" often fails in liquidity crises — everything gets sold simultaneously regardless of fundamental relationship.
- Correlation between two assets can appear low but they may have high **tail correlation** (move together in extreme events only).

See also: [[beta|Beta]], [[volatility|Volatility]], [[sharpe-ratio|Sharpe Ratio]], [[rebalancing|Rebalancing]], [[tail-risk|Tail Risk]], [[mean-reversion|Mean Reversion]].
