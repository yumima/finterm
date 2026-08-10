// src/services/equity/TechnicalRating.h
#pragma once
#include "services/equity/EquityResearchModels.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace fincept::services::equity {

/// The trend reading under the ER Technicals tab.
///
/// What it claims, and what it does not. This panel used to print STRONG BUY …
/// STRONG SELL. Those words are a forecast, and the forecast was measured: over
/// 178 large caps and twelve years of daily bars, scored against the sign of a
/// 40-day log-price regression, the evidence here agrees with the trend already
/// in place ~81% of the time and with the direction of the *next* 40 days ~47%
/// — the wrong side of a coin flip, and consistent with a rank correlation
/// against forward returns that is indistinguishable from zero. So it now reads
/// STRONG UPTREND … STRONG DOWNTREND, which is the claim the evidence supports.
/// The measurement lives in scripts/factor_rating and re-runs with
/// `python -m factor_rating --describe`.
///
/// Two rules shape the whole design, both learned from what the previous
/// scorer got wrong:
///
/// 1. **Trend and mean reversion must both get a vote.** The old scorer had a
///    branch for RSI, Stochastic, CCI, Williams %R, MFI and BB %B — every one
///    of them an oscillator that fires *against* the move — and no branch at
///    all for price versus its moving averages. Fourteen of the twenty-eight
///    displayed indicators could never be anything but Neutral. The result was
///    a rating that read a falling stock as STRONG BUY (its oscillators were
///    oversold) and a breakout as STRONG SELL (overbought), which is the exact
///    inverse of what the tape said. Here the moving averages carry the trend
///    bucket and the oscillators are confined to their own bucket with less
///    weight.
///
/// 2. **An extreme reading is not a signal until it turns.** Oversold and still
///    falling is a falling knife. Every oscillator here requires the value to
///    be turning back out of its extreme before it votes, and a mean-reversion
///    call that fights a trend confirmed by ADX is demoted to Neutral. Without
///    those two guards an oscillator votes Buy the whole way down a downtrend.
///
/// Indicators that carry no directional information on their own — ATR, band
/// widths, the Bollinger mid-line (which is just the 20-SMA again), the MACD
/// signal line, the second Aroon and Stochastic lines — are still displayed but
/// marked as non-voting, so they cannot pad the tally with agreement they never
/// expressed.
///
/// Scoring lives here rather than in EquityResearchService so it stays free of
/// Qt widgets and network I/O and can be pinned by unit tests against fixed
/// inputs, the same way EarningsSignal is.

/// One row of computed indicators, keyed by the snake_case column names
/// compute_technicals emits. Absent and NaN columns are simply not stored,
/// so `has()` is the single "do we have this reading" test.
class IndicatorRow {
  public:
    void set(const QString& column, double value);
    bool has(const QString& column) const;
    /// Value for `column`, or `fallback` when it was never set.
    double get(const QString& column, double fallback = 0.0) const;
    bool isEmpty() const { return values_.isEmpty(); }

  private:
    QHash<QString, double> values_;
};

/// Everything the scorer is allowed to look at.
///
/// Three rows, not one: the latest reading answers "where are we", `prev`
/// answers "which way is it going" (the turn confirmation above), and `back`
/// — roughly a week earlier — gives the running-total series (OBV, ADI) a
/// slope, since a single bar of a cumulative sum is noise.
struct RatingInput {
    IndicatorRow now;
    IndicatorRow prev;
    IndicatorRow back;
    double close = 0.0;
    int bars = 0; // candles the indicators were computed from
};

/// A single indicator's contribution.
struct IndicatorVerdict {
    TechSignal signal = TechSignal::Neutral;
    /// False for display-only indicators — shown with their value and colour,
    /// excluded from the tally and from the composite.
    bool votes = false;
    /// Which weighted bucket this vote lands in — "trend", "momentum" or
    /// "volume". Deliberately *not* the panel the indicator is displayed under.
    /// CCI is drawn with the trend indicators but behaves as a mean-reversion
    /// oscillator, and KAMA and VWAP are drawn under momentum and volume but
    /// are moving averages. Letting the display grouping set the weight put a
    /// contrarian oscillator inside the heaviest bucket, where it cancelled out
    /// the trend evidence it was supposed to be a counterweight to.
    QString bucket;
};

/// The composite read plus everything the UI needs to explain it.
struct RatingVerdict {
    TechSignal overall = TechSignal::Neutral;
    /// Weighted composite in [-1, +1]. The gauge is drawn straight off this so
    /// the bar and the words can never disagree.
    double net = 0.0;
    int voting = 0;   // indicators that actually cast a vote
    int displayed = 0;// indicators shown, voting or not
    int strong_bullish = 0, bullish = 0, neutral = 0, bearish = 0, strong_bearish = 0;
    /// Per-bucket contribution, or the reason there is no rating.
    QString basis;
};

namespace technical_rating {

/// Below this many voting indicators the composite is not reported at all.
inline constexpr int kMinVotingIndicators = 8;
/// …nor below this many voting *trend* indicators. A rating whose trend bucket
/// rests on one or two readings is dominated by whichever ones warmed up first.
inline constexpr int kMinTrendVoters = 3;
/// Rows back to sample for cumulative-series slope.
inline constexpr int kSlopeLookback = 5;

/// Whether there is enough history for the trend evidence to mean anything.
///
/// Counting voters is not enough. On a one-month daily window the 50- and
/// 200-period averages, MACD, ADX and Aroon are all still inside their warm-up,
/// yet three near-identical short averages plus CCI clear a "3 trend voters"
/// bar. Worse, ADX being absent silently disables the trend filter that keeps
/// oversold oscillators from voting Buy into a decline — so the thinnest window
/// is exactly the one where the guards are weakest, and a stock 6% below every
/// average comes back BUY. The rating therefore requires the structural
/// indicators to actually exist, not merely enough columns to count.
bool has_sufficient_history(const RatingInput& in);

/// Score one indicator column against the snapshot.
IndicatorVerdict score(const QString& column, const RatingInput& in);

/// Combine already-scored indicators into the composite. Reads `rating_bucket`
/// and `votes` off each entry; indicators with `votes == false` are ignored.
/// `in` is needed for the history check above.
RatingVerdict aggregate(const QVector<TechIndicator>& scored, const RatingInput& in);

} // namespace technical_rating

} // namespace fincept::services::equity
