// test_macro_releases.cpp — the printed number for a macro release (FRED),
// and Treasury auctions (TreasuryDirect). Fixtures shaped from live answers
// taken 2026-11-13.

#include "services/calendar/MacroReleases.h"
#include "services/calendar/TreasuryAuctions.h"

#include <QJsonDocument>
#include <QTest>

using namespace finterm::macro_releases;
namespace ta = finterm::treasury_auctions;

namespace {
QVector<Obs> monthly(const QDate& first, const QVector<double>& v) {
    QVector<Obs> out;
    for (int i = 0; i < v.size(); ++i)
        out.append({first.addMonths(i), v[i]});
    return out;
}
} // namespace

class TestMacroReleases : public QObject {
    Q_OBJECT
  private slots:
    void csv_parse_skips_missing_values() {
        const auto obs = parse_fred_csv("observation_date,PAYEMS\n2026-08-01,159068\n2026-09-01,.\n"
                                        "2026-10-01,159293\n\n");
        QCOMPARE(obs.size(), 2);
        QCOMPARE(obs[0].date, QDate(2026, 8, 1));
        QCOMPARE(obs[1].value, 159293.0);
        QVERIFY(parse_fred_csv("<html>error</html>").isEmpty());
    }

    void spec_mapping() {
        QCOMPARE(spec_for("CPI m/m")->fred_id, QStringLiteral("CPIAUCSL"));
        QCOMPARE(spec_for("Core CPI m/m")->fred_id, QStringLiteral("CPILFESL"));
        QCOMPARE(spec_for("CPI y/y")->fred_id, QStringLiteral("CPIAUCNS"));
        QCOMPARE(spec_for("CPI y/y")->transform, Transform::PctYoY);
        QCOMPARE(spec_for("Non-Farm Employment Change")->transform, Transform::DiffK);
        QCOMPARE(spec_for("Unemployment Claims")->freq, Frequency::Weekly);
        QCOMPARE(spec_for("Advance GDP q/q")->freq, Frequency::Quarterly);
        QCOMPARE(spec_for("JOLTS Job Openings")->lag_months, 2);
        QCOMPARE(spec_for("Core PPI m/m")->fred_id, QStringLiteral("PPIFES"));
        QVERIFY(!spec_for("ISM Manufacturing PMI"));     // not on FRED
        QVERIFY(!spec_for("Existing Home Sales"));        // NAR, not FRED
        QVERIFY(!spec_for("Continuing Jobless Claims"));
        QVERIFY(!spec_for("Unemployment Rate y/y"));      // exact title only
    }

    void expected_period_rules() {
        const SeriesSpec cpi{"CPIAUCSL", Transform::PctMoM};
        QCOMPARE(expected_period(cpi, QDate(2026, 11, 10)), QDate(2026, 10, 1));
        QCOMPARE(expected_period(cpi, QDate(2027, 1, 13)), QDate(2026, 12, 1));
        const SeriesSpec jolts{"JTSJOL", Transform::LevelMFromK, Frequency::Monthly, 2};
        QCOMPARE(expected_period(jolts, QDate(2026, 11, 3)), QDate(2026, 9, 1));
        const SeriesSpec gdp{"A191RL1Q225SBEA", Transform::LevelPct, Frequency::Quarterly, 0};
        QCOMPARE(expected_period(gdp, QDate(2026, 10, 29)), QDate(2026, 7, 1)); // advance Q3
        QCOMPARE(expected_period(gdp, QDate(2026, 12, 22)), QDate(2026, 7, 1)); // third estimate Q3
        QCOMPARE(expected_period(gdp, QDate(2027, 1, 28)), QDate(2026, 10, 1)); // advance Q4
        const SeriesSpec claims{"ICSA", Transform::LevelK, Frequency::Weekly, 0};
        QVERIFY(is_expected(claims, QDate(2026, 11, 7), QDate(2026, 11, 12)));  // Thu, w/e Sat
        QVERIFY(!is_expected(claims, QDate(2026, 10, 31), QDate(2026, 11, 12))); // last week's
        QVERIFY(!is_expected(claims, QDate(2026, 11, 14), QDate(2026, 11, 12))); // future
    }

    void cpi_actual_only_once_fred_has_the_month() {
        const SeriesSpec cpi = *spec_for("CPI m/m");
        // Through September only: the Nov 10 release (October CPI) is NOT out
        // on FRED yet — printing Sep's change as the actual is the bug.
        auto obs = monthly(QDate(2026, 7, 1), {320.0, 321.6, 322.2});
        auto p = printed(cpi, obs, QDate(2026, 11, 10));
        QVERIFY(p.actual.isEmpty());
        QVERIFY(p.prior.isEmpty());
        QVERIFY(!p.period.isValid());
        obs.append({QDate(2026, 10, 1), 323.2});
        p = printed(cpi, obs, QDate(2026, 11, 10));
        QCOMPARE(p.actual, QStringLiteral("0.3%")); // 323.2/322.2
        QCOMPARE(p.prior, QStringLiteral("0.2%"));  // 322.2/321.6
        QCOMPARE(p.period, QDate(2026, 10, 1));
        QCOMPARE(period_label(cpi, p.period), QStringLiteral("Oct 2026"));
    }

    void payrolls_claims_unemployment_formats() {
        const auto pay = printed(*spec_for("Non-Farm Employment Change"),
                                 monthly(QDate(2026, 8, 1), {159068, 159128, 159293}), QDate(2026, 11, 6));
        QCOMPARE(pay.actual, QStringLiteral("165K"));
        QCOMPARE(pay.prior, QStringLiteral("60K"));
        const auto neg = printed(*spec_for("Non-Farm Employment Change"),
                                 monthly(QDate(2026, 9, 1), {159128, 159096}), QDate(2026, 11, 6));
        QCOMPARE(neg.actual, QStringLiteral("-32K"));
        const auto ur = printed(*spec_for("Unemployment Rate"), monthly(QDate(2026, 9, 1), {4.3, 4.4}),
                                QDate(2026, 11, 6));
        QCOMPARE(ur.actual, QStringLiteral("4.4%"));
        QCOMPARE(ur.prior, QStringLiteral("4.3%"));
        const QVector<Obs> icsa = {{QDate(2026, 10, 31), 189000}, {QDate(2026, 11, 7), 227400}};
        const auto cl = printed(*spec_for("Unemployment Claims"), icsa, QDate(2026, 11, 12));
        QCOMPARE(cl.actual, QStringLiteral("227K"));
        QCOMPARE(cl.prior, QStringLiteral("189K"));
        QCOMPARE(period_label(*spec_for("Unemployment Claims"), cl.period), QStringLiteral("w/e Nov 7"));
        const auto jolts = printed(*spec_for("JOLTS Job Openings"), monthly(QDate(2026, 8, 1), {7227, 7670}),
                                   QDate(2026, 11, 3));
        QCOMPARE(jolts.actual, QStringLiteral("7.67M"));
    }

    void yoy_needs_the_same_month_a_year_back() {
        QVector<double> v;
        for (int i = 0; i < 13; ++i)
            v << 300.0 + i;
        const auto obs = monthly(QDate(2025, 10, 1), v); // Oct 2025 … Oct 2026
        const auto p = printed(*spec_for("CPI y/y"), obs, QDate(2026, 11, 10));
        QCOMPARE(p.actual, QStringLiteral("4.0%")); // 312/300
        QVERIFY(p.prior.isEmpty());                  // Sep 2025 not in the window → absent, not 0
        // A gap: 12 observations back is not the same month — no number.
        auto gappy = obs;
        gappy.remove(3);
        QVERIFY(std::isnan(value_at(gappy, gappy.size() - 1, Transform::PctYoY)));
        // m/m and payroll change across a skipped month (FRED "." — CPI Oct
        // 2025) would be a two-month change: no number.
        const QVector<Obs> skip = {{QDate(2025, 9, 1), 324.0}, {QDate(2025, 11, 1), 325.0}};
        QVERIFY(std::isnan(value_at(skip, 1, Transform::PctMoM)));
        QVERIFY(std::isnan(value_at(skip, 1, Transform::DiffK)));
    }

    void gdp_quarterly() {
        const QVector<Obs> g = {{QDate(2026, 4, 1), 3.8}, {QDate(2026, 7, 1), 2.8}};
        const auto p = printed(*spec_for("Advance GDP q/q"), g, QDate(2026, 10, 29));
        QCOMPARE(p.actual, QStringLiteral("2.8%"));
        QCOMPARE(p.prior, QStringLiteral("3.8%"));
        QCOMPARE(period_label(*spec_for("Advance GDP q/q"), p.period), QStringLiteral("Q3 2026"));
    }

    void negative_zero_reads_zero() { QCOMPARE(format_value(Transform::PctMoM, -0.04), QStringLiteral("0.0%")); }

    // ── TreasuryDirect ────────────────────────────────────────────────────
    void auctions_scope_merge_and_figures() {
        const QJsonArray upcoming = QJsonDocument::fromJson(R"([
          {"cusip":"912797TV9","securityType":"Bill","securityTerm":"13-Week","reopening":"Yes",
           "originalSecurityTerm":"52-Week","competitiveAuctionCloseDate":"2026-11-16T00:00:00",
           "competitiveAuctionCloseTime":"11:30 AM","offeringAmount":"95000000000","cashManagementBill":"No"},
          {"cusip":"912797AA1","securityType":"Bill","securityTerm":"6-Week","competitiveAuctionCloseDate":"2026-11-17T00:00:00",
           "offeringAmount":"100000000000","cashManagementBill":"No"},
          {"cusip":"912797CM0","securityType":"Bill","securityTerm":"17-Week","competitiveAuctionCloseDate":"2026-11-18T00:00:00",
           "cashManagementBill":"Yes"},
          {"cusip":"91282CPA1","securityType":"Note","securityTerm":"9-Year 8-Month","reopening":"Yes",
           "originalSecurityTerm":"10-Year","competitiveAuctionCloseDate":"2026-11-19T00:00:00",
           "competitiveAuctionCloseTime":"01:00 PM","offeringAmount":"19000000000","floatingRate":"No"},
          {"cusip":"91282CPB9","securityType":"Note","securityTerm":"2-Year","competitiveAuctionCloseDate":"2026-11-23T00:00:00",
           "competitiveAuctionCloseTime":"","offeringAmount":"","floatingRate":"No"},
          {"cusip":"91282CPC7","securityType":"Note","securityTerm":"1-Year 11-Month","reopening":"Yes",
           "originalSecurityTerm":"2-Year","competitiveAuctionCloseDate":"2026-11-24T00:00:00","floatingRate":"Yes"}
        ])").array();
        const QJsonArray auctioned = QJsonDocument::fromJson(R"([
          {"cusip":"912810UA1","securityType":"Bond","securityTerm":"30-Year","competitiveAuctionCloseDate":"2026-11-12T00:00:00",
           "competitiveAuctionCloseTime":"01:00 PM","offeringAmount":"25000000000","highYield":"5.441",
           "bidToCoverRatio":"2.36","floatingRate":"No"},
          {"cusip":"912797TV9","securityType":"Bill","securityTerm":"13-Week","reopening":"Yes",
           "originalSecurityTerm":"52-Week","competitiveAuctionCloseDate":"2026-11-16T00:00:00",
           "competitiveAuctionCloseTime":"11:30 AM","offeringAmount":"95000000000","highDiscountRate":"3.955",
           "highInvestmentRate":"4.022","bidToCoverRatio":"2.94","cashManagementBill":"No"}
        ])").array();
        const auto a = ta::parse(upcoming, auctioned);
        QStringList labels;
        for (const auto& x : a)
            labels << x.label;
        // 6-week bill and the CMB are out of scope; the bill result replaced
        // its announcement (one row, not two).
        QCOMPARE(labels, (QStringList{"30Y bond", "13W bill", "10Y note (reopening)", "2Y note",
                                      "2Y FRN (reopening)"}));
        QCOMPARE(ta::figures(a[0]), QStringLiteral("Yield 5.441% · bid-to-cover 2.36"));
        QVERIFY(a[0].has_result());
        QCOMPARE(a[0].close.toTimeZone(QTimeZone("America/New_York")).time(), QTime(13, 0));
        QCOMPARE(ta::figures(a[1]), QStringLiteral("Discount rate 3.955% · bid-to-cover 2.94"));
        QCOMPARE(a[2].years, 10);
        QCOMPARE(ta::figures(a[2]), QStringLiteral("$19B")); // announced, no result yet
        QVERIFY(!a[3].announced);                           // tentative schedule
        QVERIFY(!a[3].close.isValid());
        QCOMPARE(ta::figures(a[3]), QString());
        QVERIFY(std::isnan(a[3].high_yield)); // absent, not 0
    }
};

QTEST_APPLESS_MAIN(TestMacroReleases)
#include "test_macro_releases.moc"
