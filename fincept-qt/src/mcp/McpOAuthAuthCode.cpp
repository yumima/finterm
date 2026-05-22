#include "mcp/McpOAuthAuthCode.h"

#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace fincept::mcp {

namespace {

constexpr const char* kAcTag = "McpOAuthAuthCode";

QString get_secure(const QString& key) {
    auto r = SecureStorage::instance().retrieve(key);
    return r.is_err() ? QString() : r.value();
}
void put_secure(const QString& key, const QString& value) {
    if (value.isEmpty())
        SecureStorage::instance().remove(key);
    else
        SecureStorage::instance().store(key, value);
}

/// Generate base64url-encoded random bytes (no padding).  PKCE
/// requires base64url alphabet — Qt's QByteArray::Base64UrlEncoding
/// gives us exactly that.
QString random_b64url(int byte_count) {
    QByteArray bytes(byte_count, 0);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < byte_count; ++i)
        bytes[i] = static_cast<char>(rng->bounded(256));
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString pkce_challenge(const QString& verifier) {
    return QString::fromLatin1(QCryptographicHash::hash(
                                   verifier.toLatin1(), QCryptographicHash::Sha256)
                                   .toBase64(QByteArray::Base64UrlEncoding |
                                             QByteArray::OmitTrailingEquals));
}

QString build_auth_url(const AuthCodeConfig& cfg,
                      const QString& client_id,
                      const QString& redirect_uri,
                      const QString& state,
                      const QString& code_challenge) {
    QUrl url(cfg.authorization_url);
    QUrlQuery q(url);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("client_id", client_id);
    q.addQueryItem("redirect_uri", redirect_uri);
    q.addQueryItem("state", state);
    q.addQueryItem("code_challenge", code_challenge);
    q.addQueryItem("code_challenge_method", "S256");
    if (!cfg.scope.isEmpty())
        q.addQueryItem("scope", cfg.scope);
    url.setQuery(q);
    return url.toString();
}

/// Inline HTML returned to the user's browser after the callback —
/// they can close the tab.  No interactivity, no JS that could leak
/// the code.
constexpr const char* kCallbackHtml =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html>\n"
    "<html><head><title>finterm — auth complete</title>\n"
    "<style>body{font-family:system-ui;background:#1a1d23;color:#ccc;"
    "display:flex;align-items:center;justify-content:center;height:100vh;"
    "margin:0}.box{text-align:center;padding:40px}"
    ".ok{color:#4ade80;font-size:48px}h1{font-weight:400}</style>\n"
    "</head><body><div class=\"box\">\n"
    "<div class=\"ok\">✓</div>\n"
    "<h1>Authentication received</h1>\n"
    "<p>You may close this window.</p>\n"
    "</div></body></html>";

constexpr const char* kCallbackErrorHtml =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><body>"
    "<h1>Authentication failed</h1>"
    "<p>finterm could not match the callback — try again.</p>"
    "</body></html>";

/// Listen on localhost:port for the redirect, return the
/// authorization code on success.  Validates the state param against
/// the value we generated.
Result<QString> await_callback(int port, const QString& expected_state, int wait_seconds) {
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        return Result<QString>::err(
            QString("oauth: callback listener could not bind 127.0.0.1:%1 — %2")
                .arg(port).arg(server.errorString()).toStdString());
    }
    LOG_INFO(kAcTag, QString("Callback listener up on 127.0.0.1:%1").arg(port));

    QEventLoop loop;
    QString result_code;
    QString error_msg;

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
        error_msg = "oauth: timed out waiting for browser callback";
        loop.quit();
    });

    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
        QTcpSocket* sock = server.nextPendingConnection();
        if (!sock)
            return;
        QObject::connect(sock, &QTcpSocket::readyRead, sock, [&, sock] {
            const QByteArray req = sock->readAll();
            // Parse "GET /callback?code=...&state=... HTTP/1.1"
            const QString first_line = QString::fromUtf8(req).section('\n', 0, 0);
            const int sp1 = first_line.indexOf(' ');
            const int sp2 = first_line.indexOf(' ', sp1 + 1);
            const QString path = (sp1 > 0 && sp2 > sp1)
                                     ? first_line.mid(sp1 + 1, sp2 - sp1 - 1)
                                     : QString();
            QUrl url("http://localhost" + path);
            QUrlQuery q(url);
            const QString code = q.queryItemValue("code");
            const QString state = q.queryItemValue("state");
            const QString err = q.queryItemValue("error");

            if (!err.isEmpty()) {
                error_msg = QString("oauth: provider returned error: %1 (%2)")
                                .arg(err, q.queryItemValue("error_description"));
                sock->write(kCallbackErrorHtml);
            } else if (state != expected_state) {
                error_msg = "oauth: callback state mismatch (possible CSRF) — aborted";
                sock->write(kCallbackErrorHtml);
            } else if (code.isEmpty()) {
                error_msg = "oauth: callback missing code parameter";
                sock->write(kCallbackErrorHtml);
            } else {
                result_code = code;
                sock->write(kCallbackHtml);
            }
            sock->flush();
            sock->disconnectFromHost();
            loop.quit();
        });
    });

    timeout.start(wait_seconds * 1000);
    loop.exec();
    server.close();

    if (!error_msg.isEmpty())
        return Result<QString>::err(error_msg.toStdString());
    return Result<QString>::ok(result_code);
}

/// POST form-encoded body, return parsed JSON.  Mirror of McpOAuth's
/// http_post but lives here to avoid the symbol clash.
Result<QJsonObject> http_post_form(const QUrl& url, const QUrlQuery& form,
                                    const QString& client_id, const QString& client_secret) {
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    if (!client_secret.isEmpty()) {
        const QByteArray basic = (client_id + ":" + client_secret).toUtf8().toBase64();
        req.setRawHeader("Authorization", "Basic " + basic);
    }
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QNetworkReply* reply = nam.post(req, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(15000);
    loop.exec();

    if (timeout.isActive()) {
        timeout.stop();
    } else {
        reply->abort();
        reply->deleteLater();
        return Result<QJsonObject>::err("oauth: token-exchange request timed out");
    }
    const QByteArray response = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto err = reply->error();
    reply->deleteLater();

    if (err != QNetworkReply::NoError) {
        return Result<QJsonObject>::err(
            QString("oauth: token exchange HTTP %1 (status %2): %3")
                .arg(static_cast<int>(err)).arg(status)
                .arg(QString::fromUtf8(response).left(300)).toStdString());
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(response, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return Result<QJsonObject>::err(
            ("oauth: token response not JSON: " + perr.errorString()).toStdString());
    const QJsonObject obj = doc.object();
    if (status >= 400 || obj.contains("error")) {
        const QString reason = obj.value("error_description").toString(obj.value("error").toString("unknown"));
        return Result<QJsonObject>::err(("oauth: token exchange rejected: " + reason).toStdString());
    }
    return Result<QJsonObject>::ok(obj);
}

void store_tokens(const QString& server_id, const QJsonObject& token_response) {
    const QString access = token_response.value("access_token").toString();
    const int expires_in = token_response.value("expires_in").toInt(3600);
    const QString refresh = token_response.value("refresh_token").toString();
    put_secure(McpOAuth::secure_key_access_token(server_id), access);
    put_secure(McpOAuth::secure_key_expires_at(server_id),
               QString::number(QDateTime::currentSecsSinceEpoch() + expires_in));
    if (!refresh.isEmpty())
        put_secure(McpOAuth::secure_key_refresh_token(server_id), refresh);
}

} // anonymous namespace

Result<QString> McpOAuthAuthCode::run_flow(const AuthCodeConfig& cfg) {
    if (cfg.authorization_url.isEmpty() || cfg.token_url.isEmpty())
        return Result<QString>::err(
            "oauth: authorization_code grant requires authorization_url + token_url");

    // Client credentials must exist before the flow can start.  DCR for
    // authorization_code clients with no pre-registration is rare and
    // not what this commit ships; admin sets client_id (+ secret if
    // confidential) into SecureStorage out-of-band.
    const QString client_id = get_secure(McpOAuth::secure_key_client_id(cfg.server_id));
    const QString client_secret = get_secure(McpOAuth::secure_key_client_secret(cfg.server_id));
    if (client_id.isEmpty())
        return Result<QString>::err(
            ("oauth: no client_id in SecureStorage for " + cfg.server_id +
             " — write mcp.oauth.<server>.client_id before invoking the flow")
                .toStdString());

    const QString verifier = random_b64url(48);     // ~64 url-safe chars
    const QString challenge = pkce_challenge(verifier);
    const QString state = random_b64url(24);
    const QString redirect_uri =
        QStringLiteral("http://localhost:%1/callback").arg(cfg.callback_port);

    const QString auth_url = build_auth_url(cfg, client_id, redirect_uri, state, challenge);
    LOG_INFO(kAcTag, QString("Opening browser to %1").arg(QString(auth_url).left(80)));
    if (!QDesktopServices::openUrl(QUrl(auth_url))) {
        LOG_WARN(kAcTag, "QDesktopServices::openUrl returned false — user may need to "
                         "open the URL manually (logged above)");
    }

    auto code_r = await_callback(cfg.callback_port, state, cfg.browser_wait_seconds);
    if (code_r.is_err())
        return Result<QString>::err(code_r.error());

    QUrlQuery form;
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code", code_r.value());
    form.addQueryItem("redirect_uri", redirect_uri);
    form.addQueryItem("code_verifier", verifier);
    if (client_secret.isEmpty())
        form.addQueryItem("client_id", client_id);

    auto token_r = http_post_form(QUrl(cfg.token_url), form, client_id, client_secret);
    if (token_r.is_err())
        return Result<QString>::err(token_r.error());

    const QJsonObject tokens = token_r.value();
    const QString access = tokens.value("access_token").toString();
    if (access.isEmpty())
        return Result<QString>::err("oauth: token response missing access_token");
    store_tokens(cfg.server_id, tokens);
    return Result<QString>::ok(access);
}

Result<QString> McpOAuthAuthCode::refresh(const AuthCodeConfig& cfg) {
    const QString refresh_token = get_secure(McpOAuth::secure_key_refresh_token(cfg.server_id));
    if (refresh_token.isEmpty())
        return Result<QString>::err("oauth: no refresh_token in SecureStorage");
    const QString client_id = get_secure(McpOAuth::secure_key_client_id(cfg.server_id));
    if (client_id.isEmpty())
        return Result<QString>::err("oauth: no client_id in SecureStorage");
    const QString client_secret = get_secure(McpOAuth::secure_key_client_secret(cfg.server_id));

    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", refresh_token);
    if (client_secret.isEmpty())
        form.addQueryItem("client_id", client_id);

    auto r = http_post_form(QUrl(cfg.token_url), form, client_id, client_secret);
    if (r.is_err())
        return Result<QString>::err(r.error());
    const QJsonObject tokens = r.value();
    const QString access = tokens.value("access_token").toString();
    if (access.isEmpty())
        return Result<QString>::err("oauth: refresh response missing access_token");
    store_tokens(cfg.server_id, tokens);
    return Result<QString>::ok(access);
}

} // namespace fincept::mcp
