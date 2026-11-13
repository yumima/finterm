# LTCM 1998 — Genius Without Liquidity

In September 1998, **Long-Term Capital Management** — a fund run by two Nobel laureates and the former vice-chairman of Salomon Brothers — lost 90% of its capital in five weeks and had to be bailed out by a consortium of banks the Fed organized to prevent systemic collapse.

It is the canonical case study for **how leverage + correlated tail risk + illiquidity destroy even brilliant risk management**.

## The setup

LTCM ran "convergence trades": pairs of similar securities trading at small price differences that should narrow over time.

Examples:
- Long off-the-run Treasuries / short on-the-run (same maturity, slightly different liquidity)
- Long Italian government bonds / short German bunds (Italian yields would converge to German as EU formed)
- Long volatility in US / short volatility in Japan

Each trade had a **tiny edge** (basis points) but very high statistical confidence based on historical convergence. LTCM levered 25:1 (and effectively 100:1+ counting derivatives) to make those small spreads meaningful.

## The math, on paper

The team — including **Myron Scholes and Robert Merton** (Nobel 1997 for option pricing) — had:

- **Sharpe ratio** > 4 in early years
- Modeled max daily loss at 95% confidence: ~$45M
- Modeled max daily loss at "5σ event" (1-in-3.5M days): ~$1B
- Capital base: ~$5B → could survive any single shock

## What actually happened

**August 17, 1998**: Russia defaulted on its sovereign debt — the "unthinkable" event for a major sovereign. Triggered a global flight-to-quality:

- Spreads on every convergence trade **widened**, not narrowed
- Off-the-run Treasuries (LTCM long) fell; on-the-run (LTCM short) rallied
- Every other "convergence" trade did the same — **all correlations went to 1**
- Daily losses: $200M+, then $400M, then $600M

LTCM's models said this was a 9σ event — should happen once in the lifetime of the universe. It happened in three weeks.

## Why the math broke

1. **Models assumed independent tails** — they were wrong. In stress, *everything* correlates.
2. **Liquidity disappeared** — even Treasuries became hard to sell at quoted prices
3. **Counterparties knew LTCM's positions** (informally) — they front-ran any unwinds
4. **Forced selling cascade** — losses → margin calls → forced sells → price drops → more losses
5. **No way to exit at any reasonable price** — LTCM was the market in many of these trades

## The bailout

By September 23: LTCM was down to $400M of equity supporting $125B of assets ($1T+ derivative notional). A liquidation would have crashed several markets and damaged the global banking system.

**The Federal Reserve organized 14 banks to put up $3.6B** to take over LTCM's positions and unwind them slowly. No public funds, but the systemic risk was real enough that the Fed considered it necessary to intervene.

## Concepts illustrated

- [[var|VaR]] — LTCM's VaR underestimated tail risk catastrophically
- [[liquidity|Liquidity]] — disappeared exactly when needed most
- [[kelly-criterion|Kelly Criterion]] — full Kelly + correlated bets = bankruptcy
- [[behavioral-biases|Overconfidence]] — Nobel laureates believed their models too much

## Lessons

1. **Models are tested by tails, not by averages.** LTCM looked great until it didn't.
2. **Leverage transforms small edges into existential risk.** 25× leverage means a 4% adverse move wipes you out.
3. **Correlations rise to 1 in stress.** Diversification fails in the moment you most need it.
4. **Position size is dictated by worst-case liquidity exit, not by typical-day metrics.**
5. **"This time is different" is sometimes true** — and it can break you when it is.

## What changed

LTCM is the reason modern bank risk management uses:
- **Stressed VaR** (Basel III) — VaR computed using historical crisis periods
- **Liquidity-adjusted position limits**
- **Counterparty concentration limits**
- More skepticism about **mean-reverting strategies in stress regimes**

The lessons keep getting forgotten — **2008**, **2020 COVID**, **2022 UK gilt blowup** are all variants of "tail-risk + leverage + correlated unwinds."

## Read more

The definitive account is Roger Lowenstein's *When Genius Failed* (2000). It reads like a thriller and remains the best business book about the limits of quantitative risk management.
