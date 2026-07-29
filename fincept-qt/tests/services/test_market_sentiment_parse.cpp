// tests/services/test_market_sentiment_parse.cpp
//
// Adanos' /compare endpoint takes up to ten tickers per request — that batching
// is what makes the free tier's 250 requests/month usable — and it returns the
// rows sorted by buzz score, NOT in the order they were asked for. Picking the
// wrong row silently attributes one company's sentiment to another, which no
// amount of UI testing would catch.
//
// Both cases below were observed against the live API:
//   - a NVDA,AAPL,META request came back AAPL, NVDA, META (buzz order)
//   - a request for BRK-B came back with the row keyed "BRK"

#include "services/equity/MarketSentimentSupport.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

using namespace fincept::services::equity;
using namespace fincept::services::equity::sentiment;

namespace {

QJsonObject stock_row(const QString& ticker, double buzz, int bullish, int mentions) {
    QJsonObject row;
    row["ticker"] = ticker;
    row["buzz_score"] = buzz;
    row["bullish_pct"] = bullish;
    row["mentions"] = mentions;
    row["sentiment_score"] = 0.02;
    return row;
}

QJsonObject payload(const QVector<QJsonObject>& rows) {
    QJsonArray stocks;
    for (const auto& row : rows) {
        stocks.append(row);
    }
    QJsonObject object;
    object["period_days"] = 7;
    object["stocks"] = stocks;
    return object;
}

} // namespace

class TestMarketSentimentParse : public QObject {
    Q_OBJECT

  private slots:
    /// The regression that batching introduced: rows arrive in buzz order, so
    /// the first row belongs to whichever stock is loudest, not to the caller.
    void picks_the_requested_row_not_the_first() {
        const auto body = payload({stock_row("AAPL", 80.7, 25, 3188),
                                   stock_row("NVDA", 80.3, 26, 2818),
                                   stock_row("META", 79.6, 20, 2361)});

        const auto nvda = parse_compare_payload_for("reddit", "NVDA", body);
        QVERIFY(nvda.available);
        QCOMPARE(nvda.buzz_score, 80.3);
        QCOMPARE(nvda.activity_count, 2818.0);

        const auto meta = parse_compare_payload_for("reddit", "META", body);
        QCOMPARE(meta.buzz_score, 79.6);
        QCOMPARE(meta.bullish_pct, 20.0);
    }

    /// Class shares lose their suffix on the round trip: BRK-B in, BRK out.
    void falls_back_to_the_base_symbol() {
        const auto body = payload({stock_row("BRK", 73.2, 25, 611), stock_row("AAPL", 80.7, 25, 3188)});
        const auto brk = parse_compare_payload_for("reddit", "BRK-B", body);
        QVERIFY(brk.available);
        QCOMPARE(brk.buzz_score, 73.2);

        // Dot notation is the same case wearing a different hat.
        const auto dotted = parse_compare_payload_for("reddit", "BRK.B", body);
        QVERIFY(dotted.available);
        QCOMPARE(dotted.buzz_score, 73.2);
    }

    /// …but never when the base is ambiguous: two share classes in one response
    /// must not be able to claim each other's numbers.
    void ambiguous_base_matches_nothing() {
        const auto body = payload({stock_row("BRK-A", 70.0, 20, 100), stock_row("BRK-B", 73.2, 25, 611)});
        // Exact match still works for both.
        QCOMPARE(parse_compare_payload_for("reddit", "BRK-B", body).buzz_score, 73.2);
        QCOMPARE(parse_compare_payload_for("reddit", "BRK-A", body).buzz_score, 70.0);
        // A bare "BRK" has two equally-good candidates — refuse rather than guess.
        const auto bare = parse_compare_payload_for("reddit", "BRK", body);
        QVERIFY(!bare.available);
    }

    /// A ticker the platform doesn't cover is a legitimate answer, not an error.
    void uncovered_ticker_reports_unavailable() {
        const auto body = payload({stock_row("AAPL", 80.7, 25, 3188)});
        const auto vmrxx = parse_compare_payload_for("reddit", "VMRXX", body);
        QVERIFY(!vmrxx.available);
        QCOMPARE(vmrxx.source_id, QStringLiteral("reddit"));
        QCOMPARE(vmrxx.label, QStringLiteral("Reddit"));
        QCOMPARE(vmrxx.buzz_score, 0.0);
    }

    void empty_payload_is_unavailable_not_a_crash() {
        QVERIFY(!parse_compare_payload_for("news", "AAPL", QJsonObject{}).available);
        QVERIFY(!parse_compare_payload_for("news", "AAPL", payload({})).available);
    }

    /// Legacy single-ticker path: no ticker given means "the only row".
    void empty_ticker_takes_the_first_row() {
        const auto body = payload({stock_row("AAPL", 80.7, 25, 3188)});
        const auto snapshot = parse_compare_payload("reddit", body);
        QVERIFY(snapshot.available);
        QCOMPARE(snapshot.buzz_score, 80.7);
    }

    /// Every row is worth caching — the request was paid for regardless of
    /// which ticker the user was waiting on.
    void lists_every_ticker_in_the_payload() {
        const auto body = payload({stock_row("AAPL", 80.7, 25, 3188),
                                   stock_row("NVDA", 80.3, 26, 2818),
                                   stock_row("META", 79.6, 20, 2361)});
        const auto tickers = tickers_in_compare_payload(body);
        QCOMPARE(tickers.size(), 3);
        QVERIFY(tickers.contains("AAPL"));
        QVERIFY(tickers.contains("NVDA"));
        QVERIFY(tickers.contains("META"));
    }

    /// Payloads wrapped in a "data" envelope must parse the same way.
    void handles_a_data_envelope() {
        QJsonObject wrapper;
        wrapper["data"] = payload({stock_row("TSLA", 88.0, 40, 900)});
        const auto snapshot = parse_compare_payload_for("x", "TSLA", wrapper);
        QVERIFY(snapshot.available);
        QCOMPARE(snapshot.buzz_score, 88.0);
    }
};

QTEST_MAIN(TestMarketSentimentParse)
#include "test_market_sentiment_parse.moc"
