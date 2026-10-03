# Earnings backtest — what is predictable before a print (2026-10-02)

Companion to `plans/PLAN_EARNINGS_REDESIGN.md`. Scripts and raw output in
`plans/research/earnings_backtest/` (`collect.py` → `prints.jsonl`, then
`analyze.py`, then `analyze2.py`; full output in `results_2026-10-02.txt`).

## Data and method

- **236 US names**, mega- to mid-cap across every sector plus high-volatility
  growth names; Yahoo `earnings_dates(limit=60)` and 12 years of daily bars.
- **10,609 prints, 2014-10 → 2026-10; 9,665 evaluable** (at least four prior
  prints for the same name).
- Every number below is measured with **the daemon's own code**:
  `_earnings_price_reaction` (BMO/AMC session alignment, closed sessions
  only), `_pre_event_vol`, and `_surprise_basis_suspect` (4.4% of prints were
  flagged as GAAP-vs-adjusted artefacts and excluded from surprise statistics).
- **Walk-forward:** every print is predicted from that name's earlier prints
  only. Pooled constants were fitted on the first half of calendar time (split
  2021-07-28) and checked on the second half.
- Market-adjusted ("abnormal") moves subtract SPY's move over the same
  session pair. They change no conclusion, so raw moves are used in the
  product.

## 1. Direction — not predictable

| Signal (all walk-forward) | IC vs next move | Hit rate |
|---|---|---|
| **Old tab scorecard** (backward legs, same constants) | **+0.003** (p=0.78) | **50.7%** |
| Prior mean reaction ("reaction history" leg) | −0.024 | 49.3% |
| "Beats get paid" | −0.007 | 49.7% |
| Beat bias / beat IR | +0.007 / +0.009 | — |
| Run-up 5d / 20d | −0.029 / −0.025 | — |
| Scenario-implied P(up) | Brier 0.2505 vs 0.2500 for a coin flip | 49.4% |
| *Always up* | — | *50.9%* |

The old verdict, by bucket: **BUY +0.17%** mean (52.0% up, n=3,621),
HOLD +0.21%, **SELL +0.56%** (49.8% up, n=1,110). The SELL bucket had the
best returns of the three.

The only signal with any sign is short-term reversal: the top third of 20-day
run-ups (above +4.7%) rose 49.3% of the time and the bottom third (below −1.2%)
53.1%. That is too faint for a call and is shown as context only.

## 2. Beat probability — predictable

- Pooled beat rate: **82.4%** (81.4% train, 83.4% test).
- Shrinking a name's last 8 quarters toward the pooled mix as if the pool were
  k extra quarters gives these Brier scores (out of sample):

  | | Brier |
  |---|---|
  | Raw record (k=0) | 0.1405 |
  | k=4 | 0.1318 |
  | **k=8** | **0.1304** |
  | k=16 | 0.1310 |
  | Base rate only | 0.1380 |

- Calibration of k=8 on the test half:

  | Stated | Realised |
  |---|---|
  | 0.56 | 0.62 |
  | 0.66 | 0.71 |
  | 0.76 | 0.78 |
  | 0.85 | 0.85 |
  | 0.91 | 0.92 |

  It is slightly conservative at the low end and well calibrated where most of
  the mass sits.
- A name's prior beat bias predicts its next surprise: **IC +0.29**
  (p≈1e-178).
- "Asked growth" (consensus vs last actual) carries nothing (IC +0.002).

## 3. Size — predictable

| Size forecaster (test half) | MAE (pp) | Corr with \|move\| |
|---|---|---|
| Trailing mean \|move\|, 8 prints | 3.642 | +0.490 |
| Trailing mean \|move\|, all prior | 3.547 | +0.509 |
| **Blend 0.5×trailing8 + 1.0×vol (shipped since 2026-08)** | 3.585 | +0.495 |
| OLS refit (0.75×trailing-all + 0.53×vol) | 3.531 | +0.514 |

The shipped forecast now sizes from up to 12 prior prints. The blend
coefficients stay: the refit gains only 0.05 pp, and the earlier
company-held-out study already chose them.

Ranges, as a ratio of realised |move| to the forecast:

| Quantile | 25% | 50% | 68% | 80% | 90% | 95% |
|---|---|---|---|---|---|---|
| \|move\| ÷ forecast | 0.46 | **0.95** | 1.43 | **1.88** | **2.46** | 3.03 |

So the tab says: half of prints land within 0.95×, four in five within 1.88×,
and one in ten go beyond 2.46×.

## 4. What each outcome has meant

Move expressed in units of the name's own expected move (z):

| Outcome | Share | z mean (all / train / test) | Mean move | Rose on |
|---|---|---|---|---|
| Miss (≤0%) | 17.6% | −0.67 / −0.64 / −0.69 | −2.56% | 34% |
| Slight beat (0–3%) | 18.8% | −0.19 / −0.09 / −0.31 | −0.77% | 44% |
| Solid beat (3–10%) | 31.3% | +0.19 / +0.23 / +0.16 | +0.60% | 55% |
| Big beat (>10%) | 32.3% | +0.41 / +0.40 / +0.43 | +1.83% | 60% |

- **The reaction depends on how much they beat, not whether.** The average
  move turns positive only above a ~3% beat. The slight-beat bucket is
  drifting more negative over time: the expected beat has become the bar.
- **Per-name scenario reactions don't persist.** For "if beat" the IC is
  −0.01. "If miss" does persist (IC +0.15, n=925), but the sample is too thin
  to use per name. The pooled table, scaled to the name's size, is the honest
  version.
- **EPS surprise explains little.** Even with hindsight, the surprise's IC
  with the move is +0.23. Revenue and guidance carry the rest, and Yahoo has
  history for neither.

## 5. Cross-check with the literature

See `earnings_research_2026-10.md`. Points that agree with this backtest:
- No product makes a signed price call.
- About 78% of S&P companies beat (FactSet).
- FactSet's asymmetric reaction is +1.0% on a beat vs −2.9% on a miss; here
  it is +0.77% vs −2.56%.
- Martineau (2022): surprises explain about 12% of announcement returns, and
  PEAD is gone for non-microcaps.
- Options straddles are the best single predictor of size, but their bias
  changes sign from regime to regime. The redesign therefore shows the implied
  move *beside* its own forecast and records both in the ledger. That ledger is
  the only source of implied-vs-realised coverage, because there is no
  historical option data.
