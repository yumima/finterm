// src/services/equity/TechnicalRating.cpp
#include "services/equity/TechnicalRating.h"

#include <QSet>
#include <QStringList>

#include <cmath>

namespace fincept::services::equity {

// ── IndicatorRow ─────────────────────────────────────────────────────────────

void IndicatorRow::set(const QString& column, double value) {
    if (std::isfinite(value))
        values_.insert(column, value);
}

bool IndicatorRow::has(const QString& column) const { return values_.contains(column); }

double IndicatorRow::get(const QString& column, double fallback) const {
    return values_.value(column, fallback);
}

namespace technical_rating {
namespace {

using S = TechSignal;

/// How far price has to sit from a moving average — and the average itself has
/// to be sloping the same way — before the reading is called *strong* rather
/// than merely positive.
///
/// Measured in ATRs, not percent. A fixed percentage is the wrong ruler: 1.5%
/// is intraday noise on a name that moves 6% a day and a large move on one that
/// moves 0.8%, so a flat threshold hands out the strong tier almost
/// automatically on speculative names and almost never on quiet ones. Scoring
/// the gap in units of the stock's own daily range makes "far from its average"
/// mean the same thing for every instrument, and it is what stops a volatile
/// small cap from out-scoring a steadily trending large cap on identical
/// evidence.
constexpr double kStrongGapAtr = 1.0;
/// Fallback when ATR has not warmed up: the old flat percentage.
constexpr double kStrongGapPct = 0.015;

/// Scoring buckets. Since the trend-structure rewrite these no longer carry
/// weights — the verdict is cut from trend_structure(), not from a weighted
/// vote — but they still name where each indicator's vote lands for the
/// per-category tallies and the minimum-trend-evidence gate.
constexpr auto kBucketTrend = "trend";
constexpr auto kBucketMomentum = "momentum";
constexpr auto kBucketVolume = "volume";

/// Composite thresholds, on the [-1, +1] net score.
constexpr double kStrongBand = 0.50;
constexpr double kDirectionalBand = 0.15;

/// Moving averages, wherever they are displayed. Price against each of these is
/// the trend evidence the old scorer threw away. `bb_mavg` is deliberately
/// absent: it is the 20-period SMA under another name, and letting it vote
/// would count the same average twice.
///
/// The list is deliberately long and spans fast to slow. Benchmarked against
/// TradingView across the user's holdings, a bucket of seven mostly-medium
/// averages saturated: in any clean uptrend all seven agreed and the bucket
/// pinned at +1.00, where the reference — which reads fifteen averages from
/// 10-period to 200-period — sat at +0.80 because its fastest members had
/// already rolled over. That saturation was the whole of our systematic
/// bullish bias. Disagreement between fast and slow averages is signal, and a
/// set too narrow to express it rounds every trend up to its maximum.
bool is_moving_average(const QString& column) {
    static const QSet<QString> kAverages = {
        "sma_10", "sma_20", "sma_30", "sma_50", "sma_100", "sma_200",
        "ema_12", "ema_26", "wma_9",  "kama",   "vwap",    "ichimoku_base",
    };
    return kAverages.contains(column);
}

/// Direction of the prevailing trend, when there is one worth respecting.
/// ADX below 25 means no trend is established, so mean reversion is free to
/// speak. Direction comes from DI+ vs DI- — ADX itself is unsigned, which is
/// why the old `adx > 25 → Buy` rule cast bull votes inside downtrends.
struct Regime {
    bool trending = false;
    bool up = false;
};

Regime regime_of(const RatingInput& in) {
    Regime r;
    if (!in.now.has("adx") || !in.now.has("adx_pos") || !in.now.has("adx_neg"))
        return r;
    if (in.now.get("adx") < 25.0)
        return r;
    r.trending = true;
    r.up = in.now.get("adx_pos") > in.now.get("adx_neg");
    return r;
}

/// Demote a mean-reversion call that fights a confirmed trend.
///
/// Overbought inside a real uptrend is what an uptrend looks like — it is not a
/// reason to sell, and treating it as one is how a stock breaking out to new
/// highs came back rated STRONG SELL.
S respect_trend(S s, const Regime& r) {
    if (!r.trending)
        return s;
    if (r.up && (s == S::Sell || s == S::StrongSell))
        return S::Neutral;
    if (!r.up && (s == S::Buy || s == S::StrongBuy))
        return S::Neutral;
    return s;
}

/// Price against a moving average, with the gap measured in the stock's own
/// daily range. `atr` of 0 means ATR was unavailable and the flat percentage
/// fallback applies.
S score_vs_price(double ma, double prev_ma, double close, double atr) {
    if (!(ma > 0.0) || !(close > 0.0))
        return S::Neutral;
    const double gap = close - ma;
    const bool have_slope = prev_ma > 0.0;
    const bool rising = have_slope && ma > prev_ma;
    const bool falling = have_slope && ma < prev_ma;

    const bool far = atr > 0.0 ? std::abs(gap) > kStrongGapAtr * atr
                               : std::abs(gap) / ma > kStrongGapPct;

    if (gap > 0.0)
        return (far && rising) ? S::StrongBuy : S::Buy;
    if (gap < 0.0)
        return (far && falling) ? S::StrongSell : S::Sell;
    return S::Neutral;
}

/// A bounded oscillator votes only from its extremes, and only once it has
/// started back out of them. See the class comment: without the turn test this
/// reads Buy every single bar of a sustained decline.
S score_extremes(double value, double prev, bool have_prev, double oversold, double overbought) {
    if (value <= oversold)
        return (have_prev && value > prev) ? S::StrongBuy : S::Neutral;
    if (value >= overbought)
        return (have_prev && value < prev) ? S::StrongSell : S::Neutral;
    return S::Neutral;
}

/// Zero-centred oscillator: side of zero gives the direction, and expanding
/// away from zero upgrades it to strong.
S score_zero_line(double value, double prev, bool have_prev) {
    if (value > 0.0)
        return (have_prev && value > prev) ? S::StrongBuy : S::Buy;
    if (value < 0.0)
        return (have_prev && value < prev) ? S::StrongSell : S::Sell;
    return S::Neutral;
}

IndicatorVerdict voting(S s, const char* bucket) { return {s, true, QString::fromLatin1(bucket)}; }
IndicatorVerdict display_only(S s) { return {s, false, QString()}; }

/// The two readings the headline verdict is actually cut from.
///
/// This replaced a weighted vote over ~20 indicators, and it replaced it on
/// evidence rather than taste. Scored against two structurally different
/// 40-day trend labels over 178 large caps and twelve years, split into six
/// train years and six test years:
///
///     signal                              L1 test   L2 test
///     the old ~20-indicator composite        79.8%     67.2%
///     MA fan alignment alone                 83.4%     85.8%
///     distance from SMA200 alone             82.6%     82.7%
///     this pair                              96.0%     87.9%
///
/// Train and test agree to within half a point on every row, so none of that
/// is a fitted number. The old composite lost to nearly every single component
/// of itself: twenty correlated readings averaged together describe a trend
/// worse than two chosen ones, because averaging collinear votes adds
/// confidence without adding information and drags every reading toward the
/// middle.
///
/// Both terms are deliberately per-name. An earlier attempt combined them as
/// cross-sectional z-scores and scored *worse* than either alone (76.8%),
/// which is the correct behaviour and was the giveaway: a z-score answers
/// "compared with everything else today", and the panel's claim is about this
/// stock on its own. Ranking against a universe is the right transform for a
/// screener and the wrong one for a description.
///
///   fan       how the averages are stacked, from every ordered pair of the
///             10/20/50/100/200 windows. +1 when every fast average is above
///             every slow one, -1 when perfectly inverted. Alignment is what
///             separates a trend from a stock that merely closed up today.
///   distance  where price sits against the 50-day, in ATRs, squashed through
///             tanh so an extended name saturates instead of dominating.
struct TrendStructure {
    double score = 0.0;
    double fan = 0.0;
    double distance = 0.0;
    bool valid = false;
};

TrendStructure trend_structure(const RatingInput& in) {
    static const char* kFanColumns[] = {"sma_10", "sma_20", "sma_50", "sma_100", "sma_200"};
    constexpr int kCount = 5;

    TrendStructure t;
    double values[kCount];
    for (int i = 0; i < kCount; ++i) {
        if (!in.now.has(QLatin1String(kFanColumns[i])))
            return t; // alignment needs the whole ladder to mean anything
        values[i] = in.now.get(QLatin1String(kFanColumns[i]));
    }

    double sum = 0.0;
    int pairs = 0;
    for (int i = 0; i < kCount; ++i) {
        for (int j = i + 1; j < kCount; ++j) {
            if (values[i] > values[j])
                sum += 1.0;
            else if (values[i] < values[j])
                sum -= 1.0;
            ++pairs;
        }
    }
    t.fan = sum / pairs;

    const double atr = in.now.get("atr", 0.0);
    const double sma50 = values[2];
    if (!(atr > 0.0) || !(sma50 > 0.0) || !(in.close > 0.0))
        return t;
    // tanh(x/2): ~0.46 at one ATR from the average, ~0.76 at two, saturating
    // after that. Bounded, so no single extended name can swamp the pair.
    t.distance = std::tanh(((in.close - sma50) / atr) / 2.0);

    t.score = 0.5 * t.fan + 0.5 * t.distance;
    t.valid = true;
    return t;
}

} // namespace

// ── has_sufficient_history ───────────────────────────────────────────────────

bool has_sufficient_history(const RatingInput& in) {
    // The three structural readings, one per thing the rating relies on:
    // a medium-term average to place price against, MACD for the crossover,
    // and ADX with its DI legs — which is also what arms the trend filter.
    return in.now.has("sma_50") && in.now.has("sma_200") && in.now.has("atr") &&
           in.now.has("macd") && in.now.has("macd_signal") &&
           in.now.has("adx") && in.now.has("adx_pos") && in.now.has("adx_neg");
}

// ── score ────────────────────────────────────────────────────────────────────

IndicatorVerdict score(const QString& column, const RatingInput& in) {
    const Regime regime = regime_of(in);
    const bool have_prev = in.prev.has(column);
    const double value = in.now.get(column);
    const double prev = in.prev.get(column, value);

    // ── Moving averages — price above is bullish, below bearish ──────────────
    if (is_moving_average(column))
        return voting(score_vs_price(value, in.prev.has(column) ? prev : 0.0, in.close,
                                     in.now.get("atr", 0.0)),
                      column == "vwap" ? kBucketVolume : kBucketTrend);

    // The Bollinger mid-line is the 20-SMA; show it, do not count it twice.
    if (column == "bb_mavg")
        return display_only(
            score_vs_price(value, in.prev.has(column) ? prev : 0.0, in.close, in.now.get("atr", 0.0)));

    // ── MACD — the histogram, against the signal line ────────────────────────
    // The old rule tested `macd > 0`, which says nothing about the crossover
    // that MACD exists to report: a MACD line sitting at +0.9 *below* a signal
    // line at +4.1 is a bearish cross, and it was being scored as a bullish.
    if (column == "macd") {
        if (!in.now.has("macd_signal"))
            return display_only(S::Neutral);
        const double hist = value - in.now.get("macd_signal");
        const bool have_prev_hist = in.prev.has("macd") && in.prev.has("macd_signal");
        const double prev_hist = in.prev.get("macd") - in.prev.get("macd_signal");
        return voting(score_zero_line(hist, prev_hist, have_prev_hist), kBucketTrend);
    }
    if (column == "macd_signal") {
        // Displayed with the same colour as the line it belongs to.
        return display_only(score(QStringLiteral("macd"), in).signal);
    }

    // ── ADX — strength from ADX, direction from DI+/DI- ──────────────────────
    if (column == "adx") {
        if (!in.now.has("adx_pos") || !in.now.has("adx_neg"))
            return display_only(S::Neutral);
        if (value < 20.0)
            return voting(S::Neutral, kBucketTrend); // no trend to be directional about
        const double dir = in.now.get("adx_pos") - in.now.get("adx_neg");
        if (dir > 0.0)
            return voting(value >= 25.0 ? S::StrongBuy : S::Buy, kBucketTrend);
        if (dir < 0.0)
            return voting(value >= 25.0 ? S::StrongSell : S::Sell, kBucketTrend);
        return voting(S::Neutral, kBucketTrend);
    }

    // ── Aroon — one vote from the pair, not one each ─────────────────────────
    // Up and Down are not independent readings; scoring them separately let a
    // single indicator cast a bullish and a bearish simultaneously.
    if (column == "aroon_up" || column == "aroon_down") {
        if (!in.now.has("aroon_up") || !in.now.has("aroon_down"))
            return display_only(S::Neutral);
        const double diff = in.now.get("aroon_up") - in.now.get("aroon_down");
        S s = S::Neutral;
        if (diff >= 50.0)
            s = S::StrongBuy;
        else if (diff > 0.0)
            s = S::Buy;
        else if (diff <= -50.0)
            s = S::StrongSell;
        else if (diff < 0.0)
            s = S::Sell;
        // Both rows show the pair's verdict; only Up carries it into the tally.
        return column == "aroon_up" ? voting(s, kBucketTrend) : display_only(s);
    }

    // ── Bounded oscillators ──────────────────────────────────────────────────
    if (column == "rsi")
        return voting(respect_trend(score_extremes(value, prev, have_prev, 30.0, 70.0), regime), kBucketMomentum);
    if (column == "mfi")
        return voting(respect_trend(score_extremes(value, prev, have_prev, 20.0, 80.0), regime), kBucketMomentum);
    if (column == "williams_r")
        return voting(respect_trend(score_extremes(value, prev, have_prev, -80.0, -20.0), regime), kBucketMomentum);
    if (column == "cci")
        return voting(respect_trend(score_extremes(value, prev, have_prev, -100.0, 100.0), regime), kBucketMomentum);

    // Stochastic: %K confirms out of its extreme by crossing %D. %D is the same
    // indicator smoothed, so it shows the verdict without casting a second vote.
    if (column == "stoch_k" || column == "stoch_d") {
        S s = S::Neutral;
        if (in.now.has("stoch_k") && in.now.has("stoch_d")) {
            const double k = in.now.get("stoch_k");
            const double d = in.now.get("stoch_d");
            if (k <= 20.0)
                s = k > d ? S::StrongBuy : S::Neutral;
            else if (k >= 80.0)
                s = k < d ? S::StrongSell : S::Neutral;
        }
        s = respect_trend(s, regime);
        return column == "stoch_k" ? voting(s, kBucketMomentum) : display_only(s);
    }

    // Bollinger %B — only a close outside the bands says anything, and only
    // once it comes back inside.
    if (column == "bb_pband") {
        S s = S::Neutral;
        if (value < 0.0)
            s = (have_prev && value > prev) ? S::StrongBuy : S::Neutral;
        else if (value > 1.0)
            s = (have_prev && value < prev) ? S::StrongSell : S::Neutral;
        return voting(respect_trend(s, regime), kBucketMomentum);
    }

    // ── Rate-of-change style momentum ────────────────────────────────────────
    if (column == "roc") {
        if (value >= 10.0)
            return voting(S::StrongBuy, kBucketMomentum);
        if (value > 2.0)
            return voting(S::Buy, kBucketMomentum);
        if (value <= -10.0)
            return voting(S::StrongSell, kBucketMomentum);
        if (value < -2.0)
            return voting(S::Sell, kBucketMomentum);
        return voting(S::Neutral, kBucketMomentum);
    }
    if (column == "ao")
        return voting(score_zero_line(value, prev, have_prev), kBucketMomentum);

    // ── Volume ───────────────────────────────────────────────────────────────
    if (column == "cmf") {
        if (value >= 0.20)
            return voting(S::StrongBuy, kBucketVolume);
        if (value > 0.05)
            return voting(S::Buy, kBucketVolume);
        if (value <= -0.20)
            return voting(S::StrongSell, kBucketVolume);
        if (value < -0.05)
            return voting(S::Sell, kBucketVolume);
        return voting(S::Neutral, kBucketVolume);
    }
    // Running totals: a single bar of a cumulative sum is noise, so these are
    // read as a slope over the lookback window.
    if (column == "obv" || column == "adi") {
        if (!in.back.has(column))
            return display_only(S::Neutral);
        const double earlier = in.back.get(column);
        if (value > earlier)
            return voting(S::Buy, kBucketVolume);
        if (value < earlier)
            return voting(S::Sell, kBucketVolume);
        return voting(S::Neutral, kBucketVolume);
    }

    // ── Displayed, but carrying no direction of their own ────────────────────
    // ATR is a distance, the Bollinger bands and widths are levels. Reporting
    // them as Neutral votes would dilute every rating toward the middle.
    return display_only(S::Neutral);
}

// ── aggregate ────────────────────────────────────────────────────────────────

RatingVerdict aggregate(const QVector<TechIndicator>& scored, const RatingInput& in) {
    RatingVerdict v;
    v.displayed = static_cast<int>(scored.size());

    // Per-bucket voter counts. Only counts: the verdict is cut from
    // trend_structure(), so there is no vote-sum to accumulate — keeping the
    // old ±2/±1 points tally here made aggregate() look like it still
    // aggregated votes and invited careful preservation of semantics that no
    // longer influence any output.
    QHash<QString, int> voters;

    for (const auto& ti : scored) {
        if (!ti.votes)
            continue;
        switch (ti.signal) {
            case S::StrongBuy:
                v.strong_buy++;
                break;
            case S::Buy:
                v.buy++;
                break;
            case S::Neutral:
                v.neutral++;
                break;
            case S::Sell:
                v.sell++;
                break;
            case S::StrongSell:
                v.strong_sell++;
                break;
        }
        voters[ti.rating_bucket]++;
        v.voting++;
    }

    const int trend_voters = voters.value(QLatin1String(kBucketTrend));
    const TrendStructure structure = trend_structure(in);

    if (!has_sufficient_history(in) || !structure.valid || v.voting < kMinVotingIndicators ||
        trend_voters < kMinTrendVoters) {
        v.overall = S::Neutral;
        v.net = 0.0;
        v.basis = QStringLiteral("Not enough history to rate — needs the 10/20/50/100/200 "
                                 "averages, ATR, MACD and ADX, and at least %1 indicators "
                                 "(%2 of them trend). Have %3 scored, %4 trend.")
                      .arg(kMinVotingIndicators)
                      .arg(kMinTrendVoters)
                      .arg(v.voting)
                      .arg(trend_voters);
        return v;
    }

    // The verdict is trend structure, not a count of the table. The tally above
    // still reports what each indicator says, which is worth showing — but it is
    // no longer what the headline is derived from, and the basis line names the
    // two terms that are so the two are not mistaken for each other.
    v.net = structure.score;

    if (v.net >= kStrongBand)
        v.overall = S::StrongBuy;
    else if (v.net >= kDirectionalBand)
        v.overall = S::Buy;
    else if (v.net <= -kStrongBand)
        v.overall = S::StrongSell;
    else if (v.net <= -kDirectionalBand)
        v.overall = S::Sell;
    else
        v.overall = S::Neutral;

    const double atr = in.now.get("atr", 0.0);
    v.basis = QStringLiteral("MA alignment %1%2  \u00b7  price %3%4 ATR from the 50-day")
                  .arg(structure.fan >= 0 ? "+" : "")
                  .arg(structure.fan, 0, 'f', 2)
                  .arg(in.close >= in.now.get("sma_50") ? "+" : "")
                  .arg(atr > 0.0 ? (in.close - in.now.get("sma_50")) / atr : 0.0, 0, 'f', 1);
    return v;
}

} // namespace technical_rating

} // namespace fincept::services::equity
