// ER Report: the figures it derives, the findings it reads off them, and the
// prompts/parsers around the model's prose.
#include "screens/equity_research/AiForecastMath.h"
#include "screens/equity_research/ReportModel.h"
#include "screens/equity_research/ReportPrompts.h"
#include "services/equity/AnalystTargets.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QTimeZone>
#include <QtTest>

using namespace finterm::screens::report;
namespace eq = finterm::services::equity;

namespace {

FiscalYear fy(const QString& period, double revenue, double op, double ni) {
    FiscalYear y;
    y.period = period;
    y.revenue = revenue;
    y.operating_income = op;
    y.net_income = ni;
    return y;
}

bool has(const QVector<Finding>& f, Tone tone, const QString& title_part) {
    for (const auto& x : f)
        if (x.tone == tone && x.title.contains(title_part, Qt::CaseInsensitive))
            return true;
    return false;
}

} // namespace

class TestReportModel : public QObject {
    Q_OBJECT
  private slots:
    void analyst_targets_carry_their_age() {
        using namespace finterm::services::equity;
        // IONQ, 2026-11-16: a $54 mean target beside a $26.6 price.
        const auto o = QJsonDocument::fromJson(R"({"firms":[
            {"firm":"Jefferies","date":"2026-11-07","target":65,"prior":80,"action":"Lowers"},
            {"firm":"B. Riley","date":"2026-11-06","target":70,"prior":75},
            {"firm":"Cantor","date":"2026-11-06","target":60,"prior":70},
            {"firm":"Needham","date":"2026-11-06","target":48,"prior":65},
            {"firm":"B of A","date":"2026-09-28","target":60,"prior":null},
            {"firm":"JP Morgan","date":"2026-05-07","target":50,"prior":42},
            {"firm":"","date":"2026-11-01","target":10},
            {"firm":"No Date","date":"","target":10},
            {"firm":"No Target","date":"2026-11-01"}]})").object();
        const auto t = parse_firm_targets(o);
        QCOMPARE(t.size(), 6); // rows without a firm, a date or a target are not targets
        QVERIFY(std::isnan(t[4].prior) && !t[4].raised() && !t[4].lowered()); // a first target moved nothing

        const auto f = target_freshness(t, QDate(2026, 11, 16), 26.6, 35.0);
        QCOMPARE(f.firms, 6);
        QCOMPARE(f.newest_days, 9);
        QCOMPARE(f.fresh_30d, 4);
        QCOMPARE(f.lowered_90d, 4);
        QCOMPARE(f.raised_90d, 0); // JP Morgan's raise is older than ninety days
        QVERIFY(std::abs(f.fresh_mean - 60.75) < 1e-9);
        QVERIFY(std::abs(f.price_since_newest_pct - (-24.0)) < 1e-9);
        QVERIFY(targets_overtaken(f));
        const QStringList lines = target_age_lines(f);
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines[0].contains("7 Nov") && lines[0].contains("9 days ago") && lines[0].contains("4 of 6"));
        QVERIFY(lines[1].contains("4 cut, 0 raised"));
        QVERIFY(lines[2].contains("-24.0%") && lines[2].contains("predate"));

        // Without the close the targets were set against, no move is claimed.
        const auto g = target_freshness(t, QDate(2026, 11, 16), 26.6, std::numeric_limits<double>::quiet_NaN());
        QVERIFY(std::isnan(g.price_since_newest_pct) && !targets_overtaken(g));
        QCOMPARE(target_age_lines(g).size(), 2);
        // Two fresh targets are not a consensus.
        const auto two = target_freshness(t.mid(0, 2), QDate(2026, 11, 16), 26.6, 35.0);
        QVERIFY(std::isnan(two.fresh_mean));
        // Nothing listed: nothing said.
        QVERIFY(target_age_lines(target_freshness({}, QDate(2026, 11, 16), 26.6, 35.0)).isEmpty());
    }

    void arithmetic_needs_its_inputs() {
        QCOMPARE(pct_of(25, 100), 25.0);
        QVERIFY(std::isnan(pct_of(kNaN, 100)));
        QVERIFY(std::isnan(pct_of(25, 0)));
        QCOMPARE(growth_pct(110, 100), 10.0);
        QVERIFY(std::isnan(growth_pct(50, -100))); // a loss turning to profit is not a growth rate
        QVERIFY(std::isnan(growth_pct(50, 0)));
        QVERIFY(std::abs(cagr_pct(100, 121, 2) - 10.0) < 1e-9);
        QVERIFY(std::isnan(cagr_pct(-1, 121, 2)));
        QCOMPARE(median({3, kNaN, 1, 2}), 2.0);
        QCOMPARE(median({4, 1}), 2.5);
        QVERIFY(std::isnan(median({kNaN})));
        QCOMPARE(compact(215.938e9), QStringLiteral("215.9B"));
        QCOMPARE(compact(-1.5e12), QStringLiteral("−1.5T"));
        QCOMPARE(compact(kNaN), QStringLiteral("—"));
        QCOMPARE(compact(0.0), QStringLiteral("0"));
    }

    void extras_keep_null_absent() {
        const auto doc = QJsonDocument::fromJson(R"({"symbol":"JPM","financial_currency":"USD","as_of":5,
            "quarters":[{"period":"2026-09-30","revenue":50172000000,"gross_profit":null,"eps":5.57,"capex":null},
                        {"period":"","revenue":1}],
            "actions":[{"date":"2026-11-02","firm":"UBS","to_grade":"Buy","from_grade":"Buy","action":"main"},
                       {"date":"bad","firm":"X"}],
            "rec_trend":[{"period":"0m","strong_buy":5,"buy":10,"hold":11,"sell":0,"strong_sell":null},
                         {"period":"-1m","strong_buy":null,"buy":null,"hold":null,"sell":null,"strong_sell":null}],
            "actions_error":"refused"})");
        const Extras x = parse_extras(doc.object());
        QCOMPARE(x.quarters.size(), 1);
        QVERIFY(std::isnan(x.quarters[0].gross_profit)); // a bank has none: absent, not zero
        QVERIFY(std::isnan(x.quarters[0].capex));
        QCOMPARE(x.quarters[0].eps, 5.57);
        QCOMPARE(x.quarters[0].label(), QStringLiteral("Sep 26"));
        QCOMPARE(x.actions.size(), 1);
        QCOMPARE(x.rec_trend.size(), 1); // the all-null month is dropped
        QCOMPARE(x.rec_trend[0].strong_sell, -1);
        QCOMPARE(x.rec_trend[0].total(), 26);
        QCOMPARE(x.problems.size(), 1);
    }

    void fiscal_years_join_the_statements() {
        eq::FinancialsData fd;
        auto obj = [](const char* json) { return QJsonDocument::fromJson(json).object(); };
        // Newest first, as the service delivers; an all-empty oldest column.
        fd.income_statement = {
            {"2026-01-25 00:00:00", obj(R"({"Total Revenue":200,"Gross Profit":140,"Operating Income":120,"Net Income":100,"Research And Development":20})")},
            {"2025-01-26 00:00:00", obj(R"({"Total Revenue":100,"Operating Income":50,"Net Income":40})")},
            {"2024-01-28 00:00:00", obj(R"({})")}};
        fd.cash_flow = {{"2026-01-25 00:00:00", obj(R"({"Operating Cash Flow":90,"Capital Expenditure":-10,"Free Cash Flow":80,"Repurchase Of Capital Stock":-30})")}};
        fd.balance_sheet = {{"2026-01-25 00:00:00", obj(R"({"Cash And Cash Equivalents":60,"Total Debt":10})")}};
        const auto years = fiscal_years(fd);
        QCOMPARE(years.size(), 2); // the empty year is not listed
        QCOMPARE(years[0].label(), QStringLiteral("FY25"));
        QCOMPARE(years[1].label(), QStringLiteral("FY26"));
        QCOMPARE(years[1].gross_margin(), 70.0);
        QCOMPARE(years[1].operating_margin(), 60.0);
        QCOMPARE(years[1].fcf_margin(), 40.0);
        QCOMPARE(years[1].cash, 60.0);
        QCOMPARE(outflow(years[1].buybacks), 30.0);
        QVERIFY(std::isnan(years[0].gross_margin())); // not reported that year
        QVERIFY(std::isnan(years[0].fcf));
        QVERIFY(std::isnan(outflow(years[0].capex)));
    }

    void year_ago_quarter_is_found_by_date() {
        auto q = [](const char* period, double revenue) {
            Quarter x;
            x.period = period;
            x.revenue = revenue;
            return x;
        };
        // Five consecutive quarters: the fifth is a year back.
        QVector<Quarter> full = {q("2026-09-30", 120), q("2026-06-30", 1), q("2026-03-31", 1), q("2025-12-31", 1), q("2025-09-30", 100)};
        QCOMPARE(year_ago_index(full, 0), 4);
        QCOMPARE(latest_quarter_yoy_pct(full), 20.0);
        // The vendor omitted a quarter: position 4 is now fifteen months back, and is not used.
        QVector<Quarter> gap = {q("2026-09-30", 120), q("2026-06-30", 1), q("2025-12-31", 1), q("2025-09-30", 100), q("2025-06-30", 60)};
        QCOMPARE(year_ago_index(gap, 0), 3);
        QCOMPARE(latest_quarter_yoy_pct(gap), 20.0);
        QVector<Quarter> none = {q("2026-09-30", 120), q("2026-06-30", 1), q("2025-06-30", 60)};
        QVERIFY(std::isnan(latest_quarter_yoy_pct(none)));
        // A 52/53-week fiscal year ends a few days off the anniversary.
        QVector<Quarter> drift = {q("2026-10-25", 110), q("2025-10-26", 100)};
        QCOMPARE(latest_quarter_yoy_pct(drift), 10.0);
    }

    void latest_quarter_review() {
        auto q = [](const char* period, double revenue, double op, double eps) {
            Quarter x;
            x.period = period;
            x.revenue = revenue;
            x.operating_income = op;
            x.eps = eps;
            return x;
        };
        const QVector<Quarter> qs = {q("2026-09-30", 120, 36, 1.2), q("2026-06-30", 100, 25, 1.0), q("2026-03-31", 90, 20, 0.9),
                                     q("2025-12-31", 95, 22, 0.95), q("2025-09-30", 96, 24, 0.8)};
        const QuarterReview r = review_quarter(qs);
        QVERIFY(r.valid());
        QCOMPARE(r.prior_period, QStringLiteral("2026-06-30"));
        QCOMPARE(r.year_ago_period, QStringLiteral("2025-09-30"));
        const auto& rev = r.metrics[0];
        QCOMPARE(rev.name, QStringLiteral("Revenue"));
        QCOMPARE(rev.vs_prior, 20.0);
        QCOMPARE(rev.vs_year_ago, 25.0);
        const auto& om = r.metrics[1]; // gross margin is absent, so operating margin is next
        QCOMPARE(om.name, QStringLiteral("Operating margin"));
        QVERIFY(om.in_points);
        QCOMPARE(om.latest, 30.0);
        QCOMPARE(om.vs_prior, 5.0);    // points, not percent
        QCOMPARE(om.vs_year_ago, 5.0);
        // A missing quarter: the next row is six months back and is not called "the prior quarter".
        const QuarterReview gap = review_quarter({q("2026-09-30", 120, 36, 1.2), q("2026-03-31", 90, 20, 0.9)});
        QVERIFY(gap.prior_period.isEmpty());
        QVERIFY(std::isnan(gap.metrics[0].vs_prior) && std::isnan(gap.metrics[0].vs_year_ago));
        QVERIFY(!review_quarter({}).valid());
    }

    void multiple_against_its_own_past() {
        namespace eq = finterm::services::equity;
        QVector<FiscalYear> years = {fy("2023-12-31", 1, 1, 1), fy("2024-12-31", 1, 1, 1), fy("2025-12-31", 1, 1, 1)};
        years[0].eps = 2.0;
        years[1].eps = -1.0; // a loss: no multiple
        years[2].eps = 4.0;
        QVector<eq::Candle> candles;
        auto bar = [&candles](const QDate& d, double close) {
            eq::Candle c;
            c.timestamp = QDateTime(d, QTime(5, 0), QTimeZone::UTC).toSecsSinceEpoch();
            c.close = close;
            candles.append(c);
        };
        bar(QDate(2023, 12, 29), 40.0); // the last session before a weekend year end
        bar(QDate(2024, 12, 31), 50.0);
        bar(QDate(2025, 12, 31), 100.0);
        const auto h = pe_history(years, candles, 30.0);
        QCOMPARE(h.size(), 3);
        QCOMPARE(h[0].label, QStringLiteral("FY23"));
        QCOMPARE(h[0].pe, 20.0);
        QCOMPARE(h[1].pe, 25.0);
        QCOMPARE(h[2].label, QStringLiteral("Now"));
        // No bar near a year end: that year is left out, not priced off a distant one.
        QCOMPARE(pe_history(years, {candles[2]}, 30.0).size(), 2);
        QVERIFY(pe_history(years, {}, 30.0).isEmpty());
    }

    void sector_changes_the_yardsticks() {
        const SectorProfile bank = sector_profile("Financial Services", "Banks - Diversified");
        QVERIFY(bank.balance_sheet_business && !bank.note.isEmpty());
        QCOMPARE(bank.high_roe_pct, 12.0);
        QVERIFY(sector_profile("Real Estate", "REIT - Industrial").reit);
        QVERIFY(!sector_profile("Technology", "Semiconductors").balance_sheet_business);
        QVERIFY(!sector_profile("Financial Services", "Financial Data & Stock Exchanges").balance_sheet_business);

        // The same balance sheet: high leverage for an industrial, not a finding for a bank.
        Inputs in;
        in.years = {fy("2025-12-31", 100, 20, 15), fy("2026-12-31", 105, 21, 16)};
        in.years[1].cash = 10;
        in.years[1].debt = 400;
        in.years[1].ebitda = 30;
        in.years[1].fcf = -5;
        QVERIFY(has(derive_findings(in), Tone::Risk, "High leverage"));
        QVERIFY(has(derive_findings(in), Tone::Risk, "Burning cash"));
        in.sector = bank;
        QVERIFY(!has(derive_findings(in), Tone::Risk, "High leverage"));
        QVERIFY(!has(derive_findings(in), Tone::Risk, "Burning cash"));
        in.info.roe = 0.13;
        QVERIFY(has(derive_findings(in), Tone::Strength, "return on equity")); // 13% is high for a bank
    }

    void a_report_is_remembered_and_compared() {
        Snapshot then;
        then.date = QDate(2026, 8, 1);
        then.price = 100;
        then.eps_fy0 = 5.0;
        then.latest_quarter = "2026-06-30";
        then.analysts_buy = 20;
        then.analysts_total = 30;
        then.strengths = {"Margins are widening"};
        then.risks = {"Priced well above peers"};
        Snapshot now = snapshot_from_json(to_json(then)); // survives the round trip
        QCOMPARE(now.price, 100.0);
        QVERIFY(std::isnan(now.target_mean)); // absent stays absent
        QVERIFY(changes_since(then, now).isEmpty());
        now.date = QDate(2026, 11, 16);
        now.price = 120;
        now.eps_fy0 = 5.01; // under half a percent: not a change worth a line
        now.latest_quarter = "2026-09-30";
        now.analysts_buy = 24;
        now.strengths = {"Margins are widening", "Estimates are being raised"};
        now.risks = {};
        const QStringList c = changes_since(then, now);
        QVERIFY(c.contains("Share price 100.00 → 120.00 (+20.0%)"));
        QVERIFY(!c.join('|').contains("Consensus EPS"));
        QVERIFY(c.contains("A new quarter has been reported: the one ended 2026-09-30 (it was 2026-06-30)."));
        QVERIFY(c.contains("Analysts rating it a buy: 20 of 30 → 24 of 30."));
        QVERIFY(c.contains("New in its favour: Estimates are being raised."));
        QVERIFY(c.contains("No longer against it: Priced well above peers."));
        QVERIFY(changes_since(Snapshot{}, now).isEmpty());
    }

    void peer_premium() {
        const PeerStat s = peer_stat("Forward P/E", 30, {20, kNaN, 10, 30});
        QCOMPARE(s.peers, 3);
        QCOMPARE(s.peer_median, 20.0);
        QCOMPARE(s.premium_pct(), 50.0);
        QVERIFY(std::isnan(peer_stat("x", kNaN, {20}).premium_pct()));
        QVERIFY(std::isnan(peer_stat("x", 5, {-2}).premium_pct())); // a negative median multiple means nothing
    }

    void beat_record_skips_estimates_and_artefacts() {
        eq::EarningsAnalysis a;
        auto pt = [](double est, double act, double surprise, double reaction, bool suspect = false, bool estimate = false) {
            eq::EarningsPoint p;
            p.is_estimate = estimate;
            p.eps_estimate = est;
            if (!estimate) {
                p.eps_actual = act;
                p.surprise_pct = surprise;
                p.reaction_pct = reaction;
            }
            p.surprise_suspect = suspect;
            return p;
        };
        a.history = {pt(1, 0, 0, 0, false, true), pt(1, 1.1, 10, 4), pt(1, 0.9, -10, -6), pt(1, 3.1, 210, 2, true),
                     pt(1, 1.2, 20, -8)};
        const BeatRecord r = beat_record(a);
        QCOMPARE(r.quarters, 3); // the accounting artefact is not a beat
        QCOMPARE(r.beats, 2);
        QCOMPARE(r.median_surprise_pct, 10.0);
        QCOMPARE(r.reactions, 4);
        QCOMPARE(r.median_abs_reaction_pct, 5.0);

        eq::EarningsTrendRow t;
        t.period = "0y";
        t.current = 5.1;
        t.d90 = 5.0;
        a.trend = {t};
        QVERIFY(std::abs(estimate_drift_pct(a, "0y") - 2.0) < 1e-9);
        QVERIFY(std::isnan(estimate_drift_pct(a, "+1y")));
    }

    void implied_price_needs_positive_earnings() {
        const ImpliedPrice p = implied_price("x", 20, 5, 80);
        QCOMPARE(p.price, 100.0);
        QCOMPARE(p.vs_now_pct, 25.0);
        QVERIFY(std::isnan(implied_price("x", 20, -5, 80).price)); // a multiple of a loss is not a price
        QVERIFY(std::isnan(implied_price("x", kNaN, 5, 80).price));
        QVERIFY(std::isnan(implied_price("x", 20, 5, kNaN).vs_now_pct));
    }

    void findings_fire_only_on_data() {
        QVERIFY(derive_findings(Inputs{}).isEmpty()); // nothing known: nothing claimed

        Inputs in;
        in.years = {fy("2024-12-31", 100, 20, 15), fy("2025-12-31", 110, 24, 18), fy("2026-12-31", 150, 45, 38)};
        in.years[2].fcf = 40;
        in.years[2].cash = 50;
        in.years[2].debt = 20;
        in.years[2].shares = 98;
        in.years[1].shares = 100;
        in.info.roe = 0.32;
        in.info.short_pct_of_float = 0.14;
        in.peer_forward_pe = peer_stat("Forward P/E", 40, {20, 22, 18});
        auto f = derive_findings(in);
        QVERIFY(has(f, Tone::Strength, "double-digit"));
        QVERIFY(has(f, Tone::Strength, "accelerating"));
        QVERIFY(has(f, Tone::Strength, "widening"));
        QVERIFY(has(f, Tone::Strength, "return on equity"));
        QVERIFY(has(f, Tone::Strength, "free cash flow"));
        QVERIFY(has(f, Tone::Strength, "More cash than debt"));
        QVERIFY(has(f, Tone::Strength, "Share count is falling"));
        QVERIFY(has(f, Tone::Risk, "Heavily shorted"));
        QVERIFY(has(f, Tone::Risk, "above peers"));
        QVERIFY(count_tone(f, Tone::Strength) >= 7);

        Inputs bad;
        bad.years = {fy("2024-12-31", 100, 20, 15), fy("2025-12-31", 110, 15, 10), fy("2026-12-31", 90, -5, -9)};
        bad.years[2].fcf = -12;
        bad.years[2].cash = 10;
        bad.years[2].debt = 100;
        bad.years[2].ebitda = 20;
        bad.years[2].shares = 110;
        bad.years[1].shares = 100;
        f = derive_findings(bad);
        QVERIFY(has(f, Tone::Risk, "shrinking"));
        QVERIFY(has(f, Tone::Risk, "at a loss"));
        QVERIFY(has(f, Tone::Risk, "narrowing"));
        QVERIFY(has(f, Tone::Risk, "Burning cash"));
        QVERIFY(has(f, Tone::Risk, "High leverage"));
        QVERIFY(has(f, Tone::Risk, "diluted"));
        QCOMPARE(count_tone(f, Tone::Strength), 0);

        // A ten-for-one split between two year ends is not dilution.
        Inputs split;
        split.years = {fy("2025-12-31", 100, 20, 15), fy("2026-12-31", 100, 20, 15)};
        split.years[0].shares = 2.4e9;
        split.years[1].shares = 24e9;
        QVERIFY(!has(derive_findings(split), Tone::Risk, "diluted"));
        split.years[1].shares = 0.24e9;
        QVERIFY(!has(derive_findings(split), Tone::Strength, "Share count"));

        // Insider trades filed without a value: the dollar comparison is unknown, so nothing is concluded from it.
        Inputs ins;
        ins.insiders = {true, 12, 2, 1, 20000.0, 0.0, false};
        QVERIFY(!has(derive_findings(ins), Tone::Strength, "Insiders are buying"));
        ins.insiders.values_complete = true;
        QVERIFY(has(derive_findings(ins), Tone::Strength, "Insiders are buying"));

        // Too few peers to call a median, and a rating that was not produced.
        Inputs thin;
        thin.peer_forward_pe = peer_stat("Forward P/E", 40, {20});
        thin.has_technicals = true;
        thin.technicals_rated = false;
        QVERIFY(derive_findings(thin).isEmpty());
    }

    void prompts_carry_the_material_and_ask_for_no_numbers() {
        Filings fl;
        fl.annual.form = "10-K";
        fl.annual.filing_date = "2026-02-25";
        fl.annual.business.text = "We design accelerated computing platforms.";
        fl.annual.risk_factors.text = "Competition could adversely impact our market share.";
        const QVector<Headline> news = {{"Chipmaker unveils new platform", "Reuters", "2026-11-10T12:00:00Z"}};
        const QString b = business_prompt("NVDA", "NVIDIA Corporation", "Profile text.", fl, news);
        QVERIFY(b.contains("<<<DATA>>>") && b.contains("<<<END>>>"));
        QVERIFY(b.contains("We design accelerated computing platforms."));
        QVERIFY(b.contains("10-K filed 2026-02-25"));
        QVERIFY(b.contains("Chipmaker unveils new platform (Reuters, 2026-11-10)"));
        QVERIFY(b.contains("\"new_technology\"") && b.contains("\"customers\"") && b.contains("\"competition\""));
        QVERIFY(!b.contains("Management's discussion")); // an absent section is not announced

        // A long section is cut to its budget on a paragraph, never mid-word.
        fl.annual.business.text = QString("Paragraph of the filing. ").repeated(200).trimmed() + "\n\n" +
                                  QString("x").repeated(kPromptBusinessChars);
        fl.quarterly.form = "10-Q";
        fl.quarterly.mdna.text = "Revenue rose.";
        fl.annual.mdna.text = "The year in review.";
        const QString cut = business_prompt("NVDA", "NVIDIA Corporation", {}, fl, {});
        QVERIFY(cut.size() < kPromptBusinessChars + 4000);
        QVERIFY(cut.contains("Paragraph of the filing.") && !cut.contains("xxxx"));
        QVERIFY(cut.contains("Revenue rose.") && !cut.contains("The year in review.")); // the quarter stands in for the year
        QCOMPARE(excerpt("short", 100), QStringLiteral("short"));

        const QString s = summary_prompt("NVDA", "NVIDIA Corporation", "Share price: $190.00", {{Tone::Risk, "VALUATION", "Priced well above peers", "Forward P/E 40."}}, news);
        QVERIFY(s.contains("Share price: $190.00"));
        QVERIFY(s.contains("[risk] VALUATION: Priced well above peers"));
        QVERIFY(s.contains("Do not give a price target, a probability"));
        QVERIFY(s.contains("\"direction\""));
    }

    void headlines_about_the_company() {
        QVERIFY(headline_mentions("Walmart vs. Target: what to watch", "WMT", "Walmart Inc."));
        QVERIFY(headline_mentions("A Town Near Walmart's Hometown", "WMT", "Walmart Inc."));
        QVERIFY(headline_mentions("Is PLTR Stock a Buy?", "PLTR", "Palantir Technologies Inc."));
        QVERIFY(headline_mentions("Retailer (WMT) raises guidance", "WMT", ""));
        QVERIFY(!headline_mentions("Eli Lilly is trouncing Nvidia", "WMT", "Walmart Inc."));
        QVERIFY(headline_mentions("Ford recalls trucks", "F", "Ford Motor Company"));
        QVERIFY(!headline_mentions("Fed keeps rates steady", "F", "Ford Motor Company"));
        // A name that is also a word: the retailer, not a price target.
        QVERIFY(headline_mentions("Target's AI Commerce Partnerships", "TGT", "Target Corporation"));
        QVERIFY(!headline_mentions("Analyst raises price target on Nvidia", "TGT", "Target Corporation"));
        QVERIFY(!headline_mentions("Targeted ads are back", "TGT", "Target Corporation"));
        // A ticker that is also a word counts only where it is marked as one.
        QVERIFY(!headline_mentions("Why NOW Is the Time to Buy Bonds", "NOW", "ServiceNow, Inc."));
        QVERIFY(headline_mentions("ServiceNow (NOW) beats estimates", "NOW", "ServiceNow, Inc."));
        QVERIFY(headline_mentions("Shares of $ON jump", "ON", "ON Semiconductor"));
        QVERIFY(!headline_mentions("American consumers keep spending", "AAL", "American Airlines Group")); // too generic
        QVERIFY(headline_mentions("The Trade Desk falls", "TTD", "The Trade Desk, Inc."));
        // A name the ticker abbreviates.
        QVERIFY(headline_mentions("Google unveils a new model", "GOOGL", "Alphabet Inc."));
        QVERIFY(!headline_mentions("Metals rally on China demand", "META", "Meta Platforms, Inc."));

        const QVector<Headline> all = {{"Eli Lilly is trouncing Nvidia", "", ""}, {"Walmart lifts outlook", "", ""}};
        QCOMPARE(relevant_headlines(all, "WMT", "Walmart Inc.").size(), 1);
        QCOMPARE(relevant_headlines(all, "ZZZ", "Zeta").size(), 2); // none can be told apart: all kept
    }

    void outlook_band_is_stated_and_graded_alike() {
        // The word the model is asked for and the word it is graded against mean one thing.
        QCOMPARE(finterm::screens::ai_forecast::flat_band_for("report"), 5.0);
        QCOMPARE(finterm::screens::ai_forecast::flat_band_for("quant"), finterm::screens::ai_forecast::kFlatBandPct);
        QCOMPARE(finterm::screens::ai_forecast::bucket_direction(2.0, finterm::screens::ai_forecast::flat_band_for("report")),
                 QStringLiteral("flat")); // a +2% quarter is a flat quarter
        const QString s = summary_prompt("X", "X Corp", "Share price: $1.00", {}, {});
        QVERIFY(s.contains("\"flat\" means within 5 percent either way"));
    }

    void replies_are_parsed_tolerantly() {
        const QString reply = QStringLiteral("Here you go:\n```json\n{\"business_model\": \"Sells  chips {to} clouds.\", "
                                             "\"segments\": [\"Data Center: AI demand\", \"\", 7], \"customers\": [\"Cloud providers\", \"OEMs\"], "
                                             "\"risks\": \"Export controls\", \"advantages\": []}\n```");
        const BusinessNarrative b = parse_business(reply);
        QVERIFY(b.valid);
        QCOMPARE(b.business_model, QStringLiteral("Sells chips {to} clouds."));
        QCOMPARE(b.segments, QStringList{QStringLiteral("Data Center: AI demand")});
        QCOMPARE(b.customers, QStringLiteral("Cloud providers OEMs"));
        QCOMPARE(b.risks, QStringList{QStringLiteral("Export controls")});
        QVERIFY(!parse_business("no json here").valid);
        // A model that thinks aloud drafts objects before it answers: the last one is the answer.
        const BusinessNarrative later = parse_business(
            R"(Let me plan: {"note": "draft"} then maybe {"business_model": "first draft"}. Final answer:
               {"business_model": "Sells chips.", "risks": ["Export controls"]})");
        QCOMPARE(later.business_model, QStringLiteral("Sells chips."));
        QVERIFY(!parse_business("{\"other\": 1}").valid);

        const SummaryNarrative s = parse_summary(R"({"summary":"Growing fast.","bull_case":["a","b"],"direction":"UP","rationale":"growth"})");
        QVERIFY(s.valid);
        QCOMPARE(s.direction, QStringLiteral("up"));
        // Anything but the three words is not a call.
        QVERIFY(parse_summary(R"({"summary":"x","direction":"up 12%"})").direction.isEmpty());
        QVERIFY(parse_summary(R"({"summary":"x","direction":"bullish"})").direction.isEmpty());
        QVERIFY(!parse_summary(R"({"direction":"up"})").valid);
    }

    void breakdowns_and_customers_parse() {
        const auto doc = QJsonDocument::fromJson(R"({"symbol":"NVDA","breakdown_source":"10-K filed 2026-02-25",
            "annual":{"form":"10-K","filing_date":"2026-02-25","url":"https:\/\/www.sec.gov\/x","sections":{}},
            "revenue_breakdown":[
              {"axis":"product","period":"2026-01-25","prior_period":"2025-01-26","coverage_pct":100.0,
               "rows":[{"label":"Compute","value":160,"prior":100},{"label":"Gaming","value":40,"prior":null},
                       {"label":"","value":5},{"label":"Bad","value":null}]},
              {"axis":"geography","period":"2026-01-25","rows":[{"label":"Only one","value":1}]}],
            "customers":[{"label":"Customer one","pct":22.0,"scope":"Compute","period":"2026-01-25"},
                         {"label":"Nobody","pct":null}]})");
        const Filings f = parse_filings(doc.object());
        QCOMPARE(f.annual.url, QStringLiteral("https://www.sec.gov/x"));
        QCOMPARE(f.breakdowns.size(), 1); // a one-row cut is not a breakdown
        const Breakdown* b = f.breakdown("product");
        QVERIFY(b && !f.breakdown("geography"));
        QCOMPARE(b->rows.size(), 2);
        QCOMPARE(b->rows[0].growth_pct(), 60.0);
        QVERIFY(std::isnan(b->rows[1].growth_pct())); // not reported the year before: no growth rate
        QCOMPARE(b->top_share_pct(), 80.0);
        QCOMPARE(b->title(), QStringLiteral("Revenue by product"));
        QCOMPARE(f.customers.size(), 1);

        const QString block = breakdown_block(f);
        QVERIFY(block.contains("Revenue by product, year ended 2026-01-25: Compute 160 (+60.0% on the year before); Gaming 40.0"));
        QVERIFY(block.contains("Customer one accounted for 22.0% of Compute revenue"));

        Inputs in;
        in.filings = f;
        const auto found = derive_findings(in);
        QVERIFY(has(found, Tone::Risk, "few customers"));
        QVERIFY(has(found, Tone::Note, "dominates"));
    }

    void risk_profile_is_realised_history() {
        namespace eq = finterm::services::equity;
        QVector<eq::Candle> stock, bench;
        const qint64 day0 = QDateTime(QDate(2026, 1, 5), QTime(5, 0), QTimeZone::UTC).toSecsSinceEpoch();
        double s = 100.0, b = 400.0;
        for (int i = 0; i < 121; ++i) {
            eq::Candle cs, cb;
            cs.timestamp = cb.timestamp = day0 + qint64(i) * 86400;
            // The benchmark alternates +1% / −0.5%; the stock moves exactly twice as far.
            const double rb = i == 0 ? 0.0 : (i % 2 ? 0.01 : -0.005);
            b *= 1.0 + rb;
            s *= 1.0 + 2.0 * rb;
            cs.close = s;
            cb.close = b;
            stock.append(cs);
            bench.append(cb);
        }
        const RiskProfile r = risk_profile(stock, bench);
        QCOMPARE(r.sessions, 120);
        QCOMPARE(r.paired_sessions, 120);
        QVERIFY(std::abs(r.beta - 2.0) < 1e-9);
        QVERIFY(std::abs(r.correlation - 1.0) < 1e-9);
        QVERIFY(std::abs(r.return_pct - (s / 100.0 - 1.0) * 100.0) < 1e-9);
        QVERIFY(std::abs(r.max_drawdown_pct - (-1.0)) < 1e-9); // each down day is 1% off the high before it
        QVERIFY(std::abs(r.volatility_pct - 0.015 * std::sqrt(120.0 / 119.0) * std::sqrt(252.0) * 100.0) < 1e-6);

        // Too short a history says nothing; no benchmark leaves beta absent, not zero.
        QCOMPARE(risk_profile(stock.mid(0, 40), bench).sessions, 0);
        const RiskProfile alone = risk_profile(stock, {});
        QVERIFY(alone.sessions == 120 && std::isnan(alone.beta) && std::isnan(alone.correlation));

        Inputs in;
        in.risk = r;
        in.risk.max_drawdown_pct = -42.0;
        QVERIFY(has(derive_findings(in), Tone::Risk, "drawdown"));
    }

    void filings_parse() {
        const auto doc = QJsonDocument::fromJson(R"({"symbol":"TSM","annual":null,"annual_error":"no 10-K on file",
            "quarterly":{"form":"10-Q","filing_date":"2026-08-26","period":"2026-07-26","accession":"a",
                         "sections":{"mdna":{"text":"Overview","length":900,"truncated":true}}}})");
        const Filings f = parse_filings(doc.object());
        QVERIFY(!f.annual.valid());
        QCOMPARE(f.annual_error, QStringLiteral("no 10-K on file"));
        QVERIFY(f.quarterly.valid() && f.any());
        QCOMPARE(f.quarterly.cite(), QStringLiteral("10-Q filed 2026-08-26"));
        QVERIFY(f.quarterly.mdna.truncated);
        QVERIFY(f.quarterly.business.text.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestReportModel)
#include "test_report_model.moc"
