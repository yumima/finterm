// test_whats_on_model.cpp — News › WHAT'S ON: sources merged into one dated
// list, filtered, de-duplicated and bucketed TODAY / THIS WEEK / THIS MONTH /
// UPCOMING.

#include "screens/news/WhatsOnModel.h"

#include <QJsonDocument>
#include <QTest>

using namespace finterm::whats_on;
namespace mc = finterm::macro_calendar;

namespace {
mc::MacroEvent ev(const QString& title, const QString& cty, int imp, const QDateTime& when) {
    mc::MacroEvent e;
    e.title = title;
    e.country = cty;
    e.importance = imp;
    e.when = when;
    e.forecast = "0.3%";
    e.previous = "0.4%";
    return e;
}
} // namespace

class TestWhatsOnModel : public QObject {
    Q_OBJECT
  private slots:
    void macro_default_filter_keeps_top_tier_us() {
        const QDateTime now(QDate(2026, 11, 10), QTime(9, 0));
        const QVector<mc::MacroEvent> events = {
            ev("CPI m/m", "USD", 3, now.addSecs(3600)),
            ev("Unemployment Claims", "USD", 2, now.addDays(2)),
            ev("Mortgage Delinquencies", "USD", 1, now.addDays(1)),
            ev("German ZEW", "EUR", 3, now.addDays(1)),
            ev("Retail Sales m/m", "USD", 3, now.addDays(-1)), // past
        };
        const auto top = items_from_macro(events, now, true);
        QCOMPARE(top.size(), 2);
        QCOMPARE(top[0].title, QStringLiteral("CPI m/m"));
        QCOMPARE(top[0].kind, QStringLiteral("INFLATION"));
        QCOMPARE(top[0].figures, QStringLiteral("F 0.3% · P 0.4%"));
        QCOMPARE(top[1].kind, QStringLiteral("LABOR"));
        const auto all = items_from_macro(events, now, false);
        QCOMPARE(all.size(), 4);
        QVERIFY(std::any_of(all.begin(), all.end(), [](const Item& i) { return i.title == "EUR German ZEW"; }));
    }

    void fomc_next_two_and_not_twice() {
        const QDate today(2026, 11, 13);
        const QVector<QDate> d = {QDate(2026, 10, 28), QDate(2026, 12, 9), QDate(2027, 1, 27), QDate(2027, 3, 17)};
        auto items = items_from_fomc(d, today, {});
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].when.date(), QDate(2026, 12, 9));
        QCOMPARE(items[0].when.toTimeZone(QTimeZone("America/New_York")).time(), QTime(14, 0));
        // The calendar already lists the decision that day: no duplicate row,
        // and it still counts as one of the next two meetings.
        Item rate;
        rate.title = "Federal Funds Rate";
        rate.when = QDateTime(QDate(2026, 12, 9), QTime(14, 0), QTimeZone("America/New_York"));
        items = items_from_fomc(d, today, {rate});
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].when.date(), QDate(2027, 1, 27));
    }

    void ipo_upcoming_only() {
        const auto deals = QJsonDocument::fromJson(R"([
          {"deal_id":"d-42","company":"Acme Corp","ticker":"ACME","status":"upcoming","expected_date":"2026-11-18","price_range":"15.00-17.00","exchange":"NYSE"},
          {"company":"Old Co","ticker":"OLD","status":"upcoming","expected_date":"2026-11-01"},
          {"company":"Priced Inc","ticker":"PRC","status":"priced","expected_date":"2026-11-20"},
          {"company":"No Date","ticker":"ND","status":"upcoming","expected_date":""}
        ])").array();
        const auto items = items_from_ipo_deals(deals, QDate(2026, 11, 13));
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].title, QStringLiteral("Acme Corp (ACME)"));
        QCOMPARE(items[0].key, QStringLiteral("ACME"));
        QCOMPARE(items[0].deal_id, QStringLiteral("d-42")); // IPO Watch selects by deal, not ticker
        QCOMPARE(items[0].figures, QStringLiteral("$15.00-17.00 · NYSE"));
    }

    // ── Earnings that matter ──────────────────────────────────────────────
    void earnings_large_caps_and_own_names() {
        const auto rows = QJsonDocument::fromJson(R"([
          {"symbol":"NVDA","name":"NVIDIA","date":"2026-11-17","session":"AMC","market_cap":4.0e12,"eps_forecast":1.25},
          {"symbol":"HD","name":"Home Depot","date":"2026-11-17","session":"BMO","market_cap":3.47e11,"eps_forecast":3.84},
          {"symbol":"MID","name":"Mid Co","date":"2026-11-17","session":"BMO","market_cap":2.0e10},
          {"symbol":"MINE","name":"Small Mine","date":"2026-11-18","session":"","market_cap":9.0e8},
          {"symbol":"NOCAP","name":"No Cap","date":"2026-11-18","session":"BMO","market_cap":null},
          {"symbol":"PBR","name":"Petroleo Brasileiro","date":"2026-11-18","session":"BMO","market_cap":1.17e11},
          {"symbol":"PBR.A","name":"Petroleo Brasileiro","date":"2026-11-18","session":"BMO","market_cap":1.17e11},
          {"symbol":"CSCO","name":"Cisco","date":"2026-11-12","session":"","market_cap":4.0e11,"eps_forecast":1.15,"eps_actual":1.21,"surprise_pct":5.22},
          {"symbol":"OLD","name":"Old","date":"2026-11-02","session":"","market_cap":4.0e11}
        ])").array();
        const auto items = items_from_earnings(rows, QDate(2026, 11, 9), norm_symbols({"mine", "brk-b"}));
        QStringList syms;
        for (const auto& i : items)
            syms << i.symbol;
        // MID is under $50B; NOCAP's size is unknown (not "small", not "large":
        // dropped unless it is yours); PBR.A is PBR's second share class.
        QCOMPARE(syms, (QStringList{"NVDA", "HD", "MINE", "PBR", "CSCO"}));
        QCOMPARE(items[0].impact, Impact::High); // $4T
        QCOMPARE(items[1].impact, Impact::Med);
        QCOMPARE(items[2].impact, Impact::Med); // yours, though small
        QVERIFY(items[2].mine && !items[0].mine);
        QCOMPARE(items[0].session, QStringLiteral("AMC"));
        QCOMPARE(items[0].when.toTimeZone(QTimeZone("America/New_York")).time(), QTime(16, 5));
        QCOMPARE(items[0].figures, QStringLiteral("EPS est 1.25"));
        QCOMPARE(items[4].figures, QStringLiteral("EPS 1.21 vs 1.15 est (+5.2%)"));
        QVERIFY(items[4].released);
        QVERIFY(!items[0].released);
        QCOMPARE(items[4].surprise_pct, 5.22);
        QVERIFY(std::isnan(items[0].surprise_pct)); // not reported: absent, not 0
        // Own names survive without a market cap.
        const auto own = items_from_earnings(rows, QDate(2026, 11, 9), norm_symbols({"NOCAP"}));
        QVERIFY(std::any_of(own.begin(), own.end(), [](const Item& i) { return i.symbol == "NOCAP" && i.mine; }));
    }

    void impact_table_is_mechanical() {
        QCOMPARE(macro_impact("CPI m/m", 3), Impact::High);
        QCOMPARE(macro_impact("Core CPI m/m", 3), Impact::High);
        QCOMPARE(macro_impact("Non-Farm Employment Change", 3), Impact::High);
        QCOMPARE(macro_impact("Advance GDP q/q", 3), Impact::High);
        QCOMPARE(macro_impact("Core PCE Price Index m/m", 3), Impact::High);
        QCOMPARE(macro_impact("Federal Funds Rate", 3), Impact::High);
        QCOMPARE(macro_impact("PPI m/m", 3), Impact::Med);
        QCOMPARE(macro_impact("Unemployment Claims", 2), Impact::Med);
        QCOMPARE(macro_impact("Fed Chair Warsh Speaks", 2), Impact::Med);
        QCOMPARE(macro_impact("Prelim UoM Consumer Sentiment", 3), Impact::Med); // calendar-high fallback
        QCOMPARE(macro_impact("Crude Oil Inventories", 1), Impact::Low);
        QCOMPARE(macro_impact("Median CPI m/m", 1), Impact::Low);
        QCOMPARE(earnings_impact(NAN, false), Impact::Low);
        QCOMPARE(earnings_impact(NAN, true), Impact::Med);
        QCOMPARE(earnings_impact(5e11, false), Impact::High);
        QCOMPARE(earnings_impact(4.99e11, false), Impact::Med);
        QCOMPARE(earnings_impact(4.9e10, false), Impact::Low);
        const QDateTime now(QDate(2026, 11, 10), QTime(9, 0));
        const auto m = items_from_macro({ev("CPI m/m", "USD", 3, now.addSecs(3600))}, now, true);
        QCOMPARE(m[0].impact, Impact::High);
    }

    // ── Released data: actual only when FRED has the period ───────────────
    void released_macro_shows_fred_actual_or_awaits() {
        const QTimeZone et("America/New_York");
        const QDateTime release(QDate(2026, 11, 10), QTime(8, 30), et);
        const QDateTime now = release.addSecs(2 * 3600);
        mc::MacroEvent cpi = ev("CPI m/m", "USD", 3, release);
        cpi.forecast = "0.3%";
        cpi.previous = "0.6%";
        const QVector<mc::MacroEvent> events = {cpi, ev("PPI m/m", "USD", 3, release.addDays(3))};
        QCOMPARE(fred_series_needed(events, now, QDate(2026, 11, 9), true), QStringList{"CPIAUCSL"});

        // FRED still ends in September: no actual — "awaiting", never Sep's number.
        FredSeries fred;
        fred["CPIAUCSL"] = {{QDate(2026, 8, 1), 321.6}, {QDate(2026, 9, 1), 322.2}};
        auto items = items_from_macro(events, now, true, QDate(2026, 11, 9), &fred);
        QCOMPARE(items[0].figures, QStringLiteral("A awaiting FRED · F 0.3% · P 0.6%"));
        QVERIFY(!items[0].released);
        // October posted.
        fred["CPIAUCSL"].append({QDate(2026, 10, 1), 323.2});
        items = items_from_macro(events, now, true, QDate(2026, 11, 9), &fred);
        QCOMPARE(items[0].figures, QStringLiteral("A 0.3% · F 0.3% · P 0.2%"));
        QVERIFY(items[0].released);
        QVERIFY(items[0].detail.contains("FRED CPIAUCSL (Oct 2026)"));
        QVERIFY(items[0].detail.contains("08:30 ET"));
        // Not out yet (PPI, Friday): forecast/previous only.
        QCOMPARE(items[1].figures, QStringLiteral("F 0.3% · P 0.4%"));
        // Without a FRED download the row is exactly as before.
        items = items_from_macro(events, now, true, QDate(2026, 11, 9));
        QCOMPARE(items[0].figures, QStringLiteral("F 0.3% · P 0.6%"));
    }

    // ── Labels ─────────────────────────────────────────────────────────────
    void when_labels_count_down_in_eastern_time() {
        const QTimeZone et("America/New_York");
        const QDateTime now(QDate(2026, 11, 13), QTime(6, 25), et); // Fri 06:25 ET
        Item it;
        it.timed = true;
        it.when = QDateTime(QDate(2026, 11, 13), QTime(8, 30), et);
        QCOMPARE(when_label(it, now), QStringLiteral("in 2h 05m · 08:30 ET"));
        it.when = now.addSecs(35 * 60);
        QCOMPARE(when_label(it, now), QStringLiteral("in 35m · 07:00 ET"));
        it.when = now.addSecs(-2 * 3600);
        QCOMPARE(when_label(it, now), QStringLiteral("2h ago"));
        it.when = QDateTime(QDate(2026, 11, 13), QTime(1, 0), et); // 5h before: a clock again
        QCOMPARE(when_label(it, now), QStringLiteral("Today 01:00 ET"));
        it.when = QDateTime(QDate(2026, 11, 17), QTime(14, 0), et);
        QCOMPARE(when_label(it, now), QStringLiteral("Tue 14:00 ET"));
        it.when = QDateTime(QDate(2026, 12, 9), QTime(14, 0), et);
        QCOMPARE(when_label(it, now), QStringLiteral("Dec 9 14:00 ET"));
        Item e;
        e.timed = false;
        e.session = "AMC";
        e.when = QDateTime(QDate(2026, 11, 17), QTime(16, 5), et);
        e.day = QDate(2026, 11, 17); // the listed ET date: Tuesday in every zone
        QCOMPARE(when_label(e, now), QStringLiteral("Tue AMC"));
        e.session.clear();
        e.day = {};
        e.when = QDateTime(QDate(2026, 11, 13), QTime(0, 0));
        QCOMPARE(when_label(e, now), QStringLiteral("Today"));
    }

    void next_high_skips_past_and_low() {
        const QDateTime now(QDate(2026, 11, 13), QTime(9, 0));
        Item past, low, high1, high2;
        past.timed = low.timed = high1.timed = high2.timed = true;
        past.impact = high1.impact = high2.impact = Impact::High;
        past.when = now.addSecs(-60);
        low.when = now.addSecs(60);
        high1.when = now.addDays(3);
        high1.title = "CPI";
        high2.when = now.addDays(5);
        const auto n = next_high({past, low, high2, high1}, now);
        QVERIFY(n.has_value());
        QCOMPARE(n->title, QStringLiteral("CPI"));
        QVERIFY(!next_high({past, low}, now).has_value());
    }

    // ── Sections, grouping, YOUR NAMES ─────────────────────────────────────
    void sectionize_groups_filters_and_keeps_earlier_week_numbers() {
        const QDate today(2026, 11, 13); // Friday
        const QTimeZone et("America/New_York");
        auto earn = [&](const QString& sym, const QDate& d, const QString& sess, Impact imp, bool mine) {
            Item i;
            i.source = Item::Source::Earnings;
            i.symbol = i.title = sym;
            i.session = sess;
            i.group = "EARN:" + sess;
            i.when = QDateTime(d, sess == "BMO" ? QTime(7, 0) : QTime(16, 5), et);
            i.impact = imp;
            i.mine = mine;
            return i;
        };
        Item mon_quiet;
        mon_quiet.title = "Wholesale inventories";
        mon_quiet.timed = true;
        mon_quiet.when = QDateTime(QDate(2026, 11, 9), QTime(10, 0), et);
        Item mon_cpi = mon_quiet;
        mon_cpi.title = "CPI m/m";
        mon_cpi.impact = Impact::High;
        mon_cpi.released = true;
        Item last_week = mon_cpi;
        last_week.when = QDateTime(QDate(2026, 11, 6), QTime(8, 30), et);
        const QVector<Item> items = {
            earn("TJX", QDate(2026, 11, 18), "BMO", Impact::Med, false),
            earn("WMT", QDate(2026, 11, 18), "BMO", Impact::High, false),
            earn("LOW", QDate(2026, 11, 18), "BMO", Impact::Med, true),
            earn("PANW", QDate(2026, 11, 18), "AMC", Impact::Med, false),
            mon_quiet, mon_cpi, last_week,
        };
        auto s = sectionize(items, today);
        // Earlier this week: only what has a number / matters; last week is gone.
        QCOMPARE(s.value(Section::Earlier).size(), 1);
        QCOMPARE(s.value(Section::Earlier)[0].head().title, QStringLiteral("CPI m/m"));
        // Next Wednesday: one BMO row (largest impact first), one AMC row.
        const auto month = s.value(Section::ThisMonth);
        QCOMPARE(month.size(), 2);
        QCOMPARE(month[0].items.size(), 3);
        QCOMPARE(month[0].head().symbol, QStringLiteral("WMT"));
        QCOMPARE(month[0].impact(), Impact::High);
        QVERIFY(month[0].mine());
        QCOMPARE(month[1].head().symbol, QStringLiteral("PANW"));
        QVERIFY(!month[1].mine());
        // YOUR NAMES: only LOW survives, alone in its row.
        s = sectionize(items, today, Filter{true});
        QCOMPARE(s.value(Section::ThisMonth).size(), 1);
        QCOMPARE(s.value(Section::ThisMonth)[0].items.size(), 1);
        QCOMPARE(s.value(Section::ThisMonth)[0].head().symbol, QStringLiteral("LOW"));
        QVERIFY(!s.contains(Section::Earlier));
    }

    void dividends_insiders_auctions_market_items() {
        const QDate from(2026, 11, 9);
        const auto div = items_from_dividends(QJsonDocument::fromJson(R"([
            {"symbol":"MSFT","name":"Microsoft","ex_date":"2026-11-19","pay_date":"2026-12-10","amount":0.98},
            {"symbol":"OLD","ex_date":"2026-11-02","amount":0.5}])").array(), from);
        QCOMPARE(div.size(), 1);
        QVERIFY(div[0].mine);
        QCOMPARE(div[0].figures, QStringLiteral("$0.98/sh"));
        const auto ins = items_from_insiders(QJsonDocument::fromJson(R"([
            {"symbol":"KO","issuer":"Coca-Cola","insiders":3,"value":1240000,"last_filed":"2026-11-11","last_trade":"2026-11-09"}])")
                                                 .array(),
                                             from, "2026-11-13");
        QCOMPARE(ins.size(), 1);
        QCOMPARE(ins[0].title, QStringLiteral("KO — 3 insiders bought"));
        QCOMPARE(ins[0].figures, QStringLiteral("$1.2M"));
        QVERIFY(ins[0].mine && ins[0].released);
        QCOMPARE(ins[0].impact, Impact::Med);

        ta::Auction bill13, bill26, ten;
        bill13.label = "13W bill";
        bill26.label = "26W bill";
        bill13.date = bill26.date = QDate(2026, 11, 16);
        bill13.close = bill26.close = QDateTime(bill13.date, QTime(11, 30), QTimeZone("America/New_York"));
        ten.label = "10Y note";
        ten.coupon = true;
        ten.years = 10;
        ten.date = QDate(2026, 11, 19);
        const auto auc = items_from_auctions({bill13, bill26, ten}, from);
        QCOMPARE(auc.size(), 3);
        QCOMPARE(auc[0].group, QStringLiteral("BILLS"));
        QVERIFY(auc[2].group.isEmpty());
        QCOMPARE(auc[2].impact, Impact::Med);
        QCOMPARE(auc[0].impact, Impact::Low);
        QVERIFY(!auc[2].timed); // tentative: no close time published
        const auto s = sectionize(auc, QDate(2026, 11, 13));
        QCOMPARE(s.value(Section::ThisMonth).size(), 2); // two bills share a row
        QCOMPARE(s.value(Section::ThisMonth)[0].items.size(), 2);

        const auto mkt_items = items_from_market_dates(finterm::market_calendar::market_dates(
            QDate(2026, 11, 20), QDate(2026, 11, 30)));
        QCOMPARE(mkt_items.size(), 4); // opex, Thanksgiving, early close, month end
        QCOMPARE(mkt_items[0].impact, Impact::Low);
        QCOMPARE(mkt_items[1].impact, Impact::Med);
        QVERIFY(mkt_items[2].timed);
        QCOMPARE(mkt_items[2].when.toTimeZone(QTimeZone("America/New_York")).time(), QTime(13, 0));
    }
};

QTEST_APPLESS_MAIN(TestWhatsOnModel)
#include "test_whats_on_model.moc"
