**The Gordon Growth Model** (also called the Dividend Discount Model) prices a stock as the present value of its future dividends, assuming those dividends grow at a constant rate forever.

```
P = D₁ / (r − g)

P   = intrinsic value (fair price) of the stock today
D₁  = expected dividend one year from now  =  D₀ × (1 + g)
r   = required rate of return (cost of equity)
g   = constant perpetuity growth rate of dividends
```

The formula collapses an infinite dividend series into a single fraction — valid only when r > g.

## Worked example — Coca-Cola (KO), May 2026

```
Current annual dividend (D₀)    $2.00
Expected dividend growth (g)     5 %
Cost of equity (r)              9 %
  (Rf = 4.5 %, β = 0.55, ERP = 5.5 % → Re = 4.5 + 0.55 × 5.5 = 7.5 %
   — add ~1.5 % premium for yield investors; use 9 % here)

D₁ = 2.00 × 1.05 = $2.10

P = 2.10 / (0.09 − 0.05)
  = 2.10 / 0.04
  = $52.50
```

If KO trades at $65, the Gordon Growth Model says it is overvalued under these assumptions — or that the market is pricing in higher growth or a lower required return.

## What the result means

The model is extremely sensitive to the spread between r and g. Halve that spread (e.g., from 4 % to 2 %) and the fair value doubles. Practically, GGM is most useful as:

1. A sanity check on implied growth: rearrange to `g = r − D₁/P` to back out what growth rate is baked into the current price.
2. A terminal value formula inside a [[dcf|DCF]] (the most common usage).
3. A quick valuation for mature, stable dividend payers (utilities, consumer staples).

## Variants

- **Multi-stage DDM** — uses higher near-term growth rates before settling into a long-run perpetuity rate; more realistic for companies still growing dividends rapidly.
- **H-model** — assumes growth declines linearly from a high rate to a stable rate; semi-explicit approximation between single-stage and full multi-stage.
- **FCFE model** — replaces dividends with free cash flow to equity when dividends are not representative of earning power (companies that hoard cash or buy back shares).

## Common mistakes

- Setting g at or above r — the formula produces a negative or infinite value, which is mathematically meaningless.
- Using the current dividend yield directly as D/P without adjusting for next year's dividend — the numerator should be the *forward* dividend D₁.
- Applying GGM to high-growth companies — the constant-growth assumption breaks down when g is expected to change significantly over the next decade.
- Ignoring share buybacks — for companies that return cash through buybacks rather than dividends, DDM understates true cash yield.

See also: [[dcf|DCF]] for the multi-period version, [[wacc|WACC]] for deriving the discount rate, [[pe-formula|P/E]] for an alternative valuation frame.
