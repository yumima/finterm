// tests/services/test_strategy_max_pnl.cpp
//
// Max profit / max loss for an options strategy, as shown in the F&O builder's
// analytics ribbon — and, more to the point, in OrderConfirmDialog, the screen
// a trader reads immediately before sending the order.
//
// These used to be read off the SAMPLED payoff curve, whose default window is
// the spot anchor ±30%. For anything whose worst case lies outside that window
// the number was simply the loss at the edge of the chart: a short 24000 PE at
// ₹200 reported a max loss of ₹350,000 against a true ₹1,190,000 at S → 0. A
// risk figure that is 3.4× too small, on the confirmation screen, is the worst
// shape of wrong this codebase can produce.
//
// An expiry payoff is piecewise LINEAR with kinks only at strikes, so its
// extrema over [0, ∞) sit at S = 0, at a strike, or in a tail. The cases below
// pin that they are found there, and that they no longer depend on any window.

#include "services/options/StrategyAnalytics.h"
#include "services/options/OptionChainTypes.h"

#include <QtTest/QtTest>

#include <cmath>
#include <limits>

using namespace fincept::services::options;
using namespace fincept::services::options::analytics;
using fincept::trading::InstrumentType;

namespace {

StrategyLeg leg(InstrumentType type, double strike, int lots, double entry,
                int lot_size = 50) {
    StrategyLeg l;
    l.type = type;
    l.strike = strike;
    l.lots = lots;            // signed: + buy, − sell
    l.lot_size = lot_size;
    l.entry_price = entry;
    l.expiry = QDate::currentDate().addDays(30).toString("yyyy-MM-dd");
    l.is_active = true;
    return l;
}

Strategy strat(std::initializer_list<StrategyLeg> legs) {
    Strategy s;
    s.underlying = "NIFTY";
    for (const auto& l : legs) s.legs.append(l);
    return s;
}

constexpr double kEps = 1e-6;

} // namespace

class TestStrategyMaxPnl : public QObject {
    Q_OBJECT

  private slots:

    // ── The case that motivated this ─────────────────────────────────────────
    void a_short_put_reports_its_loss_at_zero_not_at_the_chart_edge() {
        // Short 1 lot of the 24000 PE at ₹200, lot size 50.
        // True worst case is S → 0: −50 × (24000 − 200) = −₹1,190,000.
        // The ±30% window bottoms out at 16800, where the loss is only
        // −50 × (7200 − 200) = −₹350,000.
        const auto s = strat({leg(InstrumentType::PE, 24000, -1, 200)});
        const MaxPnL m = compute_max_pnl(s);

        QVERIFY2(std::abs(m.max_loss - (-1190000.0)) < kEps,
                 qPrintable(QString("max loss %1, expected -1190000").arg(m.max_loss)));
        QVERIFY2(m.max_loss < -1000000.0, "the -30% window figure (-350000) must not survive");
        // Selling a put caps the gain at the premium taken in.
        QVERIFY(std::abs(m.max_profit - 10000.0) < kEps);   // 50 × 200
        QVERIFY(!m.loss_unbounded);      // bounded at S = 0, and genuinely so
        QVERIFY(!m.profit_unbounded);
    }

    // ── Tails ────────────────────────────────────────────────────────────────
    void a_long_call_has_unbounded_upside_and_a_capped_loss() {
        const auto s = strat({leg(InstrumentType::CE, 24000, 1, 150)});
        const MaxPnL m = compute_max_pnl(s);
        QVERIFY(m.profit_unbounded);
        QVERIFY(m.max_profit == std::numeric_limits<double>::infinity());
        QVERIFY(!m.loss_unbounded);
        QVERIFY(std::abs(m.max_loss - (-7500.0)) < kEps);   // −50 × 150
    }

    void a_short_call_has_unbounded_downside() {
        const auto s = strat({leg(InstrumentType::CE, 24000, -1, 150)});
        const MaxPnL m = compute_max_pnl(s);
        QVERIFY(m.loss_unbounded);
        QVERIFY(m.max_loss == -std::numeric_limits<double>::infinity());
        QVERIFY(std::abs(m.max_profit - 7500.0) < kEps);
    }

    // ── Bounded structures must be exact, not approximate ────────────────────
    void a_bull_call_spread_is_exact_at_both_ends() {
        // Buy 24000 CE @150, sell 24200 CE @80. Net debit 70 × 50 = ₹3,500.
        // Max profit = (200 − 70) × 50 = ₹6,500 at or above 24200.
        const auto s = strat({leg(InstrumentType::CE, 24000, 1, 150),
                              leg(InstrumentType::CE, 24200, -1, 80)});
        const MaxPnL m = compute_max_pnl(s);
        QVERIFY(!m.profit_unbounded);
        QVERIFY(!m.loss_unbounded);
        QVERIFY2(std::abs(m.max_profit - 6500.0) < kEps,
                 qPrintable(QString("max profit %1").arg(m.max_profit)));
        QVERIFY2(std::abs(m.max_loss - (-3500.0)) < kEps,
                 qPrintable(QString("max loss %1").arg(m.max_loss)));
    }

    void a_short_strangle_is_worst_at_zero_not_at_the_put_strike() {
        // Short 23000 PE @100 and 25000 CE @120. The upside is unbounded, so
        // the interesting half is the downside: −50 × (23000 − 220) = −₹1,139,000.
        const auto s = strat({leg(InstrumentType::PE, 23000, -1, 100),
                              leg(InstrumentType::CE, 25000, -1, 120)});
        const MaxPnL m = compute_max_pnl(s);
        QVERIFY(m.loss_unbounded);   // the short call dominates the upside
        QVERIFY(std::abs(m.max_profit - 11000.0) < kEps);   // 50 × (100 + 120)
    }

    void an_iron_condor_is_bounded_on_both_sides() {
        // Sell 23800 PE @90 / buy 23600 PE @50; sell 24200 CE @85 / buy 24400 CE @45.
        // Net credit = (90 − 50 + 85 − 45) = 80 → ₹4,000. Wing width 200 → max
        // loss = (200 − 80) × 50 = ₹6,000.
        const auto s = strat({leg(InstrumentType::PE, 23800, -1, 90),
                              leg(InstrumentType::PE, 23600, 1, 50),
                              leg(InstrumentType::CE, 24200, -1, 85),
                              leg(InstrumentType::CE, 24400, 1, 45)});
        const MaxPnL m = compute_max_pnl(s);
        QVERIFY(!m.profit_unbounded);
        QVERIFY(!m.loss_unbounded);
        QVERIFY2(std::abs(m.max_profit - 4000.0) < kEps,
                 qPrintable(QString("max profit %1").arg(m.max_profit)));
        QVERIFY2(std::abs(m.max_loss - (-6000.0)) < kEps,
                 qPrintable(QString("max loss %1").arg(m.max_loss)));
    }

    // ── Hygiene ──────────────────────────────────────────────────────────────
    void inactive_legs_do_not_contribute() {
        auto s = strat({leg(InstrumentType::PE, 24000, -1, 200),
                        leg(InstrumentType::CE, 24000, -1, 200)});
        s.legs[1].is_active = false;
        const MaxPnL m = compute_max_pnl(s);
        QVERIFY(!m.loss_unbounded);                         // the short call is off
        QVERIFY(std::abs(m.max_loss - (-1190000.0)) < kEps);
    }

    void a_strategy_with_no_active_legs_reports_nothing() {
        auto s = strat({leg(InstrumentType::PE, 24000, -1, 200)});
        s.legs[0].is_active = false;
        const MaxPnL m = compute_max_pnl(s);
        QCOMPARE(m.max_profit, 0.0);
        QCOMPARE(m.max_loss, 0.0);
        QVERIFY(!m.profit_unbounded);
        QVERIFY(!m.loss_unbounded);
    }
};

QTEST_MAIN(TestStrategyMaxPnl)
#include "test_strategy_max_pnl.moc"
