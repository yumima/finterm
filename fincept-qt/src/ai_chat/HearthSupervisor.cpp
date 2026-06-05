#include "ai_chat/HearthSupervisor.h"

#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace fincept {

namespace {
constexpr const char* kHearthSupTag = "HearthSupervisor";
// How long to wait before retrying after a crash (caps at 30 s).
constexpr int kRetryBaseMs  = 3000;
constexpr int kRetryMaxMs   = 30000;
constexpr int kMaxRetries   = 8;
} // namespace

HearthSupervisor& HearthSupervisor::instance() {
    // Owned by qApp so the process is stopped when Qt tears down.
    static HearthSupervisor* s = new HearthSupervisor(qApp);
    return *s;
}

HearthSupervisor::HearthSupervisor(QObject* parent) : QObject(parent) {
    retry_timer_ = new QTimer(this);
    retry_timer_->setSingleShot(true);
    connect(retry_timer_, &QTimer::timeout, this, &HearthSupervisor::start_process);
}

// static
QString HearthSupervisor::hearth_binary() {
    // 1. Explicit override.
    const QByteArray env = qgetenv("FINCEPT_HEARTH_BIN");
    if (!env.isEmpty() && QFile::exists(QString::fromUtf8(env)))
        return QString::fromUtf8(env);

    // 2. pipx / pip user install puts scripts in ~/.local/bin on Linux/macOS.
    const QStringList candidates = {
        QDir::homePath() + "/.local/bin/hearth",
        QDir::homePath() + "/.hearth/bin/hearth",
        QDir::homePath() + "/hearth/.venv/bin/hearth", // dev checkout
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p))
            return p;
    }

    // 3. PATH lookup.
    const QString found = QStandardPaths::findExecutable("hearth");
    return found;
}

void HearthSupervisor::set_enabled(bool enabled) {
    if (enabled_ == enabled)
        return;
    enabled_ = enabled;
    if (enabled) {
        retry_count_ = 0;
        start_process();
    } else {
        stop_process();
    }
}

void HearthSupervisor::start_process() {
    if (!enabled_)
        return;

    const QString bin = hearth_binary();
    if (bin.isEmpty()) {
        LOG_WARN(kHearthSupTag,
            "hearth binary not found — install with `pip install hearth` "
            "or set FINCEPT_HEARTH_BIN. Supervision disabled.");
        enabled_ = false;
        state_ = State::Disabled;
        emit state_changed(state_);
        return;
    }

    if (process_ && process_->state() == QProcess::Running)
        return;

    // Re-use or allocate the process object.
    if (!process_) {
        process_ = new QProcess(this);
        process_->setProcessChannelMode(QProcess::MergedChannels);
        connect(process_, &QProcess::finished, this,
                &HearthSupervisor::on_process_finished);
    }

    LOG_INFO(kHearthSupTag, QString("starting hearth: %1 start").arg(bin));
    process_->start(bin, {"start"});
    state_ = State::Starting;
    emit state_changed(state_);

    // Give it 3 s to settle; if the /admin/health probe in HearthService::detect()
    // succeeds the UI chip updates independently. We just need to track the
    // process state here — not the engine readiness.
    QTimer::singleShot(3000, this, [this]() {
        if (process_ && process_->state() == QProcess::Running
            && state_ == State::Starting) {
            state_ = State::Running;
            emit state_changed(state_);
            retry_count_ = 0;
        }
    });
}

void HearthSupervisor::stop_process() {
    retry_timer_->stop();
    if (!process_ || process_->state() == QProcess::NotRunning) {
        state_ = State::Disabled;
        emit state_changed(state_);
        return;
    }
    LOG_INFO(kHearthSupTag, "stopping hearth");
    process_->terminate();
    if (!process_->waitForFinished(2000))
        process_->kill();
    state_ = State::Disabled;
    emit state_changed(state_);
}

void HearthSupervisor::on_process_finished(int exit_code,
                                            QProcess::ExitStatus /*status*/) {
    if (!enabled_) {
        state_ = State::Disabled;
        emit state_changed(state_);
        return;
    }

    LOG_WARN(kHearthSupTag, QString("hearth exited (code %1)").arg(exit_code));
    state_ = State::Crashed;
    emit state_changed(state_);

    if (retry_count_ >= kMaxRetries) {
        LOG_WARN(kHearthSupTag, "hearth kept crashing — giving up supervision");
        enabled_ = false;
        state_ = State::Disabled;
        emit state_changed(state_);
        return;
    }

    const int delay = std::min(kRetryBaseMs * (1 << retry_count_), kRetryMaxMs);
    ++retry_count_;
    LOG_INFO(kHearthSupTag, QString("restarting hearth in %1 ms (attempt %2/%3)")
                          .arg(delay).arg(retry_count_).arg(kMaxRetries));
    retry_timer_->start(delay);
}

} // namespace fincept
