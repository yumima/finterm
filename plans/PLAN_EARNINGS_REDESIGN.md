# Earnings — Redesign

**Status:** Implemented 2026-10-02 (engine, tab, chart, ledger v052, MCP tool `get_equity_earnings_outlook`)
**Replaces:** the BUY / HOLD / SELL pre-earnings scorecard, the signed "predicted move", and the six-predictor comparison chart in ER › Earnings
**Evidence:**
- `plans/research/earnings_backtest_2026-10.md` — own backtest: 9,665 prints, 236 names
- `plans/research/earnings_research_2026-10.md` — products and literature

---

## The verdict in five lines

1. **Stop calling direction.** The scorecard was right 50.7% of the time; "always up" is right 50.9%. Its SELL calls averaged +0.56%. No pre-print input this stack has predicts direction, and no serious product claims to.
2. **Beat probability is real.** A name's own record, shrunk toward the 82% pooled rate (k=8), beats the base rate out of sample (Brier 0.130 vs 0.138) and is calibrated.
3. **Size is real.** The vol-blended expected move works, and so do the ranges stated around it (half within 0.95×, 80% within 1.88×). The options-implied move sits beside it, not in it.
4. **The bar is ~3%, not 0%.** A 0–3% beat has traded down (−0.19× the expected move). Above 3%, the average move turns positive. This is the single most useful thing the tab can teach, so the scenarios are bucketed around it.
5. **Make it checkable.** Every day before a print, the ledger records the beat probability, the expected move and the implied move. The tab grades them once the print settles.

## Layout

```
NEXT REPORT   date · slot · countdown · confirmed │ consensus EPS, range, revenue, YoY, analysts
              headline: "Likely to beat (84%) · expected move ±5.2% · no reliable read on direction. …"
┌ 1 · WILL THEY BEAT? ┐┌ 2 · HOW BIG A MOVE? ┐┌ 3 · WHICH WAY? ──────────────────┐
│ 84%                 ││ ±5.2%  ≈ ±$9.10     ││ NO RELIABLE CALL                 │
│ beat 7/8, median +4%││ half within ±4.9    ││ IF EPS IS…  CHANCE MOVE  ROSE ON │
│ ▇▇▇▇▇▇ outcome strip││ 4 in 5 within ±9.8  ││ miss / slight / solid / big      │
│ ESTIMATE MOMENTUM   ││ OPTIONS ±6.1 (1.2×) ││ ALREADY PRICED IN (context)      │
│ rising +2.1% 30d …  ││ RECENT PRINTS …     ││ caveats                          │
└─────────────────────┘└─────────────────────┘└──────────────────────────────────┘
PAST PRINTS chart — bars: surprise (default) │ line: next-session move │ band: expected move before each print
REPORTED QUARTERS table (+ OUTCOME bucket, EXPECTED)  │  FORECAST RECORD · THIS NAME (size, beat, ledger)
CURRENT consensus trend │ revisions            FUTURE analyst estimates
```

## Decisions

| Kept | Why |
|---|---|
| Expected-move blend (0.5×trailing + 1.0×vol); window raised from 8 to 12 prints | Validated twice; the longer window measured better |
| GAAP-artefact filter, ET session alignment, closed-session-only reactions, ledger hindsight guard | Correctness foundations that the backtest reused |
| Consensus trend, revisions and estimate tables | Context the answers draw on |
| Options event move (quadrature-stripped) | The market's own size estimate; now compared against ours and recorded |

| Dropped | Why |
|---|---|
| BUY/HOLD/SELL, composite score, SETUP/BAR axes, horizon rotation | No directional skill (see the verdict above) |
| Signed predicted move; SCORECARD / EPS MODEL / ADAPTIVE / RUN-UP FIT / MEAN / NO MOVE overlay | Same reason; six lines to interpret and none worth reading |
| Stock-vs-index growth table | Yahoo's INDEX column is a market-wide constant |

| Context, not scored | Why |
|---|---|
| Estimate momentum (30/90d drift, up/down counts) | Yahoo has no history for it, so its value can't be tested. The ledger will accumulate the evidence |
| Run-up, 52-week high, price-vs-estimate race, analyst target | Weak or untestable; stated as plain lines, never folded into a number |

## Follow-ups

- After about 50 settled ledger prints: test whether estimate drift improves the Brier score of the beat probability. Fold it in only if it does.
- Implied-vs-realised coverage from the ledger. Literature says the bias flips by regime, so this is worth tracking per regime before trusting the options figure over ours.
- Revenue surprise needs a revenue-actual history source. Yahoo has none, so the EPS-only label stays.
