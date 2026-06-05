#include "ai_chat/EmbeddingsClient.h"

#include "ai_chat/HearthService.h"
#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace fincept {

namespace {

constexpr const char* kTag = "EmbeddingsClient";
constexpr int kTimeoutMs = 20000; // local + tiny model, but allow a cold load

// File-local QNAM parented to qApp — same lifetime discipline as the other
// network-using tool families.
QNetworkAccessManager* emb_nam() {
    static QNetworkAccessManager* nam = [] {
        auto* m = new QNetworkAccessManager(qApp);
        m->setObjectName(QStringLiteral("EmbeddingsNam"));
        return m;
    }();
    return nam;
}

} // namespace

QString EmbeddingsClient::base_url() {
    // An embeddings-specific override still wins (lets you send embeddings
    // somewhere other than chat); otherwise use the one engine base that the
    // chat path also resolves through — so chat and embeddings never diverge.
    const QByteArray env = qgetenv("FINCEPT_EMBEDDINGS_BASE_URL");
    if (!env.isEmpty()) {
        QString url = QString::fromUtf8(env);
        while (url.endsWith('/'))
            url.chop(1);
        return url;
    }
    return HearthService::instance().engine_base_url();
}

void EmbeddingsClient::embed(
    const QString& text, std::function<void(std::optional<QVector<float>>)> done) {
    if (text.trimmed().isEmpty()) {
        done(std::nullopt);
        return;
    }

    QUrl url(base_url() + QStringLiteral("/v1/embeddings"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // hearth ignores auth on loopback; harmless for other OpenAI-compat servers.
    req.setRawHeader("Authorization", "Bearer sk-no-key");

    QJsonObject body{
        {"model", "embedding"}, // role alias; engine resolves to the bound model
        {"input", text},
    };
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = emb_nam()->post(req, payload);

    QTimer::singleShot(kTimeoutMs, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });

    QObject::connect(
        reply, &QNetworkReply::finished, reply, [reply, done = std::move(done)]() {
            const QByteArray resp = reply->readAll();
            const auto err = reply->error();
            reply->deleteLater();

            if (err != QNetworkReply::NoError) {
                LOG_DEBUG(kTag, QString("embeddings request failed: %1")
                                    .arg(static_cast<int>(err)));
                done(std::nullopt);
                return;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(resp);
            const QJsonArray data = doc.object().value("data").toArray();
            if (data.isEmpty()) {
                done(std::nullopt);
                return;
            }
            const QJsonArray vec = data.first().toObject().value("embedding").toArray();
            if (vec.isEmpty()) {
                done(std::nullopt);
                return;
            }
            QVector<float> out;
            out.reserve(vec.size());
            for (const auto& v : vec)
                out.push_back(static_cast<float>(v.toDouble()));
            done(std::move(out));
        });
}

} // namespace fincept
