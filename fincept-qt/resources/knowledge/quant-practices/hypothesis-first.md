# Hypothesis-First Research

The single most expensive habit in quant research is opening the data first, then deciding what to test. Once you've seen the data, every "hypothesis" you formulate is implicitly conditioned on patterns you already noticed — and the resulting test is no longer a test, it's a recapitulation. The cheapest, hardest, and most effective discipline is to write the hypothesis, the falsification test, and the stopping rule **before** loading a single CSV.

This is sometimes called **pre-registration** in scientific contexts. Munafò et al.'s "A manifesto for reproducible science" (*Nature Human Behaviour* 2017) is the canonical statement of why it matters across all empirical fields. Bailey-López de Prado make essentially the same argument for finance research; AQR's published research consistently includes "what we expected before we ran this" statements as a credibility signal.

## The cost of p-hacking

P-hacking is the family of techniques — conscious or otherwise — that produce statistical significance from noise:

- **HARKing** (Hypothesising After Results are Known). Formulate the "hypothesis" once you see what the data shows.
- **Optional stopping.** Run the test, check significance, run more data, check again, stop when p < 0.05.
- **Garden of forking paths** (Gelman & Loken 2013). The analyst makes many small choices (winsorisation level, lookback length, universe filter) that individually look innocent but jointly multiply the implicit testing.
- **Specification search.** Try many variants of the strategy, report the one that "worked."

Each of these inflates the false discovery rate. Simmons, Nelson & Simonsohn ("False-Positive Psychology," 2011) demonstrated that with four common p-hacking moves a researcher can produce a statistically significant result from pure noise about 60% of the time. The same arithmetic applies one-for-one to financial backtests, with one extra hazard: in psychology a false positive damages a reputation; in trading, it loses real money.

See [[multiple-testing-correction|multiple-testing correction]] for the statistical machinery to deflate after the fact. Hypothesis-first is the cheaper alternative: don't generate the inflation in the first place.

## What a pre-registration document contains

Before opening the data, write — in a dated, version-controlled file — at minimum:

- **The economic hypothesis.** A one-paragraph story for *why* the effect should exist. "Companies with high gross profitability outperform low-profitability peers because the market under-prices durable competitive advantage" (Novy-Marx 2013) is a valid hypothesis. "High-vol stocks should beat low-vol stocks" with no mechanism is not.
- **The exact signal definition.** Universe, frequency, lookback, ranking method, neutralisation. If you can't write the formula now, you don't have a hypothesis yet.
- **The falsification test.** What result would make you abandon this idea? "If the long-short Sharpe in the validation window is below 0.5, I will not pursue this further." Without a falsification criterion, the test is unfalsifiable.
- **The data slice.** Which sample, which dates, which fields. Lock these.
- **The IS/OOS split.** Dates and ratio.
- **The stopping rule.** How many parameter variants you will try before deflating. ("I will test exactly 3 lookback values: 6, 12, 18 months.")
- **The pre-trade prior.** Your subjective probability the effect exists. Useful only for calibrating your own forecasting over time — see [[log-keeping|log-keeping]].

Save the document with a hash; some shops commit it to a separate git repo with restricted-write history, so a later "I always meant to test this" can't be smuggled in.

## Why pre-registration matters even for solo research

The standard objection: "I'm a one-person shop, I'm not publishing, who am I lying to?" Answer: yourself, six months from now, when you've forgotten what you tried.

Kahneman's *Thinking, Fast and Slow* (2011) covers the relevant cognitive biases:

- **Hindsight bias.** Once you see a result, it feels inevitable. You "knew" the high-vol-beats-low-vol effect was real because in retrospect the story is clear.
- **Confirmation bias.** You'll notice and remember the parameter variants that "almost worked"; you'll discount the ones that flatly failed.
- **Narrative thinking.** A coherent story is more persuasive than a coherent statistical test. Pre-registration forces the test before the story.

A pre-registration document is a contract with your future self. When the result comes in and you're tempted to "just add one more filter," the document is the rule that prevents it. The discipline is what makes a track record honest 10 years out, not "I'm pretty sure I didn't cheat."

## The exploratory / confirmatory split

Most real research has two stages and pre-registration applies asymmetrically:

- **Exploratory phase.** Look at the data, try things, build intuition. p-values here are descriptive, not decisive. Output: ideas worth testing.
- **Confirmatory phase.** Pre-register the spec, lock the data, run *once*, accept the result.

The mistake is conflating the two. An exploratory finding with t = 2.5 is interesting; it is not evidence. To become evidence, it must be re-tested on data that didn't generate it — either an OOS holdout (see [[walk-forward-validation|walk-forward validation]]) or fresh data collected after the pre-registration.

Practically, allocate research time as ~70% exploratory and 30% confirmatory. The exploratory work generates many candidate ideas; only a few earn confirmatory testing budget; only a fraction of those survive. Most quant researchers run inverted ratios — almost all confirmatory time disguised as exploratory — which is how you ship a Sharpe-2 backtest that fails in production.

## What to do when the pre-registered test fails

A pre-registered hypothesis that fails its falsification test is *not* a wasted experiment. It is the most informative kind of result:

- The economic story is wrong, or the effect is too small to extract under your costs. Both are true findings.
- The strategy is dead. Don't iterate on it; iterate on a different hypothesis.
- The methodology is intact; you can stand by the negative result publicly without embarrassment.

The wrong response: tweak the spec until it passes. That's how you turn a failed test into a successful overfit. Log the failure honestly (see [[log-keeping|log-keeping]]) and move on. Failed hypotheses are evidence for the *next* hypothesis — they refine your prior about which ideas are worth testing.

## When pre-registration is hard

Some research patterns genuinely resist clean pre-registration:

- **Feature engineering on alternative data.** You often don't know the relevant features until you've explored. Solution: pre-register the *evaluation protocol* (universe, OOS dates, costs, Sharpe threshold), not the feature set.
- **ML hyperparameter search.** Inherent multiple testing. Solution: pre-commit a fixed CV protocol and report cross-validated test-set performance, not the best fold.
- **Discretionary overlay on systematic signal.** Pre-register the systematic rule and the criteria under which discretion can override it.

In all cases, the goal is to make the *test* unfalsifiable-resistant. Some flexibility in the *idea generation* is fine, even necessary.

## Common mistakes

- **Pre-registering after looking.** Writing the document with the data already in front of you. Useless. The whole point is the order.
- **Pre-registering everything trivially.** Pre-registration becomes ritual; researchers tick the box without actually constraining themselves. Falsification criteria must have teeth.
- **No falsification criterion.** A pre-registration that doesn't specify when you'd abandon the idea isn't pre-registration; it's a research plan.
- **Treating exploratory results as confirmatory.** The single biggest error. A back-of-envelope finding has *not* been tested.

## Where to do this in the terminal

The **AI Quant Lab** has a pre-registration template that locks signal definition, universe, dates and falsification criteria into a hash-stamped record before backtest execution. The **Research Journal** screen surfaces past pre-registrations alongside their realised outcomes so you can audit your own hit rate. See [[log-keeping|log-keeping]].

## See also

- [[multiple-testing-correction|Multiple-testing correction]] — the deflation you avoid by not multiple-testing in the first place
- [[backtesting-discipline|Backtest hygiene]] — sin #4 (multiple-testing inflation) and sin #7 (backtest churn) are both fought by hypothesis-first
- [[walk-forward-validation|Walk-forward validation]] — confirmatory test of choice once a hypothesis is pre-registered
- [[log-keeping|Log-keeping]] — pre-registrations belong in the same log as outcomes
- [[quant-research-workflow|Quant research workflow]] — where hypothesis-first fits in the pipeline

## External references

- Munafò et al. (2017). "A manifesto for reproducible science." *Nature Human Behaviour* 1, 0021.
- Simmons, Nelson & Simonsohn (2011). "False-Positive Psychology." *Psychological Science* 22(11).
- Gelman & Loken (2013). "The garden of forking paths." Working paper, Columbia.
- Kahneman (2011). *Thinking, Fast and Slow*. Farrar, Straus and Giroux.
- Nosek et al. (2018). "The preregistration revolution." *PNAS* 115(11).
- Arnott, Harvey & Markowitz (2019). "A Backtesting Protocol in the Era of Machine Learning." *JFDS* — includes pre-registration template for quant.
