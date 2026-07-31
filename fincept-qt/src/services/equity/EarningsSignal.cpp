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

constexpr double kFullExpectationGap = 20.0;  // pts of price-vs-estimate divergence
constexpr double kFullRelRunup       = 15.0;  // index-relative run-up into the print

// Component weights, before the horizon rotation below.
//
// The legs split into two groups, and the split matters more than any single
// number in it. SETUP legs describe the business and what the street thinks of
// it; BAR legs describe how much of that is already in the price. An earlier
// version of this scorer was SETUP-only, which made it a quality detector —
// and quality is precisely what gets priced in. It read BUY on names that then
// beat and fell, because every leg it had peaked exactly when the bar did.
// BAR now carries 35% of the weight, and the two are reported separately so a
// "strong company, very high bar" setup is legible instead of averaging into
// a confident-looking BUY.
constexpr double kWeightTrackRecord = 0.10;   // SETUP
constexpr double kWeightReaction    = 0.09;   // SETUP
constexpr double kWeightRevisions   = 0.14;   // SETUP
constexpr double kWeightGuidance    = 0.18;   // SETUP
constexpr double kWeightBreadth     = 0.06;   // SETUP
constexpr double kWeightGrowth      = 0.08;   // SETUP
constexpr double kWeightExpectations = 0.20;  // BAR
constexpr double kWeightCrowding     = 0.09;  // BAR
constexpr double kWeightValuation    = 0.06;  // BAR

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

// Beating quarters needed before "beats get paid" is allowed to score.
constexpr int kMinBeatReactions = 3;
// Past prints needed in the "walked in hot" subset before the reaction leg
// will narrow to it. Below this the conditional mean is one or two sessions
// and the unconditional average describes the name better.
constexpr int kMinConditionalPrints = 3;
// Distance below the 52-week high at which a name stops reading as extended.
constexpr double kNearHighPct = 20.0;
// Gap between the two axes (on the ±100 scale) worth calling out in the
// headline rather than leaving the reader to spot in the breakdown.
constexpr double kAxisTensionPts = 50.0;

// ── Expected-move blend ──────────────────────────────────────────────────────
// expected move = kMoveFromHistory x (trailing mean |move|)
//               + kMoveFromVol     x (20-session realised vol, daily %)
//
// Fitted pooled across 181 names and 1463 prints, time-split and then held out
// by COMPANY so the test names were never trained on. The weights are rounded
// hard on purpose: across subsamples the fitted volatility coefficient ranged
// 0.6 to 2.2 while never once changing sign, and every blend between 0.55/1.0
// and 0.3/1.6 landed within a point of the best. A precise pair would be false
// precision on a coefficient that loose; these sit mid-range.
//
//     trailing average alone   3.450 pp MAE, corr +0.433
//     this blend               3.193 pp MAE, corr +0.528
//
// Volatility earns its place by carrying what the earnings history cannot: a
// dozen prints months apart say nothing about whether the stock has been calm
// or wild in the weeks right before this one.
constexpr double kMoveFromHistory = 0.5;
constexpr double kMoveFromVol     = 1.0;

// Composite score at which the engine will predict this name's FULL typical
// move. Deliberately well short of 100: a ±100 composite would need every leg
// maxed in the same direction, which essentially never happens, so anchoring
// full conviction there would compress every real prediction to a rounding
// error. At ±50 a strongly-leaning setup predicts the whole typical move and
// anything beyond is clamped.
constexpr double kFullConvictionScore = 50.0;

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
    c.axis = SignalAxis::Setup;
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
    // "Beats get paid" needs more than one beat to mean anything: a single
    // quarter's reaction is one draw from a wide distribution, and letting it
    // carry 40% of the leg would put the loudest number on the thinnest
    // evidence. Below the floor the leg falls back to the surprise alone.
    std::optional<double> paid;
    if (beat_reactions.size() >= kMinBeatReactions) {
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
    c.axis = SignalAxis::Setup;
    c.explanation = QStringLiteral(
        "How the stock actually traded on past prints, close to close. Beating "
        "consensus and rising are different things — this leg measures the "
        "second. When the stock has already run into THIS print, it narrows to "
        "the past prints that also followed a run: a name that reliably fades "
        "after a strong week into the date is telling you something specific "
        "about the setup in front of you, not about its average quarter.");

    int considered = 0, ups = 0;
    double sum = 0, abs_sum = 0;
    QVector<double> runups;                    // run-ups of the same quarters
    QVector<QPair<double, double>> pairs;       // (runup, reaction)
    for (const auto& p : a.history) {
        if (considered >= kTrackRecordQuarters) break;
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        ++considered;
        if (*p.reaction_pct > 0) ++ups;
        sum += *p.reaction_pct;
        abs_sum += std::abs(*p.reaction_pct);
        if (p.runup_pct.has_value()) {
            runups.append(*p.runup_pct);
            pairs.append({*p.runup_pct, *p.reaction_pct});
        }
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

    // Conditional read: how this name traded on the prints it walked into hot.
    // Only used when the CURRENT setup is hot too — otherwise it is answering
    // a question nobody asked, and the unconditional average is the better
    // description of an ordinary setup.
    std::optional<double> conditional_avg;
    int conditional_n = 0;
    if (runups.size() >= 2 * kMinConditionalPrints && a.runup_5d_pct.has_value()) {
        QVector<double> sorted = runups;
        std::sort(sorted.begin(), sorted.end());
        // Upper of the two middles on an even count, not their average: it
        // makes the conditional read harder to trigger, and the safe direction
        // for a subset-of-a-subset is "don't narrow unless clearly warranted".
        const double median = sorted.at(sorted.size() / 2);
        if (*a.runup_5d_pct >= median) {
            double hot_sum = 0;
            for (const auto& [runup, reaction] : pairs) {
                if (runup < median) continue;
                hot_sum += reaction;
                ++conditional_n;
            }
            if (conditional_n >= kMinConditionalPrints) {
                conditional_avg = hot_sum / conditional_n;
                v.hot_runup_reaction_pct = conditional_avg;
                v.hot_runup_prints = conditional_n;
            }
        }
    }

    c.available = true;
    if (conditional_avg) {
        // Scored off the conditional mean alone: mixing in the up-rate over
        // all quarters would dilute the very selection that makes this useful.
        c.score = clamp_unit(*conditional_avg / kFullReactionPct);
        c.detail = QString("Rose on %1 of %2 prints · average %3 · but %4 after a run-up "
                           "like today's (%5 prints)")
                       .arg(ups).arg(considered).arg(pct_str(avg), pct_str(*conditional_avg))
                       .arg(conditional_n);
    } else {
        c.score = clamp_unit(0.5 * (2.0 * up_rate - 1.0) +
                             0.5 * clamp_unit(avg / kFullReactionPct));
        c.detail = QString("Rose on %1 of %2 prints · average %3 next session")
                       .arg(ups).arg(considered).arg(pct_str(avg));
    }
    return c;
}

SignalComponent score_expectations(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("EXPECTATIONS GAP");
    c.base_weight = kWeightExpectations;
    c.horizon = SignalHorizon::Short;
    c.axis = SignalAxis::Bar;
    c.explanation = QStringLiteral(
        "The race between the price and the number, over the same 90 days. "
        "Estimates up 3% while the stock is up 25% means the multiple did the "
        "work and the bar has been raised without earnings to back it — the "
        "company then has to beat a number the market has already moved past. "
        "Estimates rising faster than the price is the setup nobody is "
        "positioned for, and it is the one this leg rewards.");

    const auto* t = find_trend(a, QStringLiteral("0q"));
    if (!t) t = find_trend(a, QStringLiteral("+1q"));
    const auto est_90d = t ? revision_pct(t->current, t->d90) : std::nullopt;
    if (!est_90d || !a.runup_90d_pct.has_value()) {
        c.detail = QStringLiteral("Needs both a 90-day estimate history and 90 days of price");
        return c;
    }

    // Negative when the price outran the number. The comparison is against
    // the absolute price move on purpose: a re-rating raises the bar whether
    // it came from the sector, the market, or the name itself.
    const double gap = *est_90d - *a.runup_90d_pct;
    c.available = true;
    c.score = clamp_unit(gap / kFullExpectationGap);
    c.detail = QString("Consensus EPS %1 over 90d while the stock did %2 — %3")
                   .arg(pct_str(*est_90d), pct_str(*a.runup_90d_pct),
                        gap < -5.0   ? QStringLiteral("the price has run ahead of the numbers")
                        : gap > 5.0  ? QStringLiteral("the numbers have run ahead of the price")
                                     : QStringLiteral("they have moved together"));
    return c;
}

SignalComponent score_crowding(const EarningsAnalysis& a) {
    SignalComponent c;
    c.name = QStringLiteral("POSITIONING");
    c.base_weight = kWeightCrowding;
    c.horizon = SignalHorizon::Short;
    c.axis = SignalAxis::Bar;
    c.explanation = QStringLiteral(
        "How extended the name is walking in: how far it has outrun the index "
        "recently, and how close it sits to its 52-week high. Scored negative "
        "on purpose. This is not a view on the company — it is a statement "
        "about who already owns it and what they already expect, which is what "
        "decides whether good news is enough.");

    // Index-relative where the benchmark was available; absolute otherwise,
    // which is noisier but still says something in a flat market.
    const auto runup = a.rel_runup_20d_pct.has_value() ? a.rel_runup_20d_pct : a.runup_20d_pct;
    const bool relative = a.rel_runup_20d_pct.has_value();
    std::optional<double> runup_score;
    if (runup)
        runup_score = clamp_unit(-*runup / kFullRelRunup);

    // 0% from the high = full crowding; kNearHighPct or further below = none.
    std::optional<double> high_score;
    if (a.pct_from_52w_high.has_value()) {
        const double below = std::clamp(-*a.pct_from_52w_high, 0.0, kNearHighPct);
        high_score = -(1.0 - below / kNearHighPct);
    }

    const auto blended = blend({{runup_score, 0.65}, {high_score, 0.35}});
    if (!blended) {
        c.detail = QStringLiteral("No recent price history to measure positioning against");
        return c;
    }

    c.available = true;
    c.score = clamp_unit(*blended);
    QStringList bits;
    if (runup) {
        bits << QString("%1 over 20d%2").arg(pct_str(*runup),
                                             relative ? QStringLiteral(" vs the index") : QString());
    }
    if (a.pct_from_52w_high.has_value()) {
        bits << (*a.pct_from_52w_high > -1.0
                     ? QStringLiteral("sitting on its 52-week high")
                     : QString("%1 from the 52-week high").arg(pct_str(*a.pct_from_52w_high)));
    }
    c.detail = bits.join(QStringLiteral(" · "));
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

/// "3 up / 1 down", or an empty string when the period has no counts.
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
    c.axis = SignalAxis::Setup;
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
    c.axis = SignalAxis::Setup;
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

    // The asymmetry is a RELATIVE measure and cannot anchor the leg on its
    // own. A name being cut everywhere — this quarter hard, next quarter
    // merely less hard — has a positive asymmetry, and blend() renormalises
    // over whatever it is given, so scoring that alone would hand a company in
    // broad decline a maximally bullish guidance read. Which way next
    // quarter's number is actually moving has to be known first.
    if (!s30 && !s90) {
        c.detail = QStringLiteral(
            "No next-quarter consensus history published — analyst counts alone can't say "
            "which way that number is moving");
        return c;
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
    c.axis = SignalAxis::Setup;
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
    c.axis = SignalAxis::Setup;
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
    c.axis = SignalAxis::Bar;
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

QVector<QuarterPrediction> reconstruct_predictions(const EarningsAnalysis& a) {
    QVector<QuarterPrediction> out;
    if (!a.valid)
        return out;

    // `history` arrives newest-first. Index i is scored from i+1 onward — the
    // quarters that had already happened when quarter i was still ahead — so a
    // point can never be informed by its own result or anything after it.
    for (int i = 0; i < a.history.size(); ++i) {
        const auto& p = a.history[i];
        if (p.is_estimate || !p.reaction_pct.has_value())
            continue;   // no settled outcome to compare against

        QuarterPrediction qp;
        qp.timestamp = p.timestamp;
        qp.actual_move_pct = p.reaction_pct;

        // The world as it stood before this print: earlier quarters only, plus
        // the run-up into this one, which WAS knowable at the time.
        EarningsAnalysis prior;
        prior.valid = true;
        prior.symbol = a.symbol;
        for (int j = i + 1; j < a.history.size(); ++j)
            prior.history.append(a.history[j]);
        prior.runup_5d_pct = p.runup_pct;
        prior.runup_20d_pct = p.runup_pct;

        EarningsVerdict pv;
        const auto record = score_track_record(prior, pv);
        const auto reaction = score_reaction(prior, pv);
        const auto crowding = score_crowding(prior);

        // Weighted over the FULL model's weights, not just these three. The
        // legs that cannot be reconstructed count as absent, which is exactly
        // what they are — so confidence lands near a third and the estimate is
        // muted accordingly.
        double weighted = 0, available = 0, total = 0;
        for (const auto* c : {&record, &reaction, &crowding}) {
            if (!c->available) continue;
            weighted += c->score * c->base_weight;
            available += c->base_weight;
        }
        for (const double w : {kWeightTrackRecord, kWeightReaction, kWeightRevisions,
                               kWeightGuidance, kWeightBreadth, kWeightGrowth,
                               kWeightExpectations, kWeightCrowding, kWeightValuation})
            total += w;

        if (available > 0 && total > 0 && pv.typical_move_pct > 0) {
            const double score = (weighted / available) * 100.0;
            const double confidence = available / total;
            // Same blend as the live verdict, using the volatility as it stood
            // before THIS print — which the history row carries, so a
            // reconstruction is not quietly using a better or worse size
            // estimate than the live reading it is drawn beside.
            const double size = p.pre_vol_pct.has_value() && *p.pre_vol_pct > 0
                                    ? kMoveFromHistory * pv.typical_move_pct +
                                          kMoveFromVol * *p.pre_vol_pct
                                    : pv.typical_move_pct;
            qp.bound_pct = size * confidence;
            qp.predicted_move_pct = *qp.bound_pct * clamp_unit(score / kFullConvictionScore);
        }
        out.append(qp);
    }
    std::reverse(out.begin(), out.end());   // oldest first, to match the charts
    return out;
}

std::optional<double> metric_value(const EarningsPoint& p, ReactionMetric m) {
    switch (m) {
        case ReactionMetric::Surprise: return p.surprise_pct;
        case ReactionMetric::QoQ:      return p.eps_qoq_pct;
        case ReactionMetric::YoY:      return p.eps_yoy_pct;
    }
    return std::nullopt;
}

// ── Competing predictors ─────────────────────────────────────────────────────
// All walk-forward: quarter i is answered using only the quarters before it.

namespace {

// Quarters needed before the run-up fit will speak. Below this the slope is
// noise wearing a regression's clothes.
constexpr int kMinFitQuarters = 4;
// Shrinkage denominator. With n prior quarters the fitted slope is kept at
// n/(n+kFitShrink) of itself, so a four-point fit asserts 40% of what it
// measured and a twelve-point fit 67%. Nothing here supports more confidence
// than that, and an unshrunk slope on this little data mostly predicts noise.
constexpr double kFitShrink = 6.0;
// Extrapolation cap, in multiples of the name's typical move. A run-up far
// outside the fitted range would otherwise produce a forecast no history
// justifies.
constexpr double kFitClampMultiple = 2.0;

// ── Adaptive model constants ─────────────────────────────────────────────────
// Half-life, in quarters, of the exponential weights. Four puts about 65% of
// the weight in the last year — long enough to have a sample, short enough
// that a business from three years ago isn't outvoting the current one.
constexpr double kEwHalfLifeQuarters = 4.0;
// Winsorising limit, in multiples of the expected magnitude. Beyond this a
// print is a event the fit should notice but not be steered by.
constexpr double kWinsorMultiple = 2.5;
// Floor on skill shrinkage. Even a model losing to the baseline keeps this
// much of its call — collapsing to exactly zero would make it indistinguishable
// from the NO MOVE line and hide whether it was recovering.
constexpr double kSkillFloor = 0.20;
// Quarters of self-replay needed before skill is measured at all. Below it the
// model is neither credited nor penalised.
constexpr int kMinSkillQuarters = 3;

struct PriorStats {
    int n = 0;
    double mean_reaction = 0;
    double typical_move = 0;
    // Least-squares of reaction on run-up, already shrunk.
    bool have_fit = false;
    double intercept = 0;
    double slope = 0;
};

/// Everything the predictors need about the quarters BEFORE index `i`.
PriorStats stats_before(const QVector<EarningsPoint>& history, int i) {
    PriorStats s;
    QVector<double> xs, ys;
    double sum = 0, abs_sum = 0;
    for (int j = i + 1; j < history.size(); ++j) {
        const auto& p = history[j];
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        ++s.n;
        sum += *p.reaction_pct;
        abs_sum += std::abs(*p.reaction_pct);
        if (p.runup_pct.has_value()) {
            xs.append(*p.runup_pct);
            ys.append(*p.reaction_pct);
        }
    }
    if (s.n == 0) return s;
    s.mean_reaction = sum / s.n;
    s.typical_move = abs_sum / s.n;

    if (xs.size() >= kMinFitQuarters) {
        double mx = 0, my = 0;
        for (int k = 0; k < xs.size(); ++k) { mx += xs[k]; my += ys[k]; }
        mx /= xs.size();
        my /= ys.size();
        double cov = 0, var = 0;
        for (int k = 0; k < xs.size(); ++k) {
            cov += (xs[k] - mx) * (ys[k] - my);
            var += (xs[k] - mx) * (xs[k] - mx);
        }
        if (var > 1e-9) {
            const double raw_slope = cov / var;
            const double shrink = xs.size() / (xs.size() + kFitShrink);
            s.slope = raw_slope * shrink;
            s.intercept = my - s.slope * mx;
            s.have_fit = true;
        }
    }
    return s;
}

std::optional<double> fit_predict(const PriorStats& s, const std::optional<double>& runup) {
    if (!s.have_fit || !runup.has_value()) return std::nullopt;
    const double raw = s.intercept + s.slope * *runup;
    const double cap = std::max(1.0, s.typical_move * kFitClampMultiple);
    return std::clamp(raw, -cap, cap);
}

} // namespace

// ── The Adaptive predictor ───────────────────────────────────────────────────

namespace {

struct AdaptiveOut {
    std::optional<double> value;
    std::optional<double> bound;   // the magnitude estimate that caps the call
};

/// Exponentially-weighted view of the quarters strictly older than `i`, and
/// the signed call that follows from them. `apply_skill` replays the model
/// over its own past to decide how much of that call to keep; the replay
/// itself runs with it off, which is what keeps the recursion one level deep.
AdaptiveOut adaptive_at(const QVector<EarningsPoint>& history, int i,
                        const std::optional<double>& runup, bool apply_skill);

AdaptiveOut adaptive_core(const QVector<EarningsPoint>& history, int i,
                          const std::optional<double>& runup) {
    AdaptiveOut out;
    double wsum = 0, r_sum = 0, abs_sum = 0, x_sum = 0, xw = 0;
    double flat_abs = 0;
    int flat_n = 0;
    QVector<double> xs, ys, ws;
    int age = 0;
    for (int j = i + 1; j < history.size(); ++j) {
        const auto& p = history[j];
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        const double w = std::pow(0.5, age / kEwHalfLifeQuarters);
        ++age;
        wsum += w;
        r_sum += w * *p.reaction_pct;
        abs_sum += w * std::abs(*p.reaction_pct);
        flat_abs += std::abs(*p.reaction_pct);
        ++flat_n;
        if (p.runup_pct.has_value()) {
            xs.append(*p.runup_pct);
            ys.append(*p.reaction_pct);
            ws.append(w);
            x_sum += w * *p.runup_pct;
            xw += w;
        }
    }
    if (wsum <= 0) return out;

    const double drift = r_sum / wsum;
    // Magnitude is the FLAT trailing mean, not the exponentially-weighted one,
    // because that was measured. Pooled across 69 names and 555 quarters,
    // out of sample, the plain mean of past |move| beat every half-life tried:
    //
    //     flat trailing mean   3.089 pp     EWMA 8q    3.121 pp
    //     EWMA 12q             3.108 pp     EWMA 4q    3.173 pp
    //     EWMA 3q              3.212 pp     EWMA 2q    3.296 pp
    //
    // i.e. recency weighting made the size forecast steadily WORSE the more
    // recent it leaned — a name's reaction size is a slow, stable property and
    // discounting three-year-old prints throws away sample for nothing. The
    // half-life stays on the regression below, where it is untested either way
    // and the recency argument still stands on its own merits.
    if (flat_n == 0) return out;
    const double magnitude = flat_abs / flat_n;
    if (magnitude <= 0) return out;
    out.bound = magnitude;

    // Property 3: winsorise against the expected magnitude before fitting, so
    // one violent quarter informs the slope without dictating it.
    const double cap = magnitude * kWinsorMultiple;
    double slope = 0;
    if (xs.size() >= kMinFitQuarters && xw > 0) {
        const double xbar = x_sum / xw;
        double ybar = 0, yw = 0;
        for (int k = 0; k < ys.size(); ++k) { ybar += ws[k] * std::clamp(ys[k], -cap, cap); yw += ws[k]; }
        ybar /= yw;
        double cov = 0, var = 0;
        for (int k = 0; k < xs.size(); ++k) {
            const double y = std::clamp(ys[k], -cap, cap);
            cov += ws[k] * (xs[k] - xbar) * (y - ybar);
            var += ws[k] * (xs[k] - xbar) * (xs[k] - xbar);
        }
        if (var > 1e-9)
            slope = (cov / var) * (xs.size() / (xs.size() + kFitShrink));
        if (runup.has_value()) {
            const double raw = drift + slope * (*runup - xbar);
            // Property 1 again: the signed call is capped by the size estimate.
            out.value = std::clamp(raw, -magnitude, magnitude);
            return out;
        }
    }
    out.value = std::clamp(drift, -magnitude, magnitude);
    return out;
}

AdaptiveOut adaptive_at(const QVector<EarningsPoint>& history, int i,
                        const std::optional<double>& runup, bool apply_skill) {
    AdaptiveOut out = adaptive_core(history, i, runup);
    if (!apply_skill || !out.value)
        return out;

    // Skill shrinkage: replay the model over the quarters before this one and
    // compare its error against doing nothing. Beating the baseline buys
    // conviction; losing to it takes conviction away.
    double model_err = 0, zero_err = 0;
    int n = 0;
    for (int j = i + 1; j < history.size(); ++j) {
        const auto& p = history[j];
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        const auto inner = adaptive_core(history, j, p.runup_pct);
        if (!inner.value) continue;
        ++n;
        model_err += std::abs(*inner.value - *p.reaction_pct);
        zero_err += std::abs(*p.reaction_pct);
    }
    if (n >= kMinSkillQuarters && zero_err > 1e-9) {
        const double skill = std::clamp(1.0 - model_err / zero_err, 0.0, 1.0);
        out.value = *out.value * (kSkillFloor + (1.0 - kSkillFloor) * skill);
    } else {
        // Too little history to have demonstrated anything. Unproven is not
        // the same as proven good, and leaving conviction at full here made the
        // model loudest on the oldest quarters — precisely where it knew least.
        // It gets the same floor as a model that has been losing.
        out.value = *out.value * kSkillFloor;
    }
    return out;
}

} // namespace

// ── The EPS model ────────────────────────────────────────────────────────────

namespace {

/// Weighted least squares through the origin-shifted means, shrunk for sample
/// size. Returns {intercept, slope}; slope is 0 when there is nothing to fit.
struct Fit { double a = 0; double b = 0; bool ok = false; };

Fit weighted_fit(const QVector<double>& xs, const QVector<double>& ys,
                 const QVector<double>& ws, int min_n) {
    Fit f;
    if (xs.size() < min_n) return f;
    double wsum = 0, xbar = 0, ybar = 0;
    for (int k = 0; k < xs.size(); ++k) { wsum += ws[k]; xbar += ws[k] * xs[k]; ybar += ws[k] * ys[k]; }
    if (wsum <= 0) return f;
    xbar /= wsum;
    ybar /= wsum;
    double cov = 0, var = 0;
    for (int k = 0; k < xs.size(); ++k) {
        cov += ws[k] * (xs[k] - xbar) * (ys[k] - ybar);
        var += ws[k] * (xs[k] - xbar) * (xs[k] - xbar);
    }
    f.a = ybar;
    if (var > 1e-9)
        f.b = (cov / var) * (xs.size() / (xs.size() + kFitShrink));
    f.a = ybar - f.b * xbar;
    f.ok = true;
    return f;
}

/// The growth analysts are asking for at quarter `i`: the consensus standing
/// into that print against what the company last delivered. Knowable before
/// the print, which is the whole reason it is usable.
std::optional<double> asked_growth(const QVector<EarningsPoint>& history, int i) {
    if (i < 0 || i >= history.size()) return std::nullopt;
    const auto& p = history[i];
    if (!p.eps_estimate.has_value()) return std::nullopt;
    for (int j = i + 1; j < history.size(); ++j) {
        if (history[j].is_estimate || !history[j].eps_actual.has_value()) continue;
        const double prev = *history[j].eps_actual;
        if (std::abs(prev) < 0.005) return std::nullopt;
        return (*p.eps_estimate - prev) / std::abs(prev) * 100.0;
    }
    return std::nullopt;
}

AdaptiveOut eps_core(const QVector<EarningsPoint>& history, int i,
                     const std::optional<double>& runup) {
    AdaptiveOut out;

    // Prior quarters, exponentially weighted, newest of them first.
    QVector<double> w, surprise, ask, reaction, runups;
    double wsum = 0, abs_sum = 0;
    int age = 0;
    for (int j = i + 1; j < history.size(); ++j) {
        const auto& p = history[j];
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        const double wt = std::pow(0.5, age / kEwHalfLifeQuarters);
        ++age;
        wsum += wt;
        abs_sum += wt * std::abs(*p.reaction_pct);
        if (!p.surprise_pct.has_value()) continue;
        const auto g = asked_growth(history, j);
        if (!g.has_value()) continue;
        w.append(wt);
        surprise.append(*p.surprise_pct);
        ask.append(*g);
        reaction.append(*p.reaction_pct);
        runups.append(p.runup_pct.value_or(0.0));
    }
    if (wsum <= 0) return out;
    const double magnitude = abs_sum / wsum;
    if (magnitude <= 0) return out;
    out.bound = magnitude;

    const auto ask_now = asked_growth(history, i);
    if (!ask_now.has_value() || w.size() < kMinFitQuarters) {
        // Not enough paired history to fit either link. Fall back to the beat
        // bias alone rather than inventing a relationship.
        if (surprise.isEmpty()) return out;
        double sw = 0, ss = 0;
        for (int k = 0; k < surprise.size(); ++k) { sw += w[k]; ss += w[k] * surprise[k]; }
        out.value = std::clamp(sw > 0 ? ss / sw * 0.1 : 0.0, -magnitude, magnitude);
        return out;
    }

    // Stage A — expected surprise, from the beat bias adjusted for the bar.
    const Fit stage_a = weighted_fit(ask, surprise, w, kMinFitQuarters);
    const double expected_surprise = stage_a.ok ? stage_a.a + stage_a.b * *ask_now : 0.0;

    // Stage B — what a surprise is worth in price, for this name.
    const Fit stage_b = weighted_fit(surprise, reaction, w, kMinFitQuarters);
    if (!stage_b.ok) return out;
    double predicted = stage_b.a + stage_b.b * expected_surprise;

    // Stage C — take out what the run-up already paid for.
    if (runup.has_value()) {
        QVector<double> resid;
        resid.reserve(reaction.size());
        for (int k = 0; k < reaction.size(); ++k)
            resid.append(reaction[k] - (stage_b.a + stage_b.b * surprise[k]));
        const Fit stage_c = weighted_fit(runups, resid, w, kMinFitQuarters);
        if (stage_c.ok)
            predicted += stage_c.a + stage_c.b * *runup;
    }

    out.value = std::clamp(predicted, -magnitude, magnitude);
    return out;
}

/// Same skill discipline as Adaptive: replay over its own past, keep
/// conviction in proportion to how it did against doing nothing, and treat
/// "not enough history to tell" as unproven rather than as proven good.
AdaptiveOut eps_at(const QVector<EarningsPoint>& history, int i,
                   const std::optional<double>& runup) {
    AdaptiveOut out = eps_core(history, i, runup);
    if (!out.value) return out;
    double model_err = 0, zero_err = 0;
    int n = 0;
    for (int j = i + 1; j < history.size(); ++j) {
        const auto& p = history[j];
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        const auto inner = eps_core(history, j, p.runup_pct);
        if (!inner.value) continue;
        ++n;
        model_err += std::abs(*inner.value - *p.reaction_pct);
        zero_err += std::abs(*p.reaction_pct);
    }
    if (n >= kMinSkillQuarters && zero_err > 1e-9) {
        const double skill = std::clamp(1.0 - model_err / zero_err, 0.0, 1.0);
        out.value = *out.value * (kSkillFloor + (1.0 - kSkillFloor) * skill);
    } else {
        out.value = *out.value * kSkillFloor;
    }
    return out;
}

} // namespace

QVector<PredictorRun> compare_predictors(const EarningsAnalysis& a, const EarningsVerdict& live) {
    QVector<PredictorRun> runs;

    PredictorRun scorecard;
    scorecard.predictor = MovePredictor::Scorecard;
    scorecard.label = QStringLiteral("SCORECARD");
    scorecard.explanation = QStringLiteral(
        "The rules-based signal. Past quarters are rebuilt from the backward legs only, so it "
        "is answering with about a third of the model it will have live.");
    scorecard.points = reconstruct_predictions(a);
    scorecard.next_move_pct = live.predicted_move_pct;
    if (live.typical_move_pct > 0)
        scorecard.next_bound_pct = live.typical_move_pct * live.confidence;
    runs.append(scorecard);

    PredictorRun eps;
    eps.predictor = MovePredictor::EpsModel;
    eps.label = QStringLiteral("EPS MODEL");
    eps.explanation = QStringLiteral(
        "Built from what the history actually holds: every past quarter carries the consensus "
        "that stood going into the print, the figure that landed, and the move that followed, "
        "so each link is fitted on real pairs. Stage A expects a surprise — the company's "
        "persistent beat bias, adjusted for how much growth analysts are asking for this time "
        "against what it usually delivers. Stage B converts that into price using this name's "
        "own measured sensitivity: how far it moves per point of beat, which some names pay for "
        "and some do not. Stage C subtracts what the run-up already paid for. The revision "
        "trajectory is deliberately excluded — it exists for the coming quarter only, so there "
        "are no past pairs to fit it against.");

    PredictorRun adaptive;
    adaptive.predictor = MovePredictor::Adaptive;
    adaptive.label = QStringLiteral("ADAPTIVE");
    adaptive.explanation = QStringLiteral(
        "Built for this problem rather than borrowed. Estimates how far the name usually "
        "travels on a print first, and lets that CAP the signed call — size is persistent, "
        "direction is dominated by the surprise nobody has beforehand. Statistics are "
        "exponentially weighted with a four-quarter half-life so a business from three years "
        "ago doesn't outvote the current one, and targets are winsorised so a single violent "
        "print informs the slope without dictating it. Then it replays itself over its own "
        "past and shrinks its output toward zero in proportion to how badly it has been losing "
        "to the do-nothing baseline — a model that can't beat NO MOVE stops making claims.");

    PredictorRun fit;
    fit.predictor = MovePredictor::RunupFit;
    fit.label = QStringLiteral("RUN-UP FIT");
    fit.explanation = QStringLiteral(
        "Least squares of the realised move on the 5-session run-up into each print, refitted "
        "on the quarters before every prediction and shrunk toward zero for the small sample. "
        "The run-up is the one useful thing that is genuinely known beforehand — surprise, QoQ "
        "and YoY only exist after the company reports, which is what makes the correlations "
        "beside this chart descriptive rather than predictive.");

    PredictorRun mean_run;
    mean_run.predictor = MovePredictor::MeanMove;
    mean_run.label = QStringLiteral("MEAN");
    mean_run.explanation = QStringLiteral(
        "Baseline: this name's average past reaction, ignoring everything about the setup. "
        "A predictor that cannot beat this is not reading the setup, it is reading the drift.");

    PredictorRun zero_run;
    zero_run.predictor = MovePredictor::NoMove;
    zero_run.label = QStringLiteral("NO MOVE");
    zero_run.explanation = QStringLiteral(
        "Baseline: zero, every quarter. It is never right and it is hard to beat on average "
        "error, because guessing a direction and missing costs more than declining to guess.");

    for (int i = 0; i < a.history.size(); ++i) {
        const auto& p = a.history[i];
        if (p.is_estimate || !p.reaction_pct.has_value()) continue;
        const auto s = stats_before(a.history, i);
        if (s.n == 0) continue;

        QuarterPrediction f;
        f.timestamp = p.timestamp;
        f.actual_move_pct = p.reaction_pct;
        f.predicted_move_pct = fit_predict(s, p.runup_pct);
        if (f.predicted_move_pct)
            f.bound_pct = std::max(1.0, s.typical_move * kFitClampMultiple);
        fit.points.append(f);

        QuarterPrediction ep;
        ep.timestamp = p.timestamp;
        ep.actual_move_pct = p.reaction_pct;
        const auto eo = eps_at(a.history, i, p.runup_pct);
        ep.predicted_move_pct = eo.value;
        ep.bound_pct = eo.bound;
        eps.points.append(ep);

        QuarterPrediction ad;
        ad.timestamp = p.timestamp;
        ad.actual_move_pct = p.reaction_pct;
        const auto ao = adaptive_at(a.history, i, p.runup_pct, /*apply_skill=*/true);
        ad.predicted_move_pct = ao.value;
        ad.bound_pct = ao.bound;
        adaptive.points.append(ad);

        QuarterPrediction m = f;
        m.predicted_move_pct = s.mean_reaction;
        m.bound_pct = s.typical_move;
        mean_run.points.append(m);

        QuarterPrediction z = f;
        z.predicted_move_pct = 0.0;
        z.bound_pct = std::nullopt;
        zero_run.points.append(z);
    }
    std::reverse(eps.points.begin(), eps.points.end());
    std::reverse(adaptive.points.begin(), adaptive.points.end());
    std::reverse(fit.points.begin(), fit.points.end());
    std::reverse(mean_run.points.begin(), mean_run.points.end());
    std::reverse(zero_run.points.begin(), zero_run.points.end());

    // The upcoming print, from the same rules each predictor used above.
    const auto s_now = stats_before(a.history, -1);
    if (s_now.n > 0) {
        const auto eo_now = eps_at(a.history, -1, a.runup_5d_pct);
        eps.next_move_pct = eo_now.value;
        eps.next_bound_pct = eo_now.bound;
        const auto ao_now = adaptive_at(a.history, -1, a.runup_5d_pct, /*apply_skill=*/true);
        adaptive.next_move_pct = ao_now.value;
        adaptive.next_bound_pct = ao_now.bound;
        fit.next_move_pct = fit_predict(s_now, a.runup_5d_pct);
        if (fit.next_move_pct)
            fit.next_bound_pct = std::max(1.0, s_now.typical_move * kFitClampMultiple);
        mean_run.next_move_pct = s_now.mean_reaction;
        mean_run.next_bound_pct = s_now.typical_move;
        zero_run.next_move_pct = 0.0;
    }

    runs.append(eps);
    runs.append(adaptive);
    runs.append(fit);
    runs.append(mean_run);
    runs.append(zero_run);

    for (auto& r : runs) {
        double err = 0;
        for (const auto& q : r.points) {
            if (!q.predicted_move_pct || !q.actual_move_pct) continue;
            ++r.graded;
            err += std::abs(*q.predicted_move_pct - *q.actual_move_pct);
            // A near-zero estimate is not a directional call. Grading it would
            // hand the NO MOVE baseline a hit rate it never claimed.
            if (std::abs(*q.predicted_move_pct) < 0.25) continue;
            ++r.direction_calls;
            if ((*q.predicted_move_pct > 0) == (*q.actual_move_pct > 0))
                ++r.direction_hits;
        }
        if (r.graded > 0)
            r.mean_abs_error = err / r.graded;
    }
    return runs;
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
    v.components.append(score_expectations(a));
    v.components.append(score_crowding(a));
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

    // The same weighted mean, taken over each axis on its own. Not a second
    // model — a decomposition, so "good company, high bar" doesn't reach the
    // reader as a bare number halfway between the two.
    for (const auto axis : {SignalAxis::Setup, SignalAxis::Bar}) {
        double sum = 0, w = 0;
        for (const auto& c : v.components) {
            if (!c.available || c.axis != axis) continue;
            sum += c.score * c.weight;
            w   += c.weight;
        }
        if (w <= 0) continue;
        (axis == SignalAxis::Setup ? v.setup_score : v.bar_score) = (sum / w) * 100.0;
    }

    // Size forecast for the coming print. `typical_move_pct` stays purely
    // descriptive — it is what the history summary and the caveats quote — and
    // this is the forecast built on top of it.
    v.expected_move_pct = v.typical_move_pct;
    if (a.pre_vol_pct.has_value() && *a.pre_vol_pct > 0) {
        v.expected_move_pct =
            kMoveFromHistory * v.typical_move_pct + kMoveFromVol * *a.pre_vol_pct;
    }

    // ── Point estimate for the move ──────────────────────────────────────────
    // Three factors, each doing one job: which way the composite leans, how
    // far this name actually travels on a print, and how much of the picture
    // is real data rather than absent legs. Multiplying by confidence is what
    // keeps a thin file from producing a bold number — a half-empty scorecard
    // leaning hard predicts half as much as a full one leaning equally hard.
    if (v.expected_move_pct > 0 && v.confidence >= kMinConfidence) {
        v.predicted_move_pct =
            v.expected_move_pct * clamp_unit(v.score / kFullConvictionScore) * v.confidence;
    }

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

        // When the two axes disagree sharply, that tension IS the story and it
        // must not be left for the reader to reconstruct from the breakdown.
        // A composite near zero can mean "nothing much either way" or "a very
        // good company at a very high price"; only one of those is worth
        // knowing about the night before a print.
        if (v.setup_score && v.bar_score &&
            std::abs(*v.setup_score - *v.bar_score) >= kAxisTensionPts) {
            v.headline += *v.setup_score > *v.bar_score
                              ? QStringLiteral(" The business reads better than the price does — "
                                               "most of the good news is already paid for.")
                              : QStringLiteral(" The price reads better than the business does — "
                                               "expectations have already come down to meet it.");
        }
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
