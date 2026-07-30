// src/services/equity/EarningsSignal.cpp
#include "services/equity/EarningsSignal.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace fincept::services::equity {
namespace {

// ── Scoring calibration ──────────────────────────────────────────────────────
// Every "full marks" constant below is the move that earns a ±1.0 on its leg.
// They are deliberately in one block: these are judgement calls, and the point
// of the scorecard is that the user can see (and argue with) exactly where the
// line was drawn.
constexpr double kFullSurprisePct   = 10.0;  // avg beat that maxes the track-record leg
constexpr double kFullSurpriseIR    = 1.5;   // avg surprise ÷ its own spread, at full marks
constexpr double kFullReactionPct   = 5.0;   // avg next-session move that maxes reaction
constexpr double kFullRevision30Pct = 3.0;   // 30-day drift in consensus EPS
constexpr double kFullRevision90Pct = 6.0;   // 90-day drift
constexpr double kFullEpsGrowth     = 0.25;  // 25% YoY EPS growth
constexpr double kFullRevGrowth     = 0.15;  // 15% YoY revenue growth
constexpr double kFullGrowthEdge    = 0.15;  // growth advantage over the index
constexpr double kFullTargetUpside  = 0.20;  // distance to mean analyst target

// Component weights, before the horizon rotation below. Guidance expectations
// and revision momentum carry the most: a company's own guide is what the
// stock actually trades on, and while nobody publishes it early, analysts move
// their numbers toward it as they hear it. Everything else is a prior.
constexpr double kWeightTrackRecord = 0.16;
constexpr double kWeightReaction    = 0.10;
constexpr double kWeightRevisions   = 0.20;
constexpr double kWeightGuidance    = 0.22;
constexpr double kWeightBreadth     = 0.12;
constexpr double kWeightGrowth      = 0.14;
constexpr double kWeightValuation   = 0.06;

// ── Horizon rotation ─────────────────────────────────────────────────────────
// Beyond this many days out, nothing has started moving yet and the weights
// sit at their far-out split; at T−0 they sit at the near split. In between it
// is linear — a fitted curve here would imply a precision that measuring the
// decay of estimate revisions on twelve quarters of Yahoo data cannot support.
constexpr double kHorizonWindowDays = 45.0;
// Multipliers at the two ends. A short-horizon leg is worth 0.7× its base
// weight six weeks out and 1.3× the night before; long-horizon legs are the
// mirror image. Weights are renormalised afterwards, so these set the tilt,
// not the totals.
constexpr double kHorizonTiltFar  = 0.7;
constexpr double kHorizonTiltNear = 1.3;
// With no published date there is nothing to rotate on: sit exactly halfway,
// where both multipliers are 1.0 and every leg keeps its base weight.
constexpr double kHorizonUnknown = 0.5;

// A verdict needs this much of the total weight backed by real data before it
// is allowed to say anything other than HOLD.
constexpr double kMinConfidence = 0.35;
// Composite score (on the ±100 scale) required to leave HOLD.
constexpr double kActionThreshold = 20.0;

// Analyst spread on the coming quarter, as a percentage of the mean, above
// which the setup is called out as wide. Twelve analysts within a couple of
// cents and twelve spanning a third of the number are different situations.
constexpr double kWideDispersionPct = 25.0;

// Quarters of history the backward-looking legs consider. Eight covers two
// full years — long enough to be a track record, short enough that a changed
// business isn't judged on its old self.
constexpr int kTrackRecordQuarters = 8;

double clamp_unit(double v) { return std::clamp(v, -1.0, 1.0); }

/// Weighted mean over the parts that have data; nullopt when none do.
std::optional<double> blend(std::initializer_list<std::pair<std::optional<double>, double>> parts) {
    double sum = 0, wsum = 0;
    for (const auto& [value, weight] : parts) {
        if (!value.has_value()) continue;
        sum  += *value * weight;
        wsum += weight;
    }
    if (wsum <= 0) return std::nullopt;
    return sum / wsum;
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

const EarningsGrowthRow* find_growth(const EarningsAnalysis& a, const QString& period) {
    for (const auto& r : a.growth)
        if (r.period == period) return &r;
    return nullptr;
}

QString pct_str(double v, int decimals = 1) {
    return QString("%1%2%").arg(v >= 0 ? "+" : "").arg(QString::number(v, 'f', decimals));
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

// ── Components ───────────────────────────────────────────────────────────────

SignalComponent score_track_record(const EarningsAnalysis& a, EarningsVerdict& v) {
    SignalComponent c;
    c.name = QStringLiteral("SURPRISE TRACK RECORD");
    c.base_weight = kWeightTrackRecord;
    c.horizon = SignalHorizon::Long;
    c.explanation = QStringLiteral(
        "Deliberately NOT the beat rate. Around four in five large caps beat "
        "every quarter — they guide to a number they can clear — so 'beats "
        "most quarters' separates almost nothing. What does: the size of the "
        "average beat measured against how much it varies quarter to quarter, "
        "and whether beating actually gets the stock paid. A name that clears "
        "the bar and sells off anyway has a track record worth knowing about.");

    QVector<double> surprises, beat_reactions;
    int considered = 0, beats = 0;
    for (const auto& p : a.history) {
        if (considered >= kTrackRecordQuarters) break;
        // A projected quarter is a forecast, not a track record.
        if (p.is_estimate || !p.surprise_pct.has_value()) continue;
        ++considered;
        surprises.append(*p.surprise_pct);
        if (*p.surprise_pct > 0) {
            ++beats;
            if (p.reaction_pct.has_value())
                beat_reactions.append(*p.reaction_pct);
        }
    }
    if (considered == 0) {
        c.detail = QStringLiteral("No reported quarters with a consensus to compare against");
        return c;
    }

    double surprise_sum = 0;
    for (const double s : surprises) surprise_sum += s;
    const double avg_surprise = surprise_sum / considered;
    double var = 0;
    for (const double s : surprises) var += (s - avg_surprise) * (s - avg_surprise);
    const double stdev = std::sqrt(var / considered);

    v.scored_quarters = considered;
    v.beat_rate = static_cast<double>(beats) / considered;
    v.avg_surprise_pct = avg_surprise;
    v.surprise_stdev_pct = stdev;

    // Surprises standardised against their own spread: +2% every quarter like
    // clockwork is a stronger statement about the next print than +8% that
    // swings between +25% and −10%. Below three quarters, or when every
    // surprise is identical, there is no spread to divide by and the raw
    // magnitude is the honest fallback.
    const double consistency =
        (considered >= 3 && stdev > 1e-9) ? clamp_unit((avg_surprise / stdev) / kFullSurpriseIR)
                                          : clamp_unit(avg_surprise / kFullSurprisePct);
    std::optional<double> paid;
    if (!beat_reactions.isEmpty()) {
        double sum = 0;
        for (const double r : beat_reactions) sum += r;
        v.beat_reaction_pct = sum / beat_reactions.size();
        paid = clamp_unit(*v.beat_reaction_pct / kFullReactionPct);
    }

    c.available = true;
    c.score = clamp_unit(*blend({{consistency, 0.6}, {paid, 0.4}}));
    QStringList bits;
    bits << QString("Beat %1 of %2").arg(beats).arg(considered);
    bits << QString("surprise %1 ± %2").arg(pct_str(avg_surprise), QString::number(stdev, 'f', 1));
    if (v.beat_reaction_pct)
        bits << QString("beats paid %1 next session").arg(pct_str(*v.beat_reaction_pct));
    c.detail = bits.join(QStringLiteral(" · "));
    return c;
}

SignalComponent score_reaction(const EarningsAnalysis& a, EarningsVerdict& v) {
    SignalComponent c;
    c.name = QStringLiteral("PRICE REACTION HISTORY");
    c.base_weight = kWeightReaction;
    c.horizon = SignalHorizon::Long;
    c.explanation = QStringLiteral(
        "How the stock actually traded on past prints, close to close. Beating "
        "consensus and rising are different things — this leg measures the second.");

    int considered = 0, ups = 0;
    double sum = 0, abs_sum = 0;
    for (const auto& p : a.history) {
        if (considered >= kTrackRecordQuarters) break;
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        ++considered;
        if (*p.reaction_pct > 0) ++ups;
        sum += *p.reaction_pct;
        abs_sum += std::abs(*p.reaction_pct);
    }
    if (considered == 0) {
        c.detail = QStringLiteral("No price history around past reports");
        return c;
    }

    const double up_rate = static_cast<double>(ups) / considered;
    const double avg = sum / considered;
    v.reaction_quarters = considered;
    v.avg_reaction_pct = avg;
    v.typical_move_pct = abs_sum / considered;
    v.up_reaction_rate = up_rate;

    c.available = true;
    c.score = clamp_unit(0.5 * (2.0 * up_rate - 1.0) +
                         0.5 * clamp_unit(avg / kFullReactionPct));
    c.detail = QString("Rose on %1 of %2 prints · average %3 next session")
                   .arg(ups).arg(considered).arg(pct_str(avg));
    return c;
}

/// Net agreement among the analysts who moved a number in the last 30 days,
/// as −1 (everyone cut) … +1 (everyone raised). nullopt when nobody moved.
std::optional<double> net_revision_ratio(const EarningsAnalysis& a, const QString& period) {
    const auto* r = find_revision(a, period);
    if (!r) return std::nullopt;
    const double up = r->up_30d.value_or(0.0), down = r->down_30d.value_or(0.0);
    if (up + down <= 0) return std::nullopt;
    return (up - down) / (up + down);
}

/// "3 raised / 1 cut", or an empty string when the period has no counts.
QString revision_counts(const EarningsAnalysis& a, const QString& period) {
    const auto* r = find_revision(a, period);
    if (!r) return {};
    const double up = r->up_30d.value_or(0.0), down = r->down_30d.value_or(0.0);
    if (up + down <= 0) return {};
    return QString("%1 up / %2 down").arg(static_cast<int>(up)).arg(static_cast<int>(down));
}

SignalComponent score_revisions(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("REVISION MOMENTUM");
    c.base_weight = kWeightRevisions;
    c.horizon = SignalHorizon::Short;
    c.explanation = QStringLiteral(
        "Which way the consensus EPS number for the coming quarter has moved over "
        "the last 30 and 90 days. Estimates drift in the direction of the news "
        "analysts are hearing, so this is the most forward-looking leg here.");

    const auto* t = find_trend(a, QStringLiteral("0q"));
    if (!t) t = find_trend(a, QStringLiteral("0y"));
    if (!t) {
        c.detail = QStringLiteral("No estimate-revision history published");
        return c;
    }

    const auto d30 = revision_pct(t->current, t->d30);
    const auto d90 = revision_pct(t->current, t->d90);
    const auto s30 = d30 ? std::optional<double>(clamp_unit(*d30 / kFullRevision30Pct)) : std::nullopt;
    const auto s90 = d90 ? std::optional<double>(clamp_unit(*d90 / kFullRevision90Pct)) : std::nullopt;
    const auto blended = blend({{s30, 0.6}, {s90, 0.4}});
    if (!blended) {
        c.detail = QStringLiteral("No estimate-revision history published");
        return c;
    }

    c.available = true;
    c.score = clamp_unit(*blended);
    QStringList bits;
    if (d30) bits << QString("%1 vs 30d ago").arg(pct_str(*d30, 2));
    if (d90) bits << QString("%1 vs 90d ago").arg(pct_str(*d90, 2));
    c.detail = QString("%1 EPS estimate %2").arg(t->label.toLower(), bits.join(", "));
    return c;
}

SignalComponent score_guidance(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("GUIDANCE EXPECTATIONS");
    c.base_weight = kWeightGuidance;
    c.horizon = SignalHorizon::Short;
    c.explanation = QStringLiteral(
        "The stock trades on the guide, not on the quarter just ended — a "
        "company can beat on both lines and still fall 10% on what it says "
        "about the next one. Nobody publishes guidance early, but analysts "
        "move NEXT quarter's number as they hear it: cuts landing on the "
        "next quarter while the current one holds is the clearest warning "
        "available before the call, and the reverse is the clearest all-clear.");

    const auto* t1 = find_trend(a, QStringLiteral("+1q"));
    std::optional<double> s30, s90, d30, d90;
    if (t1) {
        d30 = revision_pct(t1->current, t1->d30);
        d90 = revision_pct(t1->current, t1->d90);
        if (d30) s30 = clamp_unit(*d30 / kFullRevision30Pct);
        if (d90) s90 = clamp_unit(*d90 / kFullRevision90Pct);
    }

    // The asymmetry: analysts revising the next quarter differently from the
    // current one. Both ratios are already −1…+1, so their difference spans
    // −2…+2 and the clamp caps a full reversal at full marks.
    std::optional<double> asymmetry;
    const auto next_ratio = net_revision_ratio(a, QStringLiteral("+1q"));
    const auto curr_ratio = net_revision_ratio(a, QStringLiteral("0q"));
    if (next_ratio && curr_ratio)
        asymmetry = clamp_unit(*next_ratio - *curr_ratio);

    // Next fiscal year, 90-day drift only: slower and noisier than the
    // quarter, but it is where a structural guide-down shows up first.
    std::optional<double> s_year, dy;
    if (const auto* ty = find_trend(a, QStringLiteral("+1y"))) {
        dy = revision_pct(ty->current, ty->d90);
        if (dy) s_year = clamp_unit(*dy / kFullRevision90Pct);
    }

    const auto blended = blend({{s30, 0.35}, {s90, 0.25}, {asymmetry, 0.25}, {s_year, 0.15}});
    if (!blended) {
        c.detail = QStringLiteral("No next-quarter estimate history published");
        return c;
    }

    c.available = true;
    c.score = clamp_unit(*blended);
    QStringList bits;
    if (d30) bits << QString("next-qtr EPS %1 vs 30d ago").arg(pct_str(*d30, 2));
    if (d90) bits << QString("%1 vs 90d ago").arg(pct_str(*d90, 2));
    if (asymmetry) {
        // Both counts are known to exist: the asymmetry is only computed when
        // each side has someone who actually moved a number.
        bits << QString("%1 on next quarter vs %2 on this one")
                    .arg(revision_counts(a, QStringLiteral("+1q")),
                         revision_counts(a, QStringLiteral("0q")));
    }
    if (dy) bits << QString("next-yr EPS %1 vs 90d ago").arg(pct_str(*dy, 2));
    c.detail = bits.join(QStringLiteral(" · "));
    return c;
}

SignalComponent score_breadth(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("ANALYST BREADTH");
    c.base_weight = kWeightBreadth;
    c.horizon = SignalHorizon::Short;
    c.explanation = QStringLiteral(
        "How many analysts raised versus cut their number for the COMING "
        "quarter in the last 30 days. Counts the votes; revision momentum "
        "measures the size of the move. The next quarter is deliberately not "
        "pooled in here — netting the two horizons together cancels out "
        "exactly the disagreement the guidance leg exists to find.");

    const auto* r = find_revision(a, QStringLiteral("0q"));
    const double up = r ? r->up_30d.value_or(0.0) : 0.0;
    const double down = r ? r->down_30d.value_or(0.0) : 0.0;
    const double total = up + down;
    if (total <= 0) {
        c.detail = QStringLiteral("No estimate changes filed in the last 30 days");
        return c;
    }

    c.available = true;
    c.score = clamp_unit((up - down) / total);
    c.detail = QString("%1 raised · %2 cut in the last 30 days")
                   .arg(static_cast<int>(up)).arg(static_cast<int>(down));
    return c;
}

SignalComponent score_growth(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("EXPECTED GROWTH");
    c.base_weight = kWeightGrowth;
    c.horizon = SignalHorizon::Long;
    c.explanation = QStringLiteral(
        "What the coming quarter is expected to deliver against the year-ago "
        "quarter, and whether that growth beats the index the stock sits in.");

    const auto eps_g = a.next.eps_growth;
    const auto rev_g = a.next.rev_growth;
    std::optional<double> edge;
    if (const auto* g = find_growth(a, QStringLiteral("0q"))) {
        if (g->stock.has_value() && g->index.has_value())
            edge = *g->stock - *g->index;
    }

    const auto s_eps  = eps_g ? std::optional<double>(clamp_unit(*eps_g / kFullEpsGrowth)) : std::nullopt;
    const auto s_rev  = rev_g ? std::optional<double>(clamp_unit(*rev_g / kFullRevGrowth)) : std::nullopt;
    const auto s_edge = edge  ? std::optional<double>(clamp_unit(*edge / kFullGrowthEdge)) : std::nullopt;
    const auto blended = blend({{s_eps, 0.45}, {s_rev, 0.30}, {s_edge, 0.25}});
    if (!blended) {
        c.detail = QStringLiteral("No forward growth estimates published");
        return c;
    }

    c.available = true;
    c.score = clamp_unit(*blended);
    QStringList bits;
    if (eps_g) bits << QString("EPS %1 YoY").arg(pct_str(*eps_g * 100.0));
    if (rev_g) bits << QString("revenue %1 YoY").arg(pct_str(*rev_g * 100.0));
    if (edge)  bits << QString("%1 vs index").arg(pct_str(*edge * 100.0));
    c.detail = bits.join(" · ");
    return c;
}

SignalComponent score_valuation(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("PRICE & EXPECTATIONS");
    c.base_weight = kWeightValuation;
    c.horizon = SignalHorizon::Long;
    c.explanation = QStringLiteral(
        "Where the price already sits against what the street expects: the "
        "distance to the mean target and the standing recommendation. Both lag "
        "— analysts move targets after the price, not before it — which is why "
        "this leg is the lightest one here. Forward-versus-trailing P/E is "
        "deliberately excluded: it is positive for every company with growing "
        "earnings, so scoring it would mean counting the growth leg twice "
        "under a valuation label.");

    const auto& val = a.valuation;
    std::optional<double> target_score;
    if (val.target_mean.has_value() && val.price.has_value() && *val.price > 0)
        target_score = clamp_unit(((*val.target_mean - *val.price) / *val.price) / kFullTargetUpside);
    std::optional<double> rec_score;
    if (val.recommendation_mean.has_value() && *val.recommendation_mean > 0) {
        // Yahoo's scale: 1 strong buy … 5 sell, 3 hold.
        rec_score = clamp_unit((3.0 - *val.recommendation_mean) / 1.5);
    }

    const auto blended = blend({{target_score, 0.60}, {rec_score, 0.40}});
    if (!blended) {
        c.detail = QStringLiteral("No target or recommendation data");
        return c;
    }

    c.available = true;
    c.score = clamp_unit(*blended);
    QStringList bits;
    if (target_score && val.price && val.target_mean)
        bits << QString("%1 to mean target")
                    .arg(pct_str((*val.target_mean - *val.price) / *val.price * 100.0));
    if (!val.recommendation.isEmpty())
        bits << QString("consensus %1").arg(val.recommendation.toUpper());
    c.detail = bits.join(" · ");
    return c;
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

/// 0 … 1 — how close the print is, on the window the rotation runs over.
/// A date that has already passed (Yahoo lagging its own calendar) is as
/// imminent as it gets.
double horizon_factor(const EarningsAnalysis& a, int days) {
    if (!a.next.timestamp.has_value()) return kHorizonUnknown;
    if (days < 0) return 1.0;
    return std::clamp((kHorizonWindowDays - days) / kHorizonWindowDays, 0.0, 1.0);
}

/// Rotate base weights toward the fast or the slow legs, then renormalise so
/// the effective weights still sum to the same total — the rotation decides
/// which legs the composite listens to, never how much composite there is.
void apply_horizon_rotation(QVector<SignalComponent>& components, double h) {
    const double tilt = kHorizonTiltFar + (kHorizonTiltNear - kHorizonTiltFar) * h;
    const double counter = kHorizonTiltNear - (kHorizonTiltNear - kHorizonTiltFar) * h;
    double base_total = 0, rotated_total = 0;
    for (auto& c : components) {
        c.weight = c.base_weight * (c.horizon == SignalHorizon::Short ? tilt : counter);
        base_total += c.base_weight;
        rotated_total += c.weight;
    }
    if (rotated_total > 0 && base_total > 0) {
        const double scale = base_total / rotated_total;
        for (auto& c : components) c.weight *= scale;
    }
}

QString horizon_note_for(const EarningsAnalysis& a, int days) {
    if (!a.next.timestamp.has_value()) {
        return QStringLiteral(
            "No report date published — every leg keeps its base weight, and nothing here "
            "knows how much time is left for the picture to change.");
    }
    if (days < 0) {
        return QStringLiteral(
            "The published date has passed and Yahoo hasn't updated it. Weights are set as "
            "for an imminent print — what analysts are doing right now outranks the record.");
    }
    const QString when = days == 0   ? QStringLiteral("Reports today")
                         : days == 1 ? QStringLiteral("Reports tomorrow")
                                     : QString("T−%1 days").arg(days);
    if (days <= 7) {
        return QString("%1 · weights rotated toward the fast legs — what analysts are doing to "
                       "their numbers this week outranks the track record and the multiple.")
            .arg(when);
    }
    if (days <= 21) {
        return QString("%1 · revisions and guidance drift carry more than their base weight; "
                       "the backward-looking legs carry less.")
            .arg(when);
    }
    return QString("%1 · too early for revisions to have moved much, so the slow legs (track "
                   "record, expected growth, price) carry the read for now.")
        .arg(when);
}

} // namespace

std::optional<double> metric_value(const EarningsPoint& p, ReactionMetric m) {
    switch (m) {
        case ReactionMetric::Surprise: return p.surprise_pct;
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
            // The trailing row pairs a forecast with a still-moving price —
            // not a completed observation, so it never enters the fit.
            if (p.is_estimate) continue;
            const auto x = metric_value(p, metric);
            if (!x.has_value() || !p.reaction_pct.has_value()) continue;
            xs.append(*x);
            ys.append(*p.reaction_pct);
        }
        c.n = xs.size();
        // Three points is the floor at which a correlation is arithmetically
        // defined; it is nowhere near enough to be meaningful, which is why
        // `n` travels with `r` everywhere it is displayed.
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
            // A metric that never moved (every quarter identical) has no
            // variance to correlate against — leave r unset rather than
            // dividing by zero into a NaN that renders as "nan".
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
    // statement about sessions. Counting intervals labels a print 20 hours
    // out as "today", and reading the dates locally drifts the answer by one
    // for every viewer west of New York — which is most of them.
    const QTimeZone et("America/New_York");
    const QDate today = now.toTimeZone(et).date();
    const QDate report = QDateTime::fromSecsSinceEpoch(*a.next.timestamp).toTimeZone(et).date();
    return static_cast<int>(today.daysTo(report));
}

EarningsVerdict evaluate_earnings(const EarningsAnalysis& a) {
    EarningsVerdict v;
    v.label = QStringLiteral("HOLD");

    if (!a.valid || !a.has_content()) {
        v.headline = QStringLiteral("No earnings data published for this security.");
        return v;
    }

    v.components.append(score_track_record(a, v));
    v.components.append(score_reaction(a, v));
    v.components.append(score_revisions(a));
    v.components.append(score_guidance(a));
    v.components.append(score_breadth(a));
    v.components.append(score_growth(a));
    v.components.append(score_valuation(a));

    // Weights depend on how much time is left for the picture to change, so
    // they are settled before anything is averaged over them.
    const int days = days_to_next_earnings(a);
    v.days_to_report = a.next.timestamp.has_value() ? days : -1;
    apply_horizon_rotation(v.components, horizon_factor(a, days));
    v.horizon_note = horizon_note_for(a, days);
    v.dispersion_pct = consensus_dispersion(a);
    v.dispersion_is_wide = v.dispersion_pct.has_value() && *v.dispersion_pct >= kWideDispersionPct;

    double weighted = 0, available_weight = 0, total_weight = 0;
    for (const auto& c : v.components) {
        total_weight += c.weight;
        if (!c.available) continue;
        weighted += c.score * c.weight;
        available_weight += c.weight;
    }
    // Confidence is measured against the ROTATED weights, so it moves with the
    // calendar too: a picture held up only by the track record and the multiple
    // is worth less the week of the print than it was six weeks out, and can
    // legitimately fall under the floor and force HOLD as the date approaches.
    v.confidence = total_weight > 0 ? available_weight / total_weight : 0.0;
    v.score = available_weight > 0 ? (weighted / available_weight) * 100.0 : 0.0;

    // ── Verdict ──────────────────────────────────────────────────────────────
    if (v.confidence < kMinConfidence) {
        v.direction = SignalDirection::Neutral;
        v.label = QStringLiteral("HOLD");
        v.headline = QStringLiteral(
            "Too little of the earnings picture is published to take a side — treat this as no signal.");
    } else if (v.score >= kActionThreshold) {
        v.direction = SignalDirection::Bullish;
        v.label = QStringLiteral("BUY");
    } else if (v.score <= -kActionThreshold) {
        v.direction = SignalDirection::Bearish;
        v.label = QStringLiteral("SELL");
    } else {
        v.direction = SignalDirection::Neutral;
        v.label = QStringLiteral("HOLD");
    }

    // ── Headline: name the strongest leg for and against ─────────────────────
    if (v.headline.isEmpty()) {
        const SignalComponent* best = nullptr;
        const SignalComponent* worst = nullptr;
        for (const auto& c : v.components) {
            if (!c.available) continue;
            const double contribution = c.score * c.weight;
            if (!best || contribution > best->score * best->weight) best = &c;
            if (!worst || contribution < worst->score * worst->weight) worst = &c;
        }
        const QString lead = v.direction == SignalDirection::Bullish  ? QStringLiteral("Constructive into the print")
                             : v.direction == SignalDirection::Bearish ? QStringLiteral("Cautious into the print")
                                                                       : QStringLiteral("Mixed into the print");
        QStringList parts;
        if (best && best->score > 0)
            parts << QString("for: %1 (%2)").arg(best->name.toLower(), best->detail);
        if (worst && worst != best) {
            // Name the weakest leg even when it isn't outright negative —
            // "nothing argues against this" is itself worth stating, and a
            // one-sided headline reads like the engine only looked for
            // confirmation.
            parts << QString("%1: %2 (%3)")
                         .arg(worst->score < 0 ? QStringLiteral("against") : QStringLiteral("weakest"),
                              worst->name.toLower(), worst->detail);
        }
        v.headline = parts.isEmpty() ? lead + "." : lead + " — " + parts.join("; ") + ".";
    }

    // ── Caveats: real risks that deliberately don't move the score ───────────
    if (v.typical_move_pct > 0) {
        v.caveats << QString("This name typically moves ±%1% on the print — size the position for that, "
                             "whichever way the signal leans.")
                         .arg(QString::number(v.typical_move_pct, 'f', 1));
    }
    if (days >= 0 && days <= 7) {
        v.caveats << QString("Report is %1 — most of the move happens in one session, and the signal "
                             "says nothing about which way that session breaks.")
                         .arg(days == 0   ? QStringLiteral("today")
                              : days == 1 ? QStringLiteral("tomorrow")
                                          : QString("in %1 days").arg(days));
    }
    if (a.next.timestamp.has_value() && a.next.is_estimated)
        v.caveats << QStringLiteral("The date is Yahoo's estimate, not company-confirmed.");
    // Dispersion is a magnitude risk, not a direction — it belongs here rather
    // than in the score. Analysts a quarter apart on the same quarter means
    // whichever side is wrong is very wrong, and the print moves accordingly.
    if (v.dispersion_is_wide) {
        v.caveats << QString("Analysts are %1% apart on this quarter's EPS (low to high, against a "
                             "%2 mean) — a spread that wide means a bigger surprise whichever way "
                             "it lands.")
                         .arg(QString::number(*v.dispersion_pct, 'f', 0))
                         .arg(a.next.eps_avg.has_value() ? QString::number(*a.next.eps_avg, 'f', 2)
                                                         : QStringLiteral("consensus"));
    }
    if (v.scored_quarters > 0 && v.scored_quarters < 4) {
        v.caveats << QString("Only %1 reported %2 of track record — the backward-looking legs are thin.")
                         .arg(v.scored_quarters)
                         .arg(v.scored_quarters == 1 ? QStringLiteral("quarter") : QStringLiteral("quarters"));
    }
    if (a.runup_5d_pct.has_value() && v.typical_move_pct > 0 &&
        std::abs(*a.runup_5d_pct) > v.typical_move_pct) {
        v.caveats << QString("The stock has already moved %1 in the last 5 sessions, more than its "
                             "typical earnings-day move — some of the expected news may be in the price.")
                         .arg(pct_str(*a.runup_5d_pct));
    }
    if (a.valuation.analyst_count.has_value() && *a.valuation.analyst_count > 0 &&
        *a.valuation.analyst_count < 5) {
        const int n = static_cast<int>(*a.valuation.analyst_count);
        v.caveats << QString("Only %1 %2 this name — consensus is easily skewed.")
                         .arg(n)
                         .arg(n == 1 ? QStringLiteral("analyst covers") : QStringLiteral("analysts cover"));
    }

    return v;
}

} // namespace fincept::services::equity
