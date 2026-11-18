// tests/screens/test_extended_hours_math.cpp
//
// The EXT% surfaces report how a holding moved outside the regular session.
// Three numbers have been on screen and wrong: a sign inversion (AAPL down
// 6.5% shown up 6.3%), today's pre-market against today's close (AMZN -7.82%
// on a +3.9% day), and — 2026-11-18 — NVDA "+1.3%" on the portfolio beside
// "+4.81%" in Equity Research: the overnight print and the after-hours print,
// both real, neither named. Each case below is one of those.
//
// Prices are real: AAPL/AMZN from the daemon on 30 Jul 2026 after the close;
// NVDA from Yahoo's quote and push stream at 00:14 ET on 18 Nov 2026.

#include "screens/dashboard/widgets/ExtendedHoursMath.h"
#include "services/equity/ExtendedSessions.h"

#include <QtTest/QtTest>

using namespace finterm::screens::widgets::exthours;
namespace eq = finterm::services::equity;

namespace {

constexpr qint64 kClose30Jul = 1785441599; // 15:59:59 ET — the official close's instant
constexpr qint64 kPost30Jul = 1785455940;  // 19:59 ET
constexpr qint64 kNow = 1785470000;        // later that evening

QJsonObject session_json(double price, qint64 ts, double ref, qint64 ref_ts, const QString& day,
                         const QString& ref_date) {
    return QJsonObject{{"price", price}, {"ts", ts},          {"day", day},
                       {"ref", ref},     {"ref_ts", ref_ts}, {"ref_date", ref_date}};
}

QJsonObject post_row(const QString& sym, double close, double post) {
    return QJsonObject{{"symbol", sym},
                       {"regular", close},
                       {"regular_ts", kClose30Jul},
                       {"declined", false},
                       {"pre", QJsonValue()},
                       {"post", session_json(post, kPost30Jul, close, kClose30Jul, "2026-07-30", "2026-07-30")}};
}

// NVDA, 17 Nov 2026: close 155.46 (16:00), after-hours last 162.94 at 19:59:59,
// overnight print 157.48 (+2.02 against that close) at 00:14 on the 18th.
constexpr qint64 kNvdaClose = 1794949199;
constexpr qint64 kNvdaPost = 1794963599;
constexpr qint64 kNvdaOvernightMs = 1794985000000LL;
constexpr qint64 kNvdaNow = 1794985100;

QJsonObject nvda_row() {
    return QJsonObject{
        {"symbol", "NVDA"},
        {"regular", 155.46},
        {"regular_ts", kNvdaClose},
        {"declined", false},
        {"pre", QJsonValue()},
        {"post", session_json(162.94, kNvdaPost, 155.46, kNvdaClose, "2026-11-17", "2026-11-17")}};
}

} // namespace

class TestExtendedHoursMath : public QObject {
    Q_OBJECT

  private slots:
    // ── the arithmetic ──────────────────────────────────────────────────────

    void direction_matches_the_market() {
        QHash<QString, SymbolExt> all;
        apply_row(all, post_row("AAPL", 334.16, 312.33), kNow);
        apply_row(all, post_row("AMZN", 235.49, 257.95), kNow);
        const auto a = latest(all["AAPL"]);
        const auto z = latest(all["AMZN"]);
        QVERIFY(a && z);
        QVERIFY2(a->pct < 0, qPrintable(QString("AAPL fell after the bell but read %1%").arg(a->pct)));
        QVERIFY(std::abs(a->pct - (-6.533)) < 0.01);
        QVERIFY2(z->pct > 0, qPrintable(QString("AMZN rose after the bell but read %1%").arg(z->pct)));
        QCOMPARE(a->side, ExtSide::Post);
    }

    void the_close_is_the_base_not_the_extended_price() {
        Reading r;
        r.price = 93.0;
        r.regular = 100.0;
        QCOMPARE(r.pct(), -7.0); // dividing by the print would read -7.53%
    }

    void a_print_before_its_close_is_not_a_reading() {
        QVERIFY(!reading_from_json(session_json(1.0, kClose30Jul - 60, 2.0, kClose30Jul, "", ""), kNow));
        QVERIFY(!reading_from_json(session_json(0.0, kPost30Jul, 2.0, kClose30Jul, "", ""), kNow));
        QVERIFY(!reading_from_json(QJsonObject{{"price", 5.0}}, kNow)); // no close: absent, never 0
    }

    // ── one reading per session ─────────────────────────────────────────────

    // The 2026-11-18 report: both numbers are real, so both are kept — each
    // under its own session — and the portfolio shows the latest, NAMED.
    void overnight_and_after_hours_are_separate_readings() {
        QHash<QString, SymbolExt> all;
        QCOMPARE(apply_row(all, nvda_row(), kNvdaNow), RowEffect::Updated);
        QVERIFY(apply_print(all, "NVDA", 4, 157.48, 2.02, kNvdaOvernightMs, kNvdaNow));
        const SymbolExt& x = all["NVDA"];
        QVERIFY(x.at(ExtSide::Post) && x.at(ExtSide::Overnight));
        QVERIFY(std::abs(x.at(ExtSide::Post)->pct() - 4.81) < 0.01);
        QVERIFY(std::abs(x.at(ExtSide::Overnight)->pct() - 1.30) < 0.01);
        const auto q = latest(x);
        QVERIFY(q);
        QCOMPARE(q->side, ExtSide::Overnight);
        QCOMPARE(ext_cell_text(*q, 2), QStringLiteral("+1.30% ON"));
        QVERIFY(ext_quote_line(*q).startsWith(QStringLiteral("Overnight $157.48 vs the $155.46 close")));
    }

    // The ER panel's headline for a session IS the service's reading — the
    // same object — so it equals what the portfolio shows for that session.
    void the_er_panel_headline_is_the_portfolio_reading() {
        QHash<QString, SymbolExt> all;
        apply_row(all, nvda_row(), kNvdaNow);
        apply_print(all, "NVDA", 4, 157.48, 2.02, kNvdaOvernightMs, kNvdaNow);
        const SymbolExt& x = all["NVDA"];

        // Bars (extended_sessions.py) for the same after-hours session, a minute
        // behind the quote: last 162.90, with range and volume.
        eq::ExtSessions bars;
        bars.after_hours.valid = true;
        bars.after_hours.date = QDate(2026, 11, 17);
        bars.after_hours.last = 162.90;
        bars.after_hours.ref_close = 155.46;
        bars.after_hours.ref_date = QDate(2026, 11, 17);
        bars.after_hours.high = 163.50;
        bars.after_hours.low = 155.10;
        bars.after_hours.volume = 1.2e7;
        bars.pre_market.valid = true;
        bars.pre_market.date = QDate(2026, 11, 17);
        bars.pre_market.last = 158.0;
        bars.pre_market.ref_close = 156.14;

        const auto shown = eq::with_live_readings(bars, x, kNvdaNow);
        const auto& post = *x.at(ExtSide::Post);
        QCOMPARE(shown.after_hours.last, post.price);
        QCOMPARE(shown.after_hours.pct, post.pct());
        QCOMPARE(shown.after_hours.ref_close, post.regular);
        QCOMPARE(shown.after_hours.volume, 1.2e7); // the bars' facts stay: same session
        QCOMPARE(shown.after_hours.high, 163.50);
        // Overnight: the stream's reading, nothing the bars could add.
        QVERIFY(shown.overnight.valid);
        const auto q = latest(x);
        QCOMPARE(shown.overnight.last, q->price);
        QCOMPARE(shown.overnight.pct, q->pct);
        QVERIFY(!std::isfinite(shown.overnight.volume));
        QVERIFY(!std::isfinite(shown.overnight.high));
        // A session the service has no reading for keeps the bars' own.
        QCOMPARE(shown.pre_market.last, 158.0);
    }

    // Bars of an OLDER session than the reading: their range is another day's.
    void bars_of_another_day_lend_no_range() {
        SymbolExt x;
        x.regular = 100;
        Reading r;
        r.price = 101;
        r.regular = 100;
        r.print_ts = 2000;
        r.day = QDate(2026, 11, 18);
        x.at(ExtSide::Post) = r;
        eq::ExtSessions bars;
        bars.after_hours.valid = true;
        bars.after_hours.date = QDate(2026, 11, 17);
        bars.after_hours.last = 99;
        bars.after_hours.high = 120;
        bars.after_hours.low = 80;
        bars.after_hours.volume = 5;
        const auto shown = eq::with_live_readings(bars, x, 3000);
        QCOMPARE(shown.after_hours.last, 101.0);
        QCOMPARE(shown.after_hours.date, QDate(2026, 11, 18));
        QVERIFY(!std::isfinite(shown.after_hours.high));
        QVERIFY(!std::isfinite(shown.after_hours.volume));
    }

    // ── which reading is the latest move ────────────────────────────────────

    // After the bell this morning's pre-market is still a true pre-market
    // reading (against yesterday's close) — but not a move since TODAY's
    // close, so the portfolio does not show it (the AMZN -7.82% inversion).
    void this_mornings_pre_market_is_not_the_move_after_the_bell() {
        QHash<QString, SymbolExt> all;
        const qint64 yclose = kClose30Jul - 86400;
        QJsonObject morning{{"symbol", "AMZN"},
                            {"regular", 226.0},
                            {"regular_ts", yclose},
                            {"pre", session_json(218.0, kClose30Jul - 8 * 3600, 226.0, yclose, "2026-07-30",
                                                 "2026-07-29")}};
        apply_row(all, morning, kClose30Jul - 7 * 3600);
        QCOMPARE(latest(all["AMZN"])->side, ExtSide::Pre); // in the morning it IS the move
        // 16:05: today's close is in, no after-hours print yet.
        QJsonObject evening{{"symbol", "AMZN"}, {"regular", 235.0}, {"regular_ts", kClose30Jul}};
        QCOMPARE(apply_row(all, evening, kClose30Jul + 300), RowEffect::Updated);
        QVERIFY(!latest(all["AMZN"]));
        QVERIFY(all["AMZN"].at(ExtSide::Pre)); // kept for the ER panel, with its own close
        QCOMPARE(all["AMZN"].at(ExtSide::Pre)->regular, 226.0);
    }

    // Before the open, yesterday's after-hours is still a move since the last
    // close (they share it), until this morning's pre-market prints.
    void before_the_open_the_prior_after_hours_is_the_move() {
        QHash<QString, SymbolExt> all;
        apply_row(all, post_row("MSFT", 500.0, 490.0), kNow);
        QCOMPARE(latest(all["MSFT"])->side, ExtSide::Post);
        QVERIFY(apply_print(all, "MSFT", 0, 505.0, 5.0, (kPost30Jul + 9 * 3600) * 1000, kNow));
        const auto q = latest(all["MSFT"]);
        QCOMPARE(q->side, ExtSide::Pre);
        QCOMPARE(q->pct, 1.0);
    }

    // ── merging fetch and stream ────────────────────────────────────────────

    void a_newer_live_print_outlives_an_older_fetch() {
        QHash<QString, SymbolExt> all;
        apply_row(all, post_row("AAPL", 334.16, 312.33), kNow);
        QVERIFY(apply_print(all, "AAPL", 2, 310.0, 310.0 - 334.16, (kPost30Jul + 60) * 1000, kNow));
        QCOMPARE(apply_row(all, post_row("AAPL", 334.16, 312.33), kNow), RowEffect::Kept);
        QCOMPARE(all["AAPL"].at(ExtSide::Post)->price, 310.0);
        // The print-built reading learnt its close's day from the fetched one.
        QCOMPARE(all["AAPL"].at(ExtSide::Post)->regular_day, QDate(2026, 7, 30));
    }

    // A reconnect can replay prints: an older one changes nothing, in any session.
    void a_replayed_older_print_changes_nothing() {
        QHash<QString, SymbolExt> all;
        apply_row(all, nvda_row(), kNvdaNow);
        QVERIFY(apply_print(all, "NVDA", 4, 157.48, 2.02, kNvdaOvernightMs, kNvdaNow));
        QVERIFY(!apply_print(all, "NVDA", 4, 150.00, 150.00 - 155.46, kNvdaOvernightMs - 60'000, kNvdaNow));
        QVERIFY(!apply_print(all, "NVDA", 2, 140.00, 140.00 - 155.46, (kNvdaPost - 600) * 1000, kNvdaNow));
        QCOMPARE(all["NVDA"].at(ExtSide::Overnight)->price, 157.48);
        QCOMPARE(all["NVDA"].at(ExtSide::Post)->price, 162.94);
    }

    // A future / coin / foreign listing is measured against its settle by the
    // fetch; its stream change is against something else, so a print may only
    // move a reading against the SAME close — never start one or swap closes.
    void a_non_us_print_never_starts_or_rebases_a_reading() {
        QHash<QString, SymbolExt> all;
        QVERIFY(!apply_print(all, "BTC-USD", 0, 91000.0, 1900.0, kNow * 1000, kNow));
        QVERIFY(!all.value("BTC-USD").at(ExtSide::Pre));
        QJsonObject es{{"symbol", "ES=F"},
                       {"regular", 7359.0},
                       {"regular_ts", kClose30Jul},
                       {"post", session_json(7372.5, kPost30Jul, 7359.0, kClose30Jul, "2026-07-30", "2026-07-30")}};
        apply_row(all, es, kNow);
        QVERIFY(!apply_print(all, "ES=F", 2, 7380.0, 7380.0 - 7300.0, (kPost30Jul + 60) * 1000, kNow)); // other close
        QCOMPARE(all["ES=F"].at(ExtSide::Post)->regular, 7359.0);
        QVERIFY(apply_print(all, "ES=F", 2, 7380.0, 7380.0 - 7359.0, (kPost30Jul + 60) * 1000, kNow)); // same close
        QCOMPARE(all["ES=F"].at(ExtSide::Post)->price, 7380.0);
    }

    // A session only the stream has seen yet (the bars trail it) is still
    // shown as trading while it prints.
    void a_print_only_session_is_live_while_it_prints() {
        SymbolExt x;
        Reading r;
        r.price = 101;
        r.regular = 100;
        r.print_ts = 10'000;
        r.day = QDate(2026, 11, 18);
        x.at(ExtSide::Pre) = r;
        QVERIFY(eq::with_live_readings({}, x, 10'060).pre_market.live);
        QVERIFY(!eq::with_live_readings({}, x, 10'000 + 16 * 60).pre_market.live);
    }

    // A regular-session print is not an extended reading.
    void a_regular_print_is_ignored() {
        QHash<QString, SymbolExt> all;
        QVERIFY(!apply_print(all, "NVDA", 1, 160.0, 4.0, kNvdaOvernightMs, kNvdaNow));
        QVERIFY(!all.value("NVDA").at(ExtSide::Pre));
    }

    void the_same_print_again_is_not_news() {
        QHash<QString, SymbolExt> all;
        QCOMPARE(apply_row(all, nvda_row(), kNvdaNow), RowEffect::Updated);
        QCOMPARE(apply_row(all, nvda_row(), kNvdaNow + 60), RowEffect::Kept);
        QVERIFY(apply_print(all, "NVDA", 4, 157.48, 2.02, kNvdaOvernightMs, kNvdaNow));
        QVERIFY(!apply_print(all, "NVDA", 4, 157.48, 2.02, kNvdaOvernightMs + 5000, kNvdaNow));
    }

    // ── the daemon's contract ───────────────────────────────────────────────

    // 2026-11-17: Yahoo 429'd the app's first minute and the heatmap read the
    // empty rows as declines — blank for good. A refusal keeps what is held
    // and leaves the symbol open to prints.
    void a_refused_row_keeps_what_is_held_and_admits_prints() {
        QHash<QString, SymbolExt> all;
        apply_row(all, post_row("AAPL", 334.16, 312.33), kNow);
        QCOMPARE(apply_row(all, QJsonObject{{"symbol", "AAPL"}, {"unavailable", true}}, kNow),
                 RowEffect::Unavailable);
        QVERIFY(latest(all["AAPL"]));
        QCOMPARE(apply_row(all, QJsonObject{{"symbol", "AMZN"}, {"unavailable", true}}, kNow),
                 RowEffect::Unavailable);
        QVERIFY(apply_print(all, "AMZN", 2, 260.20, 2.31, kNow * 1000, kNow));
        QVERIFY(latest(all["AMZN"])->pct > 0);
    }

    void a_row_without_prints_is_not_a_decline() {
        QHash<QString, SymbolExt> all;
        apply_row(all, QJsonObject{{"symbol", "MSFT"}, {"regular", 505.0}, {"pre", QJsonValue()}}, kNow);
        QVERIFY(!all["MSFT"].declined);
        QVERIFY(apply_print(all, "MSFT", 2, 510.0, 5.0, kNow * 1000, kNow));
    }

    void a_declined_row_clears_and_blocks_prints() {
        QHash<QString, SymbolExt> all;
        apply_row(all, post_row("AAPL", 334.16, 312.33), kNow);
        QJsonObject stale = post_row("AAPL", 334.16, 312.33);
        stale.insert("declined", true);
        QCOMPARE(apply_row(all, stale, kNow), RowEffect::Declined);
        QVERIFY(!latest(all["AAPL"]));
        QVERIFY(!all["AAPL"].at(ExtSide::Post));
        QVERIFY(!apply_print(all, "AAPL", 2, 300.0, -34.16, kNow * 1000, kNow));
    }

    // ── one label set ───────────────────────────────────────────────────────

    void every_session_has_one_key_tag_and_name() {
        for (const ExtSide s : kSides) {
            QCOMPARE(ext_side_from_key(ext_side_key(s)), std::optional<ExtSide>(s));
            QVERIFY(!ext_side_tag(s).isEmpty());
        }
        QCOMPARE(ext_side_tag(ExtSide::Pre), QStringLiteral("PRE"));
        QCOMPARE(ext_side_tag(ExtSide::Post), QStringLiteral("AH"));
        QCOMPARE(ext_side_tag(ExtSide::Overnight), QStringLiteral("ON"));
        QCOMPARE(ext_side_name(ExtSide::Post), QStringLiteral("After-hours"));
        // Nothing but the after-hours session is ever called after-hours.
        QVERIFY(!ext_side_name(ExtSide::Overnight).contains(QStringLiteral("fter")));
        QVERIFY(!ext_side_name(ExtSide::Pre).contains(QStringLiteral("fter")));
    }

    // Mirrors extended_rules.has_us_extended_session (Python).
    void only_plain_us_tickers_have_extended_sessions() {
        for (const char* s : {"NVDA", "BRK.B", "BRK-B", "VFIAX"})
            QVERIFY2(has_us_extended_session(s), s);
        for (const char* s : {"^GSPC", "ES=F", "BTC-USD", "7203.T", "SHOP.TO", "", "TOOLONGX"})
            QVERIFY2(!has_us_extended_session(s), s);
    }
};

QTEST_MAIN(TestExtendedHoursMath)
#include "test_extended_hours_math.moc"
