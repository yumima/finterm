#include "ai_chat/HearthService.h"

#include "ai_chat/LlmUrl.h"
#include "core/logging/Logger.h"

#include <QByteArray>
#include <QCoreApplication> // qApp
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <QtGlobal> // qgetenv

namespace fincept {

namespace {

constexpr const char* kHearthTag = "HearthService";
constexpr int kProbeTimeoutMs = 600;

/// One-shot synchronous GET with a hard timeout. Runs on a QtConcurrent worker
/// (its own QEventLoop drives the local QNAM); never on a UI-blocking path.
QByteArray hs_sync_get(const QString& url, int* status_out) {
    QNetworkAccessManager nam;
    QNetworkRequest req((QUrl(url)));
    req.setRawHeader("Accept", "application/json");
    QNetworkReply* reply = nam.get(req);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kProbeTimeoutMs);
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body =
        (reply->error() == QNetworkReply::NoError) ? reply->readAll() : QByteArray();
    if (status_out)
        *status_out = status;
    reply->deleteLater();
    return body;
}

// Probe the resolved base (root, no /v1) for /admin/version. Pure worker-thread work.
HearthService::Status hs_probe_status(const QString& base_root) {
    HearthService::Status s;
    s.probed = true;
    s.base_url = base_root;

    int st = 0;
    const QByteArray body = hs_sync_get(base_root + "/admin/version", &st);
    if (st == 200) {
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        if (o.value("name").toString() == QLatin1String("hearth")) {
            s.present = s.is_hearth = true;
            s.version = o.value("version").toString();
            s.contract = o.value("contract").toString();
            const int major = s.contract.split('.').value(0).toInt();
            if (major > HearthService::kKnownContractMajor) {
                LOG_WARN(kHearthTag, QString("hearth contract %1 is newer than finterm "
                                       "targets (%2.x) — some engine features may "
                                       "not be wired")
                                   .arg(s.contract)
                                   .arg(HearthService::kKnownContractMajor));
            }
            return s;
        }
    }

    // Reachable but not hearth? Treat a 200 from /v1/models as "engine present".
    int st2 = 0;
    hs_sync_get(base_root + "/v1/models", &st2);
    s.present = (st2 == 200);
    return s;
}

} // namespace

HearthService& HearthService::instance() {
    static HearthService s;
    return s;
}

HearthService::Resolution HearthService::resolve() const {
    const QByteArray env = qgetenv("FINCEPT_ENGINE_BASE_URL");
    const QString base = env.isEmpty() ? normalize_llm_base(QString::fromLatin1(kDefaultBase))
                                       : normalize_llm_base(QString::fromUtf8(env));
    // Treat hearth's default port as hearth even before any probe, so the
    // common zero-config case addresses role aliases (primary_chat) and "just
    // works" the moment the engine is up at :11435. Compare normalized so an
    // env value with a /v1 suffix still resolves to is_hearth=true.
    return {base, base == normalize_llm_base(QString::fromLatin1(kDefaultBase))};
}

void HearthService::detect() {
    {
        QMutexLocker lk(&mutex_);
        if (detecting_)
            return; // coalesce — one probe in flight at a time
        detecting_ = true;
    }
    const QString base = resolve().base_url;
    QPointer<HearthService> self(this);
    (void)QtConcurrent::run([self, base]() {
        Status s = hs_probe_status(base);
        if (!self)
            return;
        // Marshal store+emit onto the GUI thread (qApp's thread) so connected
        // slots run there — independent of which thread first touched the
        // singleton, so a future off-GUI caller can't break status delivery.
        QMetaObject::invokeMethod(
            qApp, [self, s]() {
                if (self)
                    self->apply_status(s);
            },
            Qt::QueuedConnection);
    });
}

void HearthService::apply_status(const Status& s) {
    {
        QMutexLocker lk(&mutex_);
        status_ = s;
        detecting_ = false;
    }
    emit detected();
}

HearthService::Status HearthService::status() const {
    QMutexLocker lk(&mutex_);
    return status_;
}

} // namespace fincept
