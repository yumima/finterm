// tests/screens/test_extended_hours_math.cpp
//
// The AFT% column reports which way a holding moved after the bell, and a sign
// error there is not a degraded answer — it is the opposite one. AAPL fell
// 6.5% after its print and the dashboard showed it up 6.3%; the arithmetic
// that produced that was unremarkable to read, which is why these cases exist.
//
// The fixtures below are real `extended_hours` payloads captured from the
// daemon on 30 Jul 2026 after the close, not invented numbers.

#include "screens/dashboard/widgets/ExtendedHoursMath.h"

#include <QtTest/QtTest>

using namespace fincept::screens::widgets::exthours;

namespace {

/// Captured verbatim: AAPL reported and sold off, AMZN reported and rallied.
// Real bar times from the same capture: the close printed at 16:00 ET and the
// last post-market bar at 19:59 ET on 30 Jul 2026.
constexpr qint64 kClose30Jul = 1785441600;   // 16:00 ET
constexpr qint64 kPost30Jul  = 1785455940;   // 19:59 ET
constexpr qint64 kNow        = 1785470000;   // shortly after, same evening

QJsonObject row(const QString& symbol, double regular, double pre, double post,
                const QString& session = QStringLiteral("CLOSED")) {
    QJsonObject o;
    o.insert("symbol", symbol);
    o.insert("regular", regular);
    o.insert("pre_market", pre);
    o.insert("post_market", post);
    o.insert("session", session);
    o.insert("regular_ts", static_cast<qint64>(kClose30Jul));
    o.insert("post_market_ts", static_cast<qint64>(kPost30Jul));
    o.insert("pre_market_ts", static_cast<qint64>(kClose30Jul - 8 * 3600));
    return o;
}

QJsonObject aapl() { return row("AAPL", 334.16, 333.06, 312.33); }
QJsonObject amzn() { return row("AMZN", 235.49, 233.30, 257.95); }
QJsonObject msft() { return row("MSFT", 451.50, 438.44, 448.47); }

} // namespace

class TestExtendedHoursMath : public QObject {
    Q_OBJECT

  private slots:
    // The sign, on the real numbers, stated as plainly as possible.
    void direction_matches_the_market() {
        const auto a = quote_from_row(aapl(), /*in_pre_session=*/false);
        QVERIFY(a.has_value());
        QVERIFY2(a->pct < 0, qPrintable(QString("AAPL fell after the bell but read %1%")
                                            .arg(a->pct)));
        QVERIFY(std::abs(a->pct - (-6.533)) < 0.01);

        const auto z = quote_from_row(amzn(), false);
        QVERIFY(z.has_value());
        QVERIFY2(z->pct > 0, qPrintable(QString("AMZN rose after the bell but read %1%")
                                            .arg(z->pct)));
        QVERIFY(std::abs(z->pct - 9.538) < 0.01);

        const auto m = quote_from_row(msft(), false);
        QVERIFY(m.has_value());
        QVERIFY(m->pct < 0);
    }

    // The specific inversion that shipped: dividing by the extended price
    // instead of the close. It doesn't just rescale — it flips the sign, and
    // the magnitude stays plausible enough to look like a real reading.
    void the_close_is_the_base_not_the_extended_price() {
        const auto a = quote_from_row(aapl(), false);
        QVERIFY(a.has_value());
        const double inverted = (a->regular - a->price) / a->price * 100.0;
        QVERIFY2(inverted > 0, "fixture no longer demonstrates the inversion");
        QVERIFY2(a->pct < 0, "the leg divided by the extended price again");
        // Both readings are ~6.5% in magnitude, which is exactly why the wrong
        // one survived review: nothing about it looks out of range.
        QVERIFY(std::abs(std::abs(inverted) - std::abs(a->pct)) < 1.0);
    }

    // Pre-market belongs to the PREVIOUS close. Pairing it with today's close
    // after the session has ended computes the day's move backwards — which is
    // what put −7.82% beside AMZN on a day it closed up 3.9%.
    void pre_market_is_never_paired_with_a_post_session_close() {
        QJsonObject o = amzn();
        o.insert("post_market", QJsonValue());   // no post print yet
        const auto q = quote_from_row(o, /*in_pre_session=*/false);
        QVERIFY2(!q.has_value(), "fell back to pre-market against the wrong close");
    }

    // Before the open the fallback IS correct: yesterday's post-market and
    // today's pre-market both reference the same close.
    void before_the_open_the_prior_post_market_is_a_valid_fallback() {
        QJsonObject o = row("AAPL", 334.16, 0, 312.33, QStringLiteral("PRE"));
        o.insert("pre_market", QJsonValue());
        const auto q = quote_from_row(o, true);
        QVERIFY(q.has_value());
        QVERIFY(!q->from_pre);
        QVERIFY(q->pct < 0);

        // With a pre-market print present it wins, and is labelled as such.
        const auto with_pre = quote_from_row(row("AAPL", 334.16, 340.00, 312.33,
                                                 QStringLiteral("PRE")), true);
        QVERIFY(with_pre.has_value());
        QVERIFY(with_pre->from_pre);
        QVERIFY(with_pre->pct > 0);
    }

    // A null extended print is "no trades", not a zero move.
    void missing_prices_yield_no_quote() {
        QJsonObject o = row("FNILX", 26.17, 0, 0);
        o.insert("pre_market", QJsonValue());
        o.insert("post_market", QJsonValue());
        QVERIFY(!quote_from_row(o, false).has_value());

        QJsonObject no_close = aapl();
        no_close.insert("regular", QJsonValue());
        QVERIFY(!quote_from_row(no_close, false).has_value());

        QJsonObject zero_close = aapl();
        zero_close.insert("regular", 0.0);
        QVERIFY(!quote_from_row(zero_close, false).has_value());
    }

    // THE regression this guard exists to prevent. A previous version compared
    // the daemon's close against the price the row was quoting and dropped the
    // pair when they differed. But the quote feed rolls over to extended-hours
    // prices, so for a stock that moved after the bell the row price IS the
    // extended print — AAPL's row read $312.33 against a $334.16 close — and
    // the "mismatch" being measured was the after-hours move itself. The guard
    // hid every symbol that moved, and only those.
    void a_big_move_is_never_suppressed_for_being_a_big_move() {
        for (const auto& r : {aapl(), amzn()}) {
            const auto q = quote_from_row(r, false);
            QVERIFY(q.has_value());
            QVERIFY2(pairing_is_sound(*q, kNow),
                     qPrintable(QString("suppressed a %1% move").arg(q->pct)));
        }
    }

    // What a real mismatch looks like: the close is from days ago, so the
    // percentage against it is a multi-day return, not an after-hours move.
    void a_stale_close_is_rejected() {
        QJsonObject o = aapl();
        o.insert("regular_ts", static_cast<qint64>(kClose30Jul - 9 * 86400));
        const auto q = quote_from_row(o, false);
        QVERIFY(q.has_value());
        QVERIFY(!pairing_is_sound(*q, kNow));
    }

    // The extended print must come after the close it is measured against.
    void an_extended_print_before_the_close_is_rejected() {
        QJsonObject o = aapl();
        o.insert("post_market_ts", static_cast<qint64>(kClose30Jul - 3600));
        const auto q = quote_from_row(o, false);
        QVERIFY(q.has_value());
        QVERIFY(!pairing_is_sound(*q, kNow));
    }

    // Mid-session, `regular` is still YESTERDAY's close — today's bell hasn't
    // rung — so this morning's pre-market print is both correctly paired
    // against it and the most recent extended move there is. Preferring
    // yesterday's post-market instead left a day-old number in a column whose
    // tooltip promises the last one.
    void during_the_regular_session_this_mornings_pre_market_wins() {
        QJsonObject o = row("AAPL", 334.16, 340.00, 312.33,
                            QStringLiteral("REGULAR"));
        // Mid-session the daemon's reference is the PREVIOUS close, so the
        // timestamps have to say so too — otherwise this morning's pre-market
        // print predates its own reference and the soundness check drops it.
        o.insert("regular_ts", static_cast<qint64>(kClose30Jul - 86400));
        o.insert("pre_market_ts", static_cast<qint64>(kClose30Jul - 8 * 3600));

        const auto q = quote_from_row(o, /*in_pre_session=*/false);
        QVERIFY(q.has_value());
        QVERIFY2(q->from_pre, "fell back to yesterday's post-market mid-session");
        QCOMPARE(q->price, 340.00);
        QVERIFY(q->pct > 0);
        QVERIFY2(pairing_is_sound(*q, kNow),
                 "the mid-session pre-market pairing was rejected as unsound");
    }

    // …and with no pre-market print yet, yesterday's post-market is still a
    // valid answer mid-session: it references the same close.
    void the_regular_session_still_falls_back_to_the_prior_post_market() {
        QJsonObject o = row("AAPL", 334.16, 0, 312.33, QStringLiteral("REGULAR"));
        o.insert("pre_market", QJsonValue());
        const auto q = quote_from_row(o, false);
        QVERIFY(q.has_value());
        QVERIFY(!q->from_pre);
        QCOMPARE(q->price, 312.33);
    }

    // An explicit null ext_price is the daemon withholding a pairing it judged
    // dishonest — a reference bar from an earlier day. This function rebuilds
    // the percentage from the raw prints and never reads ext_price, so without
    // this it reconstructed the exact number that was withheld, and
    // pairing_is_sound waved it through: a two-day-old close is well inside
    // the five-day tolerance.
    void an_explicitly_withheld_extended_price_is_not_recomputed() {
        QJsonObject o = row("FNILX", 30.00, 0, 27.90);
        o.insert("pre_market", QJsonValue());
        o.insert("regular_ts", static_cast<qint64>(kClose30Jul - 2 * 86400));
        o.insert("ext_price", QJsonValue());   // daemon: no honest pairing here
        QVERIFY(!quote_from_row(o, false).has_value());

        // Sanity: the fixture really would have produced a number, and the
        // timestamp guard really would not have stopped it.
        QJsonObject without_the_field = o;
        without_the_field.remove("ext_price");
        const auto q = quote_from_row(without_the_field, false);
        QVERIFY(q.has_value());
        QVERIFY(std::abs(q->pct - (-7.0)) < 0.01);
        QVERIFY(pairing_is_sound(*q, kNow));
    }

    // Silence is not evidence of a bad pairing: a payload without times is
    // shown rather than dropped.
    void a_payload_without_timestamps_is_not_judged() {
        QJsonObject o = aapl();
        o.remove("regular_ts");
        o.remove("post_market_ts");
        const auto q = quote_from_row(o, false);
        QVERIFY(q.has_value());
        QVERIFY(pairing_is_sound(*q, kNow));
    }
};

QTEST_MAIN(TestExtendedHoursMath)
#include "test_extended_hours_math.moc"
