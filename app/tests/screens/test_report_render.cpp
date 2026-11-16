// The ER Report rendered end to end on synthetic data.
// Guards the things a unit test of the arithmetic cannot see: that every
// section is laid out, that every chart is produced, and that a missing value
// never reaches the page as "nan" or "inf".
//
// Set FINTERM_RENDER_DIR to a directory to keep the rendered pages as PNGs.
#include "screens/equity_research/ReportBuilder.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

using namespace finterm::screens;
using namespace finterm::screens::report;
namespace eq = finterm::services::equity;

namespace {

class ImageDocument : public QTextDocument {
  public:
    QHash<QString, QImage> images;

  protected:
    QVariant loadResource(int type, const QUrl& name) override {
        if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("report"))
            return images.value(name.path().mid(1));
        return QTextDocument::loadResource(type, name);
    }
};

QJsonObject obj(const char* json) {
    return QJsonDocument::fromJson(json).object();
}

void keep(const QImage& img, const QString& name) {
    const QString dir = qEnvironmentVariable("FINTERM_RENDER_DIR");
    if (!dir.isEmpty())
        QVERIFY(img.save(QDir(dir).filePath(name)));
}

ReportDoc sample(bool rich) {
    ReportDoc d;
    d.symbol = "ACME";
    d.generated = QDateTime(QDate(2026, 11, 16), QTime(9, 30));
    d.bench_symbol = "SPY";
    d.has_info = true;
    d.info.symbol = "ACME";
    d.info.company_name = "Acme Robotics & Co";
    d.info.sector = "Technology";
    d.info.industry = "Semiconductors";
    d.info.exchange = "NMS";
    d.info.country = "United States";
    d.info.currency = "USD";
    d.info.description = "Acme designs robots <and> the chips inside them.";
    d.info.current_price = 190.25;
    d.sources = {{"Company profile and ratios", "Yahoo Finance", Source::State::Ready, "as of 16 Nov 09:30"},
                 {"Annual and quarterly reports", "SEC EDGAR", Source::State::Failed, "no 10-K on file"}};
    d.narrative_note = "The written sections need a language model.";
    if (!rich)
        return d; // a company about which almost nothing is known

    d.info.employees = 36000;
    d.info.market_cap = 4.6e12;
    d.info.enterprise_value = 4.55e12;
    d.info.week52_low = 86.6;
    d.info.week52_high = 212.2;
    d.info.pe_ratio = 52.1;
    d.info.forward_pe = 31.4;
    d.info.peg_ratio = 1.2;
    d.info.price_to_book = 40.2;
    d.info.ev_to_revenue = 21.3;
    d.info.ev_to_ebitda = 33.1;
    d.info.roe = 1.07;
    d.info.beta = 1.9;
    d.info.dividend_yield = 0.0002;
    d.info.target_low = 140;
    d.info.target_high = 275;
    d.info.target_mean = 228;
    d.info.analyst_count = 58;
    d.info.recommendation_key = "strong_buy";
    d.info.held_institutions_pct = 0.68;
    d.info.held_insiders_pct = 0.041;
    d.info.short_pct_of_float = 0.011;
    d.info.short_ratio = 1.3;

    d.has_financials = true;
    d.financials.income_statement = {
        {"2026-01-25", obj(R"({"Total Revenue":215.9e9,"Gross Profit":153.5e9,"Operating Income":130.4e9,"Net Income":120.1e9,"EBITDA":135e9,"Diluted EPS":4.9,"Research And Development":18.5e9})")},
        {"2025-01-26", obj(R"({"Total Revenue":130.5e9,"Gross Profit":97.9e9,"Operating Income":81.5e9,"Net Income":72.9e9,"EBITDA":86e9,"Diluted EPS":2.94,"Research And Development":12.9e9})")},
        {"2024-01-28", obj(R"({"Total Revenue":60.9e9,"Gross Profit":44.3e9,"Operating Income":33e9,"Net Income":29.8e9,"EBITDA":35e9,"Diluted EPS":1.19,"Research And Development":8.7e9})")},
        {"2023-01-29", obj(R"({"Total Revenue":27e9,"Gross Profit":15.4e9,"Operating Income":4.2e9,"Net Income":4.4e9,"Diluted EPS":0.17})")}};
    d.financials.cash_flow = {
        {"2026-01-25", obj(R"({"Operating Cash Flow":102e9,"Capital Expenditure":-6.1e9,"Free Cash Flow":95.9e9,"Repurchase Of Capital Stock":-52e9,"Cash Dividends Paid":-0.98e9,"Stock Based Compensation":6.4e9,"Purchase Of Business":-1.2e9})")},
        {"2025-01-26", obj(R"({"Operating Cash Flow":64.1e9,"Capital Expenditure":-3.2e9,"Free Cash Flow":60.9e9,"Repurchase Of Capital Stock":-33.7e9,"Cash Dividends Paid":-0.83e9,"Stock Based Compensation":4.7e9})")},
        {"2024-01-28", obj(R"({"Operating Cash Flow":28.1e9,"Capital Expenditure":-1.1e9,"Free Cash Flow":27e9,"Repurchase Of Capital Stock":-9.5e9})")}};
    d.financials.balance_sheet = {
        {"2026-01-25", obj(R"({"Cash Cash Equivalents And Short Term Investments":62e9,"Total Debt":10.3e9,"Ordinary Shares Number":24.3e9})")},
        {"2025-01-26", obj(R"({"Cash Cash Equivalents And Short Term Investments":43.2e9,"Total Debt":10.3e9,"Ordinary Shares Number":24.5e9})")},
        {"2024-01-28", obj(R"({"Cash Cash Equivalents And Short Term Investments":26e9,"Total Debt":11.1e9})")}};

    d.has_extras = true;
    d.extras.financial_currency = "USD";
    const double rev[] = {96.2e9, 81.6e9, 68.1e9, 57e9, 46.7e9};
    const char* periods[] = {"2026-10-26", "2026-07-27", "2026-04-27", "2026-01-25", "2025-10-26"};
    for (int i = 0; i < 5; ++i) {
        Quarter q;
        q.period = periods[i];
        q.revenue = rev[i];
        q.operating_income = rev[i] * (0.66 - 0.01 * i);
        q.eps = 2.46 - 0.4 * i;
        d.extras.quarters.append(q);
    }
    d.extras.quarters[4].operating_income = kNaN; // one quarter did not report it
    d.extras.actions = {{QDate(2026, 11, 12), "Melius Research", "Hold", "Buy", "down"},
                        {QDate(2026, 11, 12), "DA Davidson", "Buy", "Buy", "main"},
                        {QDate(2026, 10, 3), "HSBC", "Buy", "Hold", "up"},
                        {QDate(2026, 9, 9), "Seaport", "Sell", "", "init"}};
    d.extras.rec_trend = {{"0m", 11, 36, 14, 1, 0}, {"-1m", 12, 36, 12, 1, 0}, {"-2m", 12, 37, 10, 1, -1}};

    d.has_earnings = true;
    for (int i = 0; i < 6; ++i) {
        eq::EarningsPoint p;
        p.timestamp = QDateTime(QDate(2026, 8, 27).addMonths(-3 * i), QTime(21, 0), QTimeZone::UTC).toSecsSinceEpoch();
        p.eps_estimate = 2.3 - 0.35 * i;
        p.eps_actual = *p.eps_estimate * (i == 2 ? 0.95 : 1.06);
        p.surprise_pct = (*p.eps_actual - *p.eps_estimate) / *p.eps_estimate * 100.0;
        if (i != 4) // one print whose reaction is not in hand
            p.reaction_pct = (i % 2 ? -1 : 1) * (2.0 + i);
        d.earnings.history.append(p);
    }
    d.earnings.next.timestamp = QDateTime(QDate(2026, 11, 19), QTime(21, 0), QTimeZone::UTC).toSecsSinceEpoch();
    d.earnings.next.is_estimated = false;
    d.earnings.next.eps_avg = 2.61;
    d.earnings.next.eps_low = 2.4;
    d.earnings.next.eps_high = 2.9;
    d.earnings.next.rev_avg = 104.2e9;
    d.earnings.next.analysts = 41;
    eq::EarningsEstimateRow e0{"0y", "FY2027", 9.4, {}, {}, {}, {}, 0.92, 395e9, {}, {}, {}, 0.83};
    eq::EarningsEstimateRow e1{"+1y", "FY2028", 12.1, {}, {}, {}, {}, 0.29, 520e9, {}, {}, {}, 0.32};
    d.earnings.estimates = {e0, e1};
    d.earnings.trend = {{"0y", "FY2027", 9.4, 9.35, 9.1, 8.7, 8.2}, {"+1y", "FY2028", 12.1, 12.0, 11.6, {}, 10.4}};

    for (const char* s : {"ACME", "AMD", "AVGO", "INTC", "QCOM", "TSM"}) {
        eq::PeerData p;
        p.symbol = s;
        const double k = qHash(QString::fromLatin1(s)) % 17;
        p.forward_pe = 14 + k;
        p.revenue_growth = (k - 6) / 40.0;
        p.operating_margin = 0.1 + k / 40.0;
        p.roe = 0.05 + k / 30.0;
        d.peers.append(p);
    }
    d.peers[3].forward_pe = eq::kUnknown; // a peer with losses has no P/E

    for (int i = 0; i < 250; ++i) {
        eq::Candle c, b;
        c.timestamp = b.timestamp = QDateTime(QDate(2025, 11, 17).addDays(i * 365 / 250), QTime(5, 0), QTimeZone::UTC).toSecsSinceEpoch();
        c.close = 120.0 + i * 0.3 + 12.0 * std::sin(i / 9.0);
        b.close = 580.0 + i * 0.25;
        d.candles.append(c);
        d.bench.append(b);
    }
    d.news = {{"Acme unveils <Titan> platform", "", "https://example.com/a?x=1&y=2", "Reuters", "2026-11-10T14:00:00Z"},
              {"Regulators weigh new export rules", "", "https://example.com/b", "Bloomberg", "2026-11-08T09:00:00Z"}};
    d.filings.annual.form = "10-K";
    d.filings.annual.filing_date = "2026-02-25";
    d.filings.annual.url = "https://www.sec.gov/Archives/edgar/data/1/a-index.html";
    d.filings.breakdown_source = "10-K filed 2026-02-25";
    d.filings.breakdowns = {
        {"product", "2026-01-25", "2025-01-26", 100.0,
         {{"Compute", 162.4e9, 102.2e9}, {"Networking", 31.4e9, 13.0e9}, {"Gaming", 16.0e9, 11.3e9}, {"New line", 6.1e9, kNaN}}},
        {"geography", "2026-01-25", "2025-01-26", 100.0, {{"United States", 149.6e9, 77.5e9}, {"Taiwan", 66.3e9, 53e9}}}};
    d.filings.customers = {{"Customer one", 22.0, "Compute & Networking", "2026-01-25"}, {"Customer two", 14.0, "", "2026-01-25"}};
    d.peers_basis = "Peers are the companies nearest it in size in its Yahoo industry (Semiconductors), not a hand-picked group.";
    d.filings.quarterly.form = "10-Q";
    d.filings.quarterly.filing_date = "2026-08-26";
    d.insiders = {true, 12, 1, 9, 2.1e6, 310e6};
    d.has_technicals = true;
    d.technicals.rated = true;
    d.technicals.overall_signal = eq::TechSignal::Buy;

    d.business.valid = true;
    d.business.business_model = "Acme sells accelerated computing systems to cloud providers and enterprises.";
    d.business.segments = {"Data Center: demand for AI training and inference systems.", "Robotics: early, growing from a small base."};
    d.business.customers = "A small number of cloud providers account for a large share of revenue.";
    d.business.competition = "Competes with other chip designers and customers' in-house silicon on performance and software.";
    d.business.investment = "Capital goes to R&D, supply commitments and buybacks.";
    d.business.new_technology = {"The Titan platform, shipping next year."};
    d.business.new_business = {"A sovereign-AI partnership programme."};
    d.business.advantages = {"A software stack developers already build on.", "Scale in supply."};
    d.business.risks = {"Export controls restrict sales to some countries.", "Customer concentration."};
    d.business.recent_developments = {"Record data center revenue in the quarter."};
    d.summary.valid = true;
    d.summary.summary = "Revenue and margins are rising quickly. The stock trades above its peers on forward earnings.";
    d.summary.bull_case = {"Revenue growth is accelerating.", "Free cash flow is strong."};
    d.summary.bear_case = {"Priced well above peers.", "Customer concentration."};
    d.summary.watch = {"Data center revenue.", "Gross margin."};
    d.summary.direction = "up";
    d.summary.rationale = "Estimates are still being raised.";
    d.narrative_note = "Written sections by test-model.";
    return d;
}

QImage page(const ReportDoc& d, const Palette& pal, int width, QString* html_out, int* charts) {
    ImageDocument doc;
    const QString html = build_report_html(d, pal, width, 1.0, [&doc](const QString& id, const QImage& img) {
        doc.images.insert(id, img);
        return QStringLiteral("report:/") + id;
    });
    *html_out = html;
    *charts = doc.images.size();
    doc.setDocumentMargin(16);
    doc.setTextWidth(width + 32);
    doc.setHtml(html);
    QImage img(doc.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    img.fill(pal.bg);
    QPainter p(&img);
    doc.drawContents(&p);
    return img;
}

} // namespace

class TestReportRender : public QObject {
    Q_OBJECT
  private slots:
    void full_report_has_every_section_and_no_nan() {
        QString html;
        int charts = 0;
        for (const auto& [name, pal, width] : {std::tuple{"report_screen.png", Palette::from_theme(), 1200},
                                               std::tuple{"report_paper.png", Palette::paper(), 680}}) {
            const QImage img = page(sample(true), pal, width, &html, &charts);
            keep(img, QLatin1String(name));
            QVERIFY(img.height() > 2500);
            QVERIFY2(charts >= 18, qPrintable(QString::number(charts)));
            QVERIFY(html.contains("CUSTOMERS THE ANNUAL REPORT SINGLES OUT") && html.contains("Compute &amp; Networking revenue"));
            QVERIFY(html.contains("HOW THE SHARES HAVE TRADED") && html.contains("Beta to SPY"));
            QVERIFY(html.contains("https://www.sec.gov/Archives/edgar/data/1/a-index.html"));
            QVERIFY(html.contains("nearest it in size in its Yahoo industry"));
            for (const char* section : {"AT A GLANCE", "THE BUSINESS", "WHAT IS NEW", "FINANCIAL PERFORMANCE",
                                        "CASH AND WHERE IT GOES", "EARNINGS AGAINST EXPECTATIONS", "VALUATION AND PEERS",
                                        "ANALYSTS AND OWNERSHIP", "ADVANTAGES AND RISKS", "OUTLOOK", "SOURCES"})
                QVERIFY2(html.contains(QLatin1String(section)), section);
            QVERIFY(!html.contains(QRegularExpression(QStringLiteral("\\b(nan|inf)\\b"), QRegularExpression::CaseInsensitiveOption)));
            // Text from outside is escaped, never markup.
            QVERIFY(html.contains("Acme Robotics &amp; Co"));
            QVERIFY(html.contains("&lt;Titan&gt;"));
            QVERIFY(html.contains("https://example.com/a?x=1&amp;y=2"));
            // A reiterated rating is not listed as a change.
            QVERIFY(!html.contains("DA Davidson"));
            QVERIFY(html.contains("Downgrade") && html.contains("Upgrade") && html.contains("New coverage"));
        }
    }

    void bare_report_stands_without_data() {
        QString html;
        int charts = 0;
        const QImage img = page(sample(false), Palette::paper(), 900, &html, &charts);
        keep(img, QStringLiteral("report_bare.png"));
        QVERIFY(img.height() > 300);
        QVERIFY(html.contains("SOURCES") && html.contains("no 10-K on file"));
        QVERIFY(html.contains("&lt;and&gt;")); // the profile stands in for the business section
        QVERIFY(!html.contains(QRegularExpression(QStringLiteral("\\b(nan|inf)\\b"), QRegularExpression::CaseInsensitiveOption)));
    }

    void figures_block_lists_only_what_exists() {
        const QString rich = figures_block(sample(true));
        QVERIFY(rich.contains("Share price: $190.25"));
        QVERIFY(rich.contains("FY26 (year ended 2026-01-25): revenue $215.9B"));
        QVERIFY(rich.contains("Consensus for FY2028"));
        QVERIFY(!rich.contains("—") && !rich.contains("nan"));
        const QString bare = figures_block(sample(false));
        QVERIFY(bare.contains("Share price") && !bare.contains("Market cap") && !bare.contains("P/E"));
    }

    void a_fund_is_said_to_be_one() {
        ReportDoc d = sample(false);
        d.info.quote_type = "ETF";
        QString html;
        int charts = 0;
        page(d, Palette::paper(), 900, &html, &charts);
        QVERIFY(html.contains("is listed as etf, not an operating company"));
    }

    void mixed_currency_is_named_and_not_cross_multiplied() {
        ReportDoc d = sample(true);
        d.extras.financial_currency = "TWD"; // an ADR: quoted in dollars, reports in Taiwan dollars
        QString html;
        int charts = 0;
        page(d, Palette::paper(), 900, &html, &charts);
        QVERIFY(html.contains("quoted in USD; the company reports in TWD"));
        QVERIFY(html.contains("TWD 215.9B"));
        QVERIFY(html.contains("earnings and the share price are in different currencies"));
    }

    void exports_are_written() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = qEnvironmentVariable("FINTERM_RENDER_DIR", tmp.path());
        const QString pdf = QDir(dir).filePath("report.pdf");
        const int pages = write_report_pdf(sample(true), pdf);
        // A page of this report is a few charts and a few paragraphs: a count
        // far outside this means the page was laid out at the wrong scale.
        QVERIFY2(pages >= 5 && pages <= 12, qPrintable(QString::number(pages)));
        QVERIFY(QFileInfo(pdf).size() > 100 * 1024);

        const QString html = QDir(dir).filePath("report.html");
        QVERIFY(write_report_html(sample(true), html));
        QFile f(html);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith("<!doctype html>") && text.contains("charset='utf-8'"));
        QVERIFY(text.count("data:image/png;base64,") >= 16); // self-contained: every chart is inside the file
        QVERIFY(!text.contains("report:/"));
    }
};

QTEST_MAIN(TestReportRender)
#include "test_report_render.moc"
