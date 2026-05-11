# Factor Investing

A **factor** is a systematic source of return that can be defined by a rule and captured by a portfolio constructed from that rule. Factor investing is the practice of building portfolios that deliberately tilt toward factors with documented long-run risk premia.

The intellectual roots are in the Capital Asset Pricing Model (CAPM) and its successors. Sharpe and Lintner's CAPM (1964) said market beta was the only priced risk; Fama and French's 1992/1993 papers added size and value; Carhart added momentum (1997); the literature since has documented dozens of additional factors, with varying degrees of academic consensus on which are real and which are data-mined.

For practical purposes, **a small handful of factors are widely accepted as real**: value, momentum, size (with caveats), quality / profitability, low volatility / low beta, and carry. The rest of the "factor zoo" (some 600+ documented in the literature; see Cochrane's 2011 AFA presidential address) is suspected to be partly statistical noise from multiple-hypothesis testing.

## The big six

### Value

**Definition.** Buy cheap stocks (low price-to-book, low price-to-earnings, low EV/EBITDA, low price-to-cash-flow) and sell expensive ones.

**Why it should exist.** Two camps. The risk explanation: cheap stocks are distressed and bear extra risk that gets compensated. The behavioural explanation: investors over-extrapolate growth and underprice mean reversion.

**Track record.** ~4–5% annual long-short premium in US equities 1927–2010 (Fama-French data). Suffered a multi-year drawdown from 2017–2020 ("value death") before recovering 2021–2023. Globally robust across markets and asset classes.

**Where it lives in this terminal:** value screens in the **equity_research** screen; multifactor portfolios in **ai_quant_lab**.

### Momentum

**Definition.** Buy stocks that have outperformed over the past 6–12 months; sell those that have underperformed. Most academic momentum portfolios skip the most recent month (the "12-2" specification) to avoid short-term reversal.

**Why it should exist.** Behavioural under-reaction to news, herding by institutional investors, slow diffusion of information across analysts.

**Track record.** Strongest factor in US equity literature — premium of ~8–10%/yr with high Sharpe in cross-section. But momentum suffers occasional sharp **crashes** (1932, 2009): when the market rebounds violently from a bear market, prior losers explode upward and momentum gets caught short.

**Where it lives:** price-momentum strategy in Kakushadze & Serur's *151 Trading Strategies* §3.1; in this terminal, the **alpha_arena** and **algo_trading** screens have momentum strategy templates.

### Size

**Definition.** Buy small-cap stocks, sell large-cap stocks (the classic "SMB" — Small Minus Big — factor).

**Why it should exist.** Small-caps are less liquid and less covered, so attract a risk premium.

**Track record.** Originally strong (Banz 1981) but has weakened materially since the 1990s. Some attribute this to ETF democratization of small-cap exposure; some say it was data-mined in the first place. **Most contemporary practitioners do not stand-alone size**, but use it as a sleeve combined with quality (small + quality = "SMOL").

### Quality / Profitability

**Definition.** Buy firms with high gross profitability, stable earnings, low leverage, high return on equity. Sell their opposites.

**Why it should exist.** Investors over-pay for growth and under-pay for proven profitability; quality firms are also less prone to bankruptcy in downturns.

**Track record.** Robust since Novy-Marx (2013). Combines well with value (cheap + quality = "QARP," quality at a reasonable price) and reduces the worst of value's drawdowns.

**Where it lives:** quality screens in **equity_research**; quality factor in **ai_quant_lab** multifactor portfolios.

### Low volatility / low beta

**Definition.** Buy low-volatility (or low-beta) stocks; sell high-volatility ones. Counterintuitive — high-vol stocks "should" have higher expected return by CAPM.

**Why it should exist.** Leverage-constrained investors (mutual funds, retail) overpay for high-beta stocks to get equity-like exposure without explicit leverage. Frazzini & Pedersen (2014) formalized this as "Betting Against Beta."

**Track record.** Robust globally; particularly strong on a Sharpe-ratio basis (volatility-adjusted) because the denominator is small.

### Carry

**Definition.** In FX, buy high-interest-rate currencies and sell low-rate ones. Generalizable across asset classes: in fixed income, buy high-yielders; in commodities, capture backwardation roll yields; in equities, buy high-dividend-yielders.

**Why it should exist.** Pure compensation for taking the other side of risk that someone else wanted to lay off (hedging demand). Carry trades have positive expected value over long horizons but recurrent crashes when funding currencies appreciate or risk-off events occur.

**Track record.** ~3–5% annual premium in FX 1976–present, but with severe drawdowns in 1998, 2008, and 2020. The "carry trade" is the canonical "picking up pennies in front of a steamroller" strategy — high Sharpe in calm regimes, fat left tail in stress.

**Where it lives:** Kakushadze & Serur *151 Trading Strategies* §8.2 (FX carry); §5.11 (fixed-income carry); §9.5 (commodity skewness premium).

## Building a multifactor portfolio

Two construction methods, roughly:

1. **Top-down sleeve.** Allocate capital to each factor sleeve (e.g. 25% value, 25% momentum, 25% quality, 25% low-vol). Each sleeve is its own long-short portfolio. Diversification across factors gives smoother returns than any one sleeve alone.

2. **Bottom-up scoring.** Score every stock on each factor, combine scores into a composite, rank, and trade the top minus bottom of the composite ranking. Tends to be more concentrated and capital-efficient than the sleeve approach.

AQR's *Quality Minus Junk* paper (Asness, Frazzini, Pedersen 2019) is a clean worked example of the bottom-up approach.

## Common pitfalls

- **Data-mining the "factor zoo."** Run 600 hypothetical factors, expect ~30 to show t-stat > 2 by pure chance. Demanding pre-registered hypotheses + out-of-sample tests filters these.
- **Factor crowding.** Once a factor is widely known, capital floods in and the premium compresses. Value premium fell from ~5% (1990s) to near-zero (2017–2020) before recovering. Crowding can erase a factor for a decade.
- **Implementation drag.** Long-short factor portfolios have meaningful turnover and shorting costs; gross factor premium and net realized return differ substantially. See [[transaction-costs|transaction-cost modelling]].
- **Concentration regimes.** When a few mega-cap names dominate index returns (FAANG 2020–2022), simple value/momentum portfolios that hold equal-weighted baskets underperform mechanically.

## Worked example — the "Q-Factor model" (2020)

Hou, Xue, & Zhang (2015, 2020) proposed a 4-factor model: market, size, investment (firms that grow assets fast underperform), and profitability. Their data: 1967–2018, US equities. Compared to Fama-French 5-factor:

| Model | Adjusted R² (cross-section) |
|---|---|
| CAPM (market only) | ~0.85 |
| FF 3-factor (+ size, value) | ~0.91 |
| FF 5-factor (+ profitability, investment) | ~0.95 |
| Q-Factor (market, size, profitability, investment) | ~0.95 |

Adding factors increases explanatory power, with diminishing returns. The marginal contribution of any factor beyond ~5 is statistically noisy — which is what motivated Harvey, Liu, & Zhu (2016)'s "...and the Cross-Section of Expected Returns" critique of the entire factor literature.

## See also

- [[capm|CAPM (formula)]]
- [[beta-formula|Beta (formula)]]
- [[backtesting-discipline|Backtesting discipline]] — how to test whether a factor is real
- [[transaction-costs|Transaction-cost modelling]] — why the gross factor premium overstates net returns
- [[quant-research-workflow|Quant research workflow]]

## External references

- Fama & French (1993). "Common risk factors in the returns on stocks and bonds." *Journal of Financial Economics*.
- Carhart (1997). "On Persistence in Mutual Fund Performance." *Journal of Finance*.
- Asness, Moskowitz, & Pedersen (2013). "Value and Momentum Everywhere." *Journal of Finance*.
- Frazzini & Pedersen (2014). "Betting Against Beta." *Journal of Financial Economics*.
- Novy-Marx (2013). "The Other Side of Value: The Gross Profitability Premium." *Journal of Financial Economics*.
- Hou, Xue, & Zhang (2015). "Digesting Anomalies: An Investment Approach." *Review of Financial Studies*.
- Harvey, Liu, & Zhu (2016). "...and the Cross-Section of Expected Returns." *Review of Financial Studies*.
- Cochrane (2011). "Presidential Address: Discount Rates." *Journal of Finance*.
- Kakushadze & Serur (2018). *151 Trading Strategies*. SSRN id 3247865.
