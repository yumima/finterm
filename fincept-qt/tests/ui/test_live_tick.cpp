// test_live_tick.cpp — the rules every live price passes through.
//
// apply_live_price: one number (the price) is stored, everything else derives
// from the quote's own previous close. Extended-hours and futures prints are
// taken as (price, price − change) pairs from ONE print, never mixed with a
// close from elsewhere.

#include <QTest>

#include "screens/dashboard/widgets/ExtendedHoursMath.h"
#include "services/markets/IndexFutures.h"
#include "services/markets/LivePrice.h"
#include "ui/tables/TickFlashDelegate.h"

#include <cmath>
#include <limits>

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
struct Q {
    double price = kNaN, change = kNaN, change_pct = kNaN, high = kNaN, low = kNaN, volume = kNaN;
};
double parsed(const QString& s) {
    double v = kNaN;
    return fincept::ui::TickFlashDelegate::parse_number(s, &v) ? v : kNaN;
}
} // namespace

class TestLiveTick : public QObject {
    Q_OBJECT
  private slots:
    void change_derives_from_prev_close() {
        Q q{100, 2, 2.04, 101, 97, 1e6};
        fincept::services::apply_live_price(q, 98.0, 102.0, 2e6);
        QCOMPARE(q.price, 102.0);
        QCOMPARE(q.change, 4.0);
        QVERIFY(std::abs(q.change_pct - 4.0 / 98.0 * 100.0) < 1e-12);
        QCOMPARE(q.high, 102.0); // stretched
        QCOMPARE(q.low, 97.0);
        QCOMPARE(q.volume, 2e6);
    }
    void unknown_baseline_keeps_change() {
        Q q{100, 2, 2.0, 101, 97, 1e6};
        fincept::services::apply_live_price(q, kNaN, 103.0, kNaN);
        QCOMPARE(q.price, 103.0);
        QCOMPARE(q.change, 2.0);
        QCOMPARE(q.volume, 1e6); // no tick volume → kept
    }
    void missing_range_stays_missing() {
        Q q{100, 2, 2.0, 0, kNaN, 1e6};
        fincept::services::apply_live_price(q, 98.0, 103.0, kNaN);
        QCOMPARE(q.high, 0.0); // a tick is one print, not a day range
        QVERIFY(std::isnan(q.low));
    }
    // ── live prints on numbers measured against a close ──────────────────
    void tick_pairs_only_with_its_own_close() {
        using fincept::services::tick_pairs_with;
        // ESZ26.CME on the wire: 7641.5, change 26.25 (float32) → close 7615.25
        QVERIFY(tick_pairs_with(7615.25, 7641.5, double(26.25f)));
        // AAPL overnight print: 355.9 / +1.019989 (float32 noise) vs the 354.88 close
        QVERIFY(tick_pairs_with(354.88, double(355.9f), double(1.019989f)));
        // after the bell: a post print (vs TODAY's close) on a quote still
        // measured against YESTERDAY's
        QVERIFY(!tick_pairs_with(355.36, 355.9, 1.02));
        // ex-dividend reference: 27 cents apart is a different close
        QVERIFY(!tick_pairs_with(355.63, 355.9, 0.54));
        // nothing to pair with
        QVERIFY(!tick_pairs_with(354.88, 355.9, kNaN));
        QVERIFY(!tick_pairs_with(0, 355.9, 1.0));
    }
    void ext_side_by_session() {
        namespace ex = fincept::screens::widgets::exthours;
        QVERIFY(ex::live_ext_side(0, 100.0, 101.0, 1.0) == ex::ExtSide::Pre);
        QVERIFY(ex::live_ext_side(2, 100.0, 99.0, -1.0) == ex::ExtSide::Post);
        // overnight (20:00–04:00 ET), measured against the same close
        QVERIFY(ex::live_ext_side(4, 100.0, 101.0, 1.0) == ex::ExtSide::Overnight);
        QVERIFY(!ex::live_ext_side(1, 100.0, 101.0, 1.0)); // regular session: not an extended print
        QVERIFY(!ex::live_ext_side(2, 100.0, 101.0, 2.0)); // measured against another close
    }
    void ext_tick_moves_only_forward() {
        namespace ex = fincept::screens::widgets::exthours;
        double price = 101.0, pct = 1.0;
        qint64 ts = 1000;
        // older print (a reconnect replay): ignored
        QVERIFY(!ex::apply_ext_tick(100.0, price, pct, ts, 2, 99.0, -1.0, 999'000));
        QCOMPARE(price, 101.0);
        // same price: nothing to do
        QVERIFY(!ex::apply_ext_tick(100.0, price, pct, ts, 2, 101.0, 1.0, 1'001'000));
        // newer post print against this close
        const auto side = ex::apply_ext_tick(100.0, price, pct, ts, 2, 98.0, -2.0, 1'002'000);
        QVERIFY(side == ex::ExtSide::Post);
        QCOMPARE(price, 98.0);
        QCOMPARE(pct, -2.0);
        QCOMPARE(ts, qint64(1002));
        // a print AT the close arrives with change 0 (proto3 omits it; the service reads 0)
        QVERIFY(ex::apply_ext_tick(100.0, price, pct, ts, 2, 100.0, 0.0, 1'003'000));
        QCOMPARE(pct, 0.0);
    }
    void ext_quote_from_one_print() {
        namespace ex = fincept::screens::widgets::exthours;
        // AAPL overnight: 355.90, Yahoo's change +1.02 against the 354.88 close
        const auto f = ex::ext_from_tick(4, 355.9, 1.02, 1794538210000);
        QVERIFY(f && f->side == ex::ExtSide::Overnight);
        QVERIFY(std::abs(f->regular - 354.88) < 1e-9);
        QVERIFY(std::abs(f->pct - 1.02 / 354.88 * 100.0) < 1e-12);
        QVERIFY(!ex::ext_from_tick(1, 355.9, 1.02, 0));  // regular session: not this column
        QVERIFY(!ex::ext_from_tick(2, 355.9, kNaN, 0)); // no change: no pair
    }
    void future_tick_needs_its_contract_and_close() {
        fincept::services::IndexFutureQuote q;
        q.index = "^GSPC";
        q.contract = "ESZ26.CME";
        q.price = 7639.25;
        q.ref_price = 7615.25;
        q.change = 24.0;
        q.pct = 24.0 / 7615.25 * 100.0;
        q.ts = 1794537600;
        using fincept::services::apply_future_tick;
        QVERIFY(!apply_future_tick(q, "ESH27.CME", 7650.0, 34.75, 1794537700000)); // another month
        QVERIFY(!apply_future_tick(q, "ESZ26.CME", 7650.0, 34.75, 1794537500000)); // older than shown
        QVERIFY(!apply_future_tick(q, "ESZ26.CME", 7650.0, kNaN, 1794537700000));  // no pair
        QVERIFY(apply_future_tick(q, "ESZ26.CME", 7641.5, 26.25, 1794537611000));
        QCOMPARE(q.price, 7641.5);
        QCOMPARE(q.change, 26.25);
        QCOMPARE(q.ref_price, 7615.25);
        QVERIFY(std::abs(q.pct - 26.25 / 7615.25 * 100.0) < 1e-12);
        QCOMPARE(q.ts, qint64(1794537611));
        // After the 18:00 ET reopen the print's close rolls with it: price and
        // close are taken as the print's pair, never mixed.
        QVERIFY(apply_future_tick(q, "ESZ26.CME", 7650.0, 9.5, 1794540000000));
        QCOMPARE(q.ref_price, 7640.5);
        // A pending row (quote refused) is filled by its first print.
        fincept::services::IndexFutureQuote p;
        p.index = "^GSPC";
        p.contract = "ESZ26.CME";
        QVERIFY(fincept::services::is_pending(p));
        QVERIFY(apply_future_tick(p, "ESZ26.CME", 7641.5, 26.25, 1794537611000));
        QVERIFY(!fincept::services::is_pending(p));
        QCOMPARE(p.session_date, QDate(2026, 11, 13)); // 21:00 ET Thu → the Fri 13 Nov session
    }
    // The flash's direction is the SIGNED number a cell shows ("▼ 1.25" = -1.25).
    void flash_reads_the_signed_number() {
        QCOMPARE(parsed(QString::fromUtf8("\xe2\x96\xbc 1.25")), -1.25);
        QCOMPARE(parsed(QString::fromUtf8("1.25 \xe2\x96\xbe")), 1.25); // trailing arrow: decoration
        QCOMPARE(parsed("+1.25%"), 1.25);
        QCOMPARE(parsed("-0.35"), -0.35);
        QCOMPARE(parsed("$1,234.50"), 1234.5);
        QCOMPARE(parsed("999.5K"), 999500.0);
        QVERIFY(std::isnan(parsed("--")));
    }
};

QTEST_MAIN(TestLiveTick)
#include "test_live_tick.moc"
