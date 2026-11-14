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
        QCOMPARE(top[0].figures, QStringLiteral("Forecast 0.3% · prior 0.4%"));
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
        QCOMPARE(items[0].title, QStringLiteral("Acme (ACME)"));
        QCOMPARE(items[0].key, QStringLiteral("ACME"));
        QCOMPARE(items[0].deal_id, QStringLiteral("d-42")); // IPO Watch selects by deal, not ticker
        QCOMPARE(items[0].figures, QStringLiteral("$15–17 a share · NYSE"));
        QCOMPARE(items[0].impact, Impact::Low); // size unknown: not notable, not "small" either
        QVERIFY(std::isnan(items[0].size));
    }

    // ── Notable IPOs: size = shares x top of range, mechanically ──────────
    void ipo_notable_by_deal_size() {
        // The Nasdaq calendar row for Anthropic on 2026-11-13 (shares and
        // range as IPO Watch merges them), plus a small deal and filers.
        const auto deals = QJsonDocument::fromJson(R"([
          {"deal_id":"1407642-119728","company":"Anthropic, PBC","ticker":"ANT","status":"upcoming",
           "expected_date":"2026-11-19","price_range":"70.00-78.00","shares":825000000.0,
           "proposed_amount":74002500000.0,"exchange":"NASDAQ Global Select"},
          {"deal_id":"t-1","company":"Tracx Logis Ltd.","ticker":"TRCX","status":"upcoming",
           "expected_date":"2026-11-13","price_range":"18.00-22.00","shares":1000000.0,"exchange":"NASDAQ Capital"},
          {"deal_id":"m-1","company":"Mid Deal Inc.","ticker":"MID","status":"upcoming",
           "expected_date":"2026-11-20","price_range":"20.00","shares":60000000.0},
          {"deal_id":"f-1","company":"Big Filer Holdings, Inc.","ticker":"BIGF","status":"filed",
           "proposed_amount":25000000000.0,
           "projection":{"kind":"estimate","date":"2026-12-10","lo":"2026-12-01","hi":"2027-01-15","basis":"comps"}},
          {"deal_id":"f-2","company":"Oura Inc.","ticker":"OURA","status":"filed","proposed_amount":2530000000.0,
           "projection":{"kind":"long_shot","basis":"few priced"}},
          {"deal_id":"f-3","company":"Small Filer","status":"filed","proposed_amount":50000000.0,
           "projection":{"kind":"estimate","date":"2026-12-10","lo":"2026-12-01","hi":"2027-01-15"}}
        ])").array();
        const auto items = items_from_ipo_deals(deals, QDate(2026, 11, 13));
        QCOMPARE(items.size(), 4); // long shot (no date) and the small filer get no row
        const Item& ant = items[0];
        QCOMPARE(ant.title, QStringLiteral("Anthropic (ANT)"));
        QCOMPARE(ant.impact, Impact::High); // 825M x $78 = $64.4B
        QCOMPARE(ant.size, 825e6 * 78.0);
        QCOMPARE(ant.figures, QStringLiteral("$70–78 a share · raising $58–64B · Nasdaq"));
        QCOMPARE(ant.when_note, QStringLiteral("expected to price"));
        QVERIFY(ant.detail.contains("825M shares at $70.00–78.00 = $58B–$64B"));
        QCOMPARE(items[1].impact, Impact::Low); // $22M
        QCOMPARE(items[1].figures, QStringLiteral("$18–22 a share · raising $18–22M · Nasdaq"));
        QCOMPARE(items[2].impact, Impact::Med); // 60M x $20 = $1.2B, single price
        QCOMPARE(items[2].figures, QStringLiteral("$20 a share · raising $1.2B"));
        const Item& big = items[3];
        QCOMPARE(big.title, QStringLiteral("Big Filer (BIGF)"));
        QCOMPARE(big.impact, Impact::Med); // $25B, but a statistical window is never HIGH
        QCOMPARE(big.local_date(), QDate(2026, 12, 10));
        QCOMPARE(big.when_note, QStringLiteral("projected, window Dec 1 – Jan 15"));
        QCOMPARE(big.figures, QStringLiteral("up to $25B"));
        // Thresholds are the stated ones.
        QCOMPARE(ipo_impact(kHighIpo), Impact::High);
        QCOMPARE(ipo_impact(kHighIpo - 1), Impact::Med);
        QCOMPARE(ipo_impact(kNotableIpo), Impact::Med);
        QCOMPARE(ipo_impact(kNotableIpo - 1), Impact::Low);
        QCOMPARE(ipo_impact(NAN), Impact::Low);
        // A malformed range is no range (and no size).
        QJsonObject bad{{"price_range", "abc-12"}, {"shares", 1e6}};
        QVERIFY(std::isnan(ipo_deal_size(bad).hi));
        bad["price_range"] = "12.00-abc";
        QVERIFY(std::isnan(ipo_deal_size(bad).hi));
        bad["price_range"] = "18.00-12.00"; // inverted
        QVERIFY(std::isnan(ipo_deal_size(bad).hi));
        bad["price_range"] = "$10.00-12.00";
        QCOMPARE(ipo_deal_size(bad).hi, 12e6);
    }

    // ── Plain-language pieces ──────────────────────────────────────────────
    void names_dates_and_figures_read_without_a_legend() {
        QCOMPARE(short_company("NVIDIA Corporation"), QStringLiteral("NVIDIA"));
        QCOMPARE(short_company("The Home Depot, Inc."), QStringLiteral("Home Depot"));
        QCOMPARE(short_company("JPMorgan Chase & Co."), QStringLiteral("JPMorgan Chase"));
        QCOMPARE(short_company("Alibaba Group Holding Limited"), QStringLiteral("Alibaba"));
        QCOMPARE(short_company("Anthropic, PBC"), QStringLiteral("Anthropic"));
        QCOMPARE(short_company("Walmart Inc."), QStringLiteral("Walmart"));
        // Nasdaq's own forms: security descriptions and "(The)" at the end.
        QCOMPARE(short_company("Home Depot, Inc. (The)"), QStringLiteral("Home Depot"));
        QCOMPARE(short_company("Walt Disney Company (The)"), QStringLiteral("Walt Disney"));
        QCOMPARE(short_company("Cognex Corporation Common Stock"), QStringLiteral("Cognex"));
        QCOMPARE(short_company("Artesian Resources Corporation Class A Common Stock"),
                 QStringLiteral("Artesian Resources"));
        QCOMPARE(short_company("Common Stock"), QStringLiteral("Common Stock")); // never to nothing
        QCOMPARE(short_company("Inc."), QStringLiteral("Inc."));  // never to nothing
        QCOMPARE(short_company("Group Inc"), QStringLiteral("Group")); // first word always stays
        QCOMPARE(date_label(QDate(2026, 11, 17), QDate(2026, 11, 13)), QStringLiteral("Tue Nov 17"));
        QCOMPARE(date_label(QDate(2027, 1, 5), QDate(2026, 11, 13)), QStringLiteral("Tue Jan 5, 2027"));
        QCOMPARE(days_away(QDate(2026, 12, 9), QDate(2026, 11, 13)), QStringLiteral("Wed Dec 9, in 26 days"));
        QCOMPARE(days_away(QDate(2026, 11, 14), QDate(2026, 11, 13)), QStringLiteral("Sat Nov 14, tomorrow"));
        QCOMPARE(days_away(QDate(2026, 11, 13), QDate(2026, 11, 13)), QStringLiteral("Fri Nov 13, today"));
        QCOMPARE(session_label("BMO"), QStringLiteral("before open"));
        QCOMPARE(session_label("AMC"), QStringLiteral("after close"));
        QCOMPARE(session_label(""), QStringLiteral("time TBA"));
        QCOMPARE(figures_text("0.4%", "0.3%", "0.6%"), QStringLiteral("Actual 0.4% vs forecast 0.3% · prior 0.6%"));
        QCOMPARE(figures_text("0.4%", "", "0.6%"), QStringLiteral("Actual 0.4% · prior 0.6%"));
        QCOMPARE(figures_text("", "", "0.6%"), QStringLiteral("Prior 0.6%"));
        QCOMPARE(figures_text("", "0.3%", "", true), QStringLiteral("Actual pending · forecast 0.3%"));
        QCOMPARE(figures_text("", "", ""), QString());
        QCOMPARE(usd_compact(64.35e9), QStringLiteral("$64B"));
        QCOMPARE(usd_compact(1.2e9), QStringLiteral("$1.2B"));
        QCOMPARE(usd_compact(240e6), QStringLiteral("$240M"));
        QCOMPARE(usd_compact(1.5e6), QStringLiteral("$1.5M"));
        QCOMPARE(usd_compact(400e3), QStringLiteral("$400K")); // not "$0M"
        DealSize tiny;
        tiny.shares = 1e6;
        tiny.price_lo = 0.5;
        tiny.price_hi = 0.6;
        tiny.lo = 5e5;
        tiny.hi = 6e5;
        QCOMPARE(ipo_figures(tiny, {}), QStringLiteral("$0.50–0.60 a share · raising $500–600K"));
        QCOMPARE(plain_macro_title("Non-Farm Employment Change"), QStringLiteral("Nonfarm payrolls"));
        QCOMPARE(plain_macro_title("CPI m/m"), QStringLiteral("CPI m/m"));
        // The FOMC de-dupe still sees the decision row under its new name.
        QVERIFY(plain_macro_title("Federal Funds Rate").contains("Federal Funds Rate"));
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
        QCOMPARE(items[0].figures, QStringLiteral("EPS estimate 1.25"));
        QCOMPARE(items[4].figures, QStringLiteral("EPS 1.21 vs 1.15 expected (+5.2% surprise)"));
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
        QCOMPARE(items[0].figures, QStringLiteral("Actual pending · forecast 0.3% · prior 0.6%"));
        QVERIFY(!items[0].released);
        // October posted.
        fred["CPIAUCSL"].append({QDate(2026, 10, 1), 323.2});
        items = items_from_macro(events, now, true, QDate(2026, 11, 9), &fred);
        QCOMPARE(items[0].figures, QStringLiteral("Actual 0.3% vs forecast 0.3% · prior 0.2%"));
        QVERIFY(items[0].released);
        QVERIFY(items[0].detail.contains("FRED CPIAUCSL (Oct 2026)"));
        QVERIFY(items[0].detail.contains("08:30 ET"));
        // Not out yet (PPI, Friday): forecast/previous only.
        QCOMPARE(items[1].figures, QStringLiteral("Forecast 0.3% · prior 0.4%"));
        // Without a FRED download the row is exactly as before.
        items = items_from_macro(events, now, true, QDate(2026, 11, 9));
        QCOMPARE(items[0].figures, QStringLiteral("Forecast 0.3% · prior 0.6%"));
    }

    // ── Labels ─────────────────────────────────────────────────────────────
    // ctest runs this binary under several TZ values: every expectation here
    // must hold in New York, Los Angeles, Tokyo and Kolkata alike.
    void when_labels_always_carry_the_date() {
        const QTimeZone et("America/New_York");
        const QDateTime now(QDate(2026, 11, 13), QTime(6, 25), et); // Fri 06:25 ET
        Item it;
        it.timed = true;
        it.when = QDateTime(QDate(2026, 11, 13), QTime(8, 30), et);
        QCOMPARE(when_label(it, now), QStringLiteral("Fri Nov 13 · 8:30 ET · in 2h 05m"));
        it.when = now.addSecs(35 * 60);
        QCOMPARE(when_label(it, now), QStringLiteral("Fri Nov 13 · 7:00 ET · in 35m"));
        it.when = now.addSecs(-2 * 3600);
        QCOMPARE(when_label(it, now), QStringLiteral("Fri Nov 13 · 4:25 ET · 2h ago"));
        it.when = QDateTime(QDate(2026, 11, 13), QTime(1, 0), et); // 5h before: no hint
        QCOMPARE(when_label(it, now), QStringLiteral("Fri Nov 13 · 1:00 ET"));
        // 24 h is the edge of the countdown hint.
        it.when = now.addSecs(24 * 3600 - 60);
        QCOMPARE(when_label(it, now), QStringLiteral("Sat Nov 14 · 6:24 ET · in 23h 59m"));
        it.when = now.addSecs(24 * 3600);
        QCOMPARE(when_label(it, now), QStringLiteral("Sat Nov 14 · 6:25 ET"));
        it.when = QDateTime(QDate(2026, 11, 17), QTime(14, 0), et);
        QCOMPARE(when_label(it, now), QStringLiteral("Tue Nov 17 · 14:00 ET"));
        // An evening ET release is Tuesday beside its ET clock, even where it
        // is already Wednesday (Tokyo).
        it.when = QDateTime(QDate(2026, 11, 17), QTime(20, 0), et);
        QCOMPARE(when_label(it, now), QStringLiteral("Tue Nov 17 · 20:00 ET"));
        it.when = QDateTime(QDate(2027, 1, 27), QTime(14, 0), et);
        QCOMPARE(when_label(it, now), QStringLiteral("Wed Jan 27, 2027 · 14:00 ET"));
        Item e;
        e.source = Item::Source::Earnings;
        e.timed = false;
        e.session = "AMC";
        e.when = QDateTime(QDate(2026, 11, 17), QTime(16, 5), et);
        e.day = QDate(2026, 11, 17); // the listed ET date: Tuesday in every zone
        QCOMPARE(when_label(e, now), QStringLiteral("Tue Nov 17 · after close"));
        e.session = "BMO";
        QCOMPARE(when_label(e, now), QStringLiteral("Tue Nov 17 · before open"));
        e.session.clear();
        QCOMPARE(when_label(e, now), QStringLiteral("Tue Nov 17 · time TBA"));
        Item ipo;
        ipo.source = Item::Source::Ipo;
        ipo.when = QDateTime(QDate(2026, 11, 19), QTime(0, 0));
        ipo.when_note = "expected to price";
        QCOMPARE(when_label(ipo, now), QStringLiteral("Thu Nov 19 · expected to price"));
        QCOMPARE(when_label(ipo, now, false), QStringLiteral("Thu Nov 19"));
    }

    void next_line_names_the_company_and_the_date() {
        const QTimeZone et("America/New_York");
        const QDateTime now(QDate(2026, 11, 13), QTime(9, 0), et);
        const auto rows = QJsonDocument::fromJson(R"([
          {"symbol":"NVDA","name":"NVIDIA Corporation","date":"2026-11-17","session":"AMC","market_cap":4.0e12}
        ])").array();
        const auto e = items_from_earnings(rows, QDate(2026, 11, 9), {});
        QCOMPARE(next_text(e[0], now), QStringLiteral("NVIDIA earnings — Tue Nov 17 · after close"));
        Item cpi;
        cpi.timed = true;
        cpi.title = "CPI m/m";
        cpi.when = QDateTime(QDate(2026, 11, 18), QTime(8, 30), et);
        QCOMPARE(next_text(cpi, now), QStringLiteral("CPI m/m — Wed Nov 18 · 8:30 ET"));
        Item ipo;
        ipo.source = Item::Source::Ipo;
        ipo.name = "Anthropic";
        ipo.title = "Anthropic (ANT)";
        ipo.when = QDateTime(QDate(2026, 11, 19), QTime(0, 0));
        ipo.when_note = "expected to price";
        QCOMPARE(next_text(ipo, now), QStringLiteral("Anthropic IPO prices — Thu Nov 19"));
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
        const auto n = next_up({past, low, high2, high1}, now);
        QVERIFY(n.has_value());
        QCOMPARE(n->title, QStringLiteral("CPI"));
        QVERIFY(!next_up({past, low}, now).has_value());
        // Same instant: the bigger company is named.
        Item a = high1, b = high1;
        a.title = "small";
        a.size = 6e11;
        b.title = "big";
        b.size = 4e12;
        QCOMPARE(next_up({a, b}, now)->title, QStringLiteral("big"));
        QCOMPARE(next_up({b, a}, now)->title, QStringLiteral("big"));
        // An untimed item today counts until its nominal hour has passed.
        Item amc;
        amc.source = Item::Source::Earnings;
        amc.impact = Impact::High;
        amc.day = now.date();
        amc.when = now.addSecs(3600);
        QVERIFY(is_ahead(amc, now));
        amc.when = now.addSecs(-60);
        QVERIFY(!is_ahead(amc, now));
        amc.day = now.date().addDays(1);
        QVERIFY(is_ahead(amc, now));
        // An IPO pricing today is an all-day item: its midnight `when` is not
        // an hour it happens at — still NEXT all day (the $10B+ IPO day).
        Item ipo;
        ipo.source = Item::Source::Ipo;
        ipo.impact = Impact::High;
        ipo.title = "Anthropic (ANT)";
        ipo.when = QDateTime(now.date(), QTime(0, 0));
        QVERIFY(is_ahead(ipo, now));
        QCOMPARE(next_up({ipo}, now)->title, QStringLiteral("Anthropic (ANT)"));
        ipo.when = QDateTime(now.date().addDays(-1), QTime(0, 0));
        QVERIFY(!is_ahead(ipo, now));
    }

    void next_in_your_names_mode_follows_your_names() {
        const QDateTime now(QDate(2026, 11, 13), QTime(9, 0));
        Item market_high, mine_low, mine_later;
        market_high.timed = mine_low.timed = mine_later.timed = true;
        market_high.impact = Impact::High;
        market_high.title = "CPI";
        market_high.when = now.addDays(1);
        mine_low.mine = true;
        mine_low.title = "KO goes ex-dividend";
        mine_low.when = now.addDays(2);
        mine_later.mine = true;
        mine_later.impact = Impact::High;
        mine_later.when = now.addDays(4);
        const QVector<Item> items = {mine_later, market_high, mine_low};
        QCOMPARE(next_up(items, now, false)->title, QStringLiteral("CPI"));
        // YOUR NAMES: the market-wide CPI is not on your names; the next
        // event on them is, at any impact.
        QCOMPARE(next_up(items, now, true)->title, QStringLiteral("KO goes ex-dividend"));
        QVERIFY(!next_up({market_high}, now, true).has_value());
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
        // Next Wednesday: one BMO row (yours first, then largest impact), one AMC row.
        const auto month = s.value(Section::ThisMonth);
        QCOMPARE(month.size(), 2);
        QCOMPARE(month[0].items.size(), 3);
        QCOMPARE(month[0].head().symbol, QStringLiteral("LOW"));
        QCOMPARE(month[0].items[1].symbol, QStringLiteral("WMT"));
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

    // ── US TOP-TIER: routine items wait in the full view ─────────────────
    void top_tier_trims_routine_items_and_counts_them() {
        const QDate today(2026, 11, 13); // Friday
        const QTimeZone et("America/New_York");
        auto mk = [&](const QString& title, const QDate& d, Impact imp, bool mine = false) {
            Item i;
            i.title = title;
            i.timed = true;
            i.when = QDateTime(d, QTime(11, 30), et);
            i.impact = imp;
            i.mine = mine;
            return i;
        };
        Item bill13 = mk("13W bill auction", QDate(2026, 11, 16), Impact::Low);
        bill13.source = Item::Source::Auction;
        bill13.group = "BILLS";
        Item bill26 = bill13;
        bill26.title = "26W bill auction";
        Item ten = mk("10Y note auction", QDate(2026, 11, 18), Impact::Med);
        ten.source = Item::Source::Auction;
        Item month_end = mk("Month end — last trading day", QDate(2026, 11, 30), Impact::Low);
        month_end.timed = false; // all-day, as items_from_market_dates makes it: the local day
        month_end.when = QDateTime(QDate(2026, 11, 30), QTime(0, 0));
        const QVector<Item> items = {
            bill13, bill26, ten, month_end,
            mk("Small IPO", QDate(2026, 11, 19), Impact::Low),
            mk("KO goes ex-dividend", QDate(2026, 11, 20), Impact::Low, /*mine=*/true),
            mk("CPI m/m", QDate(2026, 11, 18), Impact::High),
        };
        QMap<Section, int> hidden;
        auto s = sectionize(items, today, Filter{false, true}, &hidden);
        QStringList kept;
        for (const auto& r : s.value(Section::ThisMonth))
            kept << r.head().title;
        // Bills, month end and the small IPO go; yours stays at any impact.
        QCOMPARE(kept, (QStringList{"10Y note auction", "CPI m/m", "KO goes ex-dividend"}));
        QCOMPARE(hidden.value(Section::ThisMonth), 4);
        // Full view: everything, the bills sharing one row; nothing hidden.
        hidden.clear();
        s = sectionize(items, today, Filter{false, false}, &hidden);
        QCOMPARE(s.value(Section::ThisMonth).size(), 6);
        QVERIFY(hidden.isEmpty());
    }

    void earnings_row_names_the_biggest_and_counts_the_rest() {
        Row r;
        auto e = [](const QString& name, double cap, Impact imp, bool mine = false) {
            Item i;
            i.source = Item::Source::Earnings;
            i.name = name;
            i.size = cap;
            i.impact = imp;
            i.mine = mine;
            return i;
        };
        r.items = {e("Target", 6e10, Impact::Med),   e("Walmart", 1e12, Impact::High), e("Unknown", NAN, Impact::Med),
                   e("Lowe's", 1.4e11, Impact::Med), e("Mine Co", 1e9, Impact::Med, true)};
        std::stable_sort(r.items.begin(), r.items.end(), earnings_before);
        QStringList order;
        for (const auto& i : r.items)
            order << i.name;
        // Yours, then impact, then size (unknown last).
        QCOMPARE(order, (QStringList{"Mine Co", "Walmart", "Lowe's", "Target", "Unknown"}));
        QCOMPARE(earnings_shown(r), 3); // "+2 more"
        QCOMPARE(earnings_shown(r, 10), 5);
        // Your names are never folded into "+N more".
        for (auto& i : r.items)
            i.mine = true;
        QCOMPARE(earnings_shown(r, 3), 5);
    }

    void dividends_insiders_auctions_market_items() {
        const QDate from(2026, 11, 9);
        const auto div = items_from_dividends(QJsonDocument::fromJson(R"([
            {"symbol":"MSFT","name":"Microsoft","ex_date":"2026-11-19","pay_date":"2026-12-10","amount":0.98},
            {"symbol":"OLD","ex_date":"2026-11-02","amount":0.5}])").array(), from);
        QCOMPARE(div.size(), 1);
        QVERIFY(div[0].mine);
        QCOMPARE(div[0].figures, QStringLiteral("$0.98 a share"));
        QCOMPARE(div[0].title, QStringLiteral("Microsoft (MSFT) goes ex-dividend"));
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
