// tests/services/test_earnings_signal.cpp
//
// The ER Earnings tab hands the user a BUY / HOLD / SELL read before a print,
// so the scorer behind it has to be boring and predictable. The cases below
// pin the properties that would be embarrassing to get wrong:
//
//   - a security with no earnings (ETF, fund) must produce HOLD at zero
//     confidence, never a confident-looking neutral;
//   - legs with no data must drop out of the average instead of scoring 0,
//     which would drag every verdict toward HOLD;
//   - a real 0.0 (a perfectly in-line quarter, a flat reaction) is a value,
//     not a missing field — the whole reason the models use std::optional;
//   - the verdict must not flip sides on a thin dataset.

#include "services/equity/EarningsSignal.h"

#include <QtTest/QtTest>

using namespace fincept::services::equity;

namespace {

/// A quarter with a surprise and a price reaction.
EarningsPoint quarter(qint64 ts, double est, double actual, double surprise, double reaction,
                      double runup = 0.0) {
    EarningsPoint p;
    p.timestamp = ts;
    p.eps_estimate = est;
    p.eps_actual = actual;
    p.surprise_pct = surprise;
    p.reaction_pct = reaction;
    p.runup_pct = runup;
    return p;
}

/// Eight quarters of a consistent beater whose stock rises on the print.
QVector<EarningsPoint> strong_history() {
    QVector<EarningsPoint> h;
    for (int i = 0; i < 8; ++i)
        h.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.08, 8.0, 4.0));
    return h;
}

/// Eight quarters of misses that the market punished.
QVector<EarningsPoint> weak_history() {
    QVector<EarningsPoint> h;
    for (int i = 0; i < 8; ++i)
        h.append(quarter(1700000000LL - i * 7776000LL, 1.0, 0.92, -8.0, -4.0));
    return h;
}

EarningsTrendRow trend(const QString& period, double current, double d30, double d90) {
    EarningsTrendRow t;
    t.period = period;
    t.label = period;
    t.current = current;
    t.d30 = d30;
    t.d90 = d90;
    return t;
}

const SignalComponent* leg(const EarningsVerdict& v, const QString& name) {
    for (const auto& c : v.components)
        if (c.name == name) return &c;
    return nullptr;
}

double weight_of(const EarningsVerdict& v, const QString& name) {
    const auto* c = leg(v, name);
    return c ? c->weight : 0.0;
}

EarningsRevisionRow revision(const QString& period, double up30, double down30) {
    EarningsRevisionRow r;
    r.period = period;
    r.label = period;
    r.up_30d = up30;
    r.down_30d = down30;
    return r;
}

EarningsAnalysis bullish_fixture() {
    EarningsAnalysis a;
    a.valid = true;
    a.symbol = "TEST";
    a.history = strong_history();
    a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 30 * 86400;
    a.next.eps_avg = 2.0;
    a.next.eps_growth = 0.30;   // +30% YoY
    a.next.rev_growth = 0.20;
    a.trend.append(trend("0q", 2.10, 2.00, 1.90));       // estimates being raised
    a.trend.append(trend("+1q", 2.30, 2.20, 2.05));      // …and so is next quarter's
    a.trend.append(trend("+1y", 9.20, 9.00, 8.60));
    a.revisions.append(revision("0q", 12, 0));
    a.revisions.append(revision("+1q", 10, 1));
    EarningsGrowthRow g;
    g.period = "0q";
    g.stock = 0.30;
    g.index = 0.10;
    a.growth.append(g);
    a.valuation.price = 100.0;
    a.valuation.target_mean = 130.0;
    a.valuation.trailing_pe = 30.0;
    a.valuation.forward_pe = 22.0;
    a.valuation.recommendation_mean = 1.6;
    a.valuation.analyst_count = 30;
    return a;
}

EarningsAnalysis bearish_fixture() {
    EarningsAnalysis a = bullish_fixture();
    a.history = weak_history();
    a.next.eps_growth = -0.30;
    a.next.rev_growth = -0.15;
    a.trend.clear();
    a.trend.append(trend("0q", 1.80, 2.00, 2.20));       // estimates being cut
    a.trend.append(trend("+1q", 1.95, 2.15, 2.40));      // …next quarter harder still
    a.trend.append(trend("+1y", 7.80, 8.40, 9.10));
    a.revisions.clear();
    a.revisions.append(revision("0q", 0, 14));
    a.revisions.append(revision("+1q", 0, 15));
    a.growth.clear();
    EarningsGrowthRow g;
    g.period = "0q";
    g.stock = -0.20;
    g.index = 0.10;
    a.growth.append(g);
    a.valuation.target_mean = 80.0;
    a.valuation.trailing_pe = 22.0;
    a.valuation.forward_pe = 30.0;
    a.valuation.recommendation_mean = 4.2;
    return a;
}

} // namespace

class TestEarningsSignal : public QObject {
    Q_OBJECT

  private slots:
    // An ETF has no earnings at all — the verdict must say so instead of
    // presenting a neutral score as if it had been computed.
    void empty_analysis_is_no_signal() {
        EarningsAnalysis a;
        a.valid = true;   // the daemon answered; the security simply has nothing
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.direction, SignalDirection::Neutral);
        QCOMPARE(v.label, QStringLiteral("HOLD"));
        QCOMPARE(v.confidence, 0.0);
        QCOMPARE(v.score, 0.0);
        QVERIFY(v.headline.contains("No earnings data"));
    }

    void bullish_setup_scores_buy() {
        const auto v = evaluate_earnings(bullish_fixture());
        QCOMPARE(v.label, QStringLiteral("BUY"));
        QCOMPARE(v.direction, SignalDirection::Bullish);
        QVERIFY(v.score > 20.0);
        QCOMPARE(v.confidence, 1.0);       // every leg has data in this fixture
        QCOMPARE(v.scored_quarters, 8);
        QCOMPARE(v.beat_rate, 1.0);
        for (const auto& c : v.components)
            QVERIFY2(c.available, qPrintable("leg missing data: " + c.name));
    }

    void bearish_setup_scores_sell() {
        const auto v = evaluate_earnings(bearish_fixture());
        QCOMPARE(v.label, QStringLiteral("SELL"));
        QCOMPARE(v.direction, SignalDirection::Bearish);
        QVERIFY(v.score < -20.0);
        QCOMPARE(v.beat_rate, 0.0);
    }

    // Missing legs must lower confidence rather than scoring zero and
    // dragging the composite toward HOLD.
    void missing_legs_lower_confidence_not_score() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();      // only the two backward-looking legs
        const auto v = evaluate_earnings(a);
        QVERIFY(v.confidence > 0.0);
        QVERIFY(v.confidence < 1.0);
        // Both available legs are strongly positive, so the composite should
        // be too — it must not be diluted by the four legs that have no data.
        QVERIFY2(v.score > 50.0, qPrintable(QString("score was %1").arg(v.score)));
        int available = 0;
        for (const auto& c : v.components)
            if (c.available) ++available;
        QCOMPARE(available, 2);
    }

    // Below the confidence floor the engine must refuse to take a side even
    // when the one leg it has is emphatic.
    void thin_data_is_forced_to_hold() {
        EarningsAnalysis a;
        a.valid = true;
        // A single quarter drives only the track-record leg (weight 0.16),
        // which is under the 0.35 confidence floor.
        a.history.append(quarter(1700000000LL, 1.0, 2.0, 100.0, 20.0));
        const auto v = evaluate_earnings(a);
        QVERIFY(v.confidence < 0.35);
        QCOMPARE(v.label, QStringLiteral("HOLD"));
        QVERIFY(v.headline.contains("Too little"));
    }

    // A 0% surprise is an in-line quarter, not a beat and not missing data.
    void zero_surprise_counts_as_a_miss_not_missing() {
        EarningsAnalysis a;
        a.valid = true;
        for (int i = 0; i < 4; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.0, 0.0, 0.0));
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.scored_quarters, 4);        // counted, not skipped
        QCOMPARE(v.beat_rate, 0.0);            // in-line is not a beat
        QCOMPARE(v.avg_surprise_pct, 0.0);
        QCOMPARE(v.typical_move_pct, 0.0);
    }

    // A quarter Yahoo hasn't filled in yet must not be scored as a miss.
    void pending_quarter_is_not_scored() {
        EarningsAnalysis a;
        a.valid = true;
        EarningsPoint pending;
        pending.timestamp = 1700000000LL;
        pending.eps_estimate = 1.0;            // no actual, no surprise
        a.history.append(pending);
        a.history.append(quarter(1690000000LL, 1.0, 1.1, 10.0, 3.0));
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.scored_quarters, 1);
        QCOMPARE(v.reaction_quarters, 1);
    }

    // Consensus swinging through zero makes the percentage change meaningless;
    // the leg must drop out rather than report a divide-by-tiny number.
    void near_zero_reference_estimate_drops_the_revision_leg() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();
        a.trend.append(trend("0q", 0.50, 0.0, 0.0));
        const auto v = evaluate_earnings(a);
        for (const auto& c : v.components) {
            if (c.name == QStringLiteral("REVISION MOMENTUM"))
                QVERIFY2(!c.available, "revision leg scored off a zero reference");
        }
    }

    void typical_move_is_the_mean_absolute_reaction() {
        EarningsAnalysis a;
        a.valid = true;
        a.history.append(quarter(1700000000LL, 1.0, 1.1, 10.0, 6.0));
        a.history.append(quarter(1690000000LL, 1.0, 1.1, 10.0, -4.0));
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.typical_move_pct, 5.0);     // mean(|+6|, |−4|)
        QCOMPARE(v.avg_reaction_pct, 1.0);     // mean(+6, −4)
        QCOMPARE(v.up_reaction_rate, 0.5);
    }

    // The QoQ-vs-reaction correlation is shown to the user as a number they
    // may act on, so its edge cases matter more than its happy path.
    void correlation_matches_hand_computed_values() {
        EarningsAnalysis a;
        a.valid = true;
        // Reaction exactly tracks surprise, and exactly opposes QoQ.
        const double surprise[] = {2.0, 4.0, 6.0, 8.0};
        const double reaction[] = {1.0, 2.0, 3.0, 4.0};
        for (int i = 0; i < 4; ++i) {
            EarningsPoint p;
            p.timestamp = 1700000000LL - i * 7776000LL;
            p.eps_actual = 1.0;
            p.surprise_pct = surprise[i];
            p.eps_qoq_pct = -surprise[i];
            p.reaction_pct = reaction[i];
            a.history.append(p);
        }
        const auto cs = correlate_reactions(a);
        QCOMPARE(cs.size(), 3);
        for (const auto& c : cs) {
            if (c.metric == ReactionMetric::Surprise) {
                QCOMPARE(c.n, 4);
                QVERIFY(c.r.has_value());
                QVERIFY(std::abs(*c.r - 1.0) < 1e-9);
            } else if (c.metric == ReactionMetric::QoQ) {
                QVERIFY(c.r.has_value());
                QVERIFY(std::abs(*c.r + 1.0) < 1e-9);
            } else {
                QCOMPARE(c.n, 0);          // no YoY values in this fixture
                QVERIFY(!c.r.has_value());
            }
        }
    }

    // A metric that never varies has no correlation to report — dividing by
    // its zero spread would surface a NaN in the UI as "rnan".
    void constant_metric_yields_no_correlation() {
        EarningsAnalysis a;
        a.valid = true;
        for (int i = 0; i < 5; ++i) {
            EarningsPoint p;
            p.timestamp = 1700000000LL - i * 7776000LL;
            p.surprise_pct = 3.0;          // identical every quarter
            p.reaction_pct = i * 1.5;
            a.history.append(p);
        }
        for (const auto& c : correlate_reactions(a)) {
            if (c.metric == ReactionMetric::Surprise) {
                QCOMPARE(c.n, 5);
                QVERIFY2(!c.r.has_value(), "correlated against a zero-variance series");
            }
        }
    }

    // Quarters missing either side of the pair must not inflate n.
    void quarters_without_a_reaction_are_excluded() {
        EarningsAnalysis a;
        a.valid = true;
        EarningsPoint pending;             // reported today, no next session yet
        pending.timestamp = 1700000000LL;
        pending.surprise_pct = 5.0;
        a.history.append(pending);
        for (int i = 1; i < 4; ++i) {
            EarningsPoint p;
            p.timestamp = 1700000000LL - i * 7776000LL;
            p.surprise_pct = i * 2.0;
            p.reaction_pct = i * 1.0;
            a.history.append(p);
        }
        for (const auto& c : correlate_reactions(a)) {
            if (c.metric == ReactionMetric::Surprise)
                QCOMPARE(c.n, 3);          // the pending quarter is not a pair
        }
    }

    void two_quarters_is_too_few_to_correlate() {
        EarningsAnalysis a;
        a.valid = true;
        for (int i = 0; i < 2; ++i) {
            EarningsPoint p;
            p.timestamp = 1700000000LL - i * 7776000LL;
            p.surprise_pct = i * 3.0;
            p.reaction_pct = i * 2.0;
            a.history.append(p);
        }
        for (const auto& c : correlate_reactions(a))
            QVERIFY(!c.r.has_value());
    }

    void metric_value_reads_the_right_field() {
        EarningsPoint p;
        p.surprise_pct = 1.0;
        p.eps_qoq_pct = 2.0;
        p.eps_yoy_pct = 3.0;
        QCOMPARE(*metric_value(p, ReactionMetric::Surprise), 1.0);
        QCOMPARE(*metric_value(p, ReactionMetric::QoQ), 2.0);
        QCOMPARE(*metric_value(p, ReactionMetric::YoY), 3.0);
        QVERIFY(!metric_value(EarningsPoint{}, ReactionMetric::QoQ).has_value());
    }

    // The trailing row carries a forecast and a still-moving price. Letting
    // either into the scorer would report a prediction as a result.
    void projected_quarter_is_never_scored() {
        EarningsAnalysis a;
        a.valid = true;
        EarningsPoint proj;
        proj.timestamp = QDateTime::currentSecsSinceEpoch() + 86400;
        proj.is_estimate = true;
        proj.has_forward_estimate = true;
        proj.eps_estimate = 9.0;
        proj.eps_qoq_pct = 50.0;
        proj.eps_yoy_pct = 40.0;
        // Deliberately hostile: values that WOULD be counted if the guard
        // were missing, including a surprise and a reaction on a quarter
        // that has not happened.
        proj.surprise_pct = -90.0;
        proj.reaction_pct = -25.0;
        proj.move_since_last_pct = -4.0;
        a.history.append(proj);
        for (const auto& q : strong_history()) a.history.append(q);

        const auto v = evaluate_earnings(a);
        QCOMPARE(v.scored_quarters, 8);        // the 8 real ones, not 9
        QCOMPARE(v.beat_rate, 1.0);            // the -90% "surprise" didn't land
        QCOMPARE(v.reaction_quarters, 8);
        QVERIFY(v.avg_reaction_pct > 0);       // the -25% "reaction" didn't land

        for (const auto& c : correlate_reactions(a))
            QVERIFY2(c.n <= 8, "a projected quarter entered the correlation");
    }

    void days_to_next_earnings_handles_missing_date() {
        EarningsAnalysis a;
        QCOMPARE(days_to_next_earnings(a), -1);
        a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 5 * 86400 + 3600;
        // Somewhere between 5 and 6 sleeps depending on the hour of day this
        // runs — the point of the fixed-clock cases below is that the boundary
        // is decided in market time rather than by 24-hour arithmetic.
        const int d = days_to_next_earnings(a);
        QVERIFY2(d == 5 || d == 6, qPrintable(QString("days was %1").arg(d)));
    }

    // A print tomorrow afternoon in New York must not read as "today" just
    // because it is less than 24 hours away — that is the countdown the user
    // reads to decide whether there is still time to act.
    void days_to_next_earnings_counts_market_calendar_days() {
        const QTimeZone et("America/New_York");
        EarningsAnalysis a;
        // 21:00 ET on 30 Jul; the report lands 16:30 ET on 31 Jul — 19.5 hours
        // later, but one sleep away.
        const QDateTime now(QDate(2026, 7, 30), QTime(21, 0), et);
        a.next.timestamp = QDateTime(QDate(2026, 7, 31), QTime(16, 30), et).toSecsSinceEpoch();
        QCOMPARE(days_to_next_earnings(a, now), 1);

        // Same instant read from Tokyo: the answer is a fact about the
        // exchange session, so it must not change with the viewer.
        const QDateTime now_jst = now.toTimeZone(QTimeZone("Asia/Tokyo"));
        QCOMPARE(days_to_next_earnings(a, now_jst), 1);

        // A print later the same session is today, not tomorrow.
        a.next.timestamp = QDateTime(QDate(2026, 7, 30), QTime(23, 0), et).toSecsSinceEpoch();
        QCOMPARE(days_to_next_earnings(a, now), 0);

        // A date Yahoo never updated after the print goes negative, which the
        // engine reads as "imminent", not "unknown".
        a.next.timestamp = QDateTime(QDate(2026, 7, 28), QTime(16, 30), et).toSecsSinceEpoch();
        QCOMPARE(days_to_next_earnings(a, now), -2);
    }

    // ── Guidance ─────────────────────────────────────────────────────────────
    // The whole point of the leg: a company can be sailing into the current
    // quarter while the street quietly takes the next one down. Netting the
    // two horizons together — which the breadth leg used to do — hides exactly
    // this, and it is the setup that produced Meta's post-print drop.
    void next_quarter_cuts_show_up_even_when_this_quarter_is_fine() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();
        a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 10 * 86400;
        a.trend.append(trend("0q", 2.05, 2.00, 1.98));    // current quarter: nudged up
        a.trend.append(trend("+1q", 1.85, 2.10, 2.20));   // next quarter: cut hard
        a.revisions.append(revision("0q", 8, 1));         // …by the same analysts
        a.revisions.append(revision("+1q", 1, 9));

        const auto v = evaluate_earnings(a);
        const auto* guidance = leg(v, QStringLiteral("GUIDANCE EXPECTATIONS"));
        const auto* revisions = leg(v, QStringLiteral("REVISION MOMENTUM"));
        const auto* breadth = leg(v, QStringLiteral("ANALYST BREADTH"));
        QVERIFY(guidance && guidance->available);
        QVERIFY2(guidance->score < -0.4, qPrintable(QString("guidance %1").arg(guidance->score)));
        // The near-term legs stay positive — they are describing a different
        // quarter, and flattening the disagreement is the failure mode.
        QVERIFY(revisions && revisions->score > 0);
        QVERIFY(breadth && breadth->score > 0);
    }

    // Breadth counts votes on the COMING quarter only. Pooling +1q back in
    // would let 9 cuts on the next quarter cancel 8 raises on this one.
    void breadth_ignores_the_next_quarter() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();   // revisions alone don't count as content
        a.revisions.append(revision("0q", 8, 0));
        a.revisions.append(revision("+1q", 0, 20));
        const auto* breadth = leg(evaluate_earnings(a), QStringLiteral("ANALYST BREADTH"));
        QVERIFY(breadth && breadth->available);
        QCOMPARE(breadth->score, 1.0);
    }

    // The asymmetry is relative, so on its own it has no direction. A company
    // being cut everywhere — this quarter hard, next quarter merely less hard
    // — has a POSITIVE asymmetry, and scoring that alone would read a broad
    // decline as maximally good guidance.
    void asymmetry_alone_cannot_carry_the_guidance_leg() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();
        a.trend.append(trend("0q", 1.60, 2.00, 2.30));   // current quarter only
        a.revisions.append(revision("0q", 0, 10));        // everyone cutting this quarter
        a.revisions.append(revision("+1q", 5, 5));        // …split on the next one
        const auto v = evaluate_earnings(a);
        const auto* guidance = leg(v, QStringLiteral("GUIDANCE EXPECTATIONS"));
        QVERIFY(guidance);
        QVERIFY2(!guidance->available,
                 qPrintable(QString("scored %1 off a relative measure").arg(guidance->score)));
        QVERIFY(guidance->detail.contains("counts alone"));
    }

    // One beat with one reaction is a single draw, not evidence. It must not
    // carry 40% of the track-record leg.
    void a_single_rewarded_beat_does_not_score() {
        EarningsAnalysis a;
        a.valid = true;
        a.history.append(quarter(1700000000LL, 1.0, 1.08, 8.0, 20.0));           // lone beat, huge pop
        a.history.append(quarter(1700000000LL - 7776000LL, 1.0, 0.95, -5.0, -2.0));
        a.history.append(quarter(1700000000LL - 15552000LL, 1.0, 0.96, -4.0, -1.0));
        const auto v = evaluate_earnings(a);
        QVERIFY2(!v.beat_reaction_pct.has_value(), "a one-quarter mean was reported as a stat");
        const auto* record = leg(v, QStringLiteral("SURPRISE TRACK RECORD"));
        QVERIFY(record && record->available);
        // Falls back to the surprise alone, which is negative here — the +20%
        // session must not drag the leg positive.
        QVERIFY2(record->score < 0, qPrintable(QString("score %1").arg(record->score)));
        QVERIFY(!record->detail.contains("beats paid"));
    }

    void guidance_leg_is_unavailable_without_next_quarter_data() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();
        a.trend.append(trend("0q", 2.10, 2.00, 1.90));    // current quarter only
        const auto* guidance = leg(evaluate_earnings(a), QStringLiteral("GUIDANCE EXPECTATIONS"));
        QVERIFY(guidance);
        QVERIFY2(!guidance->available, "guidance scored with no +1q data");
    }

    // ── Track record ─────────────────────────────────────────────────────────
    // Beating every quarter by a hair is not the same as beating by a lot with
    // wild swings, and a company whose beats get sold is not a bullish record.
    void beats_that_are_not_rewarded_pull_the_track_record_down() {
        EarningsAnalysis a;
        a.valid = true;
        for (int i = 0; i < 8; ++i)      // beats every quarter, sells off every time
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.08, 8.0, -5.0));
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.beat_rate, 1.0);      // still reported for the panel
        const auto* record = leg(v, QStringLiteral("SURPRISE TRACK RECORD"));
        QVERIFY(record && record->available);
        QVERIFY2(record->score < 0.3, qPrintable(QString("score %1").arg(record->score)));
        QVERIFY(v.beat_reaction_pct.has_value());
        QCOMPARE(*v.beat_reaction_pct, -5.0);
    }

    // Same average surprise, different consistency: the steady one must score
    // higher. Under the old beat-rate rule both were 8/8 and identical.
    void steady_surprises_outscore_erratic_ones() {
        const double steady[] = {4.0, 4.5, 3.5, 4.0, 4.2, 3.8, 4.1, 3.9};
        const double erratic[] = {20.0, -6.0, 18.0, -4.0, 15.0, -2.0, 12.0, -1.0};
        auto score_of = [](const double* surprises) {
            EarningsAnalysis a;
            a.valid = true;
            for (int i = 0; i < 8; ++i) {
                EarningsPoint p;
                p.timestamp = 1700000000LL - i * 7776000LL;
                p.eps_actual = 1.0;
                p.surprise_pct = surprises[i];
                a.history.append(p);
            }
            const auto* c = leg(evaluate_earnings(a), QStringLiteral("SURPRISE TRACK RECORD"));
            return c ? c->score : 0.0;
        };
        QVERIFY2(score_of(steady) > score_of(erratic),
                 qPrintable(QString("steady %1 vs erratic %2")
                                .arg(score_of(steady)).arg(score_of(erratic))));
    }

    // ── Horizon rotation ─────────────────────────────────────────────────────
    // The same picture read six weeks out and three days out is not the same
    // read: what analysts did this month is stale in one case and the whole
    // story in the other.
    void weights_rotate_toward_the_fast_legs_as_the_date_approaches() {
        EarningsAnalysis far = bullish_fixture();
        far.next.timestamp = QDateTime::currentSecsSinceEpoch() + 60 * 86400;
        EarningsAnalysis near = bullish_fixture();
        near.next.timestamp = QDateTime::currentSecsSinceEpoch() + 2 * 86400;

        const auto vf = evaluate_earnings(far);
        const auto vn = evaluate_earnings(near);

        QVERIFY(weight_of(vn, "GUIDANCE EXPECTATIONS") > weight_of(vf, "GUIDANCE EXPECTATIONS"));
        QVERIFY(weight_of(vn, "REVISION MOMENTUM") > weight_of(vf, "REVISION MOMENTUM"));
        QVERIFY(weight_of(vn, "ANALYST BREADTH") > weight_of(vf, "ANALYST BREADTH"));
        QVERIFY(weight_of(vn, "SURPRISE TRACK RECORD") < weight_of(vf, "SURPRISE TRACK RECORD"));
        QVERIFY(weight_of(vn, "PRICE & EXPECTATIONS") < weight_of(vf, "PRICE & EXPECTATIONS"));

        // Rotation redistributes weight; it must never create or destroy it,
        // or the score would drift with the calendar for no other reason.
        for (const auto* v : {&vf, &vn}) {
            double total = 0;
            for (const auto& c : v->components) total += c.weight;
            QVERIFY2(std::abs(total - 1.0) < 1e-9, qPrintable(QString("weights summed to %1").arg(total)));
        }
        QVERIFY2(vn.horizon_note.contains("fast legs"), qPrintable(vn.horizon_note));
        QVERIFY2(vf.horizon_note.contains("too early"), qPrintable(vf.horizon_note));
    }

    // With no date there is nothing to rotate on, and inventing a tilt would
    // silently favour legs for no reason the user could see.
    void unknown_date_leaves_weights_at_their_base_split() {
        EarningsAnalysis a = bullish_fixture();
        a.next.timestamp.reset();
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.days_to_report, -1);
        for (const auto& c : v.components)
            QVERIFY2(std::abs(c.weight - c.base_weight) < 1e-9, qPrintable(c.name));
        QVERIFY(v.horizon_note.contains("No report date"));
    }

    // ── Dispersion ───────────────────────────────────────────────────────────
    // A wide consensus is a magnitude risk, not a direction. It must reach the
    // reader as a caveat and never as a thumb on the score.
    void wide_consensus_spread_is_a_caveat_not_a_score() {
        EarningsAnalysis tight = bullish_fixture();
        tight.next.eps_avg = 2.00;
        tight.next.eps_low = 1.96;
        tight.next.eps_high = 2.04;
        EarningsAnalysis wide = bullish_fixture();
        wide.next.eps_avg = 2.00;
        wide.next.eps_low = 1.40;
        wide.next.eps_high = 2.60;

        const auto vt = evaluate_earnings(tight);
        const auto vw = evaluate_earnings(wide);
        QVERIFY(vt.dispersion_pct.has_value());
        QVERIFY(std::abs(*vt.dispersion_pct - 4.0) < 1e-9);
        QVERIFY(!vt.dispersion_is_wide);
        QVERIFY(std::abs(*vw.dispersion_pct - 60.0) < 1e-9);
        QVERIFY(vw.dispersion_is_wide);
        QVERIFY(vw.caveats.join(" | ").contains("apart on this quarter"));
        // Identical in every scored respect.
        QVERIFY2(std::abs(vt.score - vw.score) < 1e-9,
                 qPrintable(QString("%1 vs %2").arg(vt.score).arg(vw.score)));
    }

    // The caveats are the honest part of the panel — a big pre-print run-up
    // and an imminent date have to be called out whichever way the score leans.
    void imminent_report_and_runup_raise_caveats() {
        EarningsAnalysis a = bullish_fixture();
        a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 2 * 86400;
        a.runup_5d_pct = 15.0;                 // typical move in the fixture is 4%
        const auto v = evaluate_earnings(a);
        const QString all = v.caveats.join(" | ");
        QVERIFY2(all.contains("typically moves"), qPrintable(all));
        QVERIFY2(all.contains("Report is"), qPrintable(all));
        QVERIFY2(all.contains("already moved"), qPrintable(all));
    }
};

QTEST_MAIN(TestEarningsSignal)
#include "test_earnings_signal.moc"
