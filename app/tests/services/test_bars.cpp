// The one set of OHLCV rules every price-history read uses
// (services/markets/Bars.h): MarketDataService::fetch_bars, the
// market:history topic, Equity Research's charts, technicals and TALIpp,
// the portfolio returns.

#include "services/markets/Bars.h"

#include <QJsonDocument>
#include <QTest>

using namespace finterm::services;

class TestBars : public QObject {
    Q_OBJECT
  private slots:
    // A null price is not a zero price; a zero or negative print is real.
    void priced_rows_keep_real_zero_and_negative_prints() {
        const QJsonArray rows = QJsonDocument::fromJson(R"([
          {"timestamp":1,"open":0.0,"high":0.01,"low":0.0,"close":0.0,"volume":0},
          {"timestamp":2,"open":null,"high":null,"low":null,"close":null,"volume":0},
          {"timestamp":3,"open":-40.3,"high":-30.1,"low":-40.3,"close":-37.63,"volume":5},
          {"timestamp":4,"open":10,"high":11,"low":null,"close":10.5,"volume":7}
        ])").array();
        const QJsonArray priced = bars::priced_rows(rows);
        QCOMPARE(priced.size(), 2);
        const auto parsed = bars::parse(rows);
        QCOMPARE(parsed.size(), 2);
        QCOMPARE(parsed[0].timestamp, qint64(1));
        QCOMPARE(parsed[1].close, -37.63);
        QCOMPARE(bars::parse(priced).size(), parsed.size()); // one rule either way
    }

    void intraday_vs_daily() {
        for (const char* i : {"1m", "5m", "15m", "30m", "90m", "1h"})
            QVERIFY2(bars::is_intraday(QString::fromLatin1(i)), i);
        for (const char* i : {"1d", "5d", "1wk", "1mo", "3mo"})
            QVERIFY2(!bars::is_intraday(QString::fromLatin1(i)), i);
    }
};

QTEST_GUILESS_MAIN(TestBars)
#include "test_bars.moc"
