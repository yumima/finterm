# VIX ETN Decay Trade (Short VXX / Long VXZ)

**VXX** is an exchange-traded note (ETN) tracking the front two months of VIX futures (UX1 and UX2). Because the VIX futures curve is **in contango most of the time**, VXX systematically loses value to the **roll** — daily rebalancing means selling cheaper near-month futures and buying more-expensive deferred futures.

The strategy: **short VXX** (capture its decay) and **hedge with a longer-maturity volatility ETN (VXZ) or with VIX futures directly**. VXZ tracks mid-maturity VIX futures (UX4–UX7), which decay less in contango. The combined short-VXX / long-VXZ trade captures the **front-of-curve roll yield** while hedging against vol spikes.

Kakushadze & Serur (2018) §7.3 cover this in two paragraphs. It is one of the cleanest **front-curve roll-yield trades** in any market.

## The economics — why VXX decays

VXX maintains a **constant ~30-day maturity** by rolling daily. Each day, it sells a fraction of UX1 (closer to expiry) and buys a fraction of UX2 (further from expiry).

In a contangoed curve, **UX2 > UX1 > spot VIX**. So the daily rebalancing means:
- Selling at the (lower) UX1 price.
- Buying at the (higher) UX2 price.
- **Net negative carry** — the roll loss.

The roll loss compounds daily. Over a year of typical contango, VXX has historically lost **50–80% per year**. Even though VIX itself ends the year roughly unchanged, VXX as a structurally-rolled product systematically erodes.

Avellaneda & Papanicolaou (2018) and Charupat & Miu (2011) formalised this analysis. The decay is real, persistent, and mathematically demonstrable.

## The strategy

The basic form (book §7.3):

```
Short VXX.
Long VXZ (or VIX futures basket) with hedge ratio h.
```

Where `h` is determined to minimise variance of the combined position — typically via a serial regression of VXX returns on VXZ returns.

```
h  =  β  =  ρ · σ_VXX / σ_VXZ
```

(From the standard regression coefficient formula; `ρ` is correlation between VXX and VXZ returns; `σ_VXX`, `σ_VXZ` are their volatilities.)

Practical hedge ratios: roughly **h ≈ 2–3** (you need 2–3 dollars of long VXZ per dollar of short VXX, because VXZ is much less volatile than VXX).

## Why this works — and why it's not arbitrage

VXX decays at ~50–80% per year. VXZ also decays in contango (it holds longer-maturity futures, which also roll), but at maybe ~15–25% per year. The decay differential is the trade's expected return.

Net annualised return historical:
- 2010–2017 (low VIX, persistent contango): ~15–25% annualised on the trade.
- 2018: roughly flat to losing (Feb 2018 vol spike then recovery).
- 2020 March: large drawdown then recovery.
- 2021–2022: positive again as contango returned.

**This is not arbitrage.** The short VXX is exposed to vol spikes. In a vol spike, VXX can rally 50%+ in a day. The long VXZ rallies less because UX4–UX7 are less sensitive to the spike than UX1–UX2.

So the trade is **net short volatility**, with VXZ providing a partial offset.

## The basic strategy variant — short VXX hedged with VIX futures

Instead of VXZ as the hedge leg, hedge directly with a basket of medium-maturity VIX futures (UX4–UX7). The book gives this in §7.3 (footnote 11, equations 7.4–7.5):

```
N VIX futures are weighted w_i:
w_i  =  σ_X · Σ_{j=1}^{N} C^{-1}_{ij} · σ_j · ρ_j         (Eq. 7.4)

with constraint:
Σ_i w_i  =  1                                              (Eq. 7.5)
```

Where:
- `ρ_j` = pairwise correlation between futures `j` and VXX.
- `C_{ij}` = covariance matrix of the futures.
- `σ_j² = C_{jj}` = historical variance of futures `j`.
- `σ_X` = historical vol of VXX.

This is essentially a regression-based hedge with multiple regressors. The hedge minimises **tracking error** of the long-futures-basket to the short VXX position.

## Variants

### Short-only VXX (no hedge)

Some traders skip the hedge entirely and just short VXX. Higher expected return (~50%/year) but much higher volatility and larger drawdowns. Position sizing must be conservative.

### Long XIV / SVXY (inverse VIX ETNs)

XIV (Velocity Shares Daily Inverse VIX) and SVXY (ProShares Short VIX) were leveraged-inverse ETNs that did the short-VXX trade internally. **XIV was liquidated on February 5, 2018**, losing 96% of its value in one day when VIX spiked.

SVXY survived but was forcibly de-leveraged from -1× to -0.5× by ProShares after the 2018 event. The lesson: **don't trust internal-leverage products on volatile reference indices**.

### Short VXX, long SPX (or long SPY)

Vol and equity are negatively correlated. A short VXX + long SPY position **doubles down on the equity bullish bet**: when stocks rise, VIX usually falls (good for short VXX) and SPY rises directly.

This is conceptually different from short-VXX / long-VXZ. The SPY hedge offers more aggressive equity exposure; the VXZ hedge stays vol-only.

### Calendar spread on VIX futures

Short UX1, long UX2 (or UX2/UX3). Doesn't carry the ETN-decay edge but captures the curve-slope mean reversion in a cleaner futures-only construction.

## Historical record

The trade was famously profitable from 2010–2017:

- **2010–2014**: low-VIX contango regime. Short-VXX-with-hedge returned ~15–25%/year.
- **2015**: VIX spike in August (China devaluation); brief drawdown then recovered.
- **2016**: low-vol resumed; strong year.
- **2017**: record low VIX; the trade was a printing press.
- **2018 February**: Volmageddon. XIV liquidated; SVXY de-leveraged; the trade lost dramatically.
- **2019**: mostly recovered.
- **2020 March**: COVID; brutal drawdown.
- **2021**: recovered as VIX normalised.
- **2022**: contango returned; trade profitable.

## The XIV liquidation — what happened

On Feb 5, 2018, VIX spiked from ~17 to ~37 in late afternoon trading.

XIV's structure required it to **rebalance daily to maintain −1× exposure**. As VIX spiked, the underlying futures (UX1, UX2) rallied violently. XIV's mark-to-market value dropped by 80%+ during the day.

Most catastrophic: the daily rebalance happened during the spike. XIV had to **buy more VIX futures at the elevated prices** to maintain leverage — which pushed prices even higher. The product was structurally amplifying its own losses.

By close, XIV had lost 96%. Credit Suisse (the issuer) announced liquidation that night.

The lesson is **fundamental**: **automatic-rebalancing-leveraged products on volatile underlyings can self-destruct**. The DIY version of this trade (short VXX without the rebalancing constraint) was less vulnerable but still painful.

## Practical implementation

- **Position size**: small. 2–5% of book max. Even with hedges, drawdowns can be severe.
- **Hedge ratio**: re-estimate periodically (monthly). Vol regime changes alter the optimal h.
- **Stop-loss**: hard stops on vol spikes. If VIX rises by 25%+ in a session, exit.
- **Counterparty risk**: VXX is an ETN issued by Barclays (replacement issuer post-2018). Issuer credit risk applies in extremis.
- **Rebalance frequency**: at least monthly to maintain hedge ratio. Some implementations rebalance weekly.

## Common mistakes

- **Treating short VXX as a long-term hold without hedge**. It works most of the year but blows up in one bad week.
- **Sizing aggressively due to high Sharpe**. The historical Sharpe is ~1.0–1.5 but is misleading because the distribution has fat left tails.
- **Using leveraged products** like SVXY or older XIV. The structural rebalancing amplifies catastrophes.
- **Ignoring the VIX expiration cycle**. Around the third Wednesday of each month (VIX futures settlement), behaviour is non-standard. Adjust position sizes that week.
- **Crowding awareness**. The trade is **highly crowded**. Every quant fund knows about it. Crowded positioning amplifies vol-spike losses.

## Risk management essentials

- **Hard stop on VIX move**: e.g., exit if VIX > +30%/day.
- **Hedge first, leverage never**: VXZ is more important than position size.
- **Diversify across vol products**: don't put all eggs in VXX/VXZ.
- **Tail hedge**: deep-OTM VIX call options to cap catastrophic losses. Cost: 1–2% per year. Worth it given the 2018 precedent.
- **Position-size for the 2018-style event**: even with all the above, assume a possible 50%+ drawdown of the trade in a single week.

## Where to do this in the terminal

- **Derivatives screen** — VIX futures term structure; VXX vs. VXZ price overlay.
- **Backtesting** — historical short-VXX/long-VXZ P&L with Feb 2018 stress-period and March 2020 flagged.
- **AI Quant Lab** — vol-ETN decay template with hedge-ratio optimiser.

## See also

- [[vol-vix-futures-basis|VIX Futures Basis Trading]] — the direct futures version
- [[volatility-quant|Volatility Trading and the Surface]] — broader vol framework, VRP context
- [[etf-leveraged-decay|Leveraged ETF Decay]] — same decay principle for equity LETFs
- [[vol-skew-risk-reversal|Vol Skew and Risk Reversals]] — directional vol-skew trade

## External references

- Avellaneda, M., Papanicolaou, A. (2018). "Statistics of VIX Futures and Applications to Trading Volatility Exchange-Traded Products." *International Journal of Theoretical and Applied Finance* 21(1), 1850001.
- Alexander, C., Korovilas, D. (2012). "Diversification of Equity with VIX Futures: Personal Views and Skewness Preference." *Quantitative Finance* 13(11), 1701–1713.
- Bordonado, C., Molnár, P., Samdal, S. (2017). "VIX Exchange Traded Products: Price Discovery, Hedging, and Trading Strategy." *Journal of Futures Markets* 37(2), 164–183.
- Eraker, B., Wu, Y. (2014). "Explaining the Negative Returns to Volatility Claims." Working paper.
- DeLisle, R., Doran, J., Krieger, K. (2014). "Volatility as an Asset Class: Holding VIX in a Portfolio." *Journal of Futures Markets* 34(7), 685–702.
- Charupat, N., Miu, P. (2011). "The Pricing and Performance of Leveraged Exchange-Traded Funds." *Journal of Banking & Finance* 35(4), 966–977.
- Husson, T., McCann, C. (2011). "The VXX ETN and Volatility Exposure." *PIABA Bar Journal* 18(2).
- Gehricke, S., Zhang, J. (2018). "Modeling VXX." *Journal of Futures Markets* 38(8), 958–976.
- Whaley, R. (2013). "Trading Volatility: At What Cost?" *Journal of Portfolio Management* 40(1), 95–108.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §7.3. https://doi.org/10.1007/978-3-030-02792-6
