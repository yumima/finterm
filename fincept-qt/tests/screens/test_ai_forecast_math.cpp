// tests/screens/test_ai_forecast_math.cpp
//
// The AI Forecast tab records what a language model says about a stock and then
// grades it against real closes. That only means anything if the recorded
// forecast is ONE claim — and a model's structured output is not.
//
// Asked for a direction, a target price and a percentage, models routinely
// return three figures that disagree: "up", "$108" and "+3%" on a $100 stock.
// The tab stored all three verbatim, so the table's DIRECTION column, its
// TARGET column, the chart's plotted segment and the accuracy metric each
// quoted a different one while the panel presented them as a single forecast.
//
// The cases below pin the reconciliation, and the one rule that has to be
// shared by both sides of the comparison: the dead band.

#include "screens/equity_research/AiForecastMath.h"

#include <QtTest/QtTest>

#include <optional>

using namespace fincept::screens::ai_forecast;

class TestAiForecastMath : public QObject {
    Q_OBJECT

  private slots:

    // ── The core case: the model's own numbers contradict each other ─────────
    void target_wins_and_the_percentage_is_derived_from_it() {
        // $100 stock, "$108 target" alongside "+3%". Both cannot be true.
        const Forecast f = reconcile(100.0, 108.0, 3.0);
        QVERIFY(!f.empty);
        QCOMPARE(f.target_price, 108.0);
        QVERIFY(std::abs(f.predicted_pct - 8.0) < 1e-9);
        QCOMPARE(f.direction, QStringLiteral("up"));
        QVERIFY2(f.incoherent, "a 5-point disagreement must be reported, not absorbed");
    }

    void the_chart_and_the_error_metric_describe_the_same_claim() {
        // Whatever the model said, target_price and predicted_pct must always
        // be the same statement about price_now. The chart draws the first and
        // the accuracy table scores the second.
        const double price = 250.0;
        for (double target : {180.0, 249.0, 251.0, 300.0, 412.5}) {
            for (double stated : {-99.0, -3.0, 0.0, 3.0, 99.0}) {
                const Forecast f = reconcile(price, target, stated);
                QVERIFY(!f.empty);
                const double implied = price * (1.0 + f.predicted_pct / 100.0);
                QVERIFY2(std::abs(implied - f.target_price) < 1e-6,
                         qPrintable(QString("target %1 vs pct %2 -> %3")
                                        .arg(f.target_price).arg(f.predicted_pct).arg(implied)));
            }
        }
    }

    // ── The dead band has to be the same on both sides ───────────────────────
    void a_prediction_is_bucketed_exactly_like_an_outcome() {
        // This is the whole point of sharing bucket_direction(). A forecast of
        // +0.3% used to be stored as the model's word "up" and then scored
        // against an outcome of +0.4%, which buckets to "flat" — marked wrong
        // for landing where it said it would.
        const Forecast f = reconcile(100.0, 100.3, 0.3);
        QCOMPARE(f.direction, QStringLiteral("flat"));
        QCOMPARE(bucket_direction(0.4), QStringLiteral("flat"));
        QCOMPARE(f.direction, bucket_direction(0.4));   // now they agree

        // …and the band is symmetric, with the boundary excluded on both sides.
        QCOMPARE(bucket_direction(1.0), QStringLiteral("flat"));
        QCOMPARE(bucket_direction(-1.0), QStringLiteral("flat"));
        QCOMPARE(bucket_direction(1.01), QStringLiteral("up"));
        QCOMPARE(bucket_direction(-1.01), QStringLiteral("down"));
    }

    void the_models_direction_word_cannot_override_its_own_numbers() {
        // "up" with a target BELOW the current price is a contradiction the
        // model cannot be trusted to resolve; the numbers decide.
        const Forecast f = reconcile(100.0, 92.0, -8.0);
        QCOMPARE(f.direction, QStringLiteral("down"));
        QVERIFY(f.predicted_pct < 0);
        QVERIFY(!f.incoherent);   // target and percentage agreed here
    }

    // ── Filling in what the model left out ───────────────────────────────────
    void a_percentage_alone_still_produces_a_target() {
        const Forecast f = reconcile(200.0, 0.0, -5.0);
        QVERIFY(!f.empty);
        QVERIFY(std::abs(f.target_price - 190.0) < 1e-9);
        QCOMPARE(f.direction, QStringLiteral("down"));
        QVERIFY2(!f.incoherent, "nothing to disagree with when only one figure was given");
    }

    void nothing_usable_is_reported_as_empty_not_as_a_flat_call() {
        // No target, no percentage at all. The caller must decline to record:
        // storing this would put a "flat" call the model never made into a
        // track record that is then graded against real closes.
        const Forecast f = reconcile(200.0, 0.0, std::nullopt);
        QVERIFY(f.empty);
        QCOMPARE(f.target_price, 0.0);
    }

    void a_stated_zero_is_a_flat_call_and_not_a_missing_field() {
        // QJsonValue::toDouble() returns 0 for both, and they mean opposite
        // things: one is a forecast of no move, the other is no forecast.
        const Forecast f = reconcile(200.0, 0.0, 0.0);
        QVERIFY(!f.empty);
        QCOMPARE(f.direction, QStringLiteral("flat"));
        QCOMPARE(f.target_price, 200.0);
    }

    void a_stated_flat_contradicting_a_target_is_flagged() {
        // "predicted_pct": 0 beside an 8% target is a contradiction. Gating the
        // check on `pct != 0` silently swallowed exactly this case.
        const Forecast f = reconcile(100.0, 108.0, 0.0);
        QVERIFY(f.incoherent);
        QCOMPARE(f.direction, QStringLiteral("up"));
    }

    // ── A model's number may not poison a permanent metric ───────────────────
    void an_absurd_target_falls_back_to_the_stated_percentage() {
        // $2,500 on a $320 stock is +681%. The chart already refuses to let
        // that set its axis; the RECORD matters more, because "avg miss"
        // averages |predicted − actual| over rows that are never rewritten.
        const Forecast f = reconcile(320.0, 2500.0, 4.0);
        QVERIFY(!f.empty);
        QVERIFY2(std::abs(f.predicted_pct - 4.0) < 1e-9, "the plausible claim must survive");
        QVERIFY(std::abs(f.target_price - 332.8) < 1e-9);
        QVERIFY2(f.incoherent, "falling back is exactly the case a reader should be told about");
    }

    void an_absurd_target_is_rejected_in_BOTH_directions() {
        // The bound used to be abs(pct) <= 100, which is silently one-sided: a
        // move computed from a positive target can never be below -100%, so it
        // could only ever reject on the upside. The same decimal slip that
        // produced $2,500 on a $320 stock (caught) produces $3.20 (waved
        // through, stored as a confident "down" call worth -99%, and averaged
        // into a metric over rows that are never rewritten).
        const Forecast down = reconcile(320.0, 3.20, -4.0);
        QVERIFY2(!down.empty, "the plausible stated percentage should survive");
        QVERIFY2(std::abs(down.predicted_pct - (-4.0)) < 1e-9,
                 "the -99% target must not become the recorded forecast");
        QVERIFY(down.incoherent);

        // …and with no usable fallback, nothing is recorded at all.
        QVERIFY(reconcile(320.0, 3.20, std::nullopt).empty);
        QVERIFY(reconcile(320.0, 2500.0, std::nullopt).empty);
    }

    void the_bound_is_symmetric_in_ratio() {
        // A doubling and a halving are the same size of claim.
        QVERIFY(!reconcile(100.0, 200.0, std::nullopt).empty);   // +100%, at the edge
        QVERIFY(!reconcile(100.0, 50.0, std::nullopt).empty);    // -50%,  its mirror
        QVERIFY(reconcile(100.0, 200.01, std::nullopt).empty);
        QVERIFY(reconcile(100.0, 49.99, std::nullopt).empty);
    }

    void two_absurd_figures_are_not_a_forecast_at_all() {
        QVERIFY(reconcile(320.0, 2500.0, 900.0).empty);
        QVERIFY(reconcile(320.0, 3.20, -900.0).empty);   // and the mirror
    }

    void a_large_but_plausible_call_is_kept_intact() {
        // The bound is on slips, not on conviction. A 45% call is a real call.
        const Forecast f = reconcile(100.0, 145.0, 45.0);
        QVERIFY(!f.empty);
        QVERIFY(!f.incoherent);
        QVERIFY(std::abs(f.predicted_pct - 45.0) < 1e-9);
    }

    void no_price_anchor_means_no_forecast() {  // NOLINT
        // price_at_pred is what every derivation hangs off. Without it there is
        // no arithmetic to do and no claim to record.
        const Forecast f = reconcile(0.0, 108.0, 3.0);
        QVERIFY(f.empty);
        QCOMPARE(f.target_price, 0.0);
        QCOMPARE(f.predicted_pct, 0.0);
    }

    // ── Rounding is not incoherence ──────────────────────────────────────────
    void a_rounded_percentage_is_not_flagged() {
        // $100 -> $103.24 is +3.24%; a model quoting "+3.2%" has not
        // contradicted itself, and flagging that would make the warning noise.
        const Forecast f = reconcile(100.0, 103.24, 3.2);
        QVERIFY(!f.incoherent);
        QVERIFY(std::abs(f.predicted_pct - 3.24) < 1e-9);
    }

    void a_big_call_is_judged_proportionally() {
        // On a +40% call, half a point is rounding; four points is not.
        QVERIFY(!reconcile(100.0, 140.0, 40.3).incoherent);
        QVERIFY(reconcile(100.0, 140.0, 30.0).incoherent);
    }
};

QTEST_MAIN(TestAiForecastMath)
#include "test_ai_forecast_math.moc"
