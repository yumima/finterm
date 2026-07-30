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
    a.revisions.append(revision("0q", 12, 0));
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
    a.revisions.clear();
    a.revisions.append(revision("0q", 0, 14));
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
        // A single quarter drives only the track-record leg (weight 0.18),
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

    void days_to_next_earnings_handles_missing_date() {
        EarningsAnalysis a;
        QCOMPARE(days_to_next_earnings(a), -1);
        a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 5 * 86400 + 3600;
        QCOMPARE(days_to_next_earnings(a), 5);
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
