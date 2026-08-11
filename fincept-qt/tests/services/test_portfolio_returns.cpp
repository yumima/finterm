// tests/services/test_portfolio_returns.cpp
//
// Pins the time-weighted return convention. The defining scenario is the one
// that motivated the engine: a flat market plus a mid-window deposit used to
// read as a gain, because period return was (NAV_now − NAV_start)/NAV_start
// with no cash-flow adjustment. TWR must report 0% for that portfolio, and
// must report the same growth rate for a small and a large account holding
// the same positions.

#include "services/portfolio/PortfolioReturns.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace fincept::portfolio;

namespace {

int seq = 0;

PortfolioSnapshot snap(const QString& date, double value) {
    PortfolioSnapshot s;
    s.snapshot_date = date;
    s.total_value = value;
    s.source = QStringLiteral("live");
    return s;
}

Transaction txn(const QString& type, double qty, double price, const QString& date) {
    Transaction t;
    t.id = QStringLiteral("t%1").arg(++seq, 4, 10, QLatin1Char('0'));
    t.transaction_type = type;
    t.quantity = qty;
    t.price = price;
    t.transaction_date = date;
    t.created_at = t.id;
    return t;
}

} // namespace

class TestPortfolioReturns : public QObject {
    Q_OBJECT

  private slots:
    void plain_growth_no_flows();
    void deposit_into_flat_market_is_zero_return();
    void withdrawal_from_flat_market_is_zero_return();
    void deposit_plus_growth_reports_only_growth();
    void gain_value_nets_out_flows();
    void dividends_are_not_flows();
    void flows_before_window_are_ignored();
    void weekend_flow_lands_in_next_segment();
    void live_point_extends_last_segment();
    void live_point_on_snapshot_date_replaces_it();
    void zero_base_segment_degrades_not_invents();
    void too_little_data_is_invalid();
    void flow_adjusted_series_strips_deposits();
    void flow_adjusted_series_marks_bad_segments_nan();
    void dust_base_is_not_a_return();
    void fabricated_opening_date_is_not_a_flow();
    void real_opening_date_remains_a_flow();
    void foreign_flow_converts_to_the_nav_currency();
    void unmapped_symbol_is_treated_as_same_currency();
};

void TestPortfolioReturns::plain_growth_no_flows() {
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 110.0), snap("2026-01-03", 121.0)},
        121.0, "2026-01-03", {});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct - 21.0) < 1e-9);
    QCOMPARE(r.gain_value, 21.0);
    QCOMPARE(r.net_external_flow, 0.0);
}

void TestPortfolioReturns::deposit_into_flat_market_is_zero_return() {
    // NAV jumps 100 → 200 because the user bought $100 more stock; prices
    // never moved. The old formula reported +100%.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 200.0), snap("2026-01-03", 200.0)},
        200.0, "2026-01-03", {txn("BUY", 10, 10.0, "2026-01-02")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);
    QCOMPARE(r.gain_value, 0.0);
    QCOMPARE(r.net_external_flow, 100.0);
}

void TestPortfolioReturns::withdrawal_from_flat_market_is_zero_return() {
    const auto r = compute_period_return(
        {snap("2026-01-01", 200.0), snap("2026-01-02", 100.0)},
        100.0, "2026-01-02", {txn("SELL", 10, 10.0, "2026-01-02")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);
    QCOMPARE(r.gain_value, 0.0);
    QCOMPARE(r.net_external_flow, -100.0);
}

void TestPortfolioReturns::deposit_plus_growth_reports_only_growth() {
    // Day 1: 100 → 110 (+10%). End of day 2: +100 deposit and the whole 210
    // then grows 10% to 231 by day 3.
    // Segment returns: +10%, (210−100−110)/110 = 0%, +10% → chained +21%.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 110.0), snap("2026-01-03", 210.0),
         snap("2026-01-04", 231.0)},
        231.0, "2026-01-04", {txn("BUY", 10, 10.0, "2026-01-03")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct - 21.0) < 1e-6);
}

void TestPortfolioReturns::gain_value_nets_out_flows() {
    // 100 → 260 with a 100 deposit on the way: the market made 60, not 160.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 210.0), snap("2026-01-03", 260.0)},
        260.0, "2026-01-03", {txn("BUY", 10, 10.0, "2026-01-02")});
    QVERIFY(r.valid);
    QCOMPARE(r.gain_value, 60.0);
}

void TestPortfolioReturns::dividends_are_not_flows() {
    // The NAV holds no cash, so a cash dividend never appears in it; counting
    // it as a withdrawal would fabricate a positive return in a flat market.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 100.0)},
        100.0, "2026-01-02", {txn("DIVIDEND", 100, 0.5, "2026-01-02")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);
    QCOMPARE(r.net_external_flow, 0.0);
}

void TestPortfolioReturns::flows_before_window_are_ignored() {
    // The opening buy is embedded in the baseline snapshot; stripping it
    // again would double-count.
    const auto r = compute_period_return(
        {snap("2026-01-05", 100.0), snap("2026-01-06", 110.0)},
        110.0, "2026-01-06", {txn("BUY", 10, 10.0, "2026-01-05")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct - 10.0) < 1e-9);
    QCOMPARE(r.net_external_flow, 0.0);
}

void TestPortfolioReturns::weekend_flow_lands_in_next_segment() {
    // Friday snapshot, Saturday buy, Monday snapshot: the flow belongs to the
    // Friday→Monday segment.
    const auto r = compute_period_return(
        {snap("2026-01-02", 100.0), snap("2026-01-05", 200.0)},
        200.0, "2026-01-05", {txn("BUY", 10, 10.0, "2026-01-03")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);
    QCOMPARE(r.net_external_flow, 100.0);
}

void TestPortfolioReturns::live_point_extends_last_segment() {
    // A buy after the last snapshot but before "now" must be stripped from
    // the live segment.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 100.0)},
        200.0, "2026-01-03", {txn("BUY", 10, 10.0, "2026-01-03")});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);
}

void TestPortfolioReturns::live_point_on_snapshot_date_replaces_it() {
    // Today's snapshot row exists (build_summary wrote it this morning) but
    // the live NAV has moved since; the live value wins, no phantom segment.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 105.0)},
        110.0, "2026-01-02", {});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct - 10.0) < 1e-9);
}

void TestPortfolioReturns::zero_base_segment_degrades_not_invents() {
    const auto r = compute_period_return(
        {snap("2026-01-01", 0.0), snap("2026-01-02", 100.0), snap("2026-01-03", 110.0)},
        110.0, "2026-01-03", {});
    QVERIFY(r.valid);     // the 100 → 110 segment is computable
    QVERIFY(r.degraded);  // the 0 → 100 segment is not, and says so
    QVERIFY(std::abs(r.twr_pct - 10.0) < 1e-9);
}

void TestPortfolioReturns::too_little_data_is_invalid() {
    const auto r = compute_period_return({}, 100.0, "2026-01-03", {});
    QVERIFY(!r.valid);
    const auto r2 = compute_period_return({snap("2026-01-03", 100.0)}, 100.0, "2026-01-03", {});
    QVERIFY(!r2.valid); // live point collapses onto the only snapshot
}

void TestPortfolioReturns::flow_adjusted_series_strips_deposits() {
    // Day 1: +10% market. Day 2: flat market, $100 deposit. Raw differences
    // would read the deposit day as +90.9% and hand volatility, Sharpe, VaR
    // and beta a fictional shock.
    const auto r = flow_adjusted_returns(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 110.0), snap("2026-01-03", 210.0)},
        {txn("BUY", 10, 10.0, "2026-01-03")});
    QCOMPARE(r.size(), 2);
    QVERIFY(std::abs(r[0] - 10.0) < 1e-9);
    QVERIFY(std::abs(r[1]) < 1e-9);
}

void TestPortfolioReturns::flow_adjusted_series_marks_bad_segments_nan() {
    const auto r = flow_adjusted_returns(
        {snap("2026-01-01", 0.0), snap("2026-01-02", 100.0), snap("2026-01-03", 110.0)}, {});
    QCOMPARE(r.size(), 2);
    QVERIFY(std::isnan(r[0]));
    QVERIFY(std::abs(r[1] - 10.0) < 1e-9);
}

void TestPortfolioReturns::dust_base_is_not_a_return() {
    // A liquidation can leave a float-residue NAV (1e-9). The next funding
    // day must not compute (10050 − 10000 − 1e-9)/1e-9 ≈ 5e14 % and let it
    // sail past the NaN filter into every risk statistic.
    const auto r = flow_adjusted_returns(
        {snap("2026-01-01", 1e-9), snap("2026-01-02", 10050.0)},
        {txn("BUY", 100, 100.0, "2026-01-02")});
    QCOMPARE(r.size(), 1);
    QVERIFY(std::isnan(r[0]));
}

void TestPortfolioReturns::fabricated_opening_date_is_not_a_flow() {
    // v049 dates a migrated position's opening BUY at migration time when
    // first_purchase_date was empty. The live snapshots already contained the
    // position's value, so counting that row as a flow fabricates a crash.
    // Fabrication is detectable: the synthesis marker plus a transaction date
    // on the same day the row was created.
    auto opening = txn("BUY", 100, 500.0, "2026-01-02");
    opening.notes = QStringLiteral("Opening balance — synthesized from the holdings row (v049)");
    opening.created_at = QStringLiteral("2026-01-02 12:00:00");
    const auto r = compute_period_return(
        {snap("2026-01-01", 100000.0), snap("2026-01-03", 100000.0)},
        100000.0, "2026-01-03", {opening});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9); // flat market stays flat
    QCOMPARE(r.net_external_flow, 0.0);
}

void TestPortfolioReturns::real_opening_date_remains_a_flow() {
    // A synthesized opening whose date is genuine (recorded long before the
    // migration ran) strips like any real purchase: reconstructed NAV jumps
    // at that date, and the flow must cancel the jump.
    auto opening = txn("BUY", 100, 500.0, "2026-01-02");
    opening.notes = QStringLiteral("Opening balance — synthesized from the holdings row (v049)");
    opening.created_at = QStringLiteral("2026-08-01 12:00:00"); // months later
    const auto r = compute_period_return(
        {snap("2026-01-01", 100000.0), snap("2026-01-03", 150000.0)},
        150000.0, "2026-01-03", {opening});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9); // the 50k jump is the buy, not growth
    QCOMPARE(r.net_external_flow, 50000.0);
}

void TestPortfolioReturns::foreign_flow_converts_to_the_nav_currency() {
    // NAV is in the PORTFOLIO currency; trade cash is in the INSTRUMENT
    // currency. A 10,000 CAD purchase at 0.73 raises a USD NAV by 7,300 —
    // stripping the unconverted 10,000 would fabricate a ~2,700 loss on a
    // day nothing moved.
    auto buy = txn("BUY", 100, 100.0, "2026-01-02"); // 10,000 CAD
    buy.symbol = QStringLiteral("RY.TO");
    const QHash<QString, double> fx{{QStringLiteral("RY.TO"), 0.73}};

    const auto r = compute_period_return(
        {snap("2026-01-01", 50000.0), snap("2026-01-03", 57300.0)},
        57300.0, "2026-01-03", {buy}, fx);
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);          // flat market stays flat
    QCOMPARE(r.net_external_flow, 7300.0);        // USD, not 10,000 CAD
    QCOMPARE(r.gain_value, 0.0);

    // The same series WITHOUT the map is the bug this pins: the flow is
    // over-stripped and a loss appears out of nowhere.
    const auto unconverted = compute_period_return(
        {snap("2026-01-01", 50000.0), snap("2026-01-03", 57300.0)},
        57300.0, "2026-01-03", {buy});
    QVERIFY(unconverted.twr_pct < -5.0);
}

void TestPortfolioReturns::unmapped_symbol_is_treated_as_same_currency() {
    // A single-currency book passes no map at all, and must be unaffected.
    const auto r = compute_period_return(
        {snap("2026-01-01", 100.0), snap("2026-01-02", 200.0)},
        200.0, "2026-01-02", {txn("BUY", 10, 10.0, "2026-01-02")}, {});
    QVERIFY(r.valid);
    QVERIFY(std::abs(r.twr_pct) < 1e-9);
    QCOMPARE(r.net_external_flow, 100.0);
}

QTEST_GUILESS_MAIN(TestPortfolioReturns)
#include "test_portfolio_returns.moc"
