// tests/screens/test_earnings_math.cpp
//
// The Earnings Calendar tile makes two numeric calls that are easy to get
// subtly wrong and impossible to eyeball once they're behind a network fetch:
//
//   parse_money()    Nasdaq ships EPS as display strings, negatives in
//                    accounting parentheses. Misreading "($0.12)" as +0.12
//                    flips a growth verdict.
//   growth_verdict() "consensus vs the year-ago quarter", with a dead band.
//                    The band exists because Nasdaq and Yahoo disagree by a
//                    few cents on the same print — META's June-2026 quarter
//                    was $7.10 on one panel and $7.22 on the other, either
//                    side of the $7.14 reported a year earlier, so the tile
//                    painted the same row red in one view and green in the
//                    other. These cases pin that behaviour.

#include "screens/dashboard/widgets/EarningsMath.h"

#include <QtTest/QtTest>

using namespace fincept::screens::widgets::earnings;

class TestEarningsMath : public QObject {
    Q_OBJECT

  private slots:
    // ── parse_money ──────────────────────────────────────────────────────────

    void parses_plain_dollar_amount() {
        double v = 0;
        QVERIFY(parse_money("$1.88", &v));
        QCOMPARE(v, 1.88);
    }

    void parses_accounting_negative() {
        double v = 0;
        QVERIFY(parse_money("($0.12)", &v));
        QCOMPARE(v, -0.12);
    }

    void parses_thousands_separators() {
        double v = 0;
        QVERIFY(parse_money("$4,994,876,028,480", &v));
        QCOMPARE(v, 4994876028480.0);
    }

    void rejects_empty_and_na() {
        double v = 42;
        QVERIFY(!parse_money("", &v));
        QVERIFY(!parse_money("   ", &v));
        QVERIFY(!parse_money("N/A", &v));
        QCOMPARE(v, 42.0); // untouched on failure
    }

    void rejects_non_numeric() {
        double v = 0;
        QVERIFY(!parse_money("--", &v));
    }

    // ── growth_verdict ───────────────────────────────────────────────────────

    void calls_clear_growth_and_decline() {
        QVERIFY(growth_verdict(2.00, 1.50) == Growth::Up);
        QVERIFY(growth_verdict(1.00, 1.50) == Growth::Down);
    }

    /// The regression that motivated the dead band: both consensus panels for
    /// META's June-2026 quarter must land on the same verdict as each other,
    /// not red-vs-green, given a $7.14 year-ago actual.
    void meta_consensus_spread_reads_flat_or_up_never_down() {
        const double year_ago = 7.14;
        const Growth nasdaq = growth_verdict(7.10, year_ago); // -0.56%
        const Growth yahoo = growth_verdict(7.22, year_ago);  // +1.12%
        QVERIFY(nasdaq == Growth::Flat);
        QVERIFY(yahoo == Growth::Up);
        QVERIFY(nasdaq != Growth::Down); // the bug: one view called it a decline
    }

    void sub_one_percent_moves_read_flat() {
        QVERIFY(growth_verdict(1.005, 1.00) == Growth::Flat);
        QVERIFY(growth_verdict(0.995, 1.00) == Growth::Flat);
        QVERIFY(growth_verdict(1.00, 1.00) == Growth::Flat);
    }

    void just_outside_the_band_is_called() {
        QVERIFY(growth_verdict(1.011, 1.00) == Growth::Up);
        QVERIFY(growth_verdict(0.989, 1.00) == Growth::Down);
    }

    /// A near-zero year-ago EPS must not make every comparison enormous —
    /// the scale floor keeps a penny move from reading as a 100% swing.
    void near_zero_year_ago_uses_scale_floor() {
        QVERIFY(growth_verdict(0.0004, 0.0) == Growth::Flat); // 0.4c on a 5c floor
        QVERIFY(growth_verdict(0.02, 0.0) == Growth::Up);
    }

    /// A narrowing loss is growth: -$0.50 against -$1.00 a year ago is an
    /// improvement, and the sign handling has to get that right.
    void narrowing_loss_is_growth() {
        QVERIFY(growth_verdict(-0.50, -1.00) == Growth::Up);
        QVERIFY(growth_verdict(-1.50, -1.00) == Growth::Down);
    }

    void loss_turning_to_profit_is_growth() {
        QVERIFY(growth_verdict(0.25, -0.75) == Growth::Up);
    }

    // ── sequential_pct ───────────────────────────────────────────────────────

    void sequential_is_current_over_previous() {
        double v = 0;
        // META: $7.22 expected for the coming quarter after $10.44 printed last
        // quarter — the numerator is the current figure, not the other way up.
        QVERIFY(sequential_pct(7.22, 10.44, &v));
        QCOMPARE(qRound(v * 10) / 10.0, -30.8);

        QVERIFY(sequential_pct(10.44, 7.22, &v));
        QVERIFY(v > 0); // and the inverse pair reads positive
    }

    /// A loss-making previous quarter must not invert the sign: $0.25 expected
    /// after a -$0.75 quarter is an improvement. A signed denominator would
    /// report that as a decline.
    void loss_base_keeps_the_sign_meaningful() {
        double v = 0;
        QVERIFY(sequential_pct(0.25, -0.75, &v));
        QVERIFY(v > 0);

        QVERIFY(sequential_pct(-1.50, -0.75, &v));
        QVERIFY(v < 0); // a deepening loss is still negative
    }

    void zero_base_is_refused_not_infinite() {
        double v = 123;
        QVERIFY(!sequential_pct(1.50, 0.0, &v));
        QVERIFY(!sequential_pct(1.50, 0.004, &v)); // under half a cent
        QCOMPARE(v, 123.0);                        // untouched on refusal
        QVERIFY(sequential_pct(1.50, 0.01, &v));   // a real cent is fine
    }

    // ── fmt_eps ──────────────────────────────────────────────────────────────

    void formats_sign_outside_the_currency_symbol() {
        QCOMPARE(fmt_eps(1.884), QStringLiteral("$1.88"));
        QCOMPARE(fmt_eps(-0.125), QStringLiteral("-$0.13"));
        QCOMPARE(fmt_eps(0.0), QStringLiteral("$0.00"));
    }
};

QTEST_MAIN(TestEarningsMath)
#include "test_earnings_math.moc"
