# VIX — The "Fear Index"

The CBOE Volatility Index measures **30-day expected volatility of the S&P 500**, derived from the prices of near-term S&P 500 options.

It's quoted as an **annualized standard deviation in percent**. A VIX of 20 implies the market expects S&P 500 returns over the next 30 days to vary at roughly a 20% annualized standard deviation.

## Worked example — VIX → expected daily move

```
VIX = 20%   (annualized vol)
Daily expected 1σ move = 20% / √252  ≈  1.26%

Implication for SPX next 30 days:
  68% probability  SPX moves within ±1.26% per day
  ~95% probability daily moves stay within ±2.5%
  ~99% probability within ±3.8%

If realized vol over the next 30 days actually comes in at 14%:
  → Implied was rich; option sellers won.
If it comes in at 28%:
  → Implied was cheap; option buyers won.
```

## How to read the level

| VIX | Market state |
|---|---|
| 10–15 | Calm / complacent |
| 15–20 | Normal |
| 20–30 | Elevated — something is going on |
| 30–40 | Stressed |
| 40+ | Crisis (GFC ~80, COVID ~85) |

## Historical VIX peaks

| Event | VIX peak |
|---|---|
| Black Monday (Oct 1987, retroactively computed) | ~150 |
| GFC (Oct/Nov 2008, Lehman fallout) | 80–96 |
| Eurozone crisis (Aug 2011) | 48 |
| COVID crash (Mar 16, 2020) | 82.7 |
| Volmageddon (Feb 5, 2018) | 50 |
| 2022 inflation cycle | 36 |
| 2024 Aug 5 yen-carry blowup | 65 (intraday) |

## VIX is not directional

A high VIX doesn't predict the *direction* of moves — only their *size*. But because puts dominate hedging demand, **VIX spikes when stocks fall** and drifts down when stocks rise. The negative correlation is roughly −0.7.

## Variants worth knowing

- **VIX9D / VIX1D** — shorter-dated equivalents (9-day, 1-day)
- **VIX3M / VIX6M** — longer-dated; term structure indicator
- **SKEW Index** — measures left-tail (crash) probability priced into options
- **VVIX** — volatility of volatility; vol of VIX itself
- **MOVE** — Treasury market equivalent of VIX
- **CVIX** — currency volatility index
- **VXN** — Nasdaq-100 equivalent

## Common uses

- **Hedging proxy**: long VIX futures or VIX call options can offset equity drawdowns. Note: VIX itself is not directly tradable — only its derivatives are.
- **Mean reversion**: VIX has historically reverted to its 15–20 range. Strategies that fade VIX extremes have worked in the long run, blown up at exactly the wrong times in the short run.
- **Term structure (VIX1, VIX3, VIX6)**: shape of expected vol over different horizons. Near-term VIX above longer-term ("inverted") signals acute stress.

## Decision rules

- **VIX < 12 sustained** → complacency; consider tail hedges (cheap)
- **VIX 25+ + curve inversion** → acute stress; pause new long-vol entries
- **VIX > 40** → don't buy more vol; capitulation often near
- **VIX falling while SPX falling** → vol crush; bullish signal sometimes
- **SKEW > 150** → tail probability rising; consider put-spread hedges

## Common mistakes

- **Comparing VIX to historical average without context.** Today's "low" VIX may be the start of regime change.
- **Buying VIX after it has already spiked.** By then most of the move is done; you're paying peak premium.
- **Confusing VIX with realized volatility.** VIX is forward-looking implied; the actual subsequent move may be much smaller (cheap protection in retrospect).
- **Holding long VIX futures for weeks** — contango drag is severe (-50%+/year typical).

## In finterm

VIX is on the Markets macro panel and Dashboard key-indices band. Watch the spread between VIX and realized SPX vol — it's a real-time gauge of how expensive insurance has become.
