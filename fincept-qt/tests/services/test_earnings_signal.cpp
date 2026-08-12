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

EarningsAnalysis bullish_fixture();

/// Composite of the untouched bullish fixture — the baseline a variant is
/// compared against when a test changes exactly one thing about it.
double bullish_fixture_score() { return evaluate_earnings(bullish_fixture()).score; }

EarningsRevisionRow revision(const QString& period, double up30, double down30) {
    EarningsRevisionRow r;
    r.period = period;
    r.label = period;
    r.up_30d = up30;
    r.down_30d = down30;
    return r;
}

/// A steady beater whose most recent print carries a GAAP one-off.
///
/// This is GOOG's shape: three ordinary quarters at +5-7% and then a headline
/// +213% produced by subtracting an as-reported figure from an adjusted
/// consensus. Averaged in, the one accounting quarter sets the whole record.
EarningsAnalysis with_accounting_quarter(bool flagged) {
    EarningsAnalysis a;
    a.valid = true;
    a.symbol = "GOOG";
    a.next.timestamp = QDateTime::currentSecsSinceEpoch() + 21 * 86400;
    a.next.eps_avg = 1.10;
    a.valuation.price = 100.0;
    for (int i = 1; i < 8; ++i)
        a.history.append(quarter(1700000000LL - i * 7776000LL, 1.00, 1.06, 6.0, 2.0));
    EarningsPoint odd = quarter(1700000000LL, 2.91, 9.11, 212.9, 2.0);
    odd.surprise_suspect = flagged;
    a.history.prepend(odd);
    return a;
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
    // A constructive-but-not-crowded setup: the consensus number has risen
    // faster than the price, the stock has lagged the index recently, and it
    // is well off its high. That is the configuration this scorer is supposed
    // to like — good news that nobody has paid for yet.
    a.runup_90d_pct = 4.0;
    a.runup_20d_pct = 1.0;
    a.rel_runup_20d_pct = -2.0;
    a.pct_from_52w_high = -25.0;
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
    // Numbers coming down while the price ran up, and sitting on its high:
    // the bar is high and rising against a business that is not.
    a.runup_90d_pct = 30.0;
    a.runup_20d_pct = 14.0;
    a.rel_runup_20d_pct = 18.0;
    a.pct_from_52w_high = -0.5;
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

    // ── The bar ──────────────────────────────────────────────────────────────
    // The failure this whole axis exists for: a company doing everything right
    // whose price has already run past the numbers. Every SETUP leg is
    // maximally bullish here and the verdict must still not read BUY.
    void a_price_that_outran_its_own_numbers_is_not_a_buy() {
        EarningsAnalysis a = bullish_fixture();
        a.runup_90d_pct = 45.0;          // stock up 45% over the quarter…
        a.runup_20d_pct = 18.0;
        a.rel_runup_20d_pct = 16.0;      // …far ahead of the index
        a.pct_from_52w_high = -0.2;      // …and sitting on its high
        // Estimates over the same 90 days: 2.10 vs 1.90, about +10%.

        const auto v = evaluate_earnings(a);
        const auto* gap = leg(v, QStringLiteral("EXPECTATIONS GAP"));
        const auto* crowd = leg(v, QStringLiteral("POSITIONING"));
        QVERIFY(gap && gap->available);
        QVERIFY2(gap->score < -0.5, qPrintable(QString("gap %1").arg(gap->score)));
        QVERIFY(crowd && crowd->available);
        QVERIFY2(crowd->score < -0.5, qPrintable(QString("crowding %1").arg(crowd->score)));

        // The setup half is still strong — that is the point. The bar half is
        // what has to pull the composite down.
        QVERIFY(v.setup_score.has_value() && *v.setup_score > 20.0);
        QVERIFY(v.bar_score.has_value() && *v.bar_score < 0.0);
        QVERIFY2(v.score < bullish_fixture_score(),
                 "a run-up past the numbers left the composite untouched");
    }

    // Estimates rising faster than the price is the setup the leg rewards.
    void numbers_rising_faster_than_the_price_scores_positive() {
        EarningsAnalysis a = bullish_fixture();
        a.runup_90d_pct = -12.0;         // stock down while the number went up
        const auto* gap = leg(evaluate_earnings(a), QStringLiteral("EXPECTATIONS GAP"));
        QVERIFY(gap && gap->available);
        QVERIFY2(gap->score > 0.5, qPrintable(QString("gap %1").arg(gap->score)));
    }

    // Both axes are reported, and a sharp disagreement between them is stated
    // rather than left to average out into a number that says neither thing.
    void axis_tension_is_named_in_the_headline() {
        EarningsAnalysis a = bullish_fixture();
        a.runup_90d_pct = 60.0;
        a.rel_runup_20d_pct = 25.0;
        a.pct_from_52w_high = 0.0;
        a.valuation.target_mean = 95.0;   // price already past the street's target
        a.valuation.recommendation_mean = 3.0;
        const auto v = evaluate_earnings(a);
        QVERIFY(v.setup_score.has_value() && v.bar_score.has_value());
        QVERIFY2(v.headline.contains("already paid for"), qPrintable(v.headline));
    }

    // A name that reliably fades after a hot week must be scored on those
    // quarters when it is walking in hot again — not on its average quarter.
    void a_stock_that_fades_after_a_run_is_scored_on_those_prints() {
        EarningsAnalysis a;
        a.valid = true;
        // Four prints after a big run-up, all sold; four after a flat run,
        // all bought. The unconditional average is a wash.
        for (int i = 0; i < 4; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.05, 5.0, -6.0, 12.0));
        for (int i = 4; i < 8; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.05, 5.0, 6.0, -1.0));

        // Walking into this one hot: the conditional read applies.
        a.runup_5d_pct = 15.0;
        const auto hot = evaluate_earnings(a);
        const auto* hot_leg = leg(hot, QStringLiteral("PRICE REACTION HISTORY"));
        QVERIFY(hot_leg && hot_leg->available);
        QVERIFY2(hot_leg->score < 0, qPrintable(QString("score %1").arg(hot_leg->score)));
        QCOMPARE(hot.hot_runup_prints, 4);
        QVERIFY(hot.hot_runup_reaction_pct.has_value());
        QCOMPARE(*hot.hot_runup_reaction_pct, -6.0);
        QVERIFY(hot_leg->detail.contains("after a run-up like today's"));

        // Walking in cold: the subset answers a question this setup doesn't
        // pose, so the leg falls back to the whole record.
        a.runup_5d_pct = -3.0;
        const auto cold = evaluate_earnings(a);
        QCOMPARE(cold.hot_runup_prints, 0);
        QVERIFY(!cold.hot_runup_reaction_pct.has_value());
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

    // ── The point estimate ───────────────────────────────────────────────────
    // Rules-based, so its properties are assertable rather than a matter of
    // taste: it leans the way the composite leans, it is bounded by how far
    // this name actually travels, and a half-empty scorecard cannot produce a
    // confident number.
    void predicted_move_follows_the_lean_and_the_name() {
        const auto bull = evaluate_earnings(bullish_fixture());
        QVERIFY(bull.predicted_move_pct.has_value());
        QVERIFY2(*bull.predicted_move_pct > 0,
                 qPrintable(QString("bullish setup predicted %1").arg(*bull.predicted_move_pct)));
        // Never larger than the move this name typically makes: the estimate is
        // a fraction of the band, not a claim that can exceed it.
        QVERIFY(std::abs(*bull.predicted_move_pct) <= bull.typical_move_pct + 1e-9);

        const auto bear = evaluate_earnings(bearish_fixture());
        QVERIFY(bear.predicted_move_pct.has_value());
        QVERIFY(*bear.predicted_move_pct < 0);
        QVERIFY(std::abs(*bear.predicted_move_pct) <= bear.typical_move_pct + 1e-9);
    }

    // A name with no reaction history has no band to scale, so there is no
    // estimate to give. "No prediction" must not arrive as "predicting zero".
    void no_reaction_history_means_no_prediction() {
        EarningsAnalysis a = bullish_fixture();
        for (auto& p : a.history)
            p.reaction_pct.reset();
        const auto v = evaluate_earnings(a);
        QCOMPARE(v.typical_move_pct, 0.0);
        QVERIFY2(!v.predicted_move_pct.has_value(), "invented an estimate with no move history");
    }

    // Below the confidence floor the engine refuses to take a side, and the
    // estimate has to refuse with it — a number there would be a bolder claim
    // than the verdict it sits beside.
    void a_thin_scorecard_makes_no_prediction() {
        EarningsAnalysis a;
        a.valid = true;
        a.history.append(quarter(1700000000LL, 1.0, 2.0, 100.0, 20.0));
        const auto v = evaluate_earnings(a);
        QVERIFY(v.confidence < 0.35);
        QVERIFY(!v.predicted_move_pct.has_value());
    }

    // Same lean, less data behind it: the estimate must shrink. This is what
    // stops a scorecard holding two legs from speaking as loudly as one
    // holding nine.
    void less_data_shrinks_the_prediction() {
        const auto full = evaluate_earnings(bullish_fixture());

        EarningsAnalysis thin = bullish_fixture();
        thin.trend.clear();          // drop revisions and guidance
        thin.revisions.clear();      // drop breadth
        const auto partial = evaluate_earnings(thin);

        QVERIFY(partial.predicted_move_pct.has_value());
        QVERIFY(partial.confidence < full.confidence);
        QVERIFY2(*partial.predicted_move_pct < *full.predicted_move_pct,
                 qPrintable(QString("thin %1 vs full %2")
                                .arg(*partial.predicted_move_pct).arg(*full.predicted_move_pct)));
    }

    // ── Reconstruction ───────────────────────────────────────────────────────
    // A reconstructed point must never see its own outcome, or the chart it
    // feeds becomes a drawing of hindsight.
    void reconstruction_never_sees_its_own_quarter() {
        EarningsAnalysis a;
        a.valid = true;
        // Seven quiet quarters, then one violent one at the front. If the
        // newest point could see itself, its typical move would jump.
        for (int i = 1; i < 8; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.02, 2.0, 1.0, 0.5));
        EarningsPoint newest = quarter(1700000000LL, 1.0, 1.40, 40.0, 25.0, 0.5);
        a.history.prepend(newest);

        const auto rec = reconstruct_predictions(a);
        QVERIFY(!rec.isEmpty());
        // Oldest first, so the newest quarter is last.
        const auto& latest = rec.last();
        QCOMPARE(latest.timestamp, 1700000000LL);
        QVERIFY(latest.actual_move_pct.has_value());
        QCOMPARE(*latest.actual_move_pct, 25.0);
        QVERIFY(latest.predicted_move_pct.has_value());
        // Its estimate is built from the quiet quarters alone, so it cannot
        // approach the 25% move it is being compared against.
        QVERIFY2(std::abs(*latest.predicted_move_pct) < 3.0,
                 qPrintable(QString("predicted %1 off quiet history")
                                .arg(*latest.predicted_move_pct)));
    }

    // The oldest quarter has nothing before it, so there is nothing to predict
    // from — it must come back without an estimate rather than with a zero.
    void the_oldest_quarter_has_no_prior_to_predict_from() {
        EarningsAnalysis a;
        a.valid = true;
        for (int i = 0; i < 3; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.05, 5.0, 3.0, 1.0));
        const auto rec = reconstruct_predictions(a);
        QCOMPARE(rec.size(), 3);
        QVERIFY2(!rec.first().predicted_move_pct.has_value(),
                 "predicted a quarter with no history behind it");
        QVERIFY(rec.first().actual_move_pct.has_value());
    }

    // Reconstruction runs the live formula with the unavailable legs counted
    // as absent, so it is deliberately more muted than a full-data prediction
    // leaning the same way. Scaling that away would overstate what it knows.
    void reconstruction_is_muted_against_a_full_scorecard() {
        const auto full = evaluate_earnings(bullish_fixture());
        QVERIFY(full.predicted_move_pct.has_value());

        EarningsAnalysis a = bullish_fixture();
        const auto rec = reconstruct_predictions(a);
        QVERIFY(!rec.isEmpty());
        QVERIFY(rec.last().predicted_move_pct.has_value());
        QVERIFY2(std::abs(*rec.last().predicted_move_pct) < std::abs(*full.predicted_move_pct),
                 qPrintable(QString("reconstruction %1 vs full %2")
                                .arg(*rec.last().predicted_move_pct)
                                .arg(*full.predicted_move_pct)));
    }

    // Quarters with no settled reaction are not plottable pairs.
    void quarters_without_an_outcome_are_left_out() {
        EarningsAnalysis a;
        a.valid = true;
        EarningsPoint pending;   // reported, reaction not yet settled
        pending.timestamp = 1700000000LL;
        pending.eps_actual = 1.1;
        pending.surprise_pct = 5.0;
        a.history.append(pending);
        for (int i = 1; i < 5; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.05, 5.0, 3.0, 1.0));
        const auto rec = reconstruct_predictions(a);
        for (const auto& r : rec)
            QVERIFY(r.timestamp != 1700000000LL);
    }

    // The band has to be the actual limit, or the chart drawn from it lies
    // about how much room the predictor had.
    void the_bound_is_the_limit_the_prediction_can_reach() {
        EarningsAnalysis a = bullish_fixture();
        const auto rec = reconstruct_predictions(a);
        QVERIFY(!rec.isEmpty());
        int checked = 0;
        for (const auto& q : rec) {
            if (!q.predicted_move_pct) continue;
            QVERIFY(q.bound_pct.has_value());
            QVERIFY(*q.bound_pct > 0);
            QVERIFY2(std::abs(*q.predicted_move_pct) <= *q.bound_pct + 1e-9,
                     qPrintable(QString("predicted %1 outside its own band %2")
                                    .arg(*q.predicted_move_pct).arg(*q.bound_pct)));
            ++checked;
        }
        QVERIFY(checked > 0);
    }

    // The reconstruction's band must be visibly narrower than a live reading's
    // — that gap IS the explanation for the flat dotted stretch, so if the two
    // ever match, the chart has stopped telling the truth about it.
    void a_reconstructions_band_is_narrower_than_a_live_one() {
        const auto full = evaluate_earnings(bullish_fixture());
        const double live_bound = full.typical_move_pct * full.confidence;
        QVERIFY(live_bound > 0);

        const auto rec = reconstruct_predictions(bullish_fixture());
        QVERIFY(!rec.isEmpty());
        QVERIFY(rec.last().bound_pct.has_value());
        QVERIFY2(*rec.last().bound_pct < live_bound,
                 qPrintable(QString("reconstruction band %1 vs live %2")
                                .arg(*rec.last().bound_pct).arg(live_bound)));
    }

    // ── Competing predictors ─────────────────────────────────────────────────
    // The baselines exist to be beaten or not beaten. If they ever stop being
    // computed the comparison silently becomes a monologue.
    void every_predictor_is_graded_walk_forward() {
        const auto runs = compare_predictors(bullish_fixture(),
                                             evaluate_earnings(bullish_fixture()));
        QCOMPARE(runs.size(), 6);
        QSet<int> seen;
        for (const auto& r : runs) {
            seen.insert(static_cast<int>(r.predictor));
            QVERIFY(!r.label.isEmpty());
            QVERIFY(!r.explanation.isEmpty());
        }
        QCOMPARE(seen.size(), 6);

        // NO MOVE must never claim a direction, or it would be scored on calls
        // it never made.
        for (const auto& r : runs)
            if (r.predictor == MovePredictor::NoMove)
                QCOMPARE(r.direction_calls, 0);
    }

    // The adaptive model's signed call is capped by its own magnitude
    // estimate. Direction is the part it knows least about, so it must never
    // out-shout the size it is confident of.
    void the_adaptive_call_never_exceeds_its_magnitude_estimate() {
        EarningsAnalysis a;
        a.valid = true;
        // Violent, alternating history with big run-ups — the configuration
        // most likely to produce a runaway extrapolation.
        const double moves[] = {18.0, -22.0, 15.0, -19.0, 24.0, -17.0, 20.0, -21.0};
        for (int i = 0; i < 8; ++i)
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.1, 8.0,
                                     moves[i], 14.0));
        const auto runs = compare_predictors(a, evaluate_earnings(a));
        for (const auto& r : runs) {
            if (r.predictor != MovePredictor::Adaptive) continue;
            int checked = 0;
            for (const auto& q : r.points) {
                if (!q.predicted_move_pct) continue;
                QVERIFY(q.bound_pct.has_value());
                QVERIFY2(std::abs(*q.predicted_move_pct) <= *q.bound_pct + 1e-9,
                         qPrintable(QString("adaptive said %1 with a %2 magnitude estimate")
                                        .arg(*q.predicted_move_pct).arg(*q.bound_pct)));
                ++checked;
            }
            QVERIFY(checked > 0);
        }
    }

    // Skill shrinkage: a model that has been losing to "assume no move" must
    // pull its own calls in. Built from a history where the run-up carries no
    // information at all, so nothing it fits can help it.
    void a_model_losing_to_the_baseline_shrinks_itself() {
        EarningsAnalysis a;
        a.valid = true;
        // Reaction alternates independently of a steadily rising run-up.
        for (int i = 0; i < 10; ++i) {
            const double reaction = (i % 2 == 0) ? 9.0 : -9.0;
            a.history.append(quarter(1700000000LL - i * 7776000LL, 1.0, 1.05, 5.0,
                                     reaction, 2.0 + i * 1.5));
        }
        const auto runs = compare_predictors(a, evaluate_earnings(a));
        for (const auto& r : runs) {
            if (r.predictor != MovePredictor::Adaptive) continue;
            for (const auto& q : r.points) {
                if (!q.predicted_move_pct || !q.bound_pct) continue;
                // Well inside its own magnitude estimate: with no usable
                // signal it must not be making full-size calls.
                QVERIFY2(std::abs(*q.predicted_move_pct) < *q.bound_pct * 0.9,
                         qPrintable(QString("predicted %1 against a %2 bound on noise")
                                        .arg(*q.predicted_move_pct).arg(*q.bound_pct)));
            }
        }
    }

    // ── The EPS model ────────────────────────────────────────────────────────
    // The claim is that fitting the two real links — how big a surprise to
    // expect, and what a surprise is worth in price — beats guessing nothing.
    // On a history where those links genuinely exist, it has to.
    void the_eps_model_learns_a_surprise_to_move_relationship() {
        EarningsAnalysis a;
        a.valid = true;
        // Surprise varies; the move is exactly half of it. Nothing else
        // carries information, so only a model that fits that link can win.
        const double surprises[] = {12.0, 4.0, 14.0, 6.0, 16.0, 8.0, 10.0, 5.0, 13.0, 7.0};
        for (int i = 0; i < 10; ++i) {
            EarningsPoint p;
            p.timestamp = 1700000000LL - i * 7776000LL;
            p.eps_estimate = 1.00 + i * 0.01;
            p.eps_actual = *p.eps_estimate * (1.0 + surprises[i] / 100.0);
            p.surprise_pct = surprises[i];
            p.reaction_pct = surprises[i] * 0.5;
            p.runup_pct = 1.0;
            a.history.append(p);
        }

        const auto runs = compare_predictors(a, evaluate_earnings(a));
        double eps_err = -1, zero_err = -1;
        for (const auto& r : runs) {
            if (r.predictor == MovePredictor::EpsModel && r.graded > 0) eps_err = r.mean_abs_error;
            if (r.predictor == MovePredictor::NoMove && r.graded > 0) zero_err = r.mean_abs_error;
        }
        QVERIFY2(eps_err >= 0 && zero_err >= 0, "the EPS model or the baseline was never graded");
        QVERIFY2(eps_err < zero_err,
                 qPrintable(QString("eps model %1 pp vs doing nothing %2 pp on a history where "
                                    "the surprise fully determines the move")
                                .arg(eps_err).arg(zero_err)));
    }

    // With no consensus recorded on past quarters there are no (expected,
    // actual) pairs to fit, and the model has to fall back rather than invent
    // a relationship out of the actuals alone.
    void the_eps_model_needs_the_historical_estimates() {
        EarningsAnalysis a;
        a.valid = true;
        for (int i = 0; i < 8; ++i) {
            EarningsPoint p;
            p.timestamp = 1700000000LL - i * 7776000LL;
            p.eps_actual = 1.1;              // no eps_estimate at all
            p.surprise_pct = 5.0;
            p.reaction_pct = 3.0;
            p.runup_pct = 1.0;
            a.history.append(p);
        }
        const auto runs = compare_predictors(a, evaluate_earnings(a));
        for (const auto& r : runs) {
            if (r.predictor != MovePredictor::EpsModel) continue;
            for (const auto& q : r.points) {
                if (!q.predicted_move_pct || !q.bound_pct) continue;
                QVERIFY2(std::abs(*q.predicted_move_pct) <= *q.bound_pct + 1e-9,
                         "fell back outside its own magnitude estimate");
            }
        }
    }

    /// The live EPS MODEL call must run the SAME staged model its displayed
    /// walk-forward record was earned by. It used to pass i = -1 into
    /// asked_growth(), which bailed, so the live number was always the crude
    /// 0.1 x mean-surprise fallback while the button showed the staged
    /// model's MAE. With a projected row carrying the consensus, the live
    /// prediction must therefore DIFFER from what the same history produces
    /// when no ask is visible (the fallback path).
    void live_eps_model_uses_projected_consensus() {
        auto settled = []() {
            QVector<EarningsPoint> h;
            for (int i = 0; i < 10; ++i) {
                const double s = 2.0 + i;                       // varying surprise
                h.append(quarter(1700000000LL - i * 7776000LL,
                                 1.0, 1.0 + s / 100.0, s, 0.5 * s, 1.0));
            }
            return h;
        };

        EarningsAnalysis without;
        without.valid = true;
        without.history = settled();
        without.next.timestamp = QDateTime::currentSecsSinceEpoch() + 7 * 86400;

        EarningsAnalysis with = without;
        EarningsPoint proj;
        proj.timestamp = *with.next.timestamp;
        proj.is_estimate = true;
        proj.has_forward_estimate = true;
        proj.eps_estimate = 1.05;              // the ask the live call must see
        with.history.prepend(proj);

        auto next_of = [](const QVector<PredictorRun>& runs) -> std::optional<double> {
            for (const auto& r : runs)
                if (r.predictor == MovePredictor::EpsModel) return r.next_move_pct;
            return std::nullopt;
        };
        const auto n_with = next_of(compare_predictors(with, evaluate_earnings(with)));
        const auto n_without = next_of(compare_predictors(without, evaluate_earnings(without)));

        QVERIFY2(n_with.has_value(), "no live EPS-model estimate with a projected row present");
        QVERIFY2(n_without.has_value(), "fallback path stopped producing an estimate");
        QVERIFY2(std::abs(*n_with - *n_without) > 1e-6,
                 "projected consensus did not reach the live EPS model — still on the fallback");
    }

    /// The "beats no move" comparison must be over the SAME quarters. Every
    /// graded run carries what NO MOVE scored on exactly its quarters; for
    /// the NO MOVE run itself the two numbers are one and the same.
    void nomove_baseline_is_paired_per_predictor() {
        EarningsAnalysis a;
        a.valid = true;
        a.history = strong_history();
        for (const auto& r : compare_predictors(a, evaluate_earnings(a))) {
            if (r.graded == 0) continue;
            QVERIFY2(r.nomove_mae_same_quarters > 0, qPrintable(r.label));
            if (r.predictor == MovePredictor::NoMove)
                QCOMPARE(r.nomove_mae_same_quarters, r.mean_abs_error);
        }
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

    // ── GAAP-vs-adjusted basis ───────────────────────────────────────────────

    /// One accounting quarter must not become a company's track record.
    ///
    /// Yahoo reports GAAP EPS against an adjusted consensus, so a one-off gain
    /// or charge lands as a vast "surprise": GOOG's July 2026 print reads
    /// +213%, META's October 2025 tax charge -84%. Across 3,857 quarters these
    /// carry no relationship to the next session's move (Spearman +0.077,
    /// p = 0.32) where ordinary quarters do (+0.114, p ~ 1e-12), so they are
    /// flagged upstream and skipped here.
    void accounting_quarter_does_not_set_the_track_record() {
        const auto flagged = evaluate_earnings(with_accounting_quarter(true));
        const auto unflagged = evaluate_earnings(with_accounting_quarter(false));

        auto avg_of = [](const EarningsVerdict& r) { return r.avg_surprise_pct; };
        // Unflagged, the outlier drags the average far above every real quarter.
        QVERIFY2(avg_of(unflagged) > 20.0, qPrintable(QString::number(avg_of(unflagged))));
        // Flagged, the average reflects the seven ordinary quarters.
        QVERIFY2(avg_of(flagged) > 4.0 && avg_of(flagged) < 8.0,
                 qPrintable(QString::number(avg_of(flagged))));
    }
};

QTEST_MAIN(TestEarningsSignal)
#include "test_earnings_signal.moc"
