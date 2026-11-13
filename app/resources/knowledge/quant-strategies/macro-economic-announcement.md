# Trading on Economic Announcements

**Stocks earn disproportionately high returns on days when major economic announcements occur**, especially FOMC (Federal Reserve) meetings. This is the **pre-announcement drift / announcement-day premium** — one of the cleanest macro anomalies in equity markets.

The strategy: be **100% allocated to equities on announcement days**, **100% to Treasuries** on non-announcement days. Kakushadze & Serur (2018) §19.5 cover this in one paragraph. **Lucca & Moench (2015) "The Pre-FOMC Announcement Drift"** is the canonical reference.

## The empirical finding

Lucca & Moench (2015) documented: over **1994–2011, more than 80% of the equity-market risk premium was earned in the 24 hours before scheduled FOMC announcements**. Roughly half of the entire equity-market excess return over Treasuries came from these 8 days per year.

This is a stunning concentration of returns:
- 8 announcement days per year × 24-hour windows = ~8 trading days.
- US equity-market excess return ≈ 6% per year on average.
- ~3% of that came from these 8 days — far more than 8/252 = 3.2% of trading days would predict.

The effect persists in out-of-sample tests, has been documented in other major central-bank announcements (ECB, BoJ), and is robust to controlling for typical risk factors.

## The strategy rule

```
For each trading day:
  IF today is an announcement day (e.g., FOMC meeting day):
    Be 100% in equities (e.g., SPY).
  ELSE:
    Be 100% in Treasuries (e.g., IEF for 7-10 year, SHY for short).
```

The strategy can be augmented with various filters (technical indicators, momentum signals) per Stotz (2016), but the core mechanism is the date-based allocation.

## Which announcements matter

In order of empirical significance:

### 1. FOMC announcements (8 per year)

**By far the strongest signal**. FOMC meetings are scheduled, the announcement time is known (typically 2:00 PM Eastern), and the equity market drifts upward in the hours and days before the release.

Pre-FOMC drift (in S&P 500):
- T-2 days: small positive drift.
- T-1 day to T=0: significant positive drift.
- Announcement → close: small additional positive return on dovish announcements.

### 2. ECB rate decisions

Documented for European equities (Stoxx 600). Lower magnitude than FOMC but real.

### 3. BoJ policy decisions

Documented for Japanese equities (Nikkei). The pre-announcement effect is partially driven by USD/JPY anticipation.

### 4. Jobs reports (US NFP — Non-Farm Payrolls)

Released first Friday of each month, 8:30 AM Eastern. Significant intraday vol but the pre-day drift is weaker than for FOMC.

### 5. CPI releases

Less well-documented as a strategy driver, but volatile around release.

### 6. GDP releases

Lower frequency (quarterly), weaker pre-announcement effect.

## Why does the pre-announcement drift exist?

Several theories:

### 1. Risk reduction before announcements

Investors who anticipate uncertainty may **liquidate hedges (puts, short positions)** in the days before an announcement. This buying pressure pushes equity prices up before the news arrives.

Empirically: trading volume rises before FOMC meetings, and the pattern of who's buying/selling is consistent with hedge unwinding.

### 2. Macroeconomic announcement premium (Savor & Wilson 2013)

Investors price macro risk into expected returns. The premium for bearing macro risk is concentrated around announcement events that resolve macro uncertainty. Holding the market across the announcement = capturing the risk-resolution premium.

### 3. Behavioural anchoring

Markets may systematically underprice the "good news" that FOMC announcements tend to deliver (Greenspan put, Bernanke put, dovish Fed). The drift is the market gradually pricing in expected dovishness.

### 4. Liquidity provision

Market-makers and dealers need to provide liquidity around announcements (volatility insurance). They charge a premium during the calm pre-announcement window for bearing this risk.

## Empirical record

- **1994–2011 sample (Lucca-Moench)**: ~80% of equity risk premium from FOMC days.
- **Post-2011 out-of-sample**: effect has **weakened materially**. Some studies (Cieslak, Morse, Vissing-Jorgensen 2019) report it's about half as strong in the 2012–2019 window.
- **2020 COVID**: emergency Fed meetings disrupted the pattern; the strategy was difficult to apply.
- **2022 tightening cycle**: pre-FOMC drift returned modestly as the market priced rate increases.

The fact that the anomaly weakened post-publication (Lucca-Moench published 2015) is consistent with the **academic-attention-arbitrages-anomalies** pattern.

## Variants

### Pre-announcement window

Instead of "all-in" on the announcement day, enter T-2 or T-3 days before and exit at announcement close. Captures the gradual buildup.

### Sector-specific

Some sectors (financials, real estate) are more sensitive to Fed decisions. The pre-FOMC drift in XLF (financials) is stronger than in XLU (utilities).

### Combine with VIX signal

When VIX is high before an FOMC meeting, the drift is stronger (more uncertainty to resolve). Filter by VIX percentile.

### Cross-asset announcement effects

Treasuries rally on dovish announcements; the strategy can short Treasuries on hawkish meetings. Requires high-quality announcement classification.

### Donninger (2015) VIX-futures variant

"Trading the Patience of Mrs. Yellen" — a short VIX-futures strategy specifically around FOMC announcement days. Pre-FOMC tends to compress VIX; selling VIX into the announcement captures the premium.

## Practical implementation

- **Announcement calendar**: maintain a forward-looking calendar of FOMC, ECB, BoJ dates.
- **Position sizing**: full exposure on announcement days (high conviction signal); standard exposure on non-announcement days.
- **Execution**: T-1 close or T=0 open is the typical entry; exit at close on T=0.
- **Instruments**: index ETFs (SPY, IWM) for liquidity; sector ETFs for sector-specific bets.
- **Costs**: round-trip costs are modest with monthly rebalancing equivalent.

## When the strategy fails

- **Surprise hawkish announcements**: when the Fed surprises with a hawkish tilt, the pre-announcement drift fully reverses post-announcement, eating the gains.
- **Emergency meetings**: 2008, 2020 — non-scheduled FOMC meetings that don't fit the pattern.
- **Liquidity events overlap**: when an announcement coincides with a broader liquidity crisis (March 2020), the announcement effect is dominated by macro flow.
- **Crowded positioning**: as the strategy becomes more well-known, the pre-announcement drift can be arbitraged earlier (front-running).

## Common mistakes

- **Treating all FOMC meetings the same**: meetings with rate decisions vs. just press conferences may have different patterns. Sometimes the larger effects are around the dot-plot updates (4 per year).
- **Ignoring the post-announcement reversion**: the drift is pre-announcement; the post-announcement move can revert if the news disappoints.
- **Over-leveraging the strategy**: 80% of risk premium in 8 days is impressive, but it's still subject to single-day macro shocks. Don't lever it.
- **Confusing scheduled vs unscheduled announcements**: emergency Fed actions don't have the pre-drift pattern; the market is too surprised.

## Risk management essentials

- **Cap position size on announcement days**: full equity exposure is acceptable; leveraged exposure is not — single-day Fed surprises can be 4%+ negative.
- **Diversify announcement instruments**: don't load 100% in SPY; consider a basket including IWM, QQQ, XLF.
- **Have an exit plan**: if pre-announcement vol is unusually high (e.g., VIX > 30), reduce position size.
- **Track announcement quality**: not all FOMC meetings are equally significant (no-change meetings vs. policy-shift meetings).

## Where to do this in the terminal

- **Economics screen** — Fed, ECB, BoJ calendar; CPI/NFP schedule.
- **AI Quant Lab** — announcement-day strategy template with announcement-quality filtering.
- **Backtesting** — historical announcement-day strategy P&L; before/after Lucca-Moench publication date.

## See also

- [[macro-fundamental-momentum|Fundamental Macro Momentum]] — the cross-sectional macro framework
- [[macro-inflation-hedge|Global Macro Inflation Hedge]] — the inflation overlay
- [[volatility-quant|Volatility Trading]] — VIX-related strategies around FOMC
- [[stocks-price-momentum|Stock Price Momentum]] — the daily-momentum cousin
- [[strategy-patterns|Strategy Patterns]] — event-driven as a strategy category

## External references

- Lucca, D., Moench, E. (2015). "The Pre-FOMC Announcement Drift." *Journal of Finance* 70(1), 329–371.
- Savor, P., Wilson, M. (2013). "How Much Do Investors Care About Macroeconomic Risk? Evidence from Scheduled Economic Announcements." *Journal of Financial and Quantitative Analysis* 48(2), 343–375.
- Ai, H., Bansal, R. (2016). "Risk Preferences and the Macro Announcement Premium." Working paper, SSRN 2827445.
- Bernanke, B., Kuttner, K. (2005). "What Explains the Stock Market's Reaction to Federal Reserve Policy?" *Journal of Finance* 60(3), 1221–1257.
- Cieslak, A., Morse, A., Vissing-Jorgensen, A. (2019). "Stock Returns over the FOMC Cycle." *Journal of Finance* 74(5), 2201–2248.
- Boyd, J., Hu, J., Jagannathan, R. (2005). "The Stock Market's Reaction to Unemployment News: Why Bad News Is Usually Good for Stocks." *Journal of Finance* 60(2), 649–672.
- Donninger, C. (2015). "Trading the Patience of Mrs. Yellen. A Short Vix-Futures Strategy for FOMC Announcement Days." Working paper, SSRN 2544045.
- Graham, M., Nikkinen, J., Sahlström, P. (2003). "Relative Importance of Scheduled Macroeconomic News for Stock Market Investors." *Journal of Economics and Finance* 27(2), 153–165.
- Stotz, O. (2016). "Investment Strategies and Macroeconomic News Announcement Days." *Journal of Asset Management* 17(1), 45–56.
- Kakushadze, Z., Serur, J. (2018). *151 Trading Strategies*, Palgrave Macmillan, §19.5. https://doi.org/10.1007/978-3-030-02792-6
