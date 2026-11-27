#include "core/errors/DataFailure.h"

#include <QRegularExpression>

namespace finterm::data_failure {

namespace {

bool df_has(const QString& lower, std::initializer_list<const char*> needles) {
    for (const char* n : needles)
        if (lower.contains(QLatin1String(n)))
            return true;
    return false;
}

/// An HTTP status named as one — "HTTP 429", "429 Client Error", "status
/// 503", "(403)", "server replied: …" is handled by words — never a bare
/// number, which a timestamp or a ticker can contain.
int df_http_status(const QString& lower) {
    static const QRegularExpression re(
        QStringLiteral(R"((?:\bhttp(?:\s*error)?[\s_]*:?\s*|\bstatus(?:\s*code)?\s*:?\s*|\bcode\s*)([1-5]\d\d)\b)"
                       R"(|\b([45]\d\d)\s+(?:client|server)\s+error\b)"));
    const auto m = re.match(lower);
    if (!m.hasMatch())
        return 0;
    return (m.captured(1).isEmpty() ? m.captured(2) : m.captured(1)).toInt();
}

QString df_source_from_text(const QString& lower) {
    if (df_has(lower, {"yahoo", "yfinance", "yfrate", "query1.finance", "query2.finance", "fc.yahoo", "invalid crumb"}))
        return QStringLiteral("Yahoo Finance");
    if (df_has(lower, {"sec.gov", "edgar"}))
        return QStringLiteral("SEC EDGAR");
    if (df_has(lower, {"nasdaq"}))
        return QStringLiteral("Nasdaq");
    if (df_has(lower, {"stlouisfed", "fred_", "fred ", "fred:"}) && !df_has(lower, {"$fred"}))
        return QStringLiteral("FRED");
    if (df_has(lower, {"forexfactory", "faireconomy"}))
        return QStringLiteral("ForexFactory");
    if (df_has(lower, {"treasury"}))
        return QStringLiteral("US Treasury");
    if (df_has(lower, {"finra"}))
        return QStringLiteral("FINRA");
    if (df_has(lower, {"newyorkfed", "ny fed"}))
        return QStringLiteral("NY Fed");
    return {};
}

} // namespace

QString kind_label(Kind kind) {
    switch (kind) {
        case Kind::RateLimited:
            return QStringLiteral("rate-limited");
        case Kind::Refused:
            return QStringLiteral("refused");
        case Kind::NoAnswer:
            return QStringLiteral("refused or no answer");
        case Kind::Unresolved:
            return QStringLiteral("address not found");
        case Kind::Unreachable:
            return QStringLiteral("no connection");
        case Kind::Timeout:
            return QStringLiteral("timed out");
        case Kind::Tls:
            return QStringLiteral("secure connection failed");
        case Kind::Proxy:
            return QStringLiteral("proxy failed");
        case Kind::ServerError:
            return QStringLiteral("server error");
        case Kind::NotFound:
            return QStringLiteral("not found");
        case Kind::NoData:
            return QStringLiteral("no data");
        case Kind::NeedsKey:
            return QStringLiteral("needs an API key");
        case Kind::LocalService:
            return QStringLiteral("data service failed");
        case Kind::Unknown:
            break;
    }
    return QStringLiteral("failed");
}

Failure classify(const QString& error, const QString& source_hint) {
    Failure f;
    f.raw = error.trimmed();
    const QString lower = f.raw.toLower();
    f.http_status = df_http_status(lower);
    f.source = df_source_from_text(lower);
    if (f.source.isEmpty())
        f.source = source_hint;
    const QString src = f.source.isEmpty() ? QStringLiteral("The data source") : f.source;
    const QString status = f.http_status ? QStringLiteral(" (HTTP %1)").arg(f.http_status) : QString();

    static const QString kNetworkAdvice =
        QStringLiteral("Check that this computer is online. Where %1 is blocked (for example in mainland China) it "
                       "needs a VPN or proxy: make sure it is connected — if your VPN works as a proxy, set it in "
                       "Settings › Data Sources › Network.");

    // Most specific first: a proxy or TLS failure also mentions "connection".
    // A refused connection to this computer itself is the local proxy (a VPN
    // client's): no data source lives on loopback.
    static const QRegularExpression loopback_refused(
        QStringLiteral(R"((?:failed to connect|could not connect|connection refused)[^\n]*(?:127\.0\.0\.1|localhost|\[::1\]))"
                       R"(|(?:127\.0\.0\.1|localhost)[^\n]*(?:connection refused|could not connect))"));
    if (f.http_status == 407 || loopback_refused.match(lower).hasMatch() ||
        df_has(lower, {"proxyerror", "proxy error", "proxy connection", "unable to connect to proxy", "proxy refused",
                       "proxy authentication"})) {
        f.kind = Kind::Proxy;
        f.reason = QStringLiteral("The proxy between this computer and %1 failed or refused the connection.").arg(src);
        f.action = QStringLiteral("Check the proxy in Settings › Data Sources › Network, and that the proxy (or the VPN "
                                  "providing it) is running.");
        f.transient = true;
    } else if (df_has(lower, {"sslerror", "ssl error", "ssl_error", "ssl_connect", "ssl handshake", "sslv3",
                              "certificate", "tls handshake", "tls error", "handshake failed",
                              "wrong version number", "curl: (35)", "curl: (60)"})) {
        f.kind = Kind::Tls;
        f.reason = QStringLiteral("A secure connection to %1 could not be set up.").arg(src);
        f.action = QStringLiteral("A proxy, firewall or antivirus that inspects secure traffic can cause this, and so "
                                  "can a wrong system clock. Check the date and time, then the proxy or VPN.");
        f.transient = true;
    } else if (df_has(lower, {"could not resolve", "couldn't resolve", "name or service not known",
                           "temporary failure in name resolution", "getaddrinfo", "nodename nor servname",
                           "nameresolutionerror", "failed to resolve", "no address associated", "curl: (6)",
                           "host not found"}) ||
               QRegularExpression(QStringLiteral(R"(\bhost\b.*\bnot found\b)")).match(lower).hasMatch()) {
        f.kind = Kind::Unresolved;
        f.reason = QStringLiteral("%1 could not be found — its address did not resolve (DNS).").arg(src);
        f.action = kNetworkAdvice.arg(src);
        f.transient = true;
    } else if (df_has(lower, {"timed out", "timeout", "time-out", "curl: (28)", "refresh_timeout", "deadline exceeded"}) &&
               f.http_status != 504) {
        f.kind = Kind::Timeout;
        f.reason = QStringLiteral("%1 did not answer in time.").arg(src);
        f.action = QStringLiteral("The connection is slow or unstable — finterm tries again by itself. If it keeps "
                                  "happening, check your network or VPN.");
        f.transient = true;
    } else if (df_has(lower, {"connection refused", "connection reset", "connection aborted", "network is unreachable",
                           "no route to host", "failed to connect", "couldn't connect", "remotedisconnected",
                           "remote end closed", "connectionerror", "max retries exceeded", "curl: (7)",
                           "curl: (56)", "recv failure", "network unreachable", "network access is disabled",
                           "connection closed"})) {
        f.kind = Kind::Unreachable;
        f.reason = QStringLiteral("finterm could not connect to %1.").arg(src);
        f.action = kNetworkAdvice.arg(src);
        f.transient = true;
    } else if (f.http_status == 429 || df_has(lower, {"too many requests", "rate limit", "ratelimit", "rate-limit",
                                                   "rate limited"})) {
        f.kind = Kind::RateLimited;
        if (!f.http_status)
            f.http_status = 429;
        f.reason = QStringLiteral("%1 is limiting how often this connection may ask (HTTP 429).").arg(src);
        f.action = QStringLiteral("finterm waits and tries again by itself — give it a minute or two. Fewer data "
                                  "screens open at once means fewer requests. On a VPN or shared network many "
                                  "people share one address: another VPN server often helps.");
        f.transient = true;
    } else if (f.http_status == 401 || f.http_status == 403 ||
               df_has(lower, {"unauthorized", "forbidden", "invalid crumb", "invalid cookie", "access denied",
                           "accessdenied"})) {
        if (df_has(lower, {"api key", "apikey", "api_key", "token"})) {
            f.kind = Kind::NeedsKey;
            f.reason = QStringLiteral("%1 did not accept the API key%2.").arg(src, status);
            f.action = QStringLiteral("Check the key in Settings › API Keys.");
        } else {
            f.kind = Kind::Refused;
            f.reason = QStringLiteral("%1 refused the request%2.").arg(src, status);
            f.action = QStringLiteral("Usually temporary — finterm renews its session and tries again. If it keeps "
                                      "happening, your network's address may be blocked: try another VPN server or "
                                      "region.");
            f.transient = true;
        }
    } else if ((f.http_status >= 500 && f.http_status <= 599) ||
               df_has(lower, {"internal server error", "bad gateway", "service unavailable", "gateway time"})) {
        f.kind = Kind::ServerError;
        f.reason = QStringLiteral("%1 had a problem on its side%2.").arg(src, status);
        f.action = QStringLiteral("Nothing to fix on this computer — it usually clears within minutes, and finterm "
                                  "tries again.");
        f.transient = true;
    } else if (f.http_status == 404 ||
               df_has(lower, {"possibly delisted", "symbol may be delisted", "no such ticker", "unknown to yahoo",
                           "not found", "no timezone found", "quote not found", "invalid symbol"})) {
        f.kind = Kind::NotFound;
        f.reason = QStringLiteral("%1 has nothing under this name or symbol.").arg(src);
        f.action = QStringLiteral("Check the symbol. Listings outside the US need the exchange suffix — 0700.HK, "
                                  "600519.SS, SAP.DE, BHP.AX.");
    } else if (df_has(lower, {"api key", "apikey", "api_key", "needs a key", "no key", "credentials"})) {
        f.kind = Kind::NeedsKey;
        f.reason = QStringLiteral("%1 needs an API key, and none is set.").arg(src);
        f.action = QStringLiteral("Add the key in Settings › API Keys.");
    } else if (df_has(lower, {"refused or did not answer", "did not answer", "no answer", "refused", "unanswered"})) {
        f.kind = Kind::NoAnswer;
        f.reason = QStringLiteral("%1 refused the request or did not answer.").arg(src);
        f.action = QStringLiteral("Usually temporary — finterm tries again by itself. If it keeps happening, check "
                                  "your network or VPN; a VPN server that %1 blocks needs changing.")
                       .arg(f.source.isEmpty() ? QStringLiteral("the source") : f.source);
        f.transient = true;
    } else if (df_has(lower, {"no data", "no priced bar", "returned nothing", "empty", "no recent", "no quote returned",
                           "no rows"})) {
        f.kind = Kind::NoData;
        f.reason = QStringLiteral("%1 answered, but had nothing for this request.").arg(src);
        f.action = QStringLiteral("It may have no data for this symbol or period yet — or it held this one back. "
                                  "Refresh in a while.");
        f.transient = true;
    } else if (df_has(lower, {"modulenotfounderror", "no module named", "importerror", "traceback", "python",
                           "data service", "daemon", "worker", "script failed", "not running", "exited",
                           "crashed"})) {
        f.kind = Kind::LocalService;
        f.reason = QStringLiteral("finterm's data service (its Python part) failed.");
        f.action = QStringLiteral("Restart finterm. If it keeps failing, Settings › Python Env can repair the "
                                  "Python setup.");
        f.transient = true;
    } else {
        f.kind = Kind::Unknown;
        f.reason = QStringLiteral("%1 returned an error.").arg(src);
        f.action = QStringLiteral("finterm tries again by itself. The details below say what was reported.");
        f.transient = true;
    }
    return f;
}

QString one_line(const QString& what, const Failure& f) {
    return QStringLiteral("Couldn't load %1 — %2").arg(what, f.reason);
}

} // namespace finterm::data_failure
