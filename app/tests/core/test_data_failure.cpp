// tests/core/test_data_failure.cpp — what the user is told about a failed
// load, for the error texts the fetch paths really produce (yfinance / curl /
// requests exceptions from the data daemon, QNetworkReply strings, finterm's
// own messages).
#include "core/errors/DataFailure.h"

#include <QTest>

using namespace finterm::data_failure;

class TestDataFailure : public QObject {
    Q_OBJECT
  private slots:
    void kinds_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("status");
        auto row = [](const char* name, const char* text, Kind k, const char* src, int st) {
            QTest::newRow(name) << QString::fromUtf8(text) << int(k) << QString::fromUtf8(src) << st;
        };
        // yfinance / curl_cffi, through the daemon
        row("yf rate limit", "Yahoo refused: YFRateLimitError('Too Many Requests. Rate limited. Try after a while.')",
            Kind::RateLimited, "Yahoo Finance", 429);
        row("429 client error", "429 Client Error: Too Many Requests for url: https://query2.finance.yahoo.com/v8",
            Kind::RateLimited, "Yahoo Finance", 429);
        row("curl dns", "Failed to perform, curl: (6) Could not resolve host: query2.finance.yahoo.com",
            Kind::Unresolved, "Yahoo Finance", 0);
        row("curl timeout", "Failed to perform, curl: (28) Operation timed out after 30001 milliseconds",
            Kind::Timeout, "", 0);
        row("curl reset", "curl: (56) Recv failure: Connection reset by peer", Kind::Unreachable, "", 0);
        // A local proxy that isn't running (a VPN client's 127.0.0.1:7890)
        row("dead local proxy",
            "Yahoo refused the quote download (90 symbols): ConnectionError('Failed to perform, curl: (7) Failed to "
            "connect to 127.0.0.1 port 9 after 1 ms: Could not connect to server.')",
            Kind::Proxy, "Yahoo Finance", 0);
        row("remote refused", "curl: (7) Failed to connect to query1.finance.yahoo.com port 443: Connection refused",
            Kind::Unreachable, "Yahoo Finance", 0);
        row("curl tls", "curl: (35) OpenSSL SSL_connect: SSL_ERROR_SYSCALL in connection to fc.yahoo.com:443",
            Kind::Tls, "Yahoo Finance", 0);
        row("requests dns",
            "HTTPSConnectionPool(host='www.sec.gov', port=443): Max retries exceeded with url: /cgi-bin "
            "(Caused by NameResolutionError(\"Failed to resolve 'www.sec.gov'\"))",
            Kind::Unresolved, "SEC EDGAR", 0);
        row("requests proxy", "HTTPSConnectionPool(host='query1.finance.yahoo.com'): Max retries exceeded "
            "(Caused by ProxyError('Unable to connect to proxy', ConnectionRefusedError(111)))",
            Kind::Proxy, "Yahoo Finance", 0);
        row("invalid crumb", "Yahoo refused or did not answer (401 Client Error: Unauthorized ... Invalid Crumb)",
            Kind::Refused, "Yahoo Finance", 401);
        row("our refusal", "Yahoo refused or did not answer", Kind::NoAnswer, "Yahoo Finance", 0);
        row("download refused", "Yahoo refused the quote download (12 symbols)", Kind::NoAnswer, "Yahoo Finance", 0);
        row("delisted", "$XYZQ: possibly delisted; no price data found (period=5d)", Kind::NotFound, "", 0);
        row("unknown 404", "unknown to Yahoo (404)", Kind::NotFound, "Yahoo Finance", 0);
        row("sec 403", "HTTP 403", Kind::Refused, "", 403);
        row("edgar 503", "SEC EDGAR: HTTP 503", Kind::ServerError, "SEC EDGAR", 503);
        // QNetworkReply::errorString()
        row("qt dns", "Host query1.finance.yahoo.com not found", Kind::Unresolved, "Yahoo Finance", 0);
        row("qt refused", "Connection refused", Kind::Unreachable, "", 0);
        row("qt proxy needs credentials", "Proxy requires authentication", Kind::Proxy, "", 0);
        row("urllib3 gave up on 429s",
            "HTTPSConnectionPool(host='query2.finance.yahoo.com', port=443): Max retries exceeded with url: /v10 "
            "(Caused by ResponseError('too many 429 error responses'))",
            Kind::RateLimited, "Yahoo Finance", 429);
        row("qt 429", "Error transferring https://api.nasdaq.com/api/calendar - server replied: Too Many Requests",
            Kind::RateLimited, "Nasdaq", 429);
        row("qt 503", "Error transferring https://www.sec.gov/x - server replied: Service Unavailable",
            Kind::ServerError, "SEC EDGAR", 0);
        row("qt ssl", "SSL handshake failed", Kind::Tls, "", 0);
        // HttpClient's sentinel with Qt's reason
        row("http_0 proxy", "HTTP_0: Proxy connection refused", Kind::Proxy, "", 0);
        row("http_503", "HTTP_503: Error transferring https://api.nasdaq.com/x - server replied: Service Unavailable",
            Kind::ServerError, "Nasdaq", 503);
        // finterm's own
        row("hub timeout", "refresh_timeout", Kind::Timeout, "", 0);
        row("no quote", "no quote returned", Kind::NoData, "", 0);
        row("python", "ModuleNotFoundError: No module named 'yfinance'", Kind::LocalService, "Yahoo Finance", 0);
        row("fred key", "FRED fetch failed: set FRED_API_KEY", Kind::NeedsKey, "FRED", 0);
        row("gibberish", "something odd", Kind::Unknown, "", 0);
        // Ticker symbols that spell protocol words are not network failures
        row("ticker SSL", "$SSL: possibly delisted; no price data found (period=5d)", Kind::NotFound, "", 0);
        row("ticker TLS", "$TLS: possibly delisted; no timezone found", Kind::NotFound, "", 0);
    }
    void kinds() {
        QFETCH(QString, text);
        QFETCH(int, kind);
        QFETCH(QString, source);
        QFETCH(int, status);
        const Failure f = classify(text);
        QCOMPARE(int(f.kind), kind);
        QCOMPARE(f.source, source);
        QCOMPARE(f.http_status, status);
        QVERIFY(!f.reason.isEmpty());
        QVERIFY(!f.action.isEmpty());
        QCOMPARE(f.raw, text);
    }

    void numbers_are_not_statuses() {
        // A timestamp or a ticker holding 429/403 is not an HTTP status.
        QCOMPARE(classify(QStringLiteral("no price for 4290.T at 2026-11-27 04:03")).http_status, 0);
        QVERIFY(classify(QStringLiteral("no price for 4290.T at 2026-11-27 04:03")).kind != Kind::RateLimited);
    }

    void hint_names_a_silent_source() {
        const Failure f = classify(QStringLiteral("Connection refused"), QStringLiteral("Yahoo Finance"));
        QCOMPARE(f.source, QStringLiteral("Yahoo Finance"));
        QVERIFY(f.reason.contains(QStringLiteral("Yahoo Finance")));
        // The text's own source wins over the hint.
        QCOMPARE(classify(QStringLiteral("HTTP 403 from www.sec.gov"), QStringLiteral("Yahoo Finance")).source,
                 QStringLiteral("SEC EDGAR"));
    }

    void wording() {
        const Failure f = classify(QStringLiteral("Too Many Requests"), QStringLiteral("Yahoo Finance"));
        QCOMPARE(one_line(QStringLiteral("quotes"), f),
                 QStringLiteral("Couldn't load quotes — Yahoo Finance is limiting how often this connection may ask "
                                "(HTTP 429)."));
        QVERIFY(f.transient);
        QVERIFY(!classify(QStringLiteral("possibly delisted")).transient); // the user has to change something
    }
};

QTEST_GUILESS_MAIN(TestDataFailure)
#include "test_data_failure.moc"
