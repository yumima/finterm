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

What this does and does not license
-----------------------------------
"Not shown to work" is not "shown not to work". The test is underpowered and
aimed at an unfavourable target:

  - ~133 non-overlapping observations at the 1-month horizon; detecting an IC
    of 0.03 at t=2 needs roughly 160. At the 6-month horizon there are 21
    observations, which is no power at all.
  - Mega caps are the hardest place to find trend or momentum effects — they
    are the most heavily arbitraged names in the market, and the momentum
    literature documents the effect as weakest exactly here.
  - The universe is survivorship-biased. Rank-based statistics inside a date
    blunt that, they do not remove it.
  - One regime: post-2014, one drawdown, one inflation shock.

So this package is deliberately not wired into the terminal. Shipping an
unvalidated composite as a user-facing rating would repeat the mistake it was
written to find. What it is good for is making the next change falsifiable:
any proposed rating can be dropped into validate.run() and scored the same way
before anyone relies on it.

To re-run:  python -m factor_rating  (from the scripts/ directory)
"""

from . import factors, panel, validate  # noqa: F401
