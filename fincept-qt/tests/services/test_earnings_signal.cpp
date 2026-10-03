// tests/services/test_earnings_signal.cpp
//
// The ER Earnings tab tells the reader how likely a beat is, how big the move
// should be, and — deliberately — nothing about which way. The cases below pin
// the properties that would be embarrassing to get wrong:
//
//   - a security with no earnings (ETF, fund) produces no numbers at all;
//   - every forecast is built from quarters BEFORE the one it describes;
//   - a beat probability is shrunk toward the pooled rate, never 100%;
//   - accounting artefacts and projected quarters never enter a record;
//   - a real 0.0 (an in-line quarter, a flat session) is a value, not missing.

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

/// Eight quarters of a consistent beater (8%) whose stock moves 4% on each.
QVector<EarningsPoint> strong_history() {
    QVector<EarningsPoint> h;
    for (int i = 0; i < 8; ++i)
        h.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.08, 8.0, 4.0));
    return h;
}

/// Eight quarters of misses.
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

EarningsAnalysis with_history(const QVector<EarningsPoint>& h) {
    EarningsAnalysis a;
    a.valid = true;
    a.symbol = "TEST";
    a.history = h;
    a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 21 * 86400;
    a.next.is_estimated = false;
    a.next.eps_avg = 1.10;
    a.valuation.price = 100.0;
    a.valuation.analyst_count = 30;
    return a;
}

double scenario_prob_sum(const EarningsOutlook& o) {
    double s = 0;
    for (const auto& sc : o.scenarios) s += sc.probability;
    return s;
}

} // namespace

class TestEarningsSignal : public QObject {
    Q_OBJECT

  private slots:
    // An ETF has no earnings at all — the outlook must say so instead of
    // presenting pooled numbers as if they described this security.
    void empty_analysis_is_no_outlook() {
        EarningsAnalysis a;
        a.valid = true;
        const auto o = evaluate_outlook(a);
        QVERIFY(!o.valid);
        QVERIFY(!o.p_beat.has_value());
        QVERIFY(!o.expected_move_pct.has_value());
        QVERIFY(o.scenarios.isEmpty());
    }

    // Eight beats in eight is not a certainty: the pooled rate is blended in
    // as eight more quarters, which is what scored best out of sample.
    void a_perfect_record_is_shrunk_not_certain() {
        const auto o = evaluate_outlook(with_history(strong_history()));
        QVERIFY(o.p_beat.has_value());
        QVERIFY(*o.p_beat < 0.99);
        QVERIFY(*o.p_beat > o.pooled_beat_rate);   // better than average, though
        QCOMPARE(o.beats, 8);
        QCOMPARE(o.scored_quarters, 8);
    }

    void a_run_of_misses_reads_below_average() {
        const auto o = evaluate_outlook(with_history(weak_history()));
        QVERIFY(o.p_beat.has_value());
        QVERIFY(*o.p_beat < 0.5);
        QVERIFY(*o.p_beat > 0.25);   // shrinkage holds it off the floor
    }

    void scenario_probabilities_sum_to_one_and_agree_with_p_beat() {
        const auto o = evaluate_outlook(with_history(strong_history()));
        QCOMPARE(o.scenarios.size(), 4);
        QVERIFY(std::abs(scenario_prob_sum(o) - 1.0) < 1e-9);
        QVERIFY(std::abs((1.0 - o.scenarios[0].probability) - *o.p_beat) < 1e-9);
        // 8% beats land in the 3–10% bucket, so that is where the name's own
        // record pushes the mass.
        QCOMPARE(o.scenarios[2].name_count, 8);
        QVERIFY(o.scenarios[2].probability > o.scenarios[1].probability);
    }

    // The scenario moves scale with the name: a miss is down, a slight beat is
    // the "beat and fall" bucket, and both beats above 3% are up.
    void scenario_moves_scale_with_the_expected_move() {
        const auto o = evaluate_outlook(with_history(strong_history()));
        QVERIFY(o.expected_move_pct.has_value());
        QVERIFY(o.scenarios[0].typical_move_pct < 0);
        QVERIFY(o.scenarios[1].typical_move_pct < 0);
        QVERIFY(o.scenarios[2].typical_move_pct > 0);
        QVERIFY(o.scenarios[3].typical_move_pct > o.scenarios[2].typical_move_pct);
        QVERIFY(std::abs(o.scenarios[3].typical_move_pct -
                         *o.expected_move_pct * o.scenarios[3].move_multiple) < 1e-9);
    }

    // Too little record: no name-specific probability, but the pooled mix is
    // still shown — and the caveat says which one the reader is looking at.
    void thin_record_falls_back_to_the_pool_with_a_caveat() {
        QVector<EarningsPoint> h = strong_history().mid(0, 2);
        const auto o = evaluate_outlook(with_history(h));
        QVERIFY(!o.p_beat.has_value());
        QVERIFY(!o.expected_move_pct.has_value());
        QCOMPARE(o.scenarios.size(), 4);
        QVERIFY(std::abs(scenario_prob_sum(o) - 1.0) < 1e-3);
        bool caveat = false;
        for (const auto& c : o.caveats) caveat |= c.contains("pooled mix");
        QVERIFY(caveat);
    }

    // A 0.0% surprise is a real result — met consensus exactly — and counts
    // with the misses, not as missing data.
    void zero_surprise_counts_as_a_miss_not_missing() {
        QVector<EarningsPoint> h;
        for (int i = 0; i < 4; ++i)
            h.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.0, 0.0, 0.0));
        const auto o = evaluate_outlook(with_history(h));
        QCOMPARE(o.scored_quarters, 4);
        QCOMPARE(o.beats, 0);
        QCOMPARE(o.scenarios[0].name_count, 4);
    }

    // GOOG's shape: a GAAP one-off reading +213% must not set the record.
    void accounting_quarter_does_not_enter_the_record() {
        QVector<EarningsPoint> h;
        EarningsPoint odd = quarter(1700000000LL, 2.91, 9.11, 212.9, 2.0);
        odd.surprise_suspect = true;
        h.append(odd);
        for (int i = 1; i < 6; ++i)
            h.append(quarter(1700000000LL - i * 7776000LL, 1.0, 0.98, -2.0, -1.0));
        const auto o = evaluate_outlook(with_history(h));
        QCOMPARE(o.scored_quarters, 5);
        QCOMPARE(o.beats, 0);
        QCOMPARE(scenario_index(odd), -1);
    }

    // The size forecast: the plain trailing mean without volatility, the
    // documented blend with it.
    void expected_move_blends_history_and_volatility() {
        auto a = with_history(strong_history());
        auto o = evaluate_outlook(a);
        QCOMPARE(*o.trailing_move_pct, 4.0);
        QCOMPARE(*o.expected_move_pct, 4.0);
        a.pre_vol_pct = 1.5;
        o = evaluate_outlook(a);
        QVERIFY(std::abs(*o.expected_move_pct - (0.5 * 4.0 + 1.5)) < 1e-9);
        // Ranges are fixed multiples of the forecast, widening outward.
        QVERIFY(*o.half_within_pct < *o.expected_move_pct);
        QVERIFY(*o.most_within_pct > *o.expected_move_pct);
        QVERIFY(*o.tail_beyond_pct > *o.most_within_pct);
    }

    // Size reads the last twelve settled prints (a slow, stable property);
    // the beat record reads only the last eight.
    void size_reads_twelve_prints_the_beat_record_eight() {
        QVector<EarningsPoint> h = strong_history();               // 8 × 4%, beats
        for (int i = 8; i < 14; ++i)                               // 6 older: 40%, misses
            h.append(quarter(1700000000LL - i * 7776000LL, 1.0, 0.9, -10.0, 40.0));
        const auto o = evaluate_outlook(with_history(h));
        QCOMPARE(*o.trailing_move_pct, (8 * 4.0 + 4 * 40.0) / 12.0);
        QCOMPARE(o.scored_quarters, 8);
        QCOMPARE(o.beats, 8);
    }

    // Options are reported beside the forecast, and an extreme gap between
    // them is flagged — but never folded into either number.
    void implied_move_is_compared_not_blended() {
        auto a = with_history(strong_history());
        EarningsImpliedMove imp;
        imp.expiry = "2026-10-23";
        imp.event_move_pct = 8.0;
        imp.total_move_pct = 9.0;
        a.next.implied = imp;
        const auto o = evaluate_outlook(a);
        QCOMPARE(*o.implied_move_pct, 8.0);
        QCOMPARE(*o.expected_move_pct, 4.0);
        QCOMPARE(*o.implied_ratio, 2.0);
        bool flagged = false;
        for (const auto& c : o.caveats) flagged |= c.contains("much bigger");
        QVERIFY(flagged);
    }

    // The point of the redesign: no directional call, ever, in the headline.
    void the_headline_never_calls_a_direction() {
        for (const auto& h : {strong_history(), weak_history()}) {
            const auto o = evaluate_outlook(with_history(h));
            QVERIFY(o.headline.contains("no reliable read on direction"));
            QVERIFY(!o.headline.contains("BUY"));
            QVERIFY(!o.headline.contains("SELL"));
        }
    }

    void estimate_drift_reads_the_thirty_day_change() {
        auto a = with_history(strong_history());
        a.trend.append(trend("0q", 1.10, 1.00, 0.95));
        auto o = evaluate_outlook(a);
        QCOMPARE(o.drift, EstimateDrift::Rising);
        a.trend[0] = trend("0q", 0.90, 1.00, 1.05);
        o = evaluate_outlook(a);
        QCOMPARE(o.drift, EstimateDrift::Falling);
        a.trend[0] = trend("0q", 1.001, 1.00, 0.80);   // 30d flat outranks 90d
        o = evaluate_outlook(a);
        QCOMPARE(o.drift, EstimateDrift::Flat);
        a.trend.clear();
        o = evaluate_outlook(a);
        QCOMPARE(o.drift, EstimateDrift::Unknown);
    }

    void wide_consensus_spread_is_a_caveat() {
        auto a = with_history(strong_history());
        a.next.eps_avg = 1.0;
        a.next.eps_low = 0.8;
        a.next.eps_high = 1.2;
        const auto o = evaluate_outlook(a);
        QVERIFY(std::abs(*o.dispersion_pct - 40.0) < 1e-9);
        QVERIFY(o.dispersion_is_wide);
    }

    // ── The forecast record ──────────────────────────────────────────────────

    // Each print is forecast from the prints before it only. Changing the
    // newest print's own outcome must not move its own forecast.
    void the_record_never_sees_its_own_quarter() {
        auto a = with_history(strong_history());
        const auto before = forecast_record(a);
        a.history[0].reaction_pct = 30.0;
        a.history[0].surprise_pct = -50.0;
        const auto after = forecast_record(a);
        QCOMPARE(before.prints.size(), after.prints.size());
        QCOMPARE(*before.prints.last().expected_move_pct, *after.prints.last().expected_move_pct);
        QCOMPARE(*before.prints.last().p_beat, *after.prints.last().p_beat);
        QCOMPARE(*after.prints.last().beat, false);
    }

    void the_oldest_prints_have_nothing_to_forecast_from() {
        const auto r = forecast_record(with_history(strong_history()));
        QCOMPARE(r.prints.size(), 8);
        // Oldest first: three prior prints are needed before a forecast.
        QVERIFY(!r.prints[0].expected_move_pct.has_value());
        QVERIFY(!r.prints[2].expected_move_pct.has_value());
        QVERIFY(r.prints[3].expected_move_pct.has_value());
        QCOMPARE(r.size_graded, 5);
        QCOMPARE(r.size_inside, 5);   // 4% every time, forecast 4% — inside
        QCOMPARE(r.beat_graded, 5);
        QCOMPARE(r.beat_hits, 5);
    }

    void the_record_skips_unsettled_and_projected_quarters() {
        auto h = strong_history();
        EarningsPoint live = quarter(1700000000LL + 7776000LL, 1.0, 1.1, 10.0, 0.0);
        live.reaction_pct.reset();
        live.reaction_live_pct = 3.0;
        h.prepend(live);
        EarningsPoint proj;
        proj.timestamp = 1700000000LL + 2 * 7776000LL;
        proj.is_estimate = true;
        h.prepend(proj);
        const auto r = forecast_record(with_history(h));
        QCOMPARE(r.prints.size(), 8);
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

        const auto o = evaluate_outlook(a);
        QCOMPARE(o.scored_quarters, 8);        // the 8 real ones, not 9
        QCOMPARE(o.beats, 8);                  // the -90% "surprise" didn't land
        QCOMPARE(o.reaction_quarters, 8);
        QCOMPARE(*o.trailing_move_pct, 4.0);   // the -25% "reaction" didn't land

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

};

QTEST_MAIN(TestEarningsSignal)
#include "test_earnings_signal.moc"
