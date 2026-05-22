#include "mcp/McpOAuth.h"

#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace fincept::mcp {

namespace {

constexpr const char* TAG = "McpOAuth";
constexpr int kRequestTimeoutMs = 15000;
constexpr int kClockSkewSeconds = 30;

QString get_secure(const QString& key) {
    auto r = SecureStorage::instance().retrieve(key);
    return r.is_err() ? QString() : r.value();
}

void put_secure(const QString& key, const QString& value) {
    if (value.isEmpty()) {
        SecureStorage::instance().remove(key);
        return;
    }
    auto r = SecureStorage::instance().store(key, value);
    if (r.is_err())
        LOG_WARN(TAG, QString("SecureStorage::store %1 failed: %2")
                          .arg(key, QString::fromStdString(r.error())));
}

/// Sync GET.  Used for `.well-known` discovery — small responses,
/// short timeout.  Same translation contract as http_post.
Result<QJsonObject> http_get(const QUrl& url, int timeout_ms = 5000) {
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(timeout_ms);
    loop.exec();

    if (timeout.isActive()) {
        timeout.stop();
    } else {
        reply->abort();
        reply->deleteLater();
        return Result<QJsonObject>::err("oauth: discovery request timed out");
    }
    const QByteArray response = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        return Result<QJsonObject>::err(
            QString("oauth: discovery HTTP error %1 (status %2)").arg(static_cast<int>(err)).arg(status).toStdString());
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(response, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        return Result<QJsonObject>::err(
            QString("oauth: discovery returned non-JSON (status %1): %2").arg(status).arg(perr.errorString()).toStdString());
    }
    return Result<QJsonObject>::ok(doc.object());
}

/// Run a sync POST.  Caller is expected to be on a non-UI thread —
/// McpHttpClient already enforces that for its own JSON-RPC calls.
Result<QJsonObject> http_post(const QUrl& url,
                              const QByteArray& body,
                              const QByteArray& content_type,
                              const QString& bearer = {},
                              const QString& basic_auth = {}) {
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, content_type);
    if (!bearer.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + bearer).toUtf8());
    if (!basic_auth.isEmpty())
        req.setRawHeader("Authorization", ("Basic " + basic_auth.toUtf8().toBase64()));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply* reply = nam.post(req, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(kRequestTimeoutMs);
    loop.exec();

    if (timeout.isActive()) {
        timeout.stop();
    } else {
        reply->abort();
        reply->deleteLater();
        return Result<QJsonObject>::err("oauth: request timed out");
    }

    const QByteArray response = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        return Result<QJsonObject>::err(
            QString("oauth: HTTP error %1 (status %2): %3")
                .arg(static_cast<int>(err))
                .arg(status)
                .arg(QString::fromUtf8(response).left(300))
                .toStdString());
    }
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(response, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        return Result<QJsonObject>::err(
            QString("oauth: invalid JSON (status %1): %2")
                .arg(status)
                .arg(perr.errorString())
                .toStdString());
    }
    QJsonObject obj = doc.object();
    if (status >= 400 || obj.contains("error")) {
        const QString reason = obj.value("error_description").toString(
            obj.value("error").toString("unknown"));
        return Result<QJsonObject>::err(
            QString("oauth: server rejected (status %1): %2")
                .arg(status).arg(reason).toStdString());
    }
    return Result<QJsonObject>::ok(obj);
}

/// Dynamic Client Registration (RFC 7591).  Returns (client_id,
/// client_secret) on success.  client_secret is empty for public
/// clients.
Result<QPair<QString, QString>> do_dcr(const OAuthConfig& cfg) {
    QJsonObject metadata;
    metadata["client_name"] = QStringLiteral("finterm");
    metadata["grant_types"] = QJsonArray{cfg.grant_type.isEmpty()
                                             ? QStringLiteral("client_credentials")
                                             : cfg.grant_type};
    metadata["token_endpoint_auth_method"] = QStringLiteral("client_secret_basic");
    if (!cfg.scope.isEmpty())
        metadata["scope"] = cfg.scope;

    const QByteArray body = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    auto r = http_post(QUrl(cfg.registration_url), body, "application/json");
    if (r.is_err())
        return Result<QPair<QString, QString>>::err(r.error());
    const auto obj = r.value();
    const QString cid = obj.value("client_id").toString();
    const QString secret = obj.value("client_secret").toString();
    if (cid.isEmpty())
        return Result<QPair<QString, QString>>::err(
            "oauth: registration response missing client_id");
    return Result<QPair<QString, QString>>::ok({cid, secret});
}

/// client_credentials token request (RFC 6749 §4.4).
Result<QJsonObject> request_client_credentials(const OAuthConfig& cfg,
                                               const QString& client_id,
                                               const QString& client_secret) {
    QUrlQuery form;
    form.addQueryItem("grant_type", "client_credentials");
    if (!cfg.scope.isEmpty())
        form.addQueryItem("scope", cfg.scope);
    // Confidential clients use HTTP Basic auth per RFC 6749 §2.3.1;
    // public clients send client_id in the body.
    QString basic;
    if (!client_secret.isEmpty()) {
        basic = client_id + ":" + client_secret;
    } else {
        form.addQueryItem("client_id", client_id);
    }
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();
    return http_post(QUrl(cfg.token_url), body,
                     "application/x-www-form-urlencoded", QString{}, basic);
}

} // anonymous namespace

QString McpOAuth::secure_key_client_id(const QString& server_id) {
    return QStringLiteral("mcp.oauth.") + server_id + QStringLiteral(".client_id");
}
QString McpOAuth::secure_key_client_secret(const QString& server_id) {
    return QStringLiteral("mcp.oauth.") + server_id + QStringLiteral(".client_secret");
}
QString McpOAuth::secure_key_access_token(const QString& server_id) {
    return QStringLiteral("mcp.oauth.") + server_id + QStringLiteral(".access_token");
}
QString McpOAuth::secure_key_refresh_token(const QString& server_id) {
    return QStringLiteral("mcp.oauth.") + server_id + QStringLiteral(".refresh_token");
}
QString McpOAuth::secure_key_expires_at(const QString& server_id) {
    return QStringLiteral("mcp.oauth.") + server_id + QStringLiteral(".expires_at_unix");
}

void McpOAuth::invalidate_cache(const QString& server_id) {
    SecureStorage::instance().remove(secure_key_access_token(server_id));
    SecureStorage::instance().remove(secure_key_expires_at(server_id));
}

Result<QString> McpOAuth::ensure_access_token(const OAuthConfig& cfg) {
    if (cfg.token_url.isEmpty())
        return Result<QString>::err("oauth: token_url is required");

    // RFC 8414 discovery — when token_url is actually pointing at
    // `.well-known/oauth-authorization-server`, fetch the metadata and
    // resolve to the real token_endpoint + registration_endpoint
    // (when present).  We rebuild the effective config rather than
    // mutating the caller's struct so this stays a pure operation.
    OAuthConfig effective = cfg;
    if (cfg.token_url.endsWith(QStringLiteral("/.well-known/oauth-authorization-server"))) {
        LOG_INFO(TAG, QString("Fetching OAuth discovery at %1").arg(cfg.token_url));
        auto disc = http_get(QUrl(cfg.token_url));
        if (disc.is_err())
            return Result<QString>::err(disc.error());
        const QJsonObject d = disc.value();
        const QString token = d.value(QStringLiteral("token_endpoint")).toString();
        if (token.isEmpty())
            return Result<QString>::err(
                "oauth: discovery document missing token_endpoint");
        effective.token_url = token;
        const QString reg = d.value(QStringLiteral("registration_endpoint")).toString();
        if (!reg.isEmpty() && effective.registration_url.isEmpty())
            effective.registration_url = reg;
    }

    const QString grant = effective.grant_type.isEmpty()
                              ? QStringLiteral("client_credentials")
                              : effective.grant_type;
    if (grant != QStringLiteral("client_credentials"))
        return Result<QString>::err(
            QString("oauth: grant_type '%1' not implemented in this build "
                    "(only client_credentials; authorization_code defers)")
                .arg(grant).toStdString());

    // Cached path — return the access_token while it's still good.
    const QString cached = get_secure(secure_key_access_token(cfg.server_id));
    const QString expires_str = get_secure(secure_key_expires_at(cfg.server_id));
    if (!cached.isEmpty() && !expires_str.isEmpty()) {
        const qint64 expires = expires_str.toLongLong();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (expires > now + kClockSkewSeconds)
            return Result<QString>::ok(cached);
    }

    // Resolve client credentials, doing DCR if needed.
    QString client_id = get_secure(secure_key_client_id(effective.server_id));
    QString client_secret = get_secure(secure_key_client_secret(effective.server_id));
    if (client_id.isEmpty()) {
        if (effective.registration_url.isEmpty())
            return Result<QString>::err(
                "oauth: no client_id in SecureStorage and no registration_url "
                "configured — set mcp.oauth.<server>.client_id manually or "
                "configure DCR");
        LOG_INFO(TAG, QString("Running DCR against %1").arg(effective.registration_url));
        auto dcr = do_dcr(effective);
        if (dcr.is_err())
            return Result<QString>::err(dcr.error());
        client_id = dcr.value().first;
        client_secret = dcr.value().second;
        put_secure(secure_key_client_id(effective.server_id), client_id);
        if (!client_secret.isEmpty())
            put_secure(secure_key_client_secret(effective.server_id), client_secret);
    }

    auto token_r = request_client_credentials(effective, client_id, client_secret);
    if (token_r.is_err())
        return Result<QString>::err(token_r.error());
    const QJsonObject obj = token_r.value();
    const QString access = obj.value("access_token").toString();
    if (access.isEmpty())
        return Result<QString>::err("oauth: token response missing access_token");
    const int expires_in = obj.value("expires_in").toInt(3600);  // RFC 6749 §5.1 default

    // Cache the token + expiry.  Refresh tokens (when present) are
    // recorded too; the authorization_code grant uses them later.
    put_secure(secure_key_access_token(effective.server_id), access);
    put_secure(secure_key_expires_at(effective.server_id),
               QString::number(QDateTime::currentSecsSinceEpoch() + expires_in));
    const QString refresh = obj.value("refresh_token").toString();
    if (!refresh.isEmpty())
        put_secure(secure_key_refresh_token(effective.server_id), refresh);

    return Result<QString>::ok(access);
}

} // namespace fincept::mcp
