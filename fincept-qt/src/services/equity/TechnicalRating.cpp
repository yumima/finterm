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

/// Bucket weights. Trend leads because it answers the question the rating is
/// actually asked ("which way is this going"); the oscillators are a smaller,
/// contrarian correction to it rather than an equal partner.
///
/// There is deliberately no volatility bucket. BB %B was the only volatility
/// reading with a direction, and a bucket holding a single voter swings to a
/// full ±1.00 on one indicator — at which point its weight, however small,
/// behaves like a tiebreak vote nobody asked for. It is an oscillator, so it
/// scores in the momentum bucket alongside the others.
constexpr double kWeightTrend = 0.50;
constexpr double kWeightMomentum = 0.30;
constexpr double kWeightVolume = 0.20;

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
/// reason to bearish, and treating it as one is how a stock breaking out to new
/// highs came back rated STRONG SELL.
S respect_trend(S s, const Regime& r) {
    if (!r.trending)
        return s;
    if (r.up && (s == S::Bearish || s == S::StrongBearish))
        return S::Neutral;
    if (!r.up && (s == S::Bullish || s == S::StrongBullish))
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
        return (far && rising) ? S::StrongBullish : S::Bullish;
    if (gap < 0.0)
        return (far && falling) ? S::StrongBearish : S::Bearish;
    return S::Neutral;
}

/// A bounded oscillator votes only from its extremes, and only once it has
/// started back out of them. See the class comment: without the turn test this
/// reads Buy every single bar of a sustained decline.
S score_extremes(double value, double prev, bool have_prev, double oversold, double overbought) {
    if (value <= oversold)
        return (have_prev && value > prev) ? S::StrongBullish : S::Neutral;
    if (value >= overbought)
        return (have_prev && value < prev) ? S::StrongBearish : S::Neutral;
    return S::Neutral;
}

/// Zero-centred oscillator: side of zero gives the direction, and expanding
/// away from zero upgrades it to strong.
S score_zero_line(double value, double prev, bool have_prev) {
    if (value > 0.0)
        return (have_prev && value > prev) ? S::StrongBullish : S::Bullish;
    if (value < 0.0)
        return (have_prev && value < prev) ? S::StrongBearish : S::Bearish;
    return S::Neutral;
}

IndicatorVerdict voting(S s, const char* bucket) { return {s, true, QString::fromLatin1(bucket)}; }
IndicatorVerdict display_only(S s) { return {s, false, QString()}; }

} // namespace

// ── has_sufficient_history ───────────────────────────────────────────────────

bool has_sufficient_history(const RatingInput& in) {
    // The three structural readings, one per thing the rating relies on:
    // a medium-term average to place price against, MACD for the crossover,
    // and ADX with its DI legs — which is also what arms the trend filter.
    return in.now.has("sma_50") && in.now.has("macd") && in.now.has("macd_signal") &&
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
            return voting(value >= 25.0 ? S::StrongBullish : S::Bullish, kBucketTrend);
        if (dir < 0.0)
            return voting(value >= 25.0 ? S::StrongBearish : S::Bearish, kBucketTrend);
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
            s = S::StrongBullish;
        else if (diff > 0.0)
            s = S::Bullish;
        else if (diff <= -50.0)
            s = S::StrongBearish;
        else if (diff < 0.0)
            s = S::Bearish;
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
                s = k > d ? S::StrongBullish : S::Neutral;
            else if (k >= 80.0)
                s = k < d ? S::StrongBearish : S::Neutral;
        }
        s = respect_trend(s, regime);
        return column == "stoch_k" ? voting(s, kBucketMomentum) : display_only(s);
    }

    // Bollinger %B — only a close outside the bands says anything, and only
    // once it comes back inside.
    if (column == "bb_pband") {
        S s = S::Neutral;
        if (value < 0.0)
            s = (have_prev && value > prev) ? S::StrongBullish : S::Neutral;
        else if (value > 1.0)
            s = (have_prev && value < prev) ? S::StrongBearish : S::Neutral;
        return voting(respect_trend(s, regime), kBucketMomentum);
    }

    // ── Rate-of-change style momentum ────────────────────────────────────────
    if (column == "roc") {
        if (value >= 10.0)
            return voting(S::StrongBullish, kBucketMomentum);
        if (value > 2.0)
            return voting(S::Bullish, kBucketMomentum);
        if (value <= -10.0)
            return voting(S::StrongBearish, kBucketMomentum);
        if (value < -2.0)
            return voting(S::Bearish, kBucketMomentum);
        return voting(S::Neutral, kBucketMomentum);
    }
    if (column == "ao")
        return voting(score_zero_line(value, prev, have_prev), kBucketMomentum);

    // ── Volume ───────────────────────────────────────────────────────────────
    if (column == "cmf") {
        if (value >= 0.20)
            return voting(S::StrongBullish, kBucketVolume);
        if (value > 0.05)
            return voting(S::Bullish, kBucketVolume);
        if (value <= -0.20)
            return voting(S::StrongBearish, kBucketVolume);
        if (value < -0.05)
            return voting(S::Bearish, kBucketVolume);
        return voting(S::Neutral, kBucketVolume);
    }
    // Running totals: a single bar of a cumulative sum is noise, so these are
    // read as a slope over the lookback window.
    if (column == "obv" || column == "adi") {
        if (!in.back.has(column))
            return display_only(S::Neutral);
        const double earlier = in.back.get(column);
        if (value > earlier)
            return voting(S::Bullish, kBucketVolume);
        if (value < earlier)
            return voting(S::Bearish, kBucketVolume);
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

    struct Bucket {
        double sum = 0.0;
        int count = 0;
    };
    QHash<QString, Bucket> buckets;

    for (const auto& ti : scored) {
        if (!ti.votes)
            continue;
        switch (ti.signal) {
            case S::StrongBullish:
                v.strong_bullish++;
                break;
            case S::Bullish:
                v.bullish++;
                break;
            case S::Neutral:
                v.neutral++;
                break;
            case S::Bearish:
                v.bearish++;
                break;
            case S::StrongBearish:
                v.strong_bearish++;
                break;
        }
        double points = 0.0;
        switch (ti.signal) {
            case S::StrongBullish:
                points = 2.0;
                break;
            case S::Bullish:
                points = 1.0;
                break;
            case S::Neutral:
                points = 0.0;
                break;
            case S::Bearish:
                points = -1.0;
                break;
            case S::StrongBearish:
                points = -2.0;
                break;
        }
        // The weighted bucket, not the panel the row is displayed under.
        auto& b = buckets[ti.rating_bucket];
        b.sum += points;
        b.count++;
        v.voting++;
    }

    const int trend_voters = buckets.value(QLatin1String(kBucketTrend)).count;
    if (!has_sufficient_history(in) || v.voting < kMinVotingIndicators ||
        trend_voters < kMinTrendVoters) {
        v.overall = S::Neutral;
        v.net = 0.0;
        v.basis = QStringLiteral("Not enough history to rate — needs SMA 50, MACD and ADX, "
                                 "and at least %1 indicators (%2 of them trend). "
                                 "Have %3 scored, %4 trend.")
                      .arg(kMinVotingIndicators)
                      .arg(kMinTrendVoters)
                      .arg(v.voting)
                      .arg(trend_voters);
        return v;
    }

    static const QVector<QPair<QString, double>> kWeights = {
        {QLatin1String(kBucketTrend), kWeightTrend},
        {QLatin1String(kBucketMomentum), kWeightMomentum},
        {QLatin1String(kBucketVolume), kWeightVolume},
    };

    double weighted = 0.0;
    double weight_used = 0.0;
    QStringList parts;
    for (const auto& kv : kWeights) {
        const Bucket b = buckets.value(kv.first);
        if (b.count == 0)
            continue; // bucket absent — its weight redistributes, not zeroes
        // Each vote is at most ±2, so dividing by 2*count normalises to [-1, 1].
        const double score = b.sum / (2.0 * b.count);
        weighted += kv.second * score;
        weight_used += kv.second;
        parts << QStringLiteral("%1 %2%3")
                     .arg(kv.first.at(0).toUpper() + kv.first.mid(1))
                     .arg(score >= 0 ? "+" : "")
                     .arg(score, 0, 'f', 2);
    }

    v.net = weight_used > 0.0 ? weighted / weight_used : 0.0;

    if (v.net >= kStrongBand)
        v.overall = S::StrongBullish;
    else if (v.net >= kDirectionalBand)
        v.overall = S::Bullish;
    else if (v.net <= -kStrongBand)
        v.overall = S::StrongBearish;
    else if (v.net <= -kDirectionalBand)
        v.overall = S::Bearish;
    else
        v.overall = S::Neutral;

    v.basis = parts.join(QStringLiteral("  ·  "));
    return v;
}

} // namespace technical_rating

} // namespace fincept::services::equity
