// Markets › WORLD: session clocks, the overnight line's fixed rules, yield /
// bp / spread maths, row status (absent is not zero) and the Treasury curve
// parse. Pure headers, no widget and no network.

#include "screens/markets/world/MarketSessions.h"
#include "screens/markets/world/OvernightSummary.h"
#include "screens/markets/world/TreasuryCurve.h"
#include "screens/markets/world/WorldMarketsModel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include <cmath>

using namespace finterm::screens::world;

namespace {
QDateTime utc(int y, int mo, int d, int h, int mi) {
    return QDateTime(QDate(y, mo, d), QTime(h, mi), QTimeZone::UTC);
}
QDateTime local(const char* tz, int y, int mo, int d, int h, int mi) {
    return QDateTime(QDate(y, mo, d), QTime(h, mi), QTimeZone(QByteArray(tz))).toUTC();
}
const ExchangeHours& ex(const char* id) {
    const ExchangeHours* e = find_exchange(QString::fromLatin1(id));
    Q_ASSERT(e);
    return *e;
}
Phase phase(const char* id, const QDateTime& t) {
    return session_at(ex(id), t).phase;
}
} // namespace

class TestWorldMarkets : public QObject {
    Q_OBJECT
  private slots:
    // ── sessions ──────────────────────────────────────────────────────────
    void nyse_phases() {
        // Wed 11 Nov 2026 (US Veterans Day — NYSE is OPEN; holidays aren't modelled anyway).
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 3, 59)), Phase::Closed);
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 4, 0)), Phase::Pre);
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 9, 29)), Phase::Pre);
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 9, 30)), Phase::Open);
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 15, 59)), Phase::Open);
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 16, 0)), Phase::Post);
        QCOMPARE(phase("NYC", local("America/New_York", 2026, 11, 11, 20, 0)), Phase::Closed);
    }

    void nyse_countdowns() {
        const QDateTime t = local("America/New_York", 2026, 11, 11, 10, 0);
        const SessionInfo s = session_at(ex("NYC"), t);
        QCOMPARE(t.secsTo(s.phase_end_utc), qint64(6 * 3600));
        QCOMPARE(s.next_phase, Phase::Post);
        QCOMPARE(next_change_text(s, t), QStringLiteral("closes in 6h 00m"));
        // Pre-market: "opens in" counts to the REGULAR open, not the PRE start.
        const QDateTime pre = local("America/New_York", 2026, 11, 11, 8, 15);
        QCOMPARE(next_change_text(session_at(ex("NYC"), pre), pre), QStringLiteral("opens in 1h 15m"));
    }

    void weekend_is_closed_until_monday() {
        // Sat 14 Nov 2026 12:00 ET → next regular open Mon 16 Nov 09:30 ET.
        const QDateTime t = local("America/New_York", 2026, 11, 14, 12, 0);
        const SessionInfo s = session_at(ex("NYC"), t);
        QCOMPARE(s.phase, Phase::Closed);
        QCOMPARE(s.next_open_utc, local("America/New_York", 2026, 11, 16, 9, 30));
        QCOMPARE(next_change_text(s, t), QStringLiteral("opens in 1d 21h"));
        QCOMPARE(phase("LON", local("Europe/London", 2026, 11, 15, 10, 0)), Phase::Closed); // Sunday
    }

    void dst_is_the_exchanges_own() {
        // UK clocks go forward 29 Mar 2026; US went forward 8 Mar. On 16 Mar
        // New York is on EDT while London is still on GMT.
        QCOMPARE(phase("NYC", utc(2026, 3, 16, 13, 45)), Phase::Open);   // 09:45 EDT
        QCOMPARE(phase("LON", utc(2026, 3, 16, 7, 55)), Phase::Pre);     // 07:55 GMT (auction)
        QCOMPARE(phase("LON", utc(2026, 3, 30, 7, 30)), Phase::Open);    // 08:30 BST
        // Sydney is on AEDT (+11) in November: 23:30 UTC = 10:30 → open.
        QCOMPARE(phase("SYD", utc(2026, 11, 12, 23, 30)), Phase::Open);
        QCOMPARE(phase("SYD", utc(2026, 11, 12, 22, 30)), Phase::Pre); // 09:30 AEDT
    }

    void asian_lunch_break() {
        const QDateTime t = local("Asia/Tokyo", 2026, 11, 13, 12, 0);
        const SessionInfo s = session_at(ex("TYO"), t);
        QCOMPARE(s.phase, Phase::Break);
        QCOMPARE(next_change_text(s, t), QStringLiteral("opens in 30m"));
        const QDateTime m = local("Asia/Tokyo", 2026, 11, 13, 11, 0);
        QCOMPARE(next_change_text(session_at(ex("TYO"), m), m), QStringLiteral("break in 30m"));
        QCOMPARE(phase("TYO", local("Asia/Tokyo", 2026, 11, 13, 15, 29)), Phase::Open); // 15:30 close since Nov 2024
        QCOMPARE(phase("HKG", local("Asia/Hong_Kong", 2026, 11, 13, 9, 15)), Phase::Pre);
        QCOMPARE(phase("HKG", local("Asia/Hong_Kong", 2026, 11, 13, 12, 30)), Phase::Break);
        QCOMPARE(phase("HKG", local("Asia/Hong_Kong", 2026, 11, 13, 16, 5)), Phase::Post);
        QCOMPARE(phase("SHA", local("Asia/Shanghai", 2026, 11, 13, 14, 59)), Phase::Open);
        QCOMPARE(phase("SHA", local("Asia/Shanghai", 2026, 11, 13, 15, 0)), Phase::Closed);
    }

    void globex_week() {
        // Wed 16:30 CT: daily halt, reopens 17:00.
        const QDateTime halt = local("America/Chicago", 2026, 11, 11, 16, 30);
        const SessionInfo h = session_at(ex("CME"), halt);
        QCOMPARE(h.phase, Phase::Break);
        QCOMPARE(next_change_text(h, halt), QStringLiteral("opens in 30m"));
        // Wed 20:00 CT: open across midnight until Thu 16:00 (merged spans).
        const QDateTime eve = local("America/Chicago", 2026, 11, 11, 20, 0);
        const SessionInfo e = session_at(ex("CME"), eve);
        QCOMPARE(e.phase, Phase::Open);
        QCOMPARE(e.phase_end_utc, local("America/Chicago", 2026, 11, 12, 16, 0));
        QCOMPARE(next_change_text(e, eve), QStringLiteral("break in 20h 00m"));
        // Fri 16:30 CT: shut for the weekend until Sun 17:00 CT.
        const QDateTime fri = local("America/Chicago", 2026, 11, 13, 16, 30);
        const SessionInfo f = session_at(ex("CME"), fri);
        QCOMPARE(f.phase, Phase::Closed);
        QCOMPARE(f.next_open_utc, local("America/Chicago", 2026, 11, 15, 17, 0));
        QCOMPARE(phase("CME", local("America/Chicago", 2026, 11, 14, 12, 0)), Phase::Closed); // Saturday
        QCOMPARE(phase("CME", local("America/Chicago", 2026, 11, 15, 17, 0)), Phase::Open);   // Sunday open
        QCOMPARE(phase("CME", local("America/Chicago", 2026, 11, 13, 15, 59)), Phase::Open);  // Friday
    }

    void fx_and_crypto() {
        QCOMPARE(phase("FX", local("America/New_York", 2026, 11, 13, 16, 59)), Phase::Open);
        QCOMPARE(phase("FX", local("America/New_York", 2026, 11, 13, 17, 0)), Phase::Closed);
        QCOMPARE(phase("FX", local("America/New_York", 2026, 11, 15, 17, 0)), Phase::Open);
        const SessionInfo c = session_at(ex("24/7"), utc(2026, 11, 14, 3, 0));
        QCOMPARE(c.phase, Phase::Open);
        QVERIFY(next_change_text(c, utc(2026, 11, 14, 3, 0)).isEmpty()); // never closes
    }

    void b3_follows_us_dst() {
        // New York on standard time (Nov): B3 trades to 18:00 Sao Paulo.
        QCOMPARE(phase("SAO", local("America/Sao_Paulo", 2026, 11, 13, 17, 30)), Phase::Open);
        // New York on DST (Jul): B3 closes 17:00.
        QCOMPARE(phase("SAO", local("America/Sao_Paulo", 2026, 7, 15, 17, 30)), Phase::Closed);
    }

    void countdown_format() {
        QCOMPARE(format_countdown(0), QStringLiteral("0m"));
        QCOMPARE(format_countdown(1), QStringLiteral("1m")); // rounds up: never "0m" before the bell
        QCOMPARE(format_countdown(59 * 60), QStringLiteral("59m"));
        QCOMPARE(format_countdown(60 * 60), QStringLiteral("1h 00m"));
        QCOMPARE(format_countdown(26 * 3600 + 120), QStringLiteral("1d 2h"));
    }

    void strip_is_complete() {
        for (const QString& id : strip_exchange_ids())
            QVERIFY2(find_exchange(id), qPrintable(id));
        // every row's exchange exists
        for (const auto& s : world_sections())
            for (const auto& r : s.rows)
                QVERIFY2(r.exchange.isEmpty() || find_exchange(r.exchange), qPrintable(r.symbol));
    }

    // ── catalogue ─────────────────────────────────────────────────────────
    void no_india_sources() {
        for (const QString& sym : world_quote_symbols()) {
            QVERIFY2(!sym.endsWith(".NS") && !sym.endsWith(".BO"), qPrintable(sym));
            QVERIFY2(sym != "^NSEI" && sym != "^BSESN" && !sym.contains("INR"), qPrintable(sym));
        }
    }

    void treasury_rows_are_not_quotes() {
        QVERIFY(!world_quote_symbols().contains(kUst2y));
        QVERIFY(!world_quote_symbols().contains(kUst2s10s));
        QVERIFY(world_quote_symbols().contains("^TNX"));
    }

    // ── yields, bp, spreads ───────────────────────────────────────────────
    void yield_scaling() {
        QCOMPARE(yield_percent(4.123), 4.123);
        QCOMPARE(yield_percent(41.23), 4.123); // Cboe x10 convention
        QVERIFY(std::isnan(yield_percent(kNaN)));
        QVERIFY(std::isnan(yield_percent(0.0))); // absent, not a 0% yield
        QCOMPARE(change_bp(4.12, 0.031), 3.1);
        QCOMPARE(change_bp(41.2, 0.31), 3.1);
        QVERIFY(std::isnan(change_bp(4.12, kNaN)));
        QVERIFY(std::isnan(change_bp(0.0, 0.03)));
    }

    void spread_math() {
        QVERIFY(std::abs(spread_bp(5.15, 4.88) - 27.0) < 1e-9);
        QVERIFY(std::isnan(spread_bp(5.15, kNaN)));
        QVERIFY(std::isnan(spread_bp(kNaN, 4.88)));
    }

    void formatting() {
        QCOMPARE(signed_pct(0.534), QStringLiteral("+0.53%"));
        QCOMPARE(signed_pct(-1.2), QString(QChar(0x2212)) + "1.20%");
        QCOMPARE(signed_pct(-0.001), QStringLiteral("0.00%")); // no "−0.00%"
        QCOMPARE(signed_pct(kNaN), QString(QChar(0x2014)));
        QCOMPARE(signed_bp(3.14), QStringLiteral("+3.1bp"));
        QCOMPARE(signed_bp(-12.4), QString(QChar(0x2212)) + "12bp");
        QCOMPARE(signed_bp(kNaN), QString(QChar(0x2014)));
        QCOMPARE(price_text(43675.121, 2), QStringLiteral("43,675.12"));
        QCOMPARE(price_text(1.08345, 4), QStringLiteral("1.0835"));
        QCOMPARE(price_text(0.0, 2), QString(QChar(0x2014))); // absent price is a dash, not 0.00
        QCOMPARE(price_text(kNaN, 2), QString(QChar(0x2014)));
        QCOMPARE(signed_num(-12.4, 2), QString(QChar(0x2212)) + "12.40");
    }

    void heat_uses_bp_for_yields() {
        QCOMPARE(heat_value(Kind::Index, 1.5, kNaN), 1.5);
        QCOMPARE(heat_value(Kind::Yield, kNaN, 10.0), 1.0);
        QCOMPARE(heat_value(Kind::Treasury, kNaN, -5.0), -0.5);
        QVERIFY(std::isnan(heat_value(Kind::Yield, 2.0, kNaN))); // the % of a yield is not its move
    }

    // ── row status: absent is not zero ────────────────────────────────────
    void row_status_rules() {
        QCOMPARE(row_status(false, 100, true, false), RowStatus::Missing);
        QCOMPARE(row_status(true, kNaN, true, false), RowStatus::Missing);
        QCOMPARE(row_status(true, 0.0, true, false), RowStatus::Missing);
        QCOMPARE(row_status(true, 100, false, false), RowStatus::Stale); // hydrated from an earlier session
        QCOMPARE(row_status(true, 100, true, true), RowStatus::Stale);   // refresh failed, quote kept
        QCOMPARE(row_status(true, 100, true, false), RowStatus::Live);
    }

    void vix_term_structure() {
        QCOMPARE(vix_term(17.8, 20.6).label, QStringLiteral("contango"));
        QCOMPARE(vix_term(30.0, 25.0).label, QStringLiteral("inverted"));
        QCOMPARE(vix_term(20.0, 20.1).label, QStringLiteral("flat"));
        QVERIFY(vix_term(kNaN, 20.0).label.isEmpty());
        QVERIFY(vix_term(0.0, 20.0).label.isEmpty());
    }

    // ── overnight line ───────────────────────────────────────────────────
    void verdict_rules() {
        QCOMPARE(region_verdict({{"a", 0.5}, {"b", 0.2}}), QStringLiteral("higher"));
        QCOMPARE(region_verdict({{"a", -0.5}, {"b", -0.2}}), QStringLiteral("lower"));
        QCOMPARE(region_verdict({{"a", 0.05}, {"b", -0.09}}), QStringLiteral("flat"));
        QCOMPARE(region_verdict({{"a", 0.5}, {"b", 0.0}}), QStringLiteral("mostly higher"));
        QCOMPARE(region_verdict({{"a", -0.5}, {"b", 0.0}}), QStringLiteral("mostly lower"));
        QCOMPARE(region_verdict({{"a", 0.8}, {"b", -1.2}}), QStringLiteral("mixed"));
        // Thresholds are inclusive at exactly ±0.10.
        QCOMPARE(region_verdict({{"a", 0.10}}), QStringLiteral("higher"));
        QCOMPARE(region_verdict({{"a", -0.10}}), QStringLiteral("lower"));
        // An unknown move is left out, never counted as a flat 0.
        QCOMPARE(region_verdict({{"a", 0.5}, {"b", kNaN}}), QStringLiteral("higher"));
        QVERIFY(region_verdict({{"a", kNaN}}).isEmpty());
        QVERIFY(region_verdict({}).isEmpty());
    }

    void overnight_full_line() {
        OvernightInputs in;
        in.asia = {{"Nikkei", 0.8}, {"Hang Seng", -1.2}, {"Shanghai", 0.1}};
        in.europe = {{"Euro Stoxx 50", 0.6}, {"DAX", 0.4}};
        in.us_futures_pct = 0.31;
        in.us10y_pct = 4.123;
        in.us10y_bp = 3.1;
        in.dollar_pct = -0.21;
        const QString m = QString(QChar(0x2212));
        QCOMPARE(overnight_line(in),
                 "Asia mixed: Nikkei +0.8%, Hang Seng " + m + "1.2% · Europe higher: Euro Stoxx 50 +0.6% "
                 "· US futures +0.3% · 10y 4.12% (+3bp) · Dollar " + m + "0.2%");
    }

    void overnight_leaves_out_unknowns() {
        OvernightInputs in;
        in.asia = {{"Nikkei", kNaN}, {"Hang Seng", -1.2}, {"Shanghai", -0.4}};
        in.dollar_pct = 0.03;
        // Nikkei unknown: the next known names step up; no "Nikkei 0.0%".
        QCOMPARE(overnight_line(in), "Asia lower: Hang Seng " + QString(QChar(0x2212)) + "1.2%, Shanghai " +
                                         QString(QChar(0x2212)) + "0.4% · Dollar flat");
        QVERIFY(overnight_line(OvernightInputs{}).isEmpty());
        OvernightInputs y;
        y.us10y_pct = 4.1;
        QCOMPARE(overnight_line(y), QStringLiteral("10y 4.10%")); // no bp when the change is unknown
    }

    // ── Treasury curve ───────────────────────────────────────────────────
    void treasury_parse() {
        QJsonObject root{{"rows", QJsonArray{
                                      QJsonObject{{"date", "2026-11-13"}, {"y2", 4.88}, {"y10", 5.15}},
                                      QJsonObject{{"date", "2026-11-12"}, {"y2", 4.86}, {"y10", 5.10}},
                                  }}};
        const TreasuryCurve c = parse_treasury_curve(root);
        QVERIFY(c.valid());
        QCOMPARE(c.date, QDate(2026, 11, 13));
        QCOMPARE(c.prev_date, QDate(2026, 11, 12));
        QVERIFY(std::abs(c.y2_change_bp() - 2.0) < 1e-9);
        QVERIFY(std::abs(c.s2s10_bp() - 27.0) < 1e-9);
        QVERIFY(std::abs(c.s2s10_change_bp() - 3.0) < 1e-9);
    }

    void treasury_null_is_absent() {
        // null 10y (the daemon's NaN) must not become a 0% yield and a −488bp spread.
        QJsonObject root{{"rows", QJsonArray{
                                      QJsonObject{{"date", "2026-11-13"}, {"y2", 4.88}, {"y10", QJsonValue()}},
                                      QJsonObject{{"date", "2026-11-12"}, {"y2", QJsonValue()}, {"y10", 5.10}},
                                  }}};
        const TreasuryCurve c = parse_treasury_curve(root);
        QVERIFY(c.valid());
        QVERIFY(std::isnan(c.s2s10_bp()));
        QVERIFY(std::isnan(c.y2_change_bp()));
        QVERIFY(std::isnan(c.s2s10_change_bp()));
        // Newest row without a 2y is skipped for the next one that has it.
        QJsonObject skip{{"rows", QJsonArray{QJsonObject{{"date", "2026-11-13"}, {"y2", QJsonValue()}},
                                             QJsonObject{{"date", "2026-11-12"}, {"y2", 4.86}}}}};
        QCOMPARE(parse_treasury_curve(skip).date, QDate(2026, 11, 12));
    }

    void treasury_error_is_not_empty_success() {
        const TreasuryCurve c = parse_treasury_curve(QJsonObject{{"error", "HTTP 503"}});
        QVERIFY(!c.valid());
        QCOMPARE(c.error, QStringLiteral("HTTP 503"));
        QVERIFY(!parse_treasury_curve(QJsonObject{{"rows", QJsonArray{}}}).valid());
    }
};

QTEST_GUILESS_MAIN(TestWorldMarkets)
#include "test_world_markets.moc"
