#include "ai_chat/HearthService.h"

#include "ai_chat/LlmService.h"
#include "ai_chat/LlmUrl.h"
#include "core/logging/Logger.h"
#include "core/util/ChildEnvironment.h"
#include "storage/repositories/EngineLinkRepository.h"
#include "storage/repositories/LlmConfigRepository.h"
#include "storage/sqlite/Database.h"

#include <QByteArray>
#include <QCoreApplication> // qApp
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <QtGlobal> // qgetenv

namespace finterm {

namespace {

constexpr const char* kHearthTag = "HearthService";
constexpr int kProbeTimeoutMs = 2500; // a port nobody listens on refuses at once; a busy hearth takes its time
constexpr int kConnectTries = 8;      // looks after a start, a second apart
constexpr int kAnswerTimeoutMs = 5000; // it is hearth, and may be busy
constexpr int kStartTimeoutMs = 90000; // the command itself gives up after 45 s
constexpr int kAwaitMs = 1000, kAwaitTries = 45; // an older one, which we look for ourselves

/// One-shot synchronous GET with a hard timeout. Runs on a QtConcurrent worker
/// (its own QEventLoop drives the local QNAM); never on a UI-blocking path.
QByteArray hs_sync_get(const QString& url, int* status_out, int timeout_ms = kProbeTimeoutMs) {
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
    timer.start(timeout_ms);
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = (reply->error() == QNetworkReply::NoError) ? reply->readAll() : QByteArray();
    if (status_out)
        *status_out = status;
    reply->deleteLater();
    return body;
}

// Look at the resolved base (root, no /v1). Pure worker-thread work.
HearthService::Status hs_probe_status(const QString& base_root) {
    HearthService::Status s;
    s.base_url = base_root;
    s.program = HearthService::program();
    engine::Probe p;
    p.installed = !s.program.isEmpty();

    int st = 0;
    const QByteArray body = hs_sync_get(base_root + "/admin/version", &st);
    p.answered = st != 0;
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (st == 200 && o.value("name").toString() == QLatin1String("hearth")) {
        p.hearth = true;
        s.version = o.value("version").toString();
        s.contract = p.contract = o.value("contract").toString();
        if (engine::contract_supported(p.contract)) {
            int roles_st = 0, models_st = 0;
            const QJsonObject roles =
                QJsonDocument::fromJson(hs_sync_get(base_root + "/admin/roles", &roles_st, kAnswerTimeoutMs)).object();
            const QJsonObject models =
                QJsonDocument::fromJson(hs_sync_get(base_root + "/v1/models", &models_st, kAnswerTimeoutMs)).object();
            const QJsonObject chat = roles.value("roles").toObject().value("primary_chat").toObject();
            s.model = chat.value("model").toString();
            s.remote = chat.value("remote").toBool();
            // One that could not say (busy, a slow cloud catalogue) is not
            // taken for one without a model.
            p.model = roles_st != 200 || models_st != 200 || engine::chat_model_present(roles, models);
        }
    }
    s.state = engine::classify(p);
    return s;
}

} // namespace

HearthService& HearthService::instance() {
    static HearthService s;
    return s;
}

HearthService::Resolution HearthService::resolve() const {
    const QByteArray env = qgetenv("FINTERM_ENGINE_BASE_URL");
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
            qApp,
            [self, s]() {
                if (self)
                    self->apply_status(s);
            },
            Qt::QueuedConnection);
    });
}

void HearthService::apply_status(const Status& s) {
    bool usable_changed = false;
    {
        QMutexLocker lk(&mutex_);
        usable_changed = (status_.state == engine::State::Unsupported) != (s.state == engine::State::Unsupported);
        status_ = s;
        detecting_ = false;
    }
    if (usable_changed) // linked() says otherwise now
        ai_chat::LlmService::instance().reload_config();
    if (connect_after_ && !starting_)
        connect_now_started(s.state);
    emit detected();
}

void HearthService::connect_now_started(engine::State state) {
    const auto give_up = [this](const QString& why) {
        connect_after_.reset();
        emit started(false, why);
    };
    if (!Database::instance().is_open() || Database::instance().path() != connect_for_) {
        connect_after_.reset(); // another user is in: it is not theirs to connect
        return;
    }
    switch (state) {
    case engine::State::Ready:
    case engine::State::NoModel: {
        const bool for_chat = *connect_after_;
        connect_after_.reset();
        if (const QString e = connect_engine(for_chat); !e.isEmpty())
            emit started(false, e);
        return;
    }
    case engine::State::Unsupported:
        return give_up(QStringLiteral("hearth is running, and does not match this finterm: it was not connected"));
    default:
        // A look from before it was up, or one it was too busy to answer, or
        // not in full (which reads as another program).
        if (--connect_tries_ <= 0)
            return give_up(state == engine::State::Foreign
                               ? QStringLiteral("another program answers where hearth should: it was not connected")
                               : QStringLiteral("hearth was started, and does not answer yet: connect it when it does"));
        QTimer::singleShot(kAwaitMs, this, [this]() { detect(); });
    }
}

HearthService::Status HearthService::status() const {
    QMutexLocker lk(&mutex_);
    return status_;
}

// ── the user's answer ────────────────────────────────────────────────────────

engine::Link HearthService::link() const {
    return EngineLinkRepository::instance().link();
}

bool HearthService::linked() const {
    if (link() != engine::Link::Connected)
        return false;
    QMutexLocker lk(&mutex_);
    return status_.state != engine::State::Unsupported;
}

void HearthService::link_was_changed() {
    ai_chat::LlmService::instance().reload_config();
    emit link_changed();
}

QString HearthService::connect_engine(bool for_chat) {
    auto r = EngineLinkRepository::instance().connect(for_chat);
    if (r.is_err())
        return QString::fromStdString(r.error());
    connect_after_.reset(); // the user's last word, whatever was waiting
    LOG_INFO(kHearthTag, for_chat ? "connected, for chat too" : "connected");
    link_was_changed();
    return {};
}

QString HearthService::use_for_chat() {
    if (link() != engine::Link::Connected)
        return QStringLiteral("hearth is not connected; the user can connect it in Settings › AI Config");
    return connect_engine(true);
}

QString HearthService::use_another_for_chat(const QString& provider) {
    auto r = LlmConfigRepository::instance().set_active(provider);
    if (r.is_err())
        return QString::fromStdString(r.error());
    if (connect_after_)
        connect_after_ = false; // still to be connected, no longer for the chat
    link_was_changed();
    return {};
}

QString HearthService::disconnect_engine() {
    auto r = EngineLinkRepository::instance().disconnect();
    if (r.is_err())
        return QString::fromStdString(r.error());
    connect_after_.reset();
    LOG_INFO(kHearthTag, "disconnected");
    link_was_changed();
    return {};
}

QString HearthService::decline_engine() {
    auto r = EngineLinkRepository::instance().decline();
    if (r.is_err())
        return QString::fromStdString(r.error());
    connect_after_.reset();
    link_was_changed();
    return {};
}

// ── its program ──────────────────────────────────────────────────────────────

QString HearthService::install_dir() {
    if (const QString own = qEnvironmentVariable("HEARTH_DATA"); QDir::isAbsolutePath(own))
        return QDir::cleanPath(QDir::fromNativeSeparators(own));
#if defined(Q_OS_WIN)
    const QString base = qEnvironmentVariable("LOCALAPPDATA", QDir::homePath() + "/AppData/Local");
#elif defined(Q_OS_MACOS)
    const QString base = QDir::homePath() + "/Library/Application Support";
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    const QString base = QDir::isAbsolutePath(xdg) ? xdg : QDir::homePath() + "/.local/share";
#endif
    return QDir::fromNativeSeparators(base) + "/hearth";
}

QString HearthService::program() {
    const QString env = qEnvironmentVariable("FINTERM_HEARTH_BIN");
    if (!env.isEmpty())
        return QFileInfo(env).isExecutable() ? env : QString();
#if defined(Q_OS_WIN)
    const QString name = QStringLiteral("hearth.exe");
#else
    const QString name = QStringLiteral("hearth");
#endif
    for (const QString& p : {install_dir() + "/bin/" + name, QDir::homePath() + "/.local/bin/" + name})
        if (QFileInfo(p).isFile() && QFileInfo(p).isExecutable())
            return p;
    return QStandardPaths::findExecutable(QStringLiteral("hearth"));
}

void HearthService::start_and_connect(bool for_chat) {
    connect_after_ = for_chat;
    connect_tries_ = kConnectTries;
    connect_for_ = Database::instance().path();
    start();
}

void HearthService::start() {
    if (starting_)
        return;
    const QString bin = program();
    if (bin.isEmpty()) {
        connect_after_.reset();
        emit started(false, QStringLiteral("hearth is not installed on this computer"));
        return;
    }
    LOG_INFO(kHearthTag, "starting " + bin);
    // Not bound to the app (lifecycle::bind_to_app): hearth goes on without it.
    starting_ = new QProcess(this);
    starting_->setProcessEnvironment(clean_environment());
    starting_->setProcessChannelMode(QProcess::MergedChannels);
    starting_->setWorkingDirectory(QDir::homePath());
    QProcess* proc = starting_;
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (proc == starting_ && e == QProcess::FailedToStart)
            finish_start(false, QStringLiteral("could not run %1: %2").arg(proc->program(), proc->errorString()));
    });
    connect(proc, &QProcess::finished, this, [this, proc, bin](int code, QProcess::ExitStatus how) {
        if (proc != starting_)
            return;
        const QString said = QString::fromUtf8(proc->readAll()).trimmed();
        if (how == QProcess::NormalExit && code == 2 && said.contains(QLatin1String("unrecognized arguments: --background"))) {
            // A hearth from before `start --background`: it serves from its own process.
            QProcess detached;
            detached.setProgram(bin);
            detached.setArguments({QStringLiteral("start")});
            detached.setProcessEnvironment(clean_environment());
            detached.setWorkingDirectory(QDir::homePath());
            detached.setStandardOutputFile(QProcess::nullDevice());
            detached.setStandardErrorFile(QProcess::nullDevice());
            if (detached.startDetached())
                await_serving(kAwaitTries);
            else
                finish_start(false, QStringLiteral("could not run %1").arg(bin));
            return;
        }
        const bool ok = how == QProcess::NormalExit && code == 0;
        finish_start(ok, ok ? QStringLiteral("hearth is running") : said.isEmpty() ? QStringLiteral("hearth did not start") : said);
    });
    QTimer::singleShot(kStartTimeoutMs, proc, [this, proc]() {
        if (proc != starting_)
            return;
        finish_start(false, QStringLiteral("hearth did not start in %1 s").arg(kStartTimeoutMs / 1000));
        proc->kill();
    });
    proc->start(bin, {QStringLiteral("start"), QStringLiteral("--background")});
}

void HearthService::await_serving(int tries_left) {
    // detect() is asynchronous: each look is followed by the next, until it serves.
    connect(this, &HearthService::detected, this,
            [this, tries_left]() {
                if (!starting_)
                    return;
                const auto st = status().state;
                if (st == engine::State::Ready || st == engine::State::NoModel || st == engine::State::Unsupported)
                    finish_start(true, QStringLiteral("hearth is running"));
                else if (tries_left <= 0)
                    finish_start(false, QStringLiteral("hearth did not answer"));
                else
                    QTimer::singleShot(kAwaitMs, this, [this, tries_left]() {
                        if (starting_)
                            await_serving(tries_left - 1);
                    });
            },
            Qt::SingleShotConnection);
    detect();
}

void HearthService::finish_start(bool ok, const QString& message) {
    if (!starting_)
        return;
    starting_->deleteLater();
    starting_ = nullptr;
    LOG_INFO(kHearthTag, "start: " + message);
    if (!ok)
        connect_after_.reset();
    emit started(ok, message);
    detect();
}

void HearthService::start_if_wanted() {
    auto& repo = EngineLinkRepository::instance();
    const QString whose = Database::instance().path();
    if (!repo.linked() || !repo.start_with_app() || start_offered_.contains(whose))
        return;
    start_offered_.insert(whose);
    // Found running, it is left alone; the command would say so as well, but
    // this way a connected user's engine is not run through a process for it.
    connect(this, &HearthService::detected, this,
            [this]() {
                if (status().state == engine::State::Stopped)
                    start();
            },
            Qt::SingleShotConnection);
    detect();
}

} // namespace finterm
