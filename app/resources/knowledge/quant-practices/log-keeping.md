# Log-Keeping

Memory is the worst dataset in quant research: noisy, compressed, biased by recency, and reconstructed selectively after every outcome. A research journal, a decision diary, a trade log, and a structured post-mortem are the cheap interventions that turn a career's worth of trial-and-error into actual learning rather than a sequence of forgotten near-misses.

Kahneman, Lovallo & Sibony's "Before You Make That Big Decision" (*Harvard Business Review*, June 2011) introduced the **decision diary** as the practical antidote to hindsight bias for individual decision-makers. Philip Tetlock's *Superforecasting* (2015) showed that systematic logging of probabilistic forecasts and outcomes — and only that — is what separates the top 2% of forecasters from the rest. The discipline is well-tested. It's also dull and almost nobody does it consistently. The few who do compound advantage that the rest cannot match.

## Four logs every quant researcher needs

These are distinct artifacts with different cadences and purposes; do not collapse them into one journal.

- **Research journal.** Hypothesis-first records (see [[hypothesis-first|hypothesis-first research]]), backtest outcomes, data-cleaning decisions. One entry per research session.
- **Decision diary.** Forecasts and decisions with explicit reasoning, recorded at decision time. Reviewed monthly/quarterly.
- **Trade log.** Every live trade: entry, exit, sizing, rationale, P&L. Synced from the broker, annotated by you.
- **Post-mortem file.** Per-strategy and per-incident: what failed, what you learned, what changes.

## What every research-journal entry should capture

The non-negotiable fields for a research session:

```
1. Date and time.
2. Hypothesis (or working question). Pre-registered if confirmatory.
3. Data. Source, vintage, universe, date range, any cleaning applied.
4. Method. Signal definition, parameters considered, IS/OOS split,
   cost model, neutralisation.
5. Result. Sharpe, drawdown, turnover, hit rate — both IS and OOS.
6. Trials so far. Number of distinct specifications evaluated to date
   on this hypothesis. Feeds [[multiple-testing-correction|DSR deflation]].
7. Decision. Proceed / kill / iterate / shelve.
8. Expected vs actual. Before running, what did you expect? How did
   the result compare?
9. Surprises. Anything that didn't fit the prior. Tag for follow-up.
10. Open threads. Questions you didn't pursue but want to remember.
```

Field 8 — expected vs actual — is the most under-used and most valuable. It's what makes the journal a calibration instrument. Tetlock's superforecasters write down a probability *before* the outcome and check the Brier score over hundreds of forecasts. The same loop applied to "I expect this strategy to have OOS Sharpe between 0.6 and 1.2" produces, over time, a researcher who knows when to trust their own backtests.

## What every decision-diary entry should capture

The Kahneman-Lovallo-Sibony format is short and specific:

```
1. Decision to be made.
2. Date.
3. Options considered.
4. The chosen option and its rationale.
5. Key assumptions the decision rests on.
6. What would change my mind. (The falsification criterion.)
7. Forecast: most likely outcome, plus 80% confidence interval.
8. Review date. When will I check whether the forecast was right?
```

Decisions worth logging in finance: allocate to a new strategy, deleverage during a drawdown, change a position-sizing rule, hire a new data vendor, abandon a research line. Not every trade — those go in the trade log — but every consequential meta-decision.

The review-date discipline is what closes the learning loop. Without scheduled review, the diary becomes write-only, and you forget what you predicted by the time the outcome arrives. Most calendar apps support recurring reminders; some shops use a simple `due-date` field with a weekly grep.

## What every trade-log entry should capture

For live trading (or simulated paper-trading at production-realism levels):

```
- Symbol, side, quantity, time-in-force, limit/market.
- Entry timestamp and fill price; slippage vs expected.
- Pre-trade thesis: signal value, expected holding period,
  target, stop, max loss.
- Position size as % of book and risk budget consumed.
- Exit timestamp, fill price, realised P&L.
- Realised vs expected: holding period, drawdown, P&L.
- Post-trade tag: rule-driven exit / discretionary exit / stop /
  target / time-stop / regime-stop.
- One-sentence learning, if any.
```

Most brokers export the mechanical fields; the annotation fields (thesis, expected, learning) are manual. The discipline of writing the thesis *before* the trade fills is what prevents post-hoc rationalisation ("I knew it would do that"). Sinclair's *Volatility Trading* (2013) is explicit on this for option traders; the practice generalises.

Bucket-level analysis is what the log buys you: profit factor by exit type, slippage by time-of-day, P&L by signal-strength quintile. Without the log you have an aggregate Sharpe; with it you have a diagnostic.

## Post-mortems — the per-incident format

When a strategy underperforms its prior, or a trading mistake costs more than threshold, write a structured post-mortem within 48 hours:

```
1. What was the expected outcome.
2. What was the actual outcome.
3. Gap analysis — where did expectation and reality diverge?
4. Root cause (use the "5 whys" or a fishbone diagram).
5. Was this:
   a. A bad outcome from a sound decision (variance), or
   b. A foreseeable failure of decision/process?
6. What changes do I make? Be specific:
   - Process change (rule, threshold, checklist).
   - Data change (new source, new field, new cleaning step).
   - Risk-management change (sizing, stop, regime filter).
   - No change (it was variance; resist the urge to fix nothing).
7. Owner and follow-up date for the change.
```

The (a) vs (b) distinction is critical and underused. Most losses in a sensible strategy are variance — "good decision, bad outcome." Treating these as failures and "fixing" them is overreaction that introduces new fragility. The post-mortem forces the distinction explicitly.

The category ought to inform position sizing too: a string of (a)-type losses is a normal drawdown; a string of (b)-type losses means something is structurally wrong. See [[drawdown-and-risk|drawdown discipline]].

## Why this is unfashionable and why it works

Log-keeping is unfashionable because:

- It feels bureaucratic. The marginal entry takes 10 minutes; the marginal value is delayed by months.
- It exposes mistakes in writing. Most researchers prefer the foggy version where every loss had a "good reason."
- Pattern-matching from logs is slow. Insight arrives after dozens of entries, not after the third.

It works because:

- Memory is unreliable about base rates. The log knows you've tried 17 mean-reversion variants and 3 worked; memory thinks "most reversion ideas pan out."
- Calibration improves only with feedback. Without "expected vs actual" logged at the point of decision, there's no signal to calibrate against.
- Pattern recognition needs cases. A note that "this is the third time I've over-sized after a winning month" is only available if the first two notes exist.

Renaissance, D.E. Shaw and Bridgewater are documented to mandate research journals and decision diaries; Bridgewater's "issue log" (Dalio, *Principles* 2017) is the most public example. The practice is not exotic in the highest-performing shops — only in the lower tiers.

## Tools and conventions

The medium matters less than the consistency. Working setups:

- Plain markdown files in a git repo. Searchable, diffable, durable. Default recommendation.
- An Obsidian / Logseq / Notion vault with templates. Useful for tagging and back-linking.
- Spreadsheet for the trade log (broker exports as CSV) plus markdown for narrative entries.
- A research-workflow tool with built-in journal (the **Research Journal** screen in this terminal is purpose-built for this).

Conventions worth keeping:

- One file per session / per trade / per post-mortem, not one giant file.
- Date in the filename (YYYY-MM-DD-slug.md).
- Tags for strategy family, asset class, status.
- Weekly review block on the calendar — non-negotiable. 30 minutes to skim the week's entries.

## Common mistakes

- **Logging only successes.** Failure logs are where the calibration signal lives.
- **Vague forecasts.** "I think this will work" is not loggable. "I expect OOS Sharpe 0.8–1.2, hit rate above 53%, max DD under 12%" is.
- **No review cadence.** A write-only log is a write-only career.
- **Over-engineering the format.** A perfect template that you abandon in week three is worse than a scratch note you keep for years.
- **Anchoring on the latest entry.** The base rate across years matters more than this month's pattern.

## Where to do this in the terminal

The **Research Journal** screen provides the four log types as separate streams with shared search and tagging. The **Decision Diary** widget templates Kahneman-Lovallo-Sibony entries with built-in review-date reminders. The **Trade Log** integrates with broker imports and surfaces per-bucket performance reports. The **Post-Mortem** template enforces the (a)/(b) decision-vs-outcome split.

## See also

- [[hypothesis-first|Hypothesis-first research]] — pre-registrations are the cleanest research-journal entries
- [[backtesting-discipline|Backtest hygiene]] — the log is where the trial count for [[multiple-testing-correction|DSR deflation]] lives
- [[quant-research-workflow|Quant research workflow]] — log-keeping is the connective tissue
- [[drawdown-and-risk|Drawdown discipline]] — post-mortems on losing streaks belong in the log

## External references

- Kahneman, Lovallo & Sibony (2011). "Before You Make That Big Decision." *Harvard Business Review*, June.
- Tetlock & Gardner (2015). *Superforecasting: The Art and Science of Prediction*. Crown.
- Kahneman (2011). *Thinking, Fast and Slow*. Farrar, Straus and Giroux.
- Dalio (2017). *Principles*. Simon & Schuster — Bridgewater's issue-log and post-mortem culture.
- Sinclair (2013). *Volatility Trading* (2nd ed). Wiley — pre-trade-thesis discipline for option traders.
- Klein (2007). "Performing a Project Premortem." *Harvard Business Review*, September — premortem template.
