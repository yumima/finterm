#include "network/http/HttpClient.h"

#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QUrl>

namespace finterm {

HttpClient& HttpClient::instance() {
    static HttpClient s;
    return s;
}

HttpClient::HttpClient() {
    nam_ = new QNetworkAccessManager(this);
}

QNetworkRequest HttpClient::build_request(const QString& url) const {
    const bool is_relative = !url.startsWith("http");
    const QString full_url = is_relative ? (base_url_ + url) : url;
    QUrl qurl(full_url);
    QNetworkRequest req{qurl};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("finterm/") + QCoreApplication::applicationVersion());

    // Bound hang time. Without this, a half-open or stalled connection
    // leaves the QNetworkReply pending until the OS TCP timeout (minutes),
    // orphaning the callback and pinning memory. 30 s is well past any
    // realistic financial-data API response. This setting can't affect
    // response semantics — it only fires on connections that already
    // weren't going to produce data.
    req.setTransferTimeout(30000);

    // We do NOT set Accept-Encoding: gzip globally, and we do NOT set
    // Http2AllowedAttribute=true globally. Both were tried during a
    // perf-tuning pass and produced intermittent empty-body failures on
    // at least one third-party endpoint (api.nasdaq.com's IPO calendar).
    // The shared cause is third-party servers/CDNs handling these
    // protocol features inconsistently — sometimes correctly, sometimes
    // returning data Qt can't parse. Both options can be opted into
    // per-request by callers that have verified their target endpoint.

    // Only attach the auth headers when the request targets the configured
    // base host (a relative URL). Notification providers (Slack, Discord, Telegram,
    // PagerDuty, user webhooks, …) and other services pass absolute third-party
    // URLs through this same singleton — without this guard their servers would
    // receive the user's X-API-Key and X-Session-Token in every call.
    const bool same_host = is_relative || [&]() {
        const QUrl base(base_url_);
        return !base.host().isEmpty() && qurl.host().compare(base.host(), Qt::CaseInsensitive) == 0;
    }();

    if (same_host) {
        if (!api_key_.isEmpty()) {
            req.setRawHeader("X-API-Key", api_key_.toUtf8());
        }
        if (!session_token_.isEmpty()) {
            req.setRawHeader("X-Session-Token", session_token_.toUtf8());
        }
    }
    return req;
}

void HttpClient::handle_reply(QNetworkReply* reply, JsonCallback callback) {
    connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(callback)]() {
        reply->deleteLater();

        QByteArray data = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        QJsonParseError parse_err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parse_err);

        if (reply->error() != QNetworkReply::NoError) {
            // Strip query string to avoid logging embedded tokens/keys
            QUrl sanitized = reply->url();
            sanitized.setQuery(QString{});
            LOG_WARN("HTTP",
                     QString("HTTP %1: %2 — %3").arg(status).arg(sanitized.toString()).arg(reply->errorString()));

            // 401/403 are always returned as err() so callers can tell an auth
            // rejection apart from a body-carrying error — even when the body is valid JSON.
            if (status == 401 || status == 403) {
                cb(Result<QJsonDocument>::err(QString("HTTP_%1").arg(status).toStdString()));
                return;
            }

            // For other HTTP errors with a parseable JSON body, pass it through
            // so callers can extract server-side error messages.
            if (!data.isEmpty() && parse_err.error == QJsonParseError::NoError && doc.isObject()) {
                cb(Result<QJsonDocument>::ok(doc));
                return;
            }

            // No parseable body — the status sentinel, and Qt's reason after
            // it ("HTTP_0: Proxy connection refused", "HTTP_503: … Service
            // Unavailable"): without it a screen could only say "failed".
            // Never the URL: Qt's text carries it, and a query or path can
            // hold a key or token (FRED ?api_key=, Discord/Telegram paths) —
            // this text reaches the screen and "Copy details". Each URL in
            // it is cut to its host.
            static const QRegularExpression url_re(QStringLiteral(R"(\b[a-z][a-z0-9+.-]*://[^\s]+)"),
                                                   QRegularExpression::CaseInsensitiveOption);
            QString why = reply->errorString();
            for (auto m = url_re.globalMatch(why); m.hasNext();) {
                const auto hit = m.next();
                why.replace(hit.captured(0), QUrl(hit.captured(0)).host());
            }
            cb(Result<QJsonDocument>::err(QString("HTTP_%1: %2").arg(status).arg(why.left(200)).toStdString()));
            return;
        }

        if (parse_err.error != QJsonParseError::NoError) {
            cb(Result<QJsonDocument>::err(parse_err.errorString().toStdString()));
            return;
        }
        cb(Result<QJsonDocument>::ok(doc));
    });
}

void HttpClient::get(const QString& url, JsonCallback callback) {
    LOG_DEBUG("HTTP", "GET " + url);
    auto* reply = nam_->get(build_request(url));
    handle_reply(reply, std::move(callback));
}

void HttpClient::get(const QString& url, const QHash<QByteArray, QByteArray>& extra_headers, JsonCallback callback) {
    LOG_DEBUG("HTTP", "GET " + url);
    QNetworkRequest req = build_request(url);
    for (auto it = extra_headers.constBegin(); it != extra_headers.constEnd(); ++it)
        req.setRawHeader(it.key(), it.value());
    auto* reply = nam_->get(req);
    handle_reply(reply, std::move(callback));
}

void HttpClient::post(const QString& url, const QJsonObject& body, JsonCallback callback) {
    LOG_DEBUG("HTTP", "POST " + url);
    QJsonDocument doc(body);
    auto* reply = nam_->post(build_request(url), doc.toJson());
    handle_reply(reply, std::move(callback));
}

void HttpClient::put(const QString& url, const QJsonObject& body, JsonCallback callback) {
    LOG_DEBUG("HTTP", "PUT " + url);
    QJsonDocument doc(body);
    auto* reply = nam_->put(build_request(url), doc.toJson());
    handle_reply(reply, std::move(callback));
}

void HttpClient::del(const QString& url, JsonCallback callback) {
    LOG_DEBUG("HTTP", "DELETE " + url);
    auto* reply = nam_->deleteResource(build_request(url));
    handle_reply(reply, std::move(callback));
}

void HttpClient::set_auth_header(const QString& api_key) {
    api_key_ = api_key;
}

void HttpClient::set_session_token(const QString& token) {
    session_token_ = token;
}

void HttpClient::clear_session_token() {
    session_token_.clear();
}

void HttpClient::set_base_url(const QString& base) {
    base_url_ = base;
}

} // namespace finterm
