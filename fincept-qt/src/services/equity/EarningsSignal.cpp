// src/services/equity/EarningsSignal.cpp
#include "services/equity/EarningsSignal.h"

#include <QDateTime>

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
constexpr double kFullReactionPct   = 5.0;   // avg next-session move that maxes reaction
constexpr double kFullRevision30Pct = 3.0;   // 30-day drift in consensus EPS
constexpr double kFullRevision90Pct = 6.0;   // 90-day drift
constexpr double kFullEpsGrowth     = 0.25;  // 25% YoY EPS growth
constexpr double kFullRevGrowth     = 0.15;  // 15% YoY revenue growth
constexpr double kFullGrowthEdge    = 0.15;  // growth advantage over the index
constexpr double kFullPeCompression = 0.20;  // (trailing−forward)/trailing
constexpr double kFullTargetUpside  = 0.20;  // distance to mean analyst target

// Component weights. Revision momentum carries the most because a moving
// consensus is the one input that is genuinely forward-looking; the track
// record and the realised reactions are backward-looking priors.
constexpr double kWeightTrackRecord = 0.18;
constexpr double kWeightReaction    = 0.12;
constexpr double kWeightRevisions   = 0.25;
constexpr double kWeightBreadth     = 0.15;
constexpr double kWeightGrowth      = 0.18;
constexpr double kWeightValuation   = 0.12;

// A verdict needs this much of the total weight backed by real data before it
// is allowed to say anything other than HOLD.
constexpr double kMinConfidence = 0.35;
// Composite score (on the ±100 scale) required to leave HOLD.
constexpr double kActionThreshold = 20.0;

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
    c.weight = kWeightTrackRecord;
    c.explanation = QStringLiteral(
        "How often this company clears the bar analysts set, and by how much. "
        "A consistent beater is more likely to clear the next one too.");

    int considered = 0, beats = 0;
    double surprise_sum = 0;
    for (const auto& p : a.history) {
        if (considered >= kTrackRecordQuarters) break;
        // A projected quarter is a forecast, not a track record.
        if (p.is_estimate || !p.surprise_pct.has_value()) continue;
        ++considered;
        if (*p.surprise_pct > 0) ++beats;
        surprise_sum += *p.surprise_pct;
    }
    if (considered == 0) {
        c.detail = QStringLiteral("No reported quarters with a consensus to compare against");
        return c;
    }

    const double beat_rate = static_cast<double>(beats) / considered;
    const double avg_surprise = surprise_sum / considered;
    v.scored_quarters = considered;
    v.beat_rate = beat_rate;
    v.avg_surprise_pct = avg_surprise;

    c.available = true;
    c.score = clamp_unit(0.6 * (2.0 * beat_rate - 1.0) +
                         0.4 * clamp_unit(avg_surprise / kFullSurprisePct));
    c.detail = QString("Beat %1 of %2 · average surprise %3")
                   .arg(beats).arg(considered).arg(pct_str(avg_surprise));
    return c;
}

SignalComponent score_reaction(const EarningsAnalysis& a, EarningsVerdict& v) {
    SignalComponent c;
    c.name = QStringLiteral("PRICE REACTION HISTORY");
    c.weight = kWeightReaction;
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

SignalComponent score_revisions(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("REVISION MOMENTUM");
    c.weight = kWeightRevisions;
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

SignalComponent score_breadth(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("ANALYST BREADTH");
    c.weight = kWeightBreadth;
    c.explanation = QStringLiteral(
        "How many analysts raised versus cut their number in the last 30 days. "
        "Counts the votes; revision momentum measures the size of the move.");

    double up = 0, down = 0;
    for (const QString& period : {QStringLiteral("0q"), QStringLiteral("+1q")}) {
        if (const auto* r = find_revision(a, period)) {
            up   += r->up_30d.value_or(0.0);
            down += r->down_30d.value_or(0.0);
        }
    }
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
    c.weight = kWeightGrowth;
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
    c.weight = kWeightValuation;
    c.explanation = QStringLiteral(
        "Where the price already sits relative to what analysts expect: how much "
        "growth the forward multiple assumes, the distance to the mean target, "
        "and the standing recommendation.");

    const auto& val = a.valuation;
    std::optional<double> pe_score;
    if (val.trailing_pe.has_value() && val.forward_pe.has_value() &&
        *val.trailing_pe > 0 && *val.forward_pe > 0) {
        // Forward multiple below trailing = the market's own estimates have
        // earnings growing into the price.
        pe_score = clamp_unit(((*val.trailing_pe - *val.forward_pe) / *val.trailing_pe) / kFullPeCompression);
    }
    std::optional<double> target_score;
    if (val.target_mean.has_value() && val.price.has_value() && *val.price > 0)
        target_score = clamp_unit(((*val.target_mean - *val.price) / *val.price) / kFullTargetUpside);
    std::optional<double> rec_score;
    if (val.recommendation_mean.has_value() && *val.recommendation_mean > 0) {
        // Yahoo's scale: 1 strong buy … 5 sell, 3 hold.
        rec_score = clamp_unit((3.0 - *val.recommendation_mean) / 1.5);
    }

    const auto blended = blend({{pe_score, 0.35}, {target_score, 0.40}, {rec_score, 0.25}});
    if (!blended) {
        c.detail = QStringLiteral("No valuation or target data");
        return c;
    }

    c.available = true;
    c.score = clamp_unit(*blended);
    QStringList bits;
    if (val.forward_pe && val.trailing_pe)
        bits << QString("fwd P/E %1 vs trailing %2")
                    .arg(QString::number(*val.forward_pe, 'f', 1), QString::number(*val.trailing_pe, 'f', 1));
    if (target_score && val.price && val.target_mean)
        bits << QString("%1 to mean target")
                    .arg(pct_str((*val.target_mean - *val.price) / *val.price * 100.0));
    if (!val.recommendation.isEmpty())
        bits << QString("consensus %1").arg(val.recommendation.toUpper());
    c.detail = bits.join(" · ");
    return c;
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
    if (!a.next.timestamp.has_value()) return -1;
    const auto when = QDateTime::fromSecsSinceEpoch(*a.next.timestamp);
    return static_cast<int>(QDateTime::currentDateTime().daysTo(when));
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
    v.components.append(score_breadth(a));
    v.components.append(score_growth(a));
    v.components.append(score_valuation(a));

    double weighted = 0, available_weight = 0, total_weight = 0;
    for (const auto& c : v.components) {
        total_weight += c.weight;
        if (!c.available) continue;
        weighted += c.score * c.weight;
        available_weight += c.weight;
    }
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
    const int days = days_to_next_earnings(a);
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
