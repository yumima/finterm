// Market-condition readings (services/markets/MarketConditions.h).
//
// Pinned: RSI / ADX agree with the `ta` library on the same bars (the
// reference the technicals tab is checked against); a percentile is NaN when
// the history does not span its window; the regime thresholds; correlation
// pairs only the dates both series have and uses differences for yields;
// FRED keys parse strictly; YoY needs the exact year-ago observation.

#include "services/markets/MarketConditions.h"

#include <QtTest/QtTest>

#include <cmath>

using namespace finterm::services::conditions;

namespace {

QVector<Bar> wave(int n, const QDate& start = QDate(2020, 1, 1)) {
    QVector<Bar> b;
    for (int i = 0; i < n; ++i) {
        const double c = 100 + 10 * std::sin(i / 7.0) + 0.3 * i;
        b.append({start.addDays(i), c + 1.0 + 0.5 * std::cos(i / 3.0), c - 1.0 - 0.5 * std::sin(i / 5.0), c});
    }
    return b;
}

QVector<DatedValue> series(const QVector<double>& xs, const QDate& start = QDate(2024, 1, 1)) {
    QVector<DatedValue> out;
    for (int i = 0; i < xs.size(); ++i)
        out.append({start.addDays(i), xs[i]});
    return out;
}

} // namespace

class TestMarketConditions : public QObject {
    Q_OBJECT
  private slots:
    void rsi_and_adx_match_reference() {
        // ta 0.11 on the same 120 bars: ADX 40.384, +DI 10.584, −DI 25.689, RSI 28.334.
        const auto b = wave(120);
        const Adx a = adx(b, 14);
        QVERIFY(std::abs(a.adx - 40.384) < 0.5);
        QVERIFY(std::abs(a.plus_di - 10.584) < 0.5);
        QVERIFY(std::abs(a.minus_di - 25.689) < 0.5);
        QVERIFY(std::abs(rsi(b, 14) - 28.334) < 0.5);
        QCOMPARE(classify(a), Regime::TrendDown); // ADX ≥ 25, −DI above +DI
    }

    void too_short_is_nan() {
        const auto b = wave(20);
        QVERIFY(std::isnan(adx(b, 14).adx));
        QVERIFY(std::isnan(sma(b, 50)));
        const Stats s = compute(b);
        QVERIFY(s.valid());
        QVERIFY(std::isnan(s.sma200));
        QVERIFY(std::isnan(s.vs_sma200_pct));
        QCOMPARE(s.regime, Regime::Unknown);
    }

    void percentile_needs_the_window() {
        // 200 calendar days of bars: no 1-year rank, let alone 5.
        QVERIFY(std::isnan(compute(wave(200)).pctile_1y));
        // A rising series spanning > 1 year: the last close is the highest.
        QVector<Bar> up;
        for (int i = 0; i < 400; ++i)
            up.append({QDate(2024, 1, 1).addDays(i), kNaN, kNaN, 10.0 + i});
        const Stats s = compute(up);
        QVERIFY(s.pctile_1y > 99.0);
        QVERIFY(std::isnan(s.pctile_5y));
        QVERIFY(s.vs_sma200_pct > 0);
    }

    void a_spliced_history_is_a_break() {
        QVector<Bar> b;
        for (int i = 0; i < 300; ++i)
            b.append({QDate(2024, 1, 1).addDays(i), kNaN, kNaN, 90.0 + (i % 7)});
        QVERIFY(!find_break(b).found());
        b.append({QDate(2024, 1, 1).addDays(300), kNaN, kNaN, 3539.75}); // QO=F's today
        const Break k = find_break(b);
        QVERIFY(k.found());
        QCOMPARE(k.date, QDate(2024, 1, 1).addDays(300));
        // A real −50% day is not a break; a −80% one is.
        QVector<Bar> c = {{QDate(2024, 1, 1), kNaN, kNaN, 100}, {QDate(2024, 1, 2), kNaN, kNaN, 50}};
        QVERIFY(!find_break(c).found());
        c.append({QDate(2024, 1, 3), kNaN, kNaN, 10});
        QVERIFY(find_break(c).found());
    }

    void live_reading_ignores_float_noise() {
        QVector<DatedValue> c;
        for (int i = 0; i < 400; ++i)
            c.append({QDate(2025, 1, 1).addDays(i), 589.4500122070312});
        // Saturday: the quote is Friday's close, without the float32 noise.
        QVERIFY(!live_reading(c, 589.45, c.last().date.addDays(1)).live);
    }

    void live_reading_uses_the_current_price() {
        QVector<DatedValue> c;
        for (int i = 0; i < 400; ++i)
            c.append({QDate(2025, 9, 1).addDays(i), 100.0}); // flat at 100, > 1 year
        const QDate last = c.last().date;
        // Bars stop yesterday; the live price is today's session: appended.
        auto r = live_reading(c, 90.0, last.addDays(1));
        QVERIFY(r.live);
        QVERIFY(std::abs(r.vs_sma200_pct - (90.0 / ((199 * 100.0 + 90) / 200) - 1) * 100) < 1e-9);
        QVERIFY(r.pctile_1y < 1.0); // the lowest close of the year
        // Bars include today (dated today or later): the last close is replaced.
        r = live_reading(c, 110.0, last);
        QVERIFY(r.live);
        QCOMPARE(r.last, 110.0);
        // A weekend: the quote still equals the last close — nothing added.
        r = live_reading(c, 100.0, last.addDays(2));
        QVERIFY(!r.live);
        // No live price (stale): completed sessions only.
        r = live_reading(c, kNaN, last.addDays(1));
        QVERIFY(!r.live);
        QCOMPARE(r.vs_sma200_pct, 0.0);
    }

    void percent_rank_ties_count_half() {
        QCOMPARE(percent_rank({1, 2, 3, 4}, 4), 87.5);
        QCOMPARE(percent_rank({5, 5}, 5), 50.0);
        QVERIFY(std::isnan(percent_rank({}, 1)));
    }

    void regime_thresholds() {
        QCOMPARE(classify({30, 30, 10}), Regime::TrendUp);
        QCOMPARE(classify({15, 30, 10}), Regime::Range);
        QCOMPARE(classify({22, 30, 10}), Regime::Mixed);
        QCOMPARE(classify({}), Regime::Unknown);
    }

    void unsorted_and_duplicate_bars_are_dropped() {
        auto b = wave(30);
        b.insert(10, b[5]); // an older session out of order
        const Stats s = compute(b);
        QCOMPARE(s.closes.size(), 30);
        QVERIFY(compute(wave(500)).closes.size() == kKeepCloses);
    }

    void correlation_pairs_common_dates() {
        // b is exactly 2·a on every common date; a also has extra (weekend) days.
        QVector<double> av, bv;
        for (int i = 0; i < 80; ++i)
            av.append(100 + std::sin(i) * 5 + i);
        auto a = series(av);
        QVector<DatedValue> b;
        for (const auto& x : a)
            if (x.date.dayOfWeek() < 6)
                b.append({x.date, 2 * x.value});
        const double r = correlation(a, false, b, false, 30);
        QVERIFY(std::isfinite(r));
        // Levels are matched by date first: each change spans the same two
        // common sessions in both series (Fri→Mon for both), so 2·a ≡ a.
        QVERIFY(std::abs(r - 1.0) < 1e-9);
        // Identical series correlate perfectly.
        QVERIFY(std::abs(correlation(a, false, a, false, 30) - 1.0) < 1e-9);
        // Too few common days → NaN.
        QVERIFY(std::isnan(correlation(a.mid(0, 10), false, a.mid(0, 10), false, 30)));
    }

    void yields_use_differences() {
        // A yield moving +0.1 then −0.1 alternately and a price doing the same in
        // percent: differences on the yield, log returns on the price.
        QVector<double> y, p;
        double yv = 4.0, pv = 100;
        for (int i = 0; i < 60; ++i) {
            yv += (i % 2 ? -0.1 : 0.1);
            pv *= (i % 2 ? 0.99 : 1.01);
            y.append(yv);
            p.append(pv);
        }
        const auto dy = daily_changes(series(y), true);
        QVERIFY(std::abs(dy.value(QDate(2024, 1, 2)) - (-0.1)) < 1e-9); // 4.1 → 4.0
        QVERIFY(correlation(series(y), true, series(p), false, 30) > 0.99);
        QVERIFY(is_yield_symbol("^TNX"));
        QVERIFY(!is_yield_symbol("SPY"));
    }

    void rolling_correlation_points() {
        QVector<double> av;
        for (int i = 0; i < 50; ++i)
            av.append(100 + i + std::cos(i));
        const auto r = rolling_correlation(series(av), false, series(av), false, 20);
        QCOMPARE(r.size(), 49 - 20 + 1);
        QVERIFY(std::abs(r.last().value - 1.0) < 1e-9);
    }

    void fred_keys_parse_strictly() {
        auto k = parse_fred_key("FRED:CPIAUCSL@yoy");
        QVERIFY(k.valid());
        QCOMPARE(k.a, QString("CPIAUCSL"));
        QVERIFY(k.yoy);
        k = parse_fred_key("FRED:NCBEILQ027S/GDP*0.1");
        QCOMPARE(k.b, QString("GDP"));
        QCOMPARE(k.scale, 0.1);
        QVERIFY(!parse_fred_key("FRED:bad id").valid());
        QVERIFY(!parse_fred_key("FRED:A/").valid());
        QVERIFY(!parse_fred_key("FRED:DFF*0").valid());
        QVERIFY(!parse_fred_key("SPY").valid());
    }

    void yoy_needs_the_year_ago_observation() {
        QVector<DatedValue> m;
        for (int i = 0; i < 25; ++i)
            m.append({QDate(2024, 1, 1).addMonths(i), 100.0 + i});
        m.removeAt(1); // Feb 2024 missing (a skipped release)
        FredKey k;
        k.a = "X";
        k.yoy = true;
        const auto y = fred_values(k, m, {});
        // Jan 2025 … Jan 2026 have a year-ago value, except Feb 2025.
        QCOMPARE(y.first().date, QDate(2025, 1, 1));
        QVERIFY(std::abs(y.first().value - 12.0) < 1e-9);
        for (const auto& x : y)
            QVERIFY(x.date != QDate(2025, 2, 1));
    }

    void ratio_matches_dates() {
        FredKey k;
        k.a = "A";
        k.b = "B";
        k.scale = 0.1;
        // Z.1 equities in $ millions, GDP in $ billions (FRED's own units).
        const QVector<DatedValue> a = {{QDate(2025, 1, 1), 60'000'000}, {QDate(2025, 4, 1), 66'000'000}};
        const QVector<DatedValue> b = {{QDate(2025, 4, 1), 30000}, {QDate(2025, 7, 1), 31000}};
        const auto r = fred_values(k, a, b);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].value, 220.0); // $66T / $30T = 220%
    }

    void level_stats_change_and_rank() {
        QVector<DatedValue> xs;
        for (int i = 0; i < 12 * 11; ++i)
            xs.append({QDate(2015, 1, 1).addMonths(i), double(i)});
        const auto s = level_stats(xs);
        QCOMPARE(s.change(), 1.0);
        QVERIFY(s.pctile_10y > 99.0);
        QVERIFY(std::isnan(level_stats(xs.mid(100)).pctile_10y)); // under ten years
    }
};

QTEST_GUILESS_MAIN(TestMarketConditions)
#include "test_market_conditions.moc"
