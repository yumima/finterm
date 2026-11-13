// test_live_tick.cpp — the two rules every live price passes through.
//
// apply_live_price: one number (the price) is stored, everything else derives
// from the quote's own previous close. TickFlashDelegate::parse_number: the
// flash direction is the SIGNED value a cell shows — MarketPanel writes the
// change as "▼ 1.25", and reading that as +1.25 flashed a deepening fall green.

#include <QTest>

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
    void down_arrow_is_the_sign() {
        QCOMPARE(parsed(QString::fromUtf8("\xe2\x96\xbc 1.25")), -1.25);
        QCOMPARE(parsed(QString::fromUtf8("\xe2\x96\xb2 1.25")), 1.25);
        QCOMPARE(parsed(QString::fromUtf8("\xe2\x96\xbc" "0.35%")), -0.35);
        // a deepening fall reads as DOWN, a cross above zero as UP
        QVERIFY(parsed(QString::fromUtf8("\xe2\x96\xbc 1.30")) < parsed(QString::fromUtf8("\xe2\x96\xbc 1.25")));
        QVERIFY(parsed(QString::fromUtf8("\xe2\x96\xb2 0.05")) > parsed(QString::fromUtf8("\xe2\x96\xbc 0.10")));
        // only a LEADING arrow is a sign; a trailing one is decoration
        QCOMPARE(parsed(QString::fromUtf8("1.25 \xe2\x96\xbe")), 1.25);
    }
    void plain_numbers() {
        QCOMPARE(parsed("+1.25%"), 1.25);
        QCOMPARE(parsed("-0.35"), -0.35);
        QCOMPARE(parsed("$1,234.50"), 1234.5);
        QCOMPARE(parsed("999.5K"), 999500.0);
        QCOMPARE(parsed("1.0M"), 1e6);
        QVERIFY(std::isnan(parsed("--")));
    }
};

QTEST_MAIN(TestLiveTick)
#include "test_live_tick.moc"
