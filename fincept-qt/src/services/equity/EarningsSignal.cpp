// src/services/equity/EarningsSignal.cpp
#include "services/equity/EarningsSignal.h"

#include "core/util/BarTime.h"

#include <QDateTime>

#include <algorithm>
#include <array>
#include <cmath>

namespace fincept::services::equity {
namespace {

// ── Calibration ──────────────────────────────────────────────────────────────
// Every constant below was measured, walk-forward, on the pooled study in
// plans/research/earnings_backtest_2026-10.md: each print predicted from that
// name's earlier prints only, pooled constants fitted on the first half of
// calendar time and checked on the second. Re-run the study before moving one.

// Quarters of a name's own history the answers read. Two years: long enough to
// be a record, short enough that a changed business isn't judged on its old self.
constexpr int kWindowQuarters = 8;
// The size forecast reads further back: a name's print-day size is a slow,
// stable property, and every past print available (the daemon sends twelve)
// beat an eight-print window out of sample — MAE 3.55 vs 3.64 pp.
constexpr int kSizeQuarters = 12;
// Fewest quarters before a name-specific answer is given at all.
constexpr int kMinQuarters = 3;

// ── Will they beat? ──────────────────────────────────────────────────────────
// Share of all prints that beat consensus EPS, pooled.
constexpr double kPooledBeatRate = 0.824;
// Weight of the pooled mix, in quarters, when shrinking a name's own record:
// p = (name's count + k × pooled share) / (name's quarters + k). Out of sample
// k = 8 scored Brier 0.1304 against 0.1380 for the pooled rate alone and 0.1405
// for the raw record; k = 4 was 0.1318 and k = 16 0.1310.
constexpr double kShrinkQuarters = 8.0;

// The four outcomes, by EPS surprise. The 3% line is where the pooled average
// reaction turns positive: a beat of 0–3% has traded DOWN (−0.19× the expected
// move, rising on 44% of prints), which is the "beat and fall" every reader has
// seen. Shares, multiples and rise rates are the pooled figures over 9,665
// prints; the multiples held within ±0.11× between the two halves of the
// sample, except the slight-beat bucket, which has drifted more negative.
struct ScenarioSpec {
    const char* label;
    const char* range;
    double upper;          // inclusive upper bound on surprise %
    double pooled_share;
    double move_multiple;
    double up_rate;
};
constexpr std::array<ScenarioSpec, 4> kScenarios{{
    {"MISS",        "at or below consensus", 0.0,  0.176, -0.67, 0.34},
    {"SLIGHT BEAT", "0 – 3% above",          3.0,  0.188, -0.19, 0.44},
    {"SOLID BEAT",  "3 – 10% above",         10.0, 0.313,  0.19, 0.55},
    {"BIG BEAT",    "over 10% above",        1e18, 0.323,  0.41, 0.60},
}};

// Analyst spread on the coming quarter, as a percentage of the mean, above
// which the setup is called out as wide.
constexpr double kWideDispersionPct = 25.0;
// Consensus drift inside this band (percent) reads as flat.
constexpr double kFlatDriftPct = 0.5;

// ── How big a move? ──────────────────────────────────────────────────────────
// expected = kMoveFromHistory × trailing mean |move| + kMoveFromVol × 20-session
// realised daily vol. Fitted on 181 names / 1,463 prints held out by company
// (MAE 3.19 vs 3.45 pp for the trailing mean alone, corr +0.53 vs +0.43). The
// 2026-10 re-run (236 names, 9,665 prints) agrees: 3.59 vs 3.64 pp against an
// eight-print mean, and a least-squares refit gained only 0.05 pp more. The coefficients are rounded on purpose —
// every blend between 0.55/1.0 and 0.3/1.6 landed within a point of the best.
constexpr double kMoveFromHistory = 0.5;
constexpr double kMoveFromVol     = 1.0;
// Realised |move| ÷ expected move, pooled quantiles: the forecast's own ranges.
constexpr double kHalfWithin = 0.95;   // 50th percentile
constexpr double kMostWithin = 1.88;   // 80th
constexpr double kTailBeyond = 2.46;   // 90th

// ── Priced in ────────────────────────────────────────────────────────────────
// The one backward-looking input with any sign at all: names in the top third
// of 20-session run-ups into a print rose on 49% of them, the bottom third on
// 53% (IC −0.025). Too faint to call a direction with; worth one line of
// context at the extremes.
constexpr double kHotRunupPct  = 4.7;    // top-third cutoff, pooled
constexpr double kColdRunupPct = -1.2;   // bottom-third cutoff, pooled
constexpr double kNearHighPct  = 3.0;    // "at its high" within this many percent
constexpr double kGapNotablePct = 10.0;  // price vs estimate race worth naming

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

QString pct_str(double v, int decimals = 1) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', decimals));
}

QString abs_pct(double v, int decimals = 1) {
    return QString("%1%").arg(QString::number(std::abs(v), 'f', decimals));
}

const EarningsTrendRow* find_trend(const EarningsAnalysis& a, const QString& period) {
    for (const auto& r : a.trend)
        if (r.period == period) return &r;
    return nullptr;
}

const EarningsRevisionRow* find_revision(const EarningsAnalysis& a, const QString& period) {
    for (const auto& r : a.revisions)
        if (r.period == period) return &r;
    return nullptr;
}

/// Percent change between two consensus readings. nullopt unless both are
/// present and the reference is non-zero — a company swinging through zero EPS
/// makes the percentage meaningless rather than merely large.
std::optional<double> revision_pct(const std::optional<double>& current,
                                   const std::optional<double>& reference) {
    if (!current.has_value() || !reference.has_value()) return std::nullopt;
    if (std::abs(*reference) < 1e-6) return std::nullopt;
    return (*current - *reference) / std::abs(*reference) * 100.0;
}

/// A quarter that has been reported and settled — the only kind any record
/// may be built from.
bool settled(const EarningsPoint& p) { return !p.is_estimate && p.reaction_pct.has_value(); }

/// A quarter whose surprise is a real beat or miss (not projected, not a
/// GAAP-vs-adjusted artefact).
bool scored(const EarningsPoint& p) {
    return !p.is_estimate && p.surprise_pct.has_value() && !p.surprise_suspect;
}

struct SizeRead {
    std::optional<double> expected;
    std::optional<double> trailing;
    int n = 0;
};

/// Size forecast from the quarters at index `from` onward (history is
/// newest-first), using `pre_vol` as the volatility going into the print.
SizeRead size_from(const QVector<EarningsPoint>& h, int from, const std::optional<double>& pre_vol) {
    SizeRead s;
    double abs_sum = 0;
    for (int j = from; j < h.size() && s.n < kSizeQuarters; ++j) {
        if (!settled(h[j])) continue;
        abs_sum += std::abs(*h[j].reaction_pct);
        ++s.n;
    }
    if (s.n < kMinQuarters || abs_sum <= 0) return s;
    s.trailing = abs_sum / s.n;
    s.expected = pre_vol.has_value() && *pre_vol > 0
                     ? kMoveFromHistory * *s.trailing + kMoveFromVol * *pre_vol
                     : *s.trailing;
    return s;
}

struct BeatRead {
    std::array<int, kScenarios.size()> counts{};
    int n = 0;
    int beats = 0;
    std::optional<double> median_surprise;
    std::array<double, kScenarios.size()> probs{};
    std::optional<double> p_beat;
};

BeatRead beat_from(const QVector<EarningsPoint>& h, int from) {
    BeatRead b;
    QVector<double> surprises;
    for (int j = from; j < h.size() && b.n < kWindowQuarters; ++j) {
        const int k = scenario_index(h[j]);
        if (k < 0) continue;
        ++b.counts[k];
        ++b.n;
        if (*h[j].surprise_pct > 0) ++b.beats;
        surprises.append(*h[j].surprise_pct);
    }
    if (!surprises.isEmpty()) {
        std::sort(surprises.begin(), surprises.end());
        const int m = surprises.size();
        b.median_surprise = m % 2 ? surprises[m / 2] : 0.5 * (surprises[m / 2 - 1] + surprises[m / 2]);
    }
    if (b.n < kMinQuarters) return b;
    for (size_t k = 0; k < kScenarios.size(); ++k)
        b.probs[k] = (b.counts[k] + kShrinkQuarters * kScenarios[k].pooled_share) /
                     (b.n + kShrinkQuarters);
    // The shares sum to 1 by construction; renormalise anyway so rounding in
    // the pooled table can never leave a probability mass unaccounted for.
    double total = 0;
    for (const double p : b.probs) total += p;
    for (double& p : b.probs) p /= total;
    b.p_beat = clamp01(1.0 - b.probs[0]);
    return b;
}

/// Analyst spread on the coming quarter, (high−low) as a percentage of the
/// mean. Falls back to the "0q" estimate row when the calendar carries no
/// range of its own — they describe the same quarter.
std::optional<double> consensus_dispersion(const EarningsAnalysis& a) {
    auto spread = [](const std::optional<double>& lo, const std::optional<double>& hi,
                     const std::optional<double>& avg) -> std::optional<double> {
        if (!lo || !hi || !avg || std::abs(*avg) < 1e-6 || *hi < *lo) return std::nullopt;
        return (*hi - *lo) / std::abs(*avg) * 100.0;
    };
    if (const auto d = spread(a.next.eps_low, a.next.eps_high, a.next.eps_avg))
        return d;
    for (const auto& e : a.estimates)
        if (e.period == QLatin1String("0q"))
            return spread(e.eps_low, e.eps_high, e.eps_avg);
    return std::nullopt;
}

/// Which way the coming quarter's consensus has been moving, in words.
void read_drift(const EarningsAnalysis& a, EarningsOutlook& o) {
    const auto* t = find_trend(a, QStringLiteral("0q"));
    const auto d30 = t ? revision_pct(t->current, t->d30) : std::nullopt;
    const auto d90 = t ? revision_pct(t->current, t->d90) : std::nullopt;
    QStringList bits;
    if (d30) bits << QString("%1 in 30 days").arg(pct_str(*d30, 1));
    if (d90) bits << QString("%1 in 90").arg(pct_str(*d90, 1));
    if (const auto* r = find_revision(a, QStringLiteral("0q"))) {
        const int up = static_cast<int>(r->up_30d.value_or(0.0));
        const int down = static_cast<int>(r->down_30d.value_or(0.0));
        if (up + down > 0) bits << QString("%1 raised / %2 cut this month").arg(up).arg(down);
    }
    if (const auto* t1 = find_trend(a, QStringLiteral("+1q"))) {
        if (const auto n90 = revision_pct(t1->current, t1->d90))
            bits << QString("next quarter %1 in 90").arg(pct_str(*n90, 1));
    }
    o.drift_detail = bits.join(QStringLiteral(" · "));

    // The 30-day number decides when there is one: it is the freshest, and
    // what analysts heard in the last month is what the print will be held to.
    const auto lead = d30 ? d30 : d90;
    if (!lead) {
        o.drift = EstimateDrift::Unknown;
        return;
    }
    o.drift = *lead > kFlatDriftPct    ? EstimateDrift::Rising
              : *lead < -kFlatDriftPct ? EstimateDrift::Falling
                                       : EstimateDrift::Flat;
}

void read_priced_in(const EarningsAnalysis& a, EarningsOutlook& o) {
    // Run-up into the print, index-relative where the benchmark was available.
    if (a.runup_20d_pct.has_value()) {
        const double r = *a.runup_20d_pct;
        QString line = QString("%1 over the last 20 sessions").arg(pct_str(r));
        if (a.rel_runup_20d_pct.has_value())
            line += QString(" (%1 against the index)").arg(pct_str(*a.rel_runup_20d_pct));
        if (r >= kHotRunupPct)
            line += QStringLiteral(" — in the top third of run-ups into a print, which rose on 49% "
                                   "of them against 53% for the bottom third; a lean, not a call.");
        else if (r <= kColdRunupPct)
            line += QStringLiteral(" — in the bottom third of run-ups into a print, which rose on 53% "
                                   "of them, slightly more than average; a lean, not a call.");
        else
            line += QStringLiteral(".");
        o.priced_in << line;
    }
    if (a.pct_from_52w_high.has_value()) {
        o.priced_in << (*a.pct_from_52w_high > -kNearHighPct
                            ? QStringLiteral("Trading at its 52-week high.")
                            : QString("%1 below its 52-week high.").arg(abs_pct(*a.pct_from_52w_high)));
    }
    // The race between the price and the number over the same 90 days: how
    // much of the move was earnings and how much was the multiple.
    const auto* t = find_trend(a, QStringLiteral("0q"));
    const auto est_90d = t ? revision_pct(t->current, t->d90) : std::nullopt;
    if (est_90d && a.runup_90d_pct.has_value()) {
        const double gap = *a.runup_90d_pct - *est_90d;
        QString line = QString("Over 90 days the stock did %1 while this quarter's estimate moved %2")
                           .arg(pct_str(*a.runup_90d_pct), pct_str(*est_90d));
        line += gap > kGapNotablePct    ? QStringLiteral(" — the multiple did the work, so the bar is higher than the estimate says.")
                : gap < -kGapNotablePct ? QStringLiteral(" — estimates have run ahead of the price.")
                                        : QStringLiteral(" — they have moved together.");
        o.priced_in << line;
    }
    const auto& val = a.valuation;
    if (val.target_mean.has_value() && val.price.has_value() && *val.price > 0) {
        QString line = QString("Mean analyst target %1 from here")
                           .arg(pct_str((*val.target_mean - *val.price) / *val.price * 100.0));
        if (!val.recommendation.isEmpty())
            line += QString(", consensus %1").arg(val.recommendation.toUpper().replace('_', ' '));
        o.priced_in << line + QStringLiteral(".");
    }
}

} // namespace

int scenario_index(const EarningsPoint& p) {
    if (!scored(p)) return -1;
    const double s = *p.surprise_pct;
    for (size_t k = 0; k < kScenarios.size(); ++k)
        if (s <= kScenarios[k].upper) return static_cast<int>(k);
    return static_cast<int>(kScenarios.size()) - 1;
}

EarningsOutlook evaluate_outlook(const EarningsAnalysis& a) {
    EarningsOutlook o;
    o.pooled_beat_rate = kPooledBeatRate;
    if (!a.valid || !a.has_content()) {
        o.headline = QStringLiteral("No earnings data published for this security.");
        return o;
    }
    o.valid = true;
    o.days_to_report = a.next.timestamp.has_value() ? days_to_next_earnings(a) : -1;

    // ── Will they beat? ──────────────────────────────────────────────────────
    const BeatRead b = beat_from(a.history, 0);
    o.scored_quarters = b.n;
    o.beats = b.beats;
    o.typical_surprise_pct = b.median_surprise;
    o.p_beat = b.p_beat;
    read_drift(a, o);
    o.dispersion_pct = consensus_dispersion(a);
    o.dispersion_is_wide = o.dispersion_pct.has_value() && *o.dispersion_pct >= kWideDispersionPct;

    // ── How big a move? ──────────────────────────────────────────────────────
    const SizeRead s = size_from(a.history, 0, a.pre_vol_pct);
    o.expected_move_pct = s.expected;
    o.trailing_move_pct = s.trailing;
    for (const auto& p : a.history)
        if (settled(p)) ++o.reaction_quarters;
    if (s.expected) {
        o.half_within_pct = *s.expected * kHalfWithin;
        o.most_within_pct = *s.expected * kMostWithin;
        o.tail_beyond_pct = *s.expected * kTailBeyond;
    }
    if (a.next.implied.has_value() && a.next.implied->event_move_pct.has_value()) {
        o.implied_move_pct = a.next.implied->event_move_pct;
        if (s.expected && *s.expected > 0)
            o.implied_ratio = *o.implied_move_pct / *s.expected;
    }

    // ── Scenarios: needs both a probability and a size to scale by ──────────
    for (size_t k = 0; k < kScenarios.size(); ++k) {
        EarningsScenario sc;
        sc.label = QString::fromLatin1(kScenarios[k].label);
        sc.range = QString::fromLatin1(kScenarios[k].range);
        sc.probability = b.p_beat ? b.probs[k] : kScenarios[k].pooled_share;
        sc.name_count = b.counts[k];
        sc.move_multiple = kScenarios[k].move_multiple;
        sc.typical_move_pct = s.expected ? *s.expected * kScenarios[k].move_multiple : 0.0;
        sc.up_rate = kScenarios[k].up_rate;
        o.scenarios.append(sc);
    }

    read_priced_in(a, o);

    // ── Headline ─────────────────────────────────────────────────────────────
    QStringList parts;
    if (o.p_beat) {
        const double p = *o.p_beat;
        parts << QString("%1 to beat (%2%)")
                     .arg(p >= 0.85   ? QStringLiteral("Very likely")
                          : p >= 0.70 ? QStringLiteral("Likely")
                          : p >= 0.50 ? QStringLiteral("Leaning")
                                      : QStringLiteral("Unlikely"))
                     .arg(QString::number(p * 100.0, 'f', 0));
    }
    if (o.expected_move_pct)
        parts << QString("expected move ±%1").arg(QString::number(*o.expected_move_pct, 'f', 1));
    parts << QStringLiteral("no reliable read on direction");
    o.headline = parts.join(QStringLiteral(" · ")) + QStringLiteral(".");
    if (o.expected_move_pct) {
        // The two buckets above the 3% line, weighted by how likely each is
        // for this name — a habitual big beater's "clear beat" is mostly big.
        const auto& solid = o.scenarios[2];
        const auto& big = o.scenarios[3];
        const double w = solid.probability + big.probability;
        const double clear = w > 0 ? (solid.probability * solid.typical_move_pct +
                                      big.probability * big.typical_move_pct) / w
                                   : 0.5 * (solid.typical_move_pct + big.typical_move_pct);
        o.headline += QString(" A miss has typically meant %1; a beat of more than 3% about %2.")
                          .arg(pct_str(o.scenarios[0].typical_move_pct), pct_str(clear));
    }

    // ── Caveats ──────────────────────────────────────────────────────────────
    if (a.next.timestamp.has_value() && o.days_to_report < 0)
        o.caveats << QStringLiteral("The published date has passed and Yahoo hasn't updated it yet.");
    if (a.next.timestamp.has_value() && a.next.is_estimated)
        o.caveats << QStringLiteral("The date is Yahoo's estimate, not company-confirmed.");
    if (!o.p_beat) {
        o.caveats << QString("Fewer than %1 reported quarters with a usable consensus — the beat "
                             "chances shown are the pooled mix, not this company's.")
                         .arg(kMinQuarters);
    } else if (o.scored_quarters < kWindowQuarters) {
        o.caveats << QString("Only %1 quarters of record — the beat probability leans on the pooled "
                             "rate more than usual.")
                         .arg(o.scored_quarters);
    }
    if (o.dispersion_is_wide) {
        o.caveats << QString("Analysts are %1% apart on this quarter's EPS — the surprise, whichever "
                             "way, is likely to be larger than usual.")
                         .arg(QString::number(*o.dispersion_pct, 'f', 0));
    }
    if (o.implied_ratio && (*o.implied_ratio >= 1.5 || *o.implied_ratio <= 0.67)) {
        o.caveats << QString("Options are pricing %1 move than this name's history and recent "
                             "volatility suggest (±%2 vs ±%3).")
                         .arg(*o.implied_ratio >= 1.5 ? QStringLiteral("a much bigger")
                                                      : QStringLiteral("a much smaller"),
                              abs_pct(*o.implied_move_pct), abs_pct(*o.expected_move_pct));
    }
    if (a.valuation.analyst_count.has_value() && *a.valuation.analyst_count > 0 &&
        *a.valuation.analyst_count < 5) {
        const int n = static_cast<int>(*a.valuation.analyst_count);
        o.caveats << QString("Only %1 %2 this name — consensus is easily skewed.")
                         .arg(n)
                         .arg(n == 1 ? QStringLiteral("analyst covers") : QStringLiteral("analysts cover"));
    }
    return o;
}

ForecastRecord forecast_record(const EarningsAnalysis& a) {
    ForecastRecord r;
    if (!a.valid) return r;
    double p_sum = 0;
    // history is newest-first; quarter i is forecast from i+1 onward — the
    // quarters that had already happened when it was still ahead.
    for (int i = 0; i < a.history.size(); ++i) {
        const auto& p = a.history[i];
        if (!settled(p)) continue;
        PrintForecast f;
        f.timestamp = p.timestamp;
        f.actual_move_pct = p.reaction_pct;
        const SizeRead s = size_from(a.history, i + 1, p.pre_vol_pct);
        f.expected_move_pct = s.expected;
        const BeatRead b = beat_from(a.history, i + 1);
        f.p_beat = b.p_beat;
        if (scored(p)) f.beat = *p.surprise_pct > 0;

        if (s.expected) {
            const double actual = std::abs(*p.reaction_pct);
            ++r.size_graded;
            if (actual <= *s.expected) ++r.size_inside;
            r.size_mae += std::abs(*s.expected - actual);
            r.trailing_mae += std::abs(*s.trailing - actual);
        }
        if (f.p_beat && f.beat.has_value()) {
            ++r.beat_graded;
            if (*f.beat) ++r.beat_hits;
            p_sum += *f.p_beat;
        }
        r.prints.append(f);
    }
    if (r.size_graded > 0) {
        r.size_mae /= r.size_graded;
        r.trailing_mae /= r.size_graded;
    }
    if (r.beat_graded > 0) r.mean_p_beat = p_sum / r.beat_graded;
    std::reverse(r.prints.begin(), r.prints.end());
    return r;
}

std::optional<double> metric_value(const EarningsPoint& p, ReactionMetric m) {
    switch (m) {
        case ReactionMetric::Surprise:
            // A flagged surprise is a GAAP-vs-adjusted artefact; one +213%
            // point would set a Pearson r almost by itself.
            return p.surprise_suspect ? std::nullopt : p.surprise_pct;
        case ReactionMetric::QoQ:      return p.eps_qoq_pct;
        case ReactionMetric::YoY:      return p.eps_yoy_pct;
    }
    return std::nullopt;
}

QVector<ReactionCorrelation> correlate_reactions(const EarningsAnalysis& a) {
    QVector<ReactionCorrelation> out;
    const std::pair<ReactionMetric, const char*> metrics[] = {
        {ReactionMetric::Surprise, "SURPRISE"},
        {ReactionMetric::QoQ, "QoQ"},
        {ReactionMetric::YoY, "YoY"},
    };
    for (const auto& [metric, label] : metrics) {
        ReactionCorrelation c;
        c.metric = metric;
        c.label = QString::fromLatin1(label);
        QVector<double> xs, ys;
        for (const auto& p : a.history) {
            if (p.is_estimate) continue;
            const auto x = metric_value(p, metric);
            if (!x.has_value() || !p.reaction_pct.has_value()) continue;
            xs.append(*x);
            ys.append(*p.reaction_pct);
        }
        c.n = xs.size();
        if (c.n >= 3) {
            double mx = 0, my = 0;
            for (int i = 0; i < c.n; ++i) { mx += xs[i]; my += ys[i]; }
            mx /= c.n;
            my /= c.n;
            double num = 0, dx = 0, dy = 0;
            for (int i = 0; i < c.n; ++i) {
                const double ax = xs[i] - mx, ay = ys[i] - my;
                num += ax * ay;
                dx  += ax * ax;
                dy  += ay * ay;
            }
            // A metric that never moved has no variance to correlate against.
            if (dx > 1e-12 && dy > 1e-12)
                c.r = num / (std::sqrt(dx) * std::sqrt(dy));
        }
        out.append(c);
    }
    return out;
}

int days_to_next_earnings(const EarningsAnalysis& a) {
    return days_to_next_earnings(a, QDateTime::currentDateTime());
}

int days_to_next_earnings(const EarningsAnalysis& a, const QDateTime& now) {
    if (!a.next.timestamp.has_value()) return -1;
    // Calendar days between the two dates *in market time*, not 24-hour
    // intervals from this instant: "reports tomorrow before the open" is a
    // statement about sessions, and reading the dates locally drifts the
    // answer by one for every viewer west of New York.
    const QDate today = core::bartime::market_date_et(now);
    const QDate report = core::bartime::market_date_et(*a.next.timestamp);
    return static_cast<int>(today.daysTo(report));
}

} // namespace fincept::services::equity
