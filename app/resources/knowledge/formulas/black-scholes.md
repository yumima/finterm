**Black-Scholes** provides a closed-form price for a European call or put option assuming continuous trading, lognormal returns, constant volatility, and no dividends — it is the foundational model for options pricing.

```
Call price:  C = S × N(d₁) − K × e^(−rT) × N(d₂)
Put price:   P = K × e^(−rT) × N(−d₂) − S × N(−d₁)

d₁ = [ ln(S/K) + (r + σ²/2) × T ] / (σ × √T)
d₂ = d₁ − σ × √T

S   = current stock price
K   = strike price
T   = time to expiration (in years)
r   = continuously compounded risk-free rate
σ   = annualized implied volatility of the underlying
N() = standard normal cumulative distribution function
```

## Worked example — AAPL at-the-money call

```
S = $175     (AAPL current price)
K = $175     (at-the-money strike)
T = 0.25     (3 months = 90/365 years)
r = 0.045    (4.5 % risk-free rate)
σ = 0.28     (28 % implied volatility)

d₁ = [ ln(175/175) + (0.045 + 0.28²/2) × 0.25 ] / (0.28 × √0.25)
   = [ 0 + (0.045 + 0.0392) × 0.25 ] / (0.28 × 0.5)
   = [ 0.0842 × 0.25 ] / 0.14
   = 0.02105 / 0.14
   = 0.1504

d₂ = 0.1504 − 0.28 × 0.5 = 0.1504 − 0.14 = 0.0104

N(d₁) = N(0.1504) ≈ 0.5598
N(d₂) = N(0.0104) ≈ 0.5041

C = 175 × 0.5598 − 175 × e^(−0.045 × 0.25) × 0.5041
  = 97.97 − 175 × 0.9889 × 0.5041
  = 97.97 − 87.24
  = $10.73

Put (via put-call parity): P = C − S + K×e^(−rT)
= 10.73 − 175 + 175 × 0.9889 = 10.73 − 175 + 173.06 = $8.79
```

## What the result means

The call price ($10.73) is what the model says the 3-month ATM call is worth under the given volatility assumption. If the market trades the option at $12, the **implied volatility** is higher than 28 %. Options traders rarely use Black-Scholes to price — they use it to translate between prices and implied volatility (IV).

**The Greeks** (sensitivity measures derived from Black-Scholes):

| Greek | Measures | Formula (call) |
|---|---|---|
| Delta (Δ) | Price sensitivity to S | N(d₁) |
| Gamma (Γ) | Rate of change of Delta | φ(d₁) / (S σ √T) |
| Theta (Θ) | Time decay per day | Complex; negative for long options |
| Vega (ν) | Sensitivity to σ | S × φ(d₁) × √T |
| Rho (ρ) | Sensitivity to r | K × T × e^(−rT) × N(d₂) |

## Variants

- **Black-Scholes-Merton** — extends Black-Scholes for continuous dividend yield q; replace S with S×e^(−qT).
- **Binomial model** — lattice-based; handles American options (early exercise) and discrete dividends.
- **Heston model** — adds stochastic volatility; captures the vol smile that Black-Scholes misses.

## Common mistakes

- Treating Black-Scholes price as the "true" price — the model's normal-distribution assumption underestimates tail risk (fat tails in equity returns).
- Ignoring the volatility smile/skew — implied volatility is not constant across strikes; Black-Scholes with a single σ is a simplification.
- Using historical volatility instead of implied volatility — for pricing, IV is the relevant input; historical vol is a starting point, not the answer.
- Applying to American options without adjustment — Black-Scholes gives the price of a European option; early exercise premium requires Binomial or BAW approximation.

See also: [[put-call-parity-formula|Put-Call Parity]], [[duration-formula|Duration]] (for bond sensitivities), [[var-parametric|VaR]].
