"""
Cross-sectional factor rating — research harness, NOT a shipped rating.

Read this before using anything in here.

Why it exists
-------------
The ER Technicals rating scores one stock in isolation by counting indicator
votes. Two things are wrong with that, independent of any bug: the votes are
not independent (twelve moving averages of one close series are one fact
measured twelve times, and averaging them produces something that reads like
confidence but is not), and it had never been checked against forward returns.
This package was built to fix the second problem — to make the question
"does this predict anything" answerable instead of arguable.

What the measurement found
--------------------------
It answered the question, and the answer was no.

Walk-forward over 178 large caps, 2014-08 to 2026-08, non-overlapping periods,
scoring each signal by its rank correlation with the *forward excess return*
over the benchmark:

    signal                          IC(21d)    t      IC(63d)    t
    trend position (price vs MA)    -0.006   -0.30    -0.005   -0.15
    trend quality (R² of log price) -0.006   -0.31    -0.008   -0.26
    12-1 momentum                   +0.012   +0.64    +0.002   +0.05
    1-month reversal                +0.030   +1.82    +0.029   +1.14
    volume trend                    -0.005   -0.47    -0.001   -0.07
    low volatility                  -0.054   -2.46    -0.095   -2.37
    MA vote (proxy, shipped rating) -0.017   -0.99    -0.039   -1.44
    equal-weight composite          -0.006   -0.34    -0.025   -0.80

Nothing clears significance except low volatility, and it clears it with the
*wrong sign* — high-volatility names beat low-volatility ones, monotonically
across every decile (rank correlation -0.95 to -0.98), at every horizon tested.
That is the opposite of the published low-volatility anomaly, and it is almost
certainly this universe and this period rather than a discovery: the sample is
today's largest companies over a decade in which high-beta technology led, so
the names that survived into the universe are disproportionately the ones whose
volatility paid.

The line that matters for the product: the proxy for the shipped rating's
dominant component has an information coefficient indistinguishable from zero,
and slightly negative. The technical rating in the app is defensible, internally
consistent, and agrees with TradingView — and none of that is evidence it
predicts anything.

Was the null result itself verified
-----------------------------------
Yes, three ways, because a broken harness also returns "nothing works".

1. Positive control (validate.selftest, runs on demand). Fed the forward
   return itself the harness returns IC 1.000; negated, -1.000; pure noise,
   0.004. Foresight plus increasing noise decays smoothly — 0.76, 0.55, 0.33,
   0.14 — with perfect decile monotonicity throughout. At IC 0.137 the t-stat
   is 17, so the instrument is not blind to a weak-but-consistent effect.
   Alignment, sign, ranking and forward-return construction are all correct.

2. Data check against a published series. The equal-weighted return of this
   universe correlates 0.979 with Ken French's Mkt-RF over the same days. The
   prices and returns are what they claim to be.

3. Factor construction against a published factor. A decile long/short
   momentum portfolio built from these factors correlates 0.701 with the
   published UMD momentum factor — the same phenomenon, independently
   constructed. It returned -4.4%/yr against UMD's +4.0%/yr over the window,
   at twice the volatility, which is what a 178-name mega-cap-only, equal
   weighted, size-uncontrolled version of UMD should look like.

What replicated, and what I got wrong
-------------------------------------
Re-run over a second, independent universe — 408 small and mid caps, same
12 years — to test the excuse offered above, that mega caps are simply the
wrong place to look. Partly wrong:

  - momentum did NOT recover (IC +0.012, t 0.77). The universe was not the
    reason it failed, and the earlier suggestion that widening it would fix
    things is not supported by the test.
  - short-term reversal DID strengthen, and is now significant: IC +0.032,
    t 2.68, decile monotonicity +0.52, against +0.030 / t 1.81 in mega caps.
    One signal, two independent universes, same sign, consistent with a large
    published literature. This is the only thing here that replicated.
  - the proxy for the shipped rating stayed negative in both universes, and in
    small/mid its deciles line up almost perfectly *inversely* (monotonicity
    -0.93): the higher price sat above its moving averages, the worse the
    forward return.

Note the direction of that last pair. The shipped rating treats price above
its averages as bullish. The one effect that replicates says recent losers
outperform over the next month. They point opposite ways.

What this does and does not license
-----------------------------------
"Not shown to work" is still not "shown not to work", and the effects that do
replicate are economically marginal — an IC of 0.03 is a few basis points of
edge per name per month before costs. Remaining limits:

  - Survivorship bias, worse in the small/mid universe than the large one,
    since today's small caps include yesterday's large caps after a collapse.
    Rank statistics within a date blunt this; they do not remove it.
  - One regime: post-2014, one drawdown, one inflation shock.
  - At the 6-month horizon there are 21 non-overlapping observations, which is
    no power at all; those columns should be read as decoration.

So this package is deliberately not wired into the terminal. Shipping an
unvalidated composite as a user-facing rating would repeat the mistake it was
written to find. What it is good for is making the next change falsifiable:
any proposed rating can be scored the same way through validate.summarise()
(python -m factor_rating is the runnable pipeline)
before anyone relies on it.

To re-run:  python -m factor_rating  (from the scripts/ directory)
"""

from . import factors, panel, validate  # noqa: F401
