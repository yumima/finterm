// Futures screen arithmetic: curve shape and carry, COT summary, trade ticket.
#include "services/futures/FuturesMath.h"

#include <QJsonDocument>
#include <QtTest>

using namespace finterm::services::futures;

namespace {
CurveContract k(const QString& sym, int y, int m, double price, const QDate& expiry = {}, bool stale = false) {
    CurveContract c;
    c.symbol = sym;
    c.year = y;
    c.month = m;
    c.price = price;
    c.expiry = expiry;
    c.stale = stale;
    return c;
}
} // namespace

class TestFuturesMath : public QObject {
    Q_OBJECT
  private slots:
    void every_contract_has_usable_terms() {
        QSet<QString> seen;
        for (const auto& c : all_contracts()) {
            QVERIFY2(!seen.contains(c.root), qPrintable(c.root));
            seen.insert(c.root);
            QVERIFY2(c.tick > 0 && c.point_value > 0, qPrintable(c.root));
            QVERIFY2(groups().contains(c.group), qPrintable(c.root));
            QVERIFY2(!c.unit.isEmpty() && !c.size.isEmpty(), qPrintable(c.root));
            QVERIFY2(c.cftc.isEmpty() || c.cftc.size() == 6, qPrintable(c.root));
        }
        for (const auto& g : groups())
            QVERIFY2(!contracts_in(g).isEmpty(), qPrintable(g));
    }

    void tick_values_are_the_exchange_ones() {
        // Dollars per tick, per contract — the published figures.
        const QHash<QString, double> want = {
            {"ES", 12.50}, {"NQ", 5.00},  {"CL", 10.00}, {"NG", 10.00}, {"GC", 10.00}, {"SI", 25.00},
            {"HG", 12.50}, {"ZC", 12.50}, {"ZS", 12.50}, {"ZL", 6.00},  {"ZM", 10.00}, {"KC", 18.75},
            {"SB", 11.20}, {"CC", 10.00}, {"CT", 5.00},  {"LE", 10.00}, {"HE", 10.00}, {"ZN", 15.625},
            {"ZT", 7.8125}, {"ZB", 31.25}, {"6E", 6.25}, {"6J", 6.25},  {"BTC", 25.00}, {"SR3", 6.25},
            {"RB", 4.20},  {"LBR", 13.75}, {"PA", 50.00}, {"GF", 12.50}, {"ZQ", 10.4175},
        };
        for (auto it = want.begin(); it != want.end(); ++it) {
            const auto* c = find_contract(it.key());
            QVERIFY2(c, qPrintable(it.key()));
            QVERIFY2(std::abs(c->tick * c->point_value - it.value()) < 1e-6, qPrintable(it.key()));
        }
    }

    void price_decimals_show_the_tick() {
        QCOMPARE(price_decimals(0.25), 2);
        QCOMPARE(price_decimals(1.0), 0);
        QCOMPARE(price_decimals(0.0001), 4);
        QCOMPARE(price_decimals(0.0000005), 7);
        QCOMPARE(price_decimals(0.015625), 5);   // Treasury 64ths: what the feed carries
        QCOMPARE(price_decimals(0.00390625), 5);
        QCOMPARE(price_decimals(0.0), 2);
    }

    void parse_keeps_null_absent() {
        const auto doc = QJsonDocument::fromJson(R"({"root":"CL","front":"CLF27.NYM","currency":"USD","as_of":10,
            "contracts":[
              {"symbol":"CLF27.NYM","month":"2027-01","price":85.6,"prev_close":87.29,"change":-1.69,"pct":-1.94,
               "volume":86464,"oi":257222,"expiry":"2026-12-21","bid":85.5,"ask":85.6,"ts":5,"stale":false},
              {"symbol":"CLG27.NYM","month":"2027-02","price":84.2,"prev_close":null,"change":null,"pct":null,
               "volume":null,"oi":null,"expiry":null,"bid":null,"ask":null,"ts":null,"stale":true},
              {"symbol":"CLH27.NYM","month":"2027-03","price":null}]})");
        const Curve c = parse_curve(doc.object());
        QCOMPARE(c.contracts.size(), 2); // the unpriced month is no row
        QCOMPARE(c.contracts[0].month_label(), QStringLiteral("Jan 27"));
        QCOMPARE(c.contracts[0].code(), QStringLiteral("CLF27"));
        QCOMPARE(c.contracts[0].expiry, QDate(2026, 12, 21));
        QVERIFY(std::isnan(c.contracts[1].oi));
        QVERIFY(std::isnan(c.contracts[1].volume));
        QVERIFY(std::isnan(c.contracts[1].change));
        QVERIFY(!c.contracts[1].expiry.isValid());
        QVERIFY(c.contracts[1].stale);
    }

    void carry_annualises_by_the_days_between_expiries() {
        const auto a = k("A", 2026, 12, 100.0, QDate(2026, 12, 20));
        const auto b = k("B", 2027, 1, 99.0, QDate(2027, 1, 20)); // 31 days later
        const Carry c = carry_between(a, b);
        QCOMPARE(c.spread, -1.0);
        QCOMPARE(c.spread_pct, -1.0);
        QVERIFY(std::abs(c.annualized_pct - (-1.0 * 365.0 / 31.0)) < 1e-9);
        // No expiry: no annualised figure, rather than an assumed month length.
        const Carry d = carry_between(a, k("B", 2027, 1, 99.0));
        QCOMPARE(d.spread_pct, -1.0);
        QVERIFY(std::isnan(d.annualized_pct));
        QVERIFY(std::isnan(carry_between(a, k("B", 2027, 1, kNaN)).spread));
    }

    void summary_reads_shape_and_roll_yield() {
        Curve c;
        c.front = "M2";
        c.contracts = {k("M1", 2026, 11, 101.0, QDate(2026, 11, 20)), k("M2", 2026, 12, 100.0, QDate(2026, 12, 20)),
                       k("M3", 2027, 1, 99.5, QDate(2027, 1, 20), true), // stale: skipped as the next month
                       k("M4", 2027, 2, 99.0, QDate(2027, 2, 20)),  k("M5", 2027, 12, 90.0, QDate(2027, 12, 20)),
                       k("M6", 2028, 12, 85.0, QDate(2028, 12, 20))};
        const CurveSummary s = summarize(c);
        QCOMPARE(s.anchor, 1); // the vendor's front, not the nearest listed month
        QCOMPARE(s.next, 3);
        QCOMPARE(s.far, 4); // the month a year past the front
        QCOMPARE(s.shape, Shape::Backwardation);
        QVERIFY(s.roll_yield_pct > 0); // a long earns the roll in backwardation
        QVERIFY(std::abs(s.roll_yield_pct - 1.0 * 365.0 / 62.0) < 1e-9);

        for (auto& x : c.contracts)
            x.price = 100.0 + (x.year * 12 + x.month - 2026 * 12 - 12);
        QCOMPARE(summarize(c).shape, Shape::Contango);
        QVERIFY(summarize(c).roll_yield_pct < 0);
        for (auto& x : c.contracts)
            x.price = 100.0;
        QCOMPARE(summarize(c).shape, Shape::Flat);

        Curve one;
        one.contracts = {k("M1", 2026, 12, 100.0)};
        QCOMPARE(summarize(one).shape, Shape::Unknown);
        QCOMPARE(summarize(one).next, -1);
        QCOMPARE(summarize(Curve{}).anchor, -1);
    }

    void implied_rate_is_100_minus_price() {
        QCOMPARE(implied_rate(95.83), 100.0 - 95.83);
        QVERIFY(std::isnan(implied_rate(kNaN)));
    }

    void cot_summary() {
        CotReport rep;
        for (int i = 0; i < 30; ++i) {
            CotRow r;
            r.date = QDate(2026, 1, 6).addDays(7 * i);
            r.oi = 1000 + i;
            r.nc_long = 100 + 10 * i; // net rises 10 a week: 50 … 340
            r.nc_short = 50;
            r.c_long = 40;
            r.c_short = 90 + 10 * i;
            rep.rows.append(r);
        }
        const CotSummary s = summarize_cot(rep);
        QCOMPARE(s.weeks, 30);
        QCOMPARE(s.nc_net, 340.0);
        QCOMPARE(s.nc_net_change, 10.0);
        QCOMPARE(s.c_net, -340.0);
        QCOMPARE(s.oi_change, 1.0);
        QCOMPARE(s.nc_range_pct, 100.0);
        QVERIFY(std::abs(s.nc_net_pct_oi - 340.0 / 1029.0 * 100.0) < 1e-9);
        QVERIFY(std::isnan(s.nr_net)); // not reported: absent, not zero

        rep.rows.resize(10); // too little history for a range reading
        QVERIFY(std::isnan(summarize_cot(rep).nc_range_pct));
        QVERIFY(!summarize_cot(CotReport{}).date.isValid());
    }

    void cot_parse_keeps_null_absent() {
        const auto doc = QJsonDocument::fromJson(
            R"({"code":"088691","market":"GOLD","rows":[{"date":"2026-11-03","oi":10,"nc_long":5,"nc_short":null},
                {"date":"bad","oi":1}]})");
        const CotReport rep = parse_cot(doc.object());
        QCOMPARE(rep.rows.size(), 1);
        QVERIFY(std::isnan(rep.rows[0].nc_short));
        QVERIFY(std::isnan(rep.rows[0].nc_net()));
    }

    void ticket() {
        const auto* cl = find_contract("CL");
        Ticket t;
        t.contracts = 2;
        t.entry = 80.00;
        t.stop = 79.50;
        t.target = 81.50;
        TicketResult r = evaluate(t, *cl);
        QVERIFY(r.problem.isEmpty());
        QCOMPARE(r.notional, 160000.0);
        QCOMPARE(r.tick_value, 20.0);
        QVERIFY(std::abs(r.risk_usd - 1000.0) < 1e-6);
        QVERIFY(std::abs(r.risk_ticks - 50.0) < 1e-6);
        QVERIFY(std::abs(r.reward_usd - 3000.0) < 1e-6);
        QVERIFY(std::abs(r.reward_to_risk - 3.0) < 1e-6);

        t.is_long = false; // the same levels are on the wrong sides for a short
        QVERIFY(!evaluate(t, *cl).problem.isEmpty());
        t.stop = 80.50;
        t.target = 78.00;
        r = evaluate(t, *cl);
        QVERIFY(r.problem.isEmpty());
        QVERIFY(std::abs(r.risk_usd - 1000.0) < 1e-6);
        QVERIFY(std::abs(r.reward_usd - 4000.0) < 1e-6);

        Ticket bare;
        bare.contracts = 1;
        bare.entry = 80.0;
        r = evaluate(bare, *cl);
        QVERIFY(r.problem.isEmpty());
        QVERIFY(std::isnan(r.risk_usd) && std::isnan(r.reward_to_risk));

        bare.contracts = 1.5;
        QVERIFY(!evaluate(bare, *cl).problem.isEmpty());
        bare.contracts = 1;
        bare.entry = kNaN;
        QVERIFY(!evaluate(bare, *cl).problem.isEmpty());

        // Corn is quoted in cents: 450¢ on 5,000 bushels is $22,500.
        Ticket corn;
        corn.contracts = 1;
        corn.entry = 450.0;
        QCOMPARE(evaluate(corn, *find_contract("ZC")).notional, 22500.0);
        QCOMPARE(contracts_from_units(100.0, *find_contract("ZC")), 2.0);
    }
};

QTEST_APPLESS_MAIN(TestFuturesMath)
#include "test_futures_math.moc"
