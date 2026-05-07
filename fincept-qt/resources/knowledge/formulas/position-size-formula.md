**Position sizing** formulas determine how much capital to allocate to a trade based on the account size, risk parameters, and edge — getting this wrong is how disciplined traders blow up accounts despite being right on direction.

## The three main frameworks

### 1. Fixed Fractional (Risk-Based)

```
Position Size = (Account × Risk %) / (Entry Price − Stop Price)

Account      = total trading capital ($)
Risk %       = fraction of account to risk on this trade (e.g., 1–2 %)
Entry Price  = planned entry
Stop Price   = stop-loss level (max acceptable loss per share)
```

### 2. R-Multiple Sizing

```
Dollar Risk per trade (1R) = Account × Risk %
Shares = 1R / (Entry − Stop)
```

R-multiples express profit/loss in units of initial risk: a 3R winner means the trade earned three times what was risked.

### 3. Kelly Criterion (see [[kelly-criterion|Kelly]])

```
f* = (bp − q) / b    (optimal fraction for defined-outcome bets)
f* = (μ − Rf) / σ²   (continuous return version)
```

## Worked example — fixed fractional sizing

```
Account size              $100,000
Max risk per trade        1 %  → $1,000
Entry (AAPL long)         $175.00
Stop-loss                 $168.50
Risk per share            $175.00 − $168.50 = $6.50

Position size = $1,000 / $6.50 = 153 shares
Dollar exposure           153 × $175 = $26,775  (26.8 % of account)
```

Risk is fixed at $1,000 regardless of share price or volatility — a wide stop produces fewer shares; a tight stop produces more shares.

## What the result means

The primary goal of position sizing is **controlling account-level risk** per trade:

- **1 % risk per trade** — 10 consecutive losses reduce account by ~9.6 %; survivable.
- **5 % risk per trade** — 10 consecutive losses reduce account by ~40 %; potentially fatal.
- **25 % risk per trade** — 4 consecutive losses reduce account by ~68 %; ruin territory.

Kelly maximizes long-run growth but requires exactly known edge (p and b) — use half-Kelly in practice to reduce variance and protect against estimation error.

## Variants

- **Percent-of-equity** — size each position as a fixed % of account regardless of volatility (e.g., always 5 % of portfolio); simpler but ignores that some positions are inherently riskier than others.
- **Volatility-normalized** — target a fixed dollar move per unit of ATR (Average True Range):
  ```
  Shares = Target Dollar Volatility / (ATR × price)
  ```
  Common in trend-following systems; ensures equal volatility contribution from each position.
- **Portfolio VaR budgeting** — allocate positions so that each contributes an equal fraction of total portfolio [[var-parametric|VaR]]; used in systematic multi-asset funds.

## Common mistakes

- Setting a stop too tight to avoid a large position, then getting stopped out by noise — the stop must be placed at a technically meaningful level, not sized backwards from a desired position.
- Ignoring correlated positions — two positions with 1 % individual risk but 0.9 correlation effectively double your risk; aggregate exposure matters.
- Using the same risk % regardless of conviction or setup quality — scaling risk with edge quality (higher R expected, tighter setup) improves risk-adjusted returns.
- Failing to reduce size during drawdown — Kelly and fixed fractional naturally reduce size as account shrinks; never manually override this by "trading bigger to make it back."

See also: [[kelly-criterion|Kelly Criterion]], [[var-parametric|VaR]], [[max-drawdown|Max Drawdown]], [[sharpe-ratio|Sharpe Ratio]].
