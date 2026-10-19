// test_screener_model.cpp — the SCREENER tab's parse of `screener_data.py scan`.
//
// The screen shows vendor numbers it did not compute, so the cases that matter
// are the absent ones: JSON null must reach the table as the placeholder,
// never as the 0.0 QJsonValue::toDouble() reads it as — a 0% ROE or a zero
// earnings yield is a claim about a real company. The evidence card is held to
// the same rule: a strategy that was not backtested must not render a 0.0%
// excess return.

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include "screens/screener/ScreenerModel.h"

using namespace fincept::screens::screener;

namespace {

QJsonObject sample() {
    const QByteArray json = R"({
      "asof": "2026-10-19",
      "universe": {"n": 1716, "priced": 1713, "min_mcap": 2e9},
      "market": {"spy": 767.3, "above50": false, "above200": true},
      "evidence_basis": "S&P 500 point-in-time",
      "subperiods": ["2010-14", "2015-19", "2020-22", "2023-26"],
      "strategies": [
        {"id": "magic_formula", "name": "Magic Formula", "family": "Academic",
         "source": "Greenblatt", "summary": "s", "rules": ["a", "b"], "rank_by": "Combined rank",
         "columns": ["earnings_yield", "roic", "no_such_column"],
         "evidence": {"tested": true, "excess": 0.019, "t": 1.01, "max_dd": -0.242,
                      "subperiods": [0.031, 0.008, null, 0.061], "note": "n"},
         "symbols": ["HRB", "NOFUND"]},
        {"id": "canslim", "name": "CAN SLIM", "family": "Practitioner", "rules": [],
         "columns": [], "evidence": {"tested": false, "note": "not backtested"}, "symbols": []},
        {"name": "no id — dropped"}
      ],
      "rows": {
        "HRB": {"name": "H&R Block", "price": 47.86, "earnings_yield": 0.1348, "roic": null, "chg_1d": -0.017},
        "NOFUND": {"name": "X", "price": 10.0}
      }
    })";
    return QJsonDocument::fromJson(json).object();
}

} // namespace

class TestScreenerModel : public QObject {
    Q_OBJECT
  private slots:
    void null_is_absent_not_zero() {
        const QJsonObject o = QJsonDocument::fromJson(R"({"a": null, "b": 0, "c": "1.5", "d": 2.5})").object();
        QVERIFY(!num(o, "a").has_value());       // null
        QVERIFY(!num(o, "missing").has_value()); // absent key
        QVERIFY(!num(o, "c").has_value());       // text is not a number
        QCOMPARE(*num(o, "b"), 0.0);             // a real zero survives
        QCOMPARE(*num(o, "d"), 2.5);
    }

    void parses_the_scan() {
        const ScanResult r = parse_scan(sample());
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.asof, QStringLiteral("2026-10-19"));
        QCOMPARE(r.universe_n, 1716);
        QCOMPARE(r.priced, 1713);
        QVERIFY(r.has_market);
        QVERIFY(!r.above50);
        QVERIFY(r.above200);
        QCOMPARE(r.strategies.size(), 2); // the one without an id is dropped
        const Strategy* m = r.strategy("magic_formula");
        QVERIFY(m);
        QCOMPARE(m->symbols, (QStringList{"HRB", "NOFUND"}));
        QCOMPARE(m->rules.size(), 2);
        QVERIFY(m->evidence.tested);
        QCOMPARE(*m->evidence.excess, 0.019);
        QCOMPARE(m->evidence.subperiods.size(), 4);
        QVERIFY(!m->evidence.subperiods[2].has_value()); // null sub-period stays absent
        QVERIFY(!r.strategy("nope"));
    }

    void untested_strategy_has_no_numbers() {
        const ScanResult r = parse_scan(sample());
        const Strategy* c = r.strategy("canslim");
        QVERIFY(c);
        QVERIFY(!c->evidence.tested);
        QVERIFY(!c->evidence.excess.has_value());
        QVERIFY(!c->evidence.t.has_value());
        QCOMPARE(c->evidence.note, QStringLiteral("not backtested"));
    }

    void error_document_yields_no_strategies() {
        const ScanResult r = parse_scan(QJsonDocument::fromJson(R"({"error": "TradingView down"})").object());
        QCOMPARE(r.error, QStringLiteral("TradingView down"));
        QVERIFY(r.strategies.isEmpty());
        QVERIFY(!r.has_market); // no market block -> no regime claim
    }

    void absent_metrics_render_as_placeholder() {
        const ScanResult r = parse_scan(sample());
        const QJsonObject hrb = r.rows.value("HRB");
        const auto* roic = column_def("roic");
        QVERIFY(roic);
        QCOMPARE(format_value(roic->kind, num(hrb, "roic")), fincept::ui::formatting::placeholder());
        QVERIFY(colour_for(Kind::PctSigned, std::nullopt).isEmpty());
        // a name with no fundamentals row at all reads absent, not zero
        const QJsonObject nf = r.rows.value("NOFUND");
        QCOMPARE(format_value(Kind::Pct, num(nf, "earnings_yield")), fincept::ui::formatting::placeholder());
    }

    void formats() {
        QCOMPARE(format_value(Kind::Pct, 0.1348), QStringLiteral("13.5%"));
        QCOMPARE(format_value(Kind::PctSigned, -0.017), QStringLiteral("-1.7%"));
        QCOMPARE(format_value(Kind::PctSigned, 0.05), QStringLiteral("+5.0%"));
        QCOMPARE(format_value(Kind::Int, 87.4), QStringLiteral("87"));
        QCOMPARE(format_value(Kind::Percentile, 0.934), QStringLiteral("93"));
        QCOMPARE(format_value(Kind::Ratio, 14.25), QStringLiteral("14.3"));
        QCOMPARE(format_value(Kind::Pct, 0.0), QStringLiteral("0.0%")); // a real zero is a value
        QVERIFY(!colour_for(Kind::Pct, 0.3).size());       // unsigned quantities are not coloured
        QVERIFY(colour_for(Kind::PctSigned, 0.3).size());
    }

    void every_emitted_column_has_a_definition() {
        // the backend's strategy columns (screener_data.STRATEGIES) — an
        // unknown key is skipped by the screen, so a typo would hide a column
        const QStringList keys = {"rs", "off_high", "above_low", "r1m", "r3m", "r6m", "r12m", "mom12_1",
                                  "adr20", "vol252", "dollar_vol50", "eps_growth_q", "eps_growth_fy",
                                  "roe", "roic", "gross_margin", "gpa", "earnings_yield", "pe", "pb",
                                  "ev_ebitda", "piotroski", "div_yield", "shareholder_yield",
                                  "value_score", "price", "chg_1d", "mcap"};
        for (const QString& k : keys)
            QVERIFY2(column_def(k), qPrintable("no ColumnDef for " + k));
        QVERIFY(!column_def("no_such_column"));
    }
};

QTEST_GUILESS_MAIN(TestScreenerModel)
#include "test_screener_model.moc"
