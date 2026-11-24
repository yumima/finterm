#include "ai_chat/HearthInstaller.h"

#include "core/logging/Logger.h"
#include "core/util/ChildEnvironment.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#if !defined(Q_OS_WIN)
#    include <csignal>
#    include <unistd.h>
#endif

namespace finterm {

namespace {
constexpr const char* kTag = "HearthInstaller";
constexpr qint64 kMaxScript = 256 * 1024;
constexpr int kFetchTimeoutMs = 30000;
} // namespace

HearthInstaller& HearthInstaller::instance() {
    static auto* s = new HearthInstaller(qApp);
    return *s;
}

HearthInstaller::HearthInstaller(QObject* parent)
    : QObject(parent), script_(engine::install_script()), nam_(new QNetworkAccessManager(this)) {
    // Not left half done behind a closed window: it goes on from there next time.
    // (Waited for, or the script alone is killed first and what it runs goes on.)
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        const QPointer<QProcess> p = proc_;
        cancel();
        if (p)
            p->waitForFinished(5000);
    });
}

void HearthInstaller::start() {
    if (running_ || !available())
        return;
    running_ = true;
    cancelled_ = false;
    tail_.clear();
    line_ = QStringLiteral("Fetching hearth's installer…");
    emit changed();

    QNetworkRequest req{QUrl(script_.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kFetchTimeoutMs);
    reply_ = nam_->get(req);
    connect(reply_, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64) {
        if (reply_ && got > kMaxScript)
            reply_->abort();
    });
    connect(reply_, &QNetworkReply::finished, this, [this]() {
        auto* r = std::exchange(reply_, nullptr);
        r->deleteLater();
        if (cancelled_)
            return end(false, QStringLiteral("Stopped."));
        if (r->error() != QNetworkReply::NoError)
            return end(false, QStringLiteral("hearth's installer could not be fetched: ") + r->errorString());
        const QByteArray body = r->readAll();
        const QString got = QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
        if (got != script_.sha256) {
            LOG_ERROR(kTag, QString("the script is not the one this finterm knows (sha256 %1)").arg(got));
            return end(false, QStringLiteral("hearth's installer is not the one this finterm knows, and was not run."));
        }
        run(body);
    });
}

void HearthInstaller::run(const QByteArray& script) {
    dir_ = std::make_unique<QTemporaryDir>(); // private to this user: nobody changes the file after the check
#if defined(Q_OS_WIN)
    const QString path = dir_->filePath(QStringLiteral("install.ps1"));
    const QString program = QStandardPaths::findExecutable(QStringLiteral("powershell"));
    const QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                           QStringLiteral("-File"), QDir::toNativeSeparators(path), QStringLiteral("--no-open")};
#else
    const QString path = dir_->filePath(QStringLiteral("install.sh"));
    const QString program = QStringLiteral("/bin/sh");
    const QStringList args{path, QStringLiteral("--no-open")};
#endif
    QFile f(path);
    if (!dir_->isValid() || !f.open(QIODevice::WriteOnly) || f.write(script) != script.size() || !f.flush())
        return end(false, QStringLiteral("The installer could not be written to disk."));
    f.close();

    proc_ = new QProcess(this);
    // hearth's own installation: nothing of finterm's environment has a part in it.
    proc_->setProcessEnvironment(clean_environment());
    proc_->setWorkingDirectory(QDir::homePath());
    proc_->setProcessChannelMode(QProcess::MergedChannels);
#if !defined(Q_OS_WIN)
    proc_->setChildProcessModifier([]() { ::setsid(); }); // a group of its own, to be stopped as one
#endif
    connect(proc_, &QProcess::readyRead, this, [this]() {
        tail_ += QString::fromUtf8(proc_->readAll());
        const QStringList lines = tail_.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& l : lines)
            if (!l.trimmed().isEmpty())
                line_ = l.trimmed();
        tail_ = tail_.right(4000);
        emit changed();
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            end(false, QStringLiteral("The installer could not be run: ") + proc_->errorString());
    });
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus how) {
        if (cancelled_)
            return end(false, QStringLiteral("Stopped. \"Get hearth\" goes on from where it was."));
        const bool ok = how == QProcess::NormalExit && code == 0;
        const QStringList lines = tail_.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        end(ok, ok ? QStringLiteral("hearth is installed.")
                   : lines.isEmpty() ? QStringLiteral("The installer ended without saying why.")
                                     : lines.mid(qMax<qsizetype>(0, lines.size() - 4)).join(QLatin1Char('\n')));
    });
    LOG_INFO(kTag, "running " + script_.url);
    proc_->start(program, args);
}

void HearthInstaller::cancel() {
    if (!running_)
        return;
    cancelled_ = true;
    if (reply_)
        reply_->abort();
    else if (proc_ && proc_->processId() > 0) {
        // The script and what it is running (uv, a download).
#if defined(Q_OS_WIN)
        QProcess::startDetached(QStringLiteral("taskkill"), {QStringLiteral("/T"), QStringLiteral("/F"), QStringLiteral("/PID"),
                                                             QString::number(proc_->processId())});
#else
        ::kill(-static_cast<pid_t>(proc_->processId()), SIGTERM);
#endif
    }
}

void HearthInstaller::end(bool ok, const QString& message) {
    if (!running_)
        return;
    running_ = false;
    if (proc_) {
        proc_->deleteLater();
        proc_ = nullptr;
    }
    dir_.reset();
    line_.clear();
    LOG_INFO(kTag, message);
    emit changed();
    emit finished(ok, message);
}

} // namespace finterm
