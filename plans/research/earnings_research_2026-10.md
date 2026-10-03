# Earnings Tab Redesign: Research Report

Date: 2026-10-02
Scope: web research only (no code changes). Goal: decide how finterm's per-stock **Earnings** tab should frame pre-earnings prediction, using only data that Yahoo Finance / yfinance exposes.
Data available: consensus EPS and revenue (avg/low/high/#analysts/year-ago/growth), `eps_trend` (current, 7d, 30d, 60d, 90d ago), `eps_revisions` (up/down over 7d/30d), `earnings_dates` / `earnings_history` (EPS estimate, reported, surprise %, timestamp), daily OHLCV, current option chains (bid/ask/last/IV/OI/volume per strike and expiry).

## Verification key

| Tag | Meaning |
|---|---|
| **OK** | I read the primary source (full paper/PDF, or the vendor's own page for a vendor claim) |
| **ABS** | I read the abstract only, on an index page (NBER, IDEAS/RePEc, publisher, or accepted-manuscript front page) |
| **SEC** | Secondary only: search-engine summary, practitioner blog, news recap, or someone else's description |
| **UNVERIFIED** | Citation exists but I could not confirm the specific finding. Do not cite the number in UI text |

Vendor accuracy claims tagged OK mean only that the vendor says so on its own page. None of them has an independent out-of-sample audit.

---

## TL;DR (decision-relevant)

1. **No serious product makes a signed next-day price call.** Products split into (a) *expectations* products (consensus, revisions, smart/whisper estimate, beat/miss history) and (b) *magnitude* products (options-implied move compared with the historical move). The current BUY/HOLD/SELL scorecard and the signed move prediction fit neither group. They should go.
2. **The surprise sign is somewhat predictable, but mostly by base rate.** About 77-78% of S&P 500 companies beat consensus EPS (FactSet 5-yr avg 78%, 10-yr 75-76%; Q1-2026 84%) [OK]. Vendor directional-accuracy claims (StarMine about 70% when |Predicted Surprise| ≥ 2% [OK vendor]; Zacks about 70% beat rate for +ESP & Rank ≤3 [SEC]) are **about the same as the unconditional beat rate**. Their value is mostly in flagging the *less likely* outcome (a miss).
3. **The announcement-return sign is close to a coin flip before the print.** Even *after* the print, the analyst surprise explains only about 12% of announcement-window return variance for non-microcaps (R² 1.7% → 11.7%, 1984-90 → 2016-19; Martineau 2022) [OK]. Earnings-window returns are symmetric with equal up/down incidence (Lipkin et al. 2025) [SEC]. Your walk-forward result ("rarely beats no-move") is what the literature predicts.
4. **Size is predictable, and options are the best single predictor.** The ATM straddle on the first expiry after the event is the industry-standard "expected move". Its average bias depends on the regime: underpriced in 1996-2013 (pre-event straddles +2.1% to +3.34%, GXZ 2018) [OK]; overpriced in 59.5% of S&P 500 events in 2021-26 (mean implied 4.14% vs realized 3.32%) [OK practitioner]; underpriced again in the 2026 season (ORATS: about +45% straddle returns vs about −2% 12-quarter average) [OK practitioner]. Show it as a range, with implied vs historical context. Do not present it as a point forecast.
5. **The reaction is asymmetric and depends on more than EPS.** FactSet 5-yr average for the −2 to +2 day window: beats **+1.0%**, misses **−2.9%** (Q1-26: +1.1% / −4.6%) [OK]. Revenue surprises and guidance surprises explain more of the announcement return than the EPS beat (Hand et al. 2022: multivariate adj-R² 3x the EPS-only model; the top drivers are next-quarter sales guidance and the sales surprise) [ABS]. Yahoo does not provide guidance or historical revenue consensus, so any "if beat" scenario is necessarily incomplete and must say so.
6. **PEAD is effectively gone for non-microcaps since 2006** (Martineau 2022) [OK]. Do not show drift-based trade suggestions. Show post-event drift only as descriptive history.
7. **Recommended frame:** (i) expected move (implied ± and its event-only share) with historical-move context; (ii) P(beat EPS) as a calibrated probability shrunk toward the base rate, with revision trend as the only adjustment; (iii) "if beat / if miss" reaction scenarios (stock history shrunk to market priors); (iv) a full surprise/reaction history table; (v) a visible track record (Brier score, implied-move coverage). Drop the composite score, BUY/HOLD/SELL, and the signed point prediction.

---

# PART A: How products present upcoming earnings

## A.1 Product-by-product

| Product | What leads the screen | How a forecast is expressed | Track record shown? | Tag |
|---|---|---|---|---|
| **Bloomberg EE / EEB / BEst** | Consensus snapshot (BEst = mean of sell-side), broker list, estimate history, surprise history | No probability. BEst consensus plus individual broker estimates. "Surprise" only after the fact | Historical surprise list | SEC |
| **Bloomberg ERN** | Table of past announcements: estimate, actual, **surprise %**, **price change on announcement day**, P/E impact | Implicit: history of reactions | Yes, per-quarter reaction | SEC |
| **Bloomberg EA** | Earnings-season monitor/screener: surprise %, price reaction across a universe | Descriptive | Season-level | SEC |
| **Bloomberg options (OMON/OVDV/OVME)** | Implied-vol surface/term structure. The earnings "implied move" is derived from the post-event ATM straddle. There is no single canonical "implied move" function name I could confirm | Magnitude only | n/a | SEC |
| **FactSet Estimates / StreetAccount** | Consensus, revisions, guidance vs consensus, StreetAccount previews ("what to watch": KPIs, guidance, buy-side bogeys). FactSet *Earnings Insight* publishes beat rates and price-reaction asymmetry | No point probability per name. Previews are qualitative (key metrics, consensus vs guidance) | Aggregate beat-rate and reaction statistics (5-yr/10-yr averages) | OK (Earnings Insight PDF); SEC (StreetAccount) |
| **LSEG StarMine SmartEstimate / Predicted Surprise** | SmartEstimate = consensus re-weighted "by analyst accuracy and estimate recency", excluding "stale, clustered and outlier estimates". **Predicted Surprise (PS) = (SmartEstimate − consensus)/consensus** | Signed % expected surprise. Screens at **PS ≥ +2% / ≤ −2%**. Signals are "corroborated by analyst revisions" | Vendor claim: "correctly predicts the direction of the earnings surprise **70%** of the time" when \|PS\| ≥ 2%. Quarterly published picks with claimed about 70% hit rate | OK (vendor pages) |
| **Estimize** | Crowd consensus (buy-side, independent, students) shown alongside Wall St consensus, plus distribution of individual estimates | Crowd mean as an alternative expectation. The gap to Wall St implies surprise direction | Jame, Johnston, Markov & Wolfe (JAR 2016): Estimize consensus more accurate than IBES **60%** of the time at 30 days, **64%** on the day before; 51,012 forecasts, about 3,000 contributors | SEC |
| **Zacks Earnings ESP** | **Most Accurate Estimate** (most recent estimates, reflecting latest information) vs **Zacks Consensus**. ESP = % difference | Signed ESP. Rule: **+ESP and Zacks Rank #1-3 → beat about 70% of the time**. 10-yr backtest about 28.3%/yr; higher ESP thresholds give higher returns (≥1%: 29.6%, ≥2%: 31.6%, >3%: 37.2%) | Marketing backtest, not independently audited | SEC (zacks.com blocked fetch; claims from Zacks-syndicated articles) |
| **Earnings Whispers** | **Whisper number** (claimed to reflect analysts' unpublished current expectations) vs consensus, sentiment, "grade" | Whisper as the "real" bar. Grades (A+ ... ) correlate with outperformance per the vendor | Vendor: whisper closer to actual than consensus **69.7%** of the time (144k+ whispers since 1998). Beat whisper → **+1.9%** avg, up 59.8% of the time. Beat consensus but missed whisper → **−0.7%**, down 55.0% | OK (vendor page) |
| **Market Chameleon** | Upcoming-earnings analysis: **ATM straddle implied move** vs **last-4 abs avg**, **last-12 abs avg**, **last-12 median**, and vs **previously implied** moves (last 4/12). Per stock: release time (BMO/AMC), "Implied Straddle" vs "Price Effect" history, strategy backtests (win rate, avg/median return, Sharpe) | Magnitude only. Moves treated as absolute ("+7% and −7% equivalent") | Yes: implied vs realized per past event, plus strategy backtests | OK (vendor page) |
| **OptionSlam** | Per-stock straddle history: Pre-ER EVR (earnings volatility rating), pre-ER close, straddle price, **Implied Move**, **Max Move**, **Inside/Outside** (did realized exceed implied), **Closing Move**, **Straddle Return**. Mean/median over last N | Magnitude, plus "did it beat the implied?" | Yes, explicitly | OK (vendor page) |
| **Barchart** | "Expected move" = **85% of the ATM straddle** for the first expiry after the report. "Average earnings move" over the **last 4** reactions | Magnitude range (high/low price band) | Last 4 reactions | SEC |
| **Koyfin** | Estimates (consensus, revisions charts), surprise history, customizable FA templates | No predictive score | Surprise history | SEC (thin) |
| **TIKR** | Estimates tab: historical actuals vs forward consensus for revenue/EBITDA/EBIT/NI/EPS, margins, growth | No prediction | Actual vs estimate history | SEC |
| **Seeking Alpha** | Earnings tab: Summary (next date, EPS/rev estimates), **EPS Surprise & Estimates by Quarter** chart, Estimates, **Revisions** (EPS & revenue revision history), **Surprise** (EPS and revenue beats/misses/in-lines counts with %), transcripts plus AI call summaries. Quant "**EPS Revisions**" factor grade A+ to F, sector-relative | Revisions grade, not a beat probability | Beat/miss counts | OK (help page) / SEC (grade inputs) |
| **Nasdaq.com earnings page** | Zacks-fed: consensus, # estimates, surprise history table (estimate, actual, surprise %) | None beyond consensus | Surprise history | UNVERIFIED (not fetched) |
| **TradingView** | "E" markers on the price chart. Hover shows estimate vs reported, beat/miss coloring. Earnings calendar and a financials panel with estimate/actual bars | None | Visual beat/miss on chart | SEC |
| **Unusual Whales** | Earnings calendar: **expected move ($)**, expected EPS, stock and options volume. Options-flow context | Magnitude (implied) | Not confirmed | SEC |

## A.2 Common conventions (observed across products)

1. **Two separate questions, two separate widgets.** "Will they beat?" (consensus, smart/whisper/most-accurate estimate, revisions) and "how much will it move?" (implied move). No mainstream product combines them into one signed price forecast.
2. **Implied move = ATM straddle on the first expiry after the event, divided by spot.** Variants: Barchart scales by 0.85, others show the raw straddle. Shown as **±%** and **±$** bands on the chart.
3. **Implied vs historical table.** Implied move next to the average and median absolute move over the last 4/8/12 events, plus the **ratio**. Market Chameleon also compares with *previously implied* moves.
4. **Per-event history table.** Date, BMO/AMC, estimate, actual, surprise %, implied move then, realized move (close-to-close and sometimes max intraday), and inside/outside the implied move.
5. **Beat/miss tallies.** "Beat 7 of last 8". EPS and revenue counted separately (SA).
6. **Revision trend.** Up/down revision counts, plus consensus 7/30/60/90 days ago (this is exactly Yahoo's `eps_trend` and `eps_revisions`).
7. **Alternative expectation.** SmartEstimate/Most Accurate/Whisper/Estimize. The *gap* to consensus is the forecast. The underlying idea is that recency-weighted and accuracy-weighted estimates beat the stale mean.
8. **Reaction stats conditional on outcome.** Earnings Whispers ("beat whisper → +1.9%"), FactSet ("beats +1.0%, misses −2.9%"), Bloomberg ERN (day-of price change).
9. **Track record is shown for the *data*, rarely for the *model*.** Vendors publish headline hit rates (70%, 69.7%) but not calibration, base-rate comparison, or out-of-sample decay. That gap is an opening for finterm.

---

# PART B: Evidence

## B.1 Predicting the SIGN of the earnings surprise

| Claim | Key numbers | Source | Tag |
|---|---|---|---|
| Base rate: consensus is usually beaten | S&P 500 % beating EPS: Q1-2026 84%; 1-yr avg 79%, 5-yr 78%, 10-yr 76% (another release says 77%/75%). Revenue beats: 81% Q1-26; 5-yr 70%, 10-yr 67%. Avg EPS surprise +7.3% (5-yr), +7.1% (10-yr) | FactSet *Earnings Insight*, 2026-05-21 | **OK** |
| Guidance walk-down: managers guide analysts to beatable targets | Firms with high transient institutional ownership, implicit claims, and value-relevant earnings are more likely to meet/beat. Done by guiding forecasts down as well as by managing earnings | Matsumoto 2002, *Accounting Review* 77(3) | SEC |
| Walk-down is strongest when insiders/firms sell after the announcement | Optimistic early forecasts walked down to beatable levels. Pronounced when firms issue equity or insiders are net sellers | Richardson, Teoh & Wysocki 2004, *CAR* 21(4) | SEC |
| Negative guidance is the norm | 5-yr avg 58% (10-yr 60%) of quarterly EPS guidance is negative. Q2-26: 47% | FactSet 2026-05-21 | **OK** |
| SmartEstimate: recency and accuracy weighting predicts surprise direction | ~70% directional accuracy when \|PS\| ≥ 2%. "Better in the presence of corroborating revisions" | LSEG StarMine pages / Lipper Alpha 2024 | **OK** (vendor) |
| Zacks ESP | +ESP & Rank ≤3 → positive surprise ~70% | Zacks articles | SEC (vendor) |
| Crowd (Estimize) more accurate than IBES | 60% (30d before), 64% (1d before); Estimize revisions also informative for returns | Jame, Johnston, Markov & Wolfe 2016, *JAR* 54(4) | SEC |
| Whispers | Whispers were more accurate than First Call for a small 1990s tech sample (943 whispers), and whisper-based strategies earned ~3x consensus-based ones. Later work: analysts more accurate, but whispers carry incremental information | Bagnoli, Beneish & Watts 1999, *JAE* 28; later studies (via secondary) | SEC |
| Predictable analyst errors from firm characteristics | Characteristic-based forecast minus analyst forecast predicts analyst forecast errors **and revisions**. Prices don't fully reflect this | So 2013, *JFE* 108(3) | ABS (abstract quoted via index) |
| Recency matters | Most recent individual forecast as an expectations proxy, compared with mean/median | O'Brien 1988, *JAE* 10 | UNVERIFIED (finding not confirmed) |
| Analysts underreact. Revisions trend | Analysts' forecasts "respond sluggishly to past news". Revisions predict returns (earnings momentum) | Chan, Jegadeesh & Lakonishok 1996, *JF* 51(5) | SEC |
| Dispersion | High-dispersion quintile underperforms low by **9.48%/yr** (cross-sectional return effect, not a surprise-sign effect). Strongest in small, past-loser stocks | Diether, Malloy & Scherbina 2002, *JF* 57 | SEC |
| Beat persistence / streaks | Investors underreact to *streaks* of same-sign surprises. Drift strong when a streak continues, negligible after a streak ends. Streaks explain about half of PEAD (pre-2006 era) | Loh & Warachka 2012, *Mgmt Sci* 58(7) | SEC |
| Consistent beaters get a premium | Meeting/beating is rewarded beyond the forecast error. Consistent meeters valued higher | Bartov, Givoly & Hayn 2002, *JAE* 33; Kasznik & McNichols 2002, *JAR* 40(3) | SEC |

**Interpretation for finterm.**
- A 70% directional hit rate sounds strong but **sits at or below the ~75-80% unconditional beat rate**. Always predicting "beat" scores about 78% on S&P 500 names. StarMine's 70% is over both positive and negative PS signals, so it is not directly comparable. No vendor publishes accuracy *by sign* against the base rate. **The informative event is a predicted miss**, and that is where revision signals plausibly add value.
- Yahoo gives **no individual analyst estimates**, so a true SmartEstimate or Most Accurate Estimate cannot be built. The best available proxy is the **revision trajectory** in `eps_trend` (current vs 7/30/60/90d) and `eps_revisions` (up/down counts). This is the same underlying signal (recent estimates move away from the stale mean). It is weaker because no accuracy weighting is possible.
- Company-specific **beat history** (from `earnings_dates`) is legitimately predictive of the next beat through the walk-down and persistence mechanisms. It needs shrinkage: N is about 8-16 quarters.

## B.2 Predicting the SIGN of the announcement-day return from pre-print information

| Claim | Key numbers | Source | Tag |
|---|---|---|---|
| Even the realized surprise explains little of the reaction | Regression of BHAR[0,1] on surprise rank: slope rose from 20 → 120 bps (non-micro) and 30 → 100 bps (micro) from early to late sample. **R² 1.7% → 11.7%** (non-micro), 2.4% → 9.2% (micro). Sample 1984-2019, IBES | Martineau 2022, *Critical Finance Review* 11(3-4) | **OK** |
| Earnings-window returns are symmetric and fat-tailed | "Symmetric, with equal incidence of up and down moves". Fat tails (~2.5% in each tail). Options "generally predict the price impact well, with some outliers" | Arjun K M, Lipkin & Tatevossian 2025, *Journal of Risk* | SEC |
| Earnings announcement premium (EAP) | Announcement-month stocks earn more. Premium >7%/yr, linked to volume surge and small-investor attention buying | Frazzini & Lamont 2007, NBER w13090 | ABS (7%/yr figure: SEC) |
| EAP as systematic risk | Announcers earn annualized abnormal **9.9%** | Savor & Wilson 2016, *JF* 71(1) | SEC |
| EAP is global | >11%/yr across 46 countries. Strongest where idiosyncratic vol jumps most around announcements | Barber, De George, Lehavy & Trueman 2013, *JFE* 108(1) | SEC |
| **EAP has disappeared in the US** | "First evidence that this premium has disappeared from the US markets in recent years". Robust internationally | Heitz, Narayanamoorthy & Zekhnini 2020 (working paper) | SEC (PDF 403) |
| Pre-EA run-up then reversal for past winners | Top-1% prior-12m winners: **+1.58%** in the 5 days before the EA, **−1.86%** in the 5 days after. Attributed to small-trader attention | Aboody, Lehavy & Trueman 2010, *RAST* 15 | SEC |
| Short-term reversals spike before EAs | Losers-minus-winners 3-day return **1.45%** around EAs vs 0.22% in pseudo-periods (**6x**), 1996-2011. Liquidity-provision explanation | So & Wang 2014, *JFE* | SEC |
| Option skew predicts bad news | Steepest smirk underperforms flattest by **10.9%/yr**. Steep-smirk firms have the worst next-quarter earnings shocks | Xing, Zhang & Zhao 2010, *JFQA* 45(3) | SEC |
| Put-call parity deviations | Expensive-call stocks beat expensive-put stocks by **50 bps/week**. **Predictability declines over the sample** | Cremers & Weinbaum 2010, *JFQA* 45(2) | SEC |
| IV spread builds before EAs | Call-put IV spread rises monotonically into the EA. Cumulative abnormal IV spread predicts announcement returns | Lei, Wang & Yan 2017, *JBF* | ABS |
| Revision / earnings momentum | Past surprises and revisions predict drift (pre-2000s evidence) | Chan, Jegadeesh & Lakonishok 1996 | SEC |

**Interpretation.**
- The pre-print signals with academic support (skew, IV spread, PCP deviations, pre-EA reversal, EAP) are **cross-sectional portfolio effects of tens of bps** that (a) need clean, liquid option data, (b) have mostly decayed (Cremers-Weinbaum say so in the paper; EAP gone in the US; PEAD gone), and (c) give a per-name hit rate barely above 50%. None supports showing a single stock a signed "+1.8% expected" number.
- The arithmetic of the current approach: even a perfect P(beat) predictor only explains about 12% of reaction variance, and real predictors are far from perfect. **A signed point forecast will almost always lose to zero in MSE or MAE.** That matches the walk-forward result.
- Option-derived directional signals (skew, IV spread) are *theoretically* available from yfinance chains. Yahoo's `impliedVolatility` field is noisy and often wrong for illiquid strikes or zero bids, and after-hours `lastPrice` is stale. They could be shown as **descriptive context** ("put skew steeper than its 60-day norm") at most. They should not drive a call.

## B.3 Predicting the SIZE of the move

| Claim | Key numbers | Source | Tag |
|---|---|---|---|
| Straddles bought before EAs were **profitable** (implied underpriced uncertainty), 1996-2013 | Non-event ATM delta-neutral straddles: −2.12%/week (t = −11.92), −0.19%/day, −17.09%/month. Around EAs (entry day −3 or −1, exit day 0 or +1): **+2.10% to +3.34%** holding-period returns, all significant. Pre-announcement effect "particularly large, significant and robust". Stronger for small, high-vol, high-kurtosis, volatile-past-surprise, low-volume names. OptionMetrics, IBES dates; day 0 = announcement date, no BMO/AMC adjustment | Gao, Xing & Zhang 2018, *JFQA* 53(6) | **OK** |
| Event uncertainty is large, time-varying, and priced | Separates announcement-day variance from diffusive vol. Anticipated uncertainty is "quantitatively large, varies across time, and is informative about future return volatility" | Dubinsky, Johannes, Kaeck & Seeger 2019, *RFS* 32(2) | ABS |
| Options forecast magnitude well | IV-based measures predict absolute returns around EAs and shareholder meetings. Market "correctly forecasts the magnitude" | Govindaraj, Jin, Livnat & Zhao 2014 (WP) | SEC |
| Recent regime: implied **rich** | S&P 500, Feb-2021 to Feb-2026, 9,651 events: implied > realized **59.5%** of the time. Mean implied **4.14%** vs realized **3.32%** (ratio ~1.25). Median edge +0.84 pp. Long ATM straddle (9 DTE entry, exit next session): 22% win rate, −18.5% mean P&L, avg IV crush 15.6 vol pts | iVolatility study (practitioner) | **OK** (practitioner; their quintile "edge" sort uses realized moves, so it is look-ahead and not a tradable signal) |
| 2026 regime: implied **cheap** | 2026 season straddles ~**+45%** avg vs ~**−2%** 12-quarter avg. Realized moves consistently exceeded implied | ORATS blog 2026 | **OK** (practitioner) |
| Historical move as predictor | Industry standard compares implied with last-4/12 abs-avg and median (Market Chameleon, Barchart, OptionSlam). ORATS uses implied ÷ trailing-12 avg actual as an expectations gauge | Vendor pages | OK (vendor) |
| Normal-approx conversion | For an ATM straddle, straddle/spot ≈ √(2/π)·σ√T ≈ 0.8·σ√T = expected *absolute* move. A 1-σ move ≈ 1.25 × straddle/spot. Barchart's "85% of straddle" is a vendor heuristic | Standard math; Barchart (SEC) | — |

**Interpretation.**
- The implied move is the **best available single estimate of magnitude**, but its bias changes sign across regimes (1996-2013 cheap, 2021-25 rich, 2026 cheap). Present it as **the market's price of uncertainty**, next to the stock's own realized history and a running coverage statistic. Do not apply a fixed haircut.
- Event isolation matters. The straddle on the first post-event expiry includes ordinary diffusion for every day until expiry. Use a two-expiry term-structure decomposition (Dubinsky-Johannes style, see C.3) to report the event-day move separately.
- **Yahoo provides no historical option prices**, so finterm cannot backfill "implied then vs realized" for past events. It must **snapshot the implied move before each event going forward** (persisted locally) to build its own track record. Until then, show "implied history: collecting (n = k)". Do not fabricate it.

## B.4 Reaction given the surprise

| Claim | Key numbers | Source | Tag |
|---|---|---|---|
| Asymmetric reaction | 5-yr avg, −2 to +2 day window: **beats +1.0%**, **misses −2.9%**. Q1-2026: +1.1% / −4.6% | FactSet *Earnings Insight* 2026-05-21 | **OK** |
| Reactions vary a lot by season | e.g. Q2-2023 beats −0.5% (vs +1.0% 5-yr avg); Q3-2023 misses −5.2% (vs −2.3%) | FactSet (via secondary) | SEC |
| Beat-and-fall is common in some seasons | Q3-2025: EPS beaters underperformed the S&P 500 by 0.35% after reporting (worst since Q4-2020) | News recap | SEC |
| Nonlinear ERC | Returns vs unexpected earnings: approximately linear for small surprises, **S-shaped** globally (large surprises get diminishing incremental response) | Freeman & Tse 1992, *JAR* 30(2) | SEC |
| Growth stocks: torpedo effect | Growth stocks show an asymmetrically large negative response to negative surprises | Skinner & Sloan 2002, *RAST* 7 | SEC |
| Revenue surprise > expense surprise | A dollar of revenue surprise is valued more than a dollar of expense surprise | Ertimur, Livnat & Martikainen 2003, *RAST* 8 | SEC |
| Revenue surprise strengthens drift | PEAD stronger when the revenue surprise agrees with the EPS surprise | Jegadeesh & Livnat 2006, *JAE* 41 and *FAJ* 62(2) | SEC |
| **Guidance and sales dominate** | 13 item surprises (FactSet + IBES) explain signed EA returns. Top 4: **next-quarter sales guidance surprise, analyst sales surprise, annual Street EPS guidance surprise, Street EPS surprise**. Multivariate adj-R² **3x** the EPS-only regression | Hand, Laurion, Lawrence & Martin 2022, *RAST* 27(4) | ABS |
| Bundled disclosure raises the response | Rising EA response 2001-2016 is largely explained by bundled guidance, analyst forecasts and disaggregated line items | Beaver, McNichols & Wang 2020, *JAE* 69(1) | ABS |
| Beating the whisper vs consensus | Beat whisper +1.9% (59.8% up); beat consensus but missed whisper −0.7% (55.0% down) | Earnings Whispers (vendor) | OK (vendor) |
| Price response to surprises has grown | Non-micro announcement responsiveness 6x higher in 2016-19 than 1984-90 | Martineau 2022 | **OK** |

**Interpretation.** "If beat / if miss" scenarios are meaningful, but they should be:
(a) asymmetric (the miss tail is about 3x the beat mean);
(b) stated as wide distributions, not points;
(c) explicitly labelled **"EPS-only conditioning"**, because the main drivers (sales and guidance surprises) are not observable from Yahoo before the print. Historical revenue consensus is not in yfinance either. Only forward `revenue_estimate` is available.

## B.5 Post-earnings announcement drift (PEAD) today

| Claim | Key numbers | Source | Tag |
|---|---|---|---|
| PEAD gone for large and mid caps | Analyst SUE fails to predict BHAR[2,60] for **all-but-microcap stocks since 2006** and for **microcaps since 2016**. Pre-announcement drift also weakened. Microcaps are 3.2% of market cap but 60.7% of stocks | Martineau 2022, *CFR* | **OK** |
| Remaining PEAD is a microcap artifact | Earnings-factor t-stat 2.18 with all stocks, **1.43 excluding microcaps** | Subrahmanyam 2025 (via UCLA Anderson Review) | SEC |
| Counter-claims | Dickerson, Julliard & Mueller (JFE, in press) and Hirshleifer, Peng & Wang (RFS 2025, t≈14) find drift, but did not filter microcaps per the critique | Same source | SEC |
| Streak-driven drift (older sample) | ~half of PEAD from streak continuation | Loh & Warachka 2012 | SEC |

**Interpretation.** For the names finterm users mostly look at (US large/mid caps), **do not frame post-earnings drift as a signal**. Showing realized +5d/+20d returns after past events as *descriptive history* is fine and useful for context.

---

# PART C: Recommendation

## C.1 Principle

Separate the three questions the user actually has, answer each with the quantity the evidence supports, and show calibration for anything probabilistic:

| Question | What to show | Why |
|---|---|---|
| "How big?" | **Options-implied move** (± %, ± $, event-only share) + historical abs moves + implied/historical ratio + coverage stat | Best-supported predictor (B.3); industry convention (A.2) |
| "Will they beat?" | **P(beat EPS)** with uncertainty, vs base rate; revision trend; consensus range and dispersion | Partially predictable; base rate dominates (B.1) |
| "What happens if…?" | **Conditional reaction scenarios** (beat / miss) from stock history shrunk to market priors, asymmetric, labelled EPS-only | Supported, but incomplete without guidance/sales (B.4) |
| "Which way?" | **Do not answer with a number.** At most: "Direction ≈ coin flip; options price ±X%" | Unpredictable pre-print (B.2); matches your walk-forward |

## C.2 Proposed layout (top to bottom)

1. **Event header.** Date, BMO/AMC (from `earnings_dates` timestamp; mark "estimated" vs "confirmed" when Yahoo shows a date range), trading days until the event, which session's close-to-close return will reflect it.
2. **Expected move card** (lead visual).
   - Big number: **±X.X% (±$Y)** = event-isolated 1-σ or expected-absolute move (pick one convention, label it, show the other on hover).
   - Sub-line: straddle-to-expiry move (raw) on expiry E1, with bid/ask-spread quality flag.
   - Price chart band: spot ± implied, overlaid on the last N events' realized reaction bars.
   - Context row: last-4 avg |move|, last-12 avg, last-12 median, max; **implied ÷ last-12 avg** ratio with a neutral "rich vs history / cheap vs history" word (no trade advice).
   - If no listed options, or chains are illiquid (zero bids, spread > ~25% of mid): "Implied move unavailable", and show historical moves only. (No synthetic fallback.)
3. **Expectations card.**
   - Consensus EPS and revenue, low-high range bar with mean marker, #analysts, **dispersion = (high−low)/|mean|**, YoY growth.
   - **Revision sparkline** from `eps_trend` (90d → 60d → 30d → 7d → now) with % change, plus `eps_revisions` up/down counts (7d, 30d).
   - Plain-language note: "Consensus lowered 3.1% over 90d (typical pre-earnings walk-down)" or "raised …".
4. **Beat odds card.**
   - **P(EPS beat) = p̂ [80% interval]**, next to the base rate (~78% S&P 500, FactSet 5-yr) and the stock's own record ("beat 10 of last 12").
   - Typical surprise size for this stock (median surprise %).
   - Caption: "A beat is the normal outcome. A miss is the informative event." State P(miss) explicitly.
5. **Reaction scenarios card** ("If beat / If miss").
   - For each branch: stock's historical mean, median, and % up of the reaction-day return; n; market prior (FactSet 5-yr: +1.0% / −2.9%, window −2 to +2); a shrunk blended estimate.
   - Footnote: "EPS-only. Revenue and guidance surprises usually move the stock more (Hand et al. 2022) and are not known in advance."
   - Optional: probability-weighted mix, shown as a **distribution** (mixture histogram or fan), never as a single signed number.
6. **History table** (one row per past event): date, BMO/AMC, EPS est, EPS actual, surprise %, reaction-day return (close-to-close, correctly aligned for BMO vs AMC), gap open %, |move|, implied move at the time (only for events finterm snapshotted, otherwise "—"), inside/outside implied, +5d and +20d drift (descriptive). Summary row: beat rate, avg |move|, % up after beat, % up after miss.
7. **Track record panel** (finterm's own model, walk-forward):
   - P(beat): Brier score vs a base-rate-only forecaster, plus a reliability bucket table once n is large enough.
   - Implied move: coverage (% of realized |move| ≤ implied) and mean realized ÷ implied, over snapshotted events, both for this stock and pooled across the watchlist.
   - Show "n" everywhere, and say "insufficient history" below a threshold.

## C.3 Computation notes (Yahoo-only)

**Reaction-day return alignment.** From the `earnings_dates` timestamp (exchange tz): before 09:30 → BMO → reaction = close[t]/close[t−1] − 1; after 16:00 → AMC → reaction = close[t+1]/close[t] − 1; missing time → flag and use a two-day window [t−1, t+1]. GXZ note that announcement-hour data are imprecise even in IBES; treat ambiguity explicitly.

**Implied move from chains.**
- E1 = first expiry whose last trading session is *at or after* the reaction session. E2 = next expiry after E1 (or E0 = last expiry before the event, if one exists with ≥ 2 days left).
- ATM straddle: call_mid + put_mid at the strike nearest to the forward (or linearly interpolated between the two strikes bracketing spot). Use mids only when bid > 0 and ask > bid. Reject spreads wider than a quality threshold. Avoid `lastPrice` (stale off-hours) and avoid Yahoo's `impliedVolatility` field. Back out IV from mids yourself (Black-Scholes; American early exercise is negligible for short-dated ATM).
- Raw move_E1 = straddle / spot (≈ expected |move| to E1).
- Event isolation (two-expiry, total-variance): with IVs σ1, σ2 at T1 < T2 (years), both expiries after the event,
  `σ_n² = (σ2²·T2 − σ1²·T1) / (T2 − T1)` (diffusive variance), `v_e = σ1²·T1 − σ_n²·T1`, event 1-σ move = √v_e, expected |event move| ≈ 0.8·√v_e.
  If v_e ≤ 0 or inputs fail quality checks, report the raw E1 straddle move only. Trading-day vs calendar-day time convention must be consistent.
- Persist a snapshot (timestamp, spot, E1/E2, straddle, σ1/σ2, event move) at the last close before the event. This is the only way to build implied-vs-realized history from Yahoo.

**P(beat).** Keep it simple and calibratable:
- Prior: Beta(a, b) with mean = universe base rate (estimate it from finterm's own pooled `earnings_dates` across the coverage universe, about 0.75-0.8; FactSet is the cross-check) and strength ≈ 8-12 pseudo-quarters.
- Update with the stock's last ≤ 12 beats/misses (in-line: count as beat if actual ≥ estimate, matching Yahoo's surprise sign; or exclude, but document the choice).
- Optional revision tilt via logistic adjustment on the 30d and 90d `eps_trend` % change and the net up-down `eps_revisions`. **Only keep it if a walk-forward test shows a Brier improvement over the Beta-binomial alone.** Report that test result in the track record panel.
- Output p̂ with a credible interval. Never output "BEAT"/"MISS" as a verdict.

**Scenario shrinkage.** For each branch, blended mean = (n·x̄_stock + k·μ_prior)/(n + k), with k ≈ 4-6 and μ_prior from the pooled universe (FactSet +1.0% / −2.9% as a sanity check; note their window is 5 days, not 1). Dispersion: use the stock's empirical |move| distribution scaled by today's implied ÷ historical ratio, which keeps fat tails (B.2).

**Data caveats to surface.**
- Yahoo's consensus provider and its definition of "adjusted EPS" are not documented. Surprise % can differ from Street/FactSet/IBES.
- `eps_trend` 7/30/60/90-day points include estimate-period roll-overs (e.g. right after a prior report). Suppress the revision signal when the fiscal period changed inside the window.
- No guidance, no historical revenue consensus, no individual estimates, no historical option prices.
- Per project rule (no fake data): any field Yahoo lacks shows "unavailable". Never impute.

## C.4 What to drop

| Drop | Reason |
|---|---|
| BUY/HOLD/SELL pre-earnings scorecard | No product precedent. Hand weights are unvalidated. Conflates beat-probability with return direction, which the evidence says are only weakly linked (R² ≈ 12% even ex-post) |
| Signed point prediction of the next-day move | Direction pre-print is ~coin flip (B.2). Walk-forward confirms it loses to zero. Invites false precision |
| Any single composite "score" mixing revisions, momentum, skew, history | Can't be calibrated or audited. Replace with separate calibrated quantities |
| PEAD-based "drift after beat" suggestions | Gone for non-microcaps since 2006 (Martineau 2022) |
| EAP-based "buy before earnings" framing | US premium reported to have disappeared (Heitz et al. 2020) |
| Yahoo `impliedVolatility` field as an input | Noisy for illiquid strikes. Recompute from bid/ask mids |

## C.5 What to keep or add (priority order)

1. Implied move (event-isolated) + historical move context + snapshot persistence (**highest value, low cost**).
2. Full surprise/reaction history table with correct BMO/AMC alignment.
3. Revision trend visuals (`eps_trend`, `eps_revisions`) and consensus range/dispersion.
4. Calibrated P(beat) with base-rate comparison and Brier-score track record.
5. Asymmetric if-beat / if-miss scenario distributions, labelled EPS-only.
6. (Optional, descriptive) pre-event run-up (last 5 days vs sector/market) with a note on reversal literature, and skew vs its own norm, labelled "context, not a signal".

---

## Sources

Primary (read):
- Martineau, C. (2022). Rest in Peace Post-Earnings Announcement Drift. *Critical Finance Review* 11(3-4): 613-646. https://cfr.ivo-welch.org/published/papers/martineau2021rest.pdf
- Gao, C., Xing, Y., Zhang, X. (2018). Anticipating Uncertainty: Straddles around Earnings Announcements. *JFQA* 53(6): 2587-2617. https://www.pbcsf.tsinghua.edu.cn/__local/A/11/35/B0A1714D795BD9799087BCCC2AD_A01C4283_54641.pdf
- FactSet, *Earnings Insight*, 2026-05-21. https://advantage.factset.com/hubfs/Website/Resources%20Section/Research%20Desk/Earnings%20Insight/EarningsInsight_052126.pdf
- LSEG StarMine SmartEstimates. https://www.lseg.com/en/data-analytics/financial-data/analytics/quantitative-analytics/starmine-smartestimates
- LSEG/Lipper Alpha, StarMine 2024 Q3 earnings forecast. https://lipperalpha.refinitiv.com/2024/10/starmine-2024-q3-earnings-forecast-predicting-beats-and-misses-for-russell-1000-companies/
- Earnings Whispers, About whispers. https://www.earningswhispers.com/about-whispers
- Market Chameleon, Upcoming Earnings Analysis. https://marketchameleon.com/EarningsReport/UpcomingEarningsAnalysis ; per-stock https://marketchameleon.com/Overview/WH/Earnings/
- OptionSlam straddle history (example). https://www.optionslam.com/earnings/straddle/HSTM
- Seeking Alpha Help, Earnings tab. https://help.seekingalpha.com/basic/what-is-the-earnings-tab-and-how-do-i-use-it
- iVolatility, "Does IVolAI's Earnings Edge Actually Pay?" https://www.ivolatility.com/news/3147
- ORATS, Earnings straddles strong season 2026. https://orats.com/blog/earnings-straddles-strong-season-2026

Abstract-level:
- Frazzini & Lamont (2007), NBER w13090. https://www.nber.org/papers/w13090
- Dubinsky, Johannes, Kaeck & Seeger (2019), *RFS* 32(2): 646-687. https://ideas.repec.org/a/oup/rfinst/v32y2019i2p646-687..html
- Hand, Laurion, Lawrence & Martin (2022), *RAST* 27(4). https://ideas.repec.org/a/spr/reaccs/v27y2022i4d10.1007_s11142-021-09597-6.html
- Beaver, McNichols & Wang (2020), *JAE* 69(1). https://ideas.repec.org/a/eee/jaecon/v69y2020i1s0165410119300394.html
- Lei, Wang & Yan (2017), *JBF*. https://leiq.bus.umich.edu/papers/Lei_Wang_Yan_JBF_2017.pdf
- So (2013), *JFE* 108(3). https://ideas.repec.org/a/eee/jfinec/v108y2013i3p615-640.html

Secondary:
- Savor & Wilson (2016) *JF*; Barber, De George, Lehavy & Trueman (2013) *JFE*; Heitz, Narayanamoorthy & Zekhnini (2020 WP); Aboody, Lehavy & Trueman (2010) *RAST*; So & Wang (2014) *JFE*; Xing, Zhang & Zhao (2010) *JFQA*; Cremers & Weinbaum (2010) *JFQA*; Matsumoto (2002) *TAR*; Richardson, Teoh & Wysocki (2004) *CAR*; Kasznik & McNichols (2002) *JAR*; Bartov, Givoly & Hayn (2002) *JAE*; Skinner & Sloan (2002) *RAST*; Freeman & Tse (1992) *JAR*; Ertimur, Livnat & Martikainen (2003) *RAST*; Jegadeesh & Livnat (2006) *JAE*/*FAJ*; Loh & Warachka (2012) *Mgmt Sci*; Diether, Malloy & Scherbina (2002) *JF*; Chan, Jegadeesh & Lakonishok (1996) *JF*; Jame, Johnston, Markov & Wolfe (2016) *JAR*; Bagnoli, Beneish & Watts (1999) *JAE*; Govindaraj, Jin, Livnat & Zhao (2014 WP); Arjun K M, Lipkin & Tatevossian (2025) *J. Risk* (https://wallstreethorizon.com/news/Earnings-moves-and-pre-earnings-implied-volatility); Subrahmanyam (2025) via https://anderson-review.ucla.edu/is-post-earnings-announcement-drift-a-thing-again ; Zacks ESP articles; Barchart expected-move pages; Bloomberg function guides (CFI, library guides).

Unverified: O'Brien (1988) *JAE* (specific "most recent forecast" finding not confirmed). Nasdaq.com earnings page specifics.
