#include "ai_chat/HearthSupervisor.h"

#include "core/logging/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace fincept {

namespace {
constexpr const char* kHearthSupTag = "HearthSupervisor";
constexpr int kRetryBaseMs  = 3000;
constexpr int kRetryMaxMs   = 30000;
constexpr int kMaxRetries   = 8;
constexpr int kSettleMs     = 3000; // how long after start to promote Starting→Running
} // namespace

HearthSupervisor& HearthSupervisor::instance() {
    static HearthSupervisor* s = new HearthSupervisor(qApp);
    return *s;
}

HearthSupervisor::HearthSupervisor(QObject* parent) : QObject(parent) {
    retry_timer_ = new QTimer(this);
    retry_timer_->setSingleShot(true);
    connect(retry_timer_, &QTimer::timeout, this, &HearthSupervisor::start_process);

    settle_timer_ = new QTimer(this);
    settle_timer_->setSingleShot(true);
    connect(settle_timer_, &QTimer::timeout, this, [this]() {
        if (process_ && process_->state() == QProcess::Running
                && state_ == State::Starting) {
            retry_count_ = 0;
            set_state(State::Running);
        }
    });
}

// static
QString HearthSupervisor::hearth_binary() {
    const QByteArray env = qgetenv("FINCEPT_HEARTH_BIN");
    if (!env.isEmpty() && QFile::exists(QString::fromUtf8(env)))
        return QString::fromUtf8(env);

    const QStringList candidates = {
        QDir::homePath() + "/.local/bin/hearth",       // pip / pipx install
        QDir::homePath() + "/fin/hearth/.venv/bin/hearth", // dev checkout
        QDir::homePath() + "/.hearth/bin/hearth",
    };
    for (const QString& p : candidates) {
        if (QFile::exists(p))
            return p;
    }

    return QStandardPaths::findExecutable("hearth");
}

void HearthSupervisor::set_state(State s) {
    if (state_ == s)
        return;
    state_ = s;
    emit state_changed(s);
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
        // Sync enabled_ back to false AND re-persist so the checkbox reflects
        // reality (prevents the checkbox/enabled_ desync bug).
        enabled_ = false;
        set_state(State::Disabled);
        emit state_changed(State::Disabled); // signals the UI to uncheck
        return;
    }

    if (process_ && process_->state() == QProcess::Running)
        return;

    if (!process_) {
        process_ = new QProcess(this);
        process_->setProcessChannelMode(QProcess::MergedChannels);
        connect(process_, &QProcess::finished, this,
                &HearthSupervisor::on_process_finished);
    }

    ++gen_; // invalidate any pending stale-kill timer from a prior stop
    LOG_INFO(kHearthSupTag, QString("starting hearth: %1 start").arg(bin));
    process_->start(bin, {"start"});
    set_state(State::Starting);

    // Promote to Running after settle period (cancellable if we stop early).
    settle_timer_->start(kSettleMs);
}

void HearthSupervisor::stop_process() {
    settle_timer_->stop();
    retry_timer_->stop();

    if (!process_ || process_->state() == QProcess::NotRunning) {
        set_state(State::Disabled);
        return;
    }

    LOG_INFO(kHearthSupTag, "stopping hearth");
    const int g = ++gen_; // mark this stop; a later start bumps gen_ again
    process_->terminate();
    // Non-blocking: we don't call waitForFinished() on the GUI thread.
    // on_process_finished will fire when the process actually exits and
    // set state to Disabled. QProcess::kill() is our backstop if terminate
    // doesn't work — schedule it after a short grace period. The gen_ guard
    // ensures a re-start within the grace window isn't killed by this timer.
    QTimer::singleShot(2000, process_, [this, g]() {
        if (gen_ == g && process_ && process_->state() != QProcess::NotRunning) {
            process_->kill();
        }
    });
    // Optimistically set Disabled now so the UI reflects the intent immediately.
    set_state(State::Disabled);
}

void HearthSupervisor::on_process_finished(int exit_code,
                                            QProcess::ExitStatus /*status*/) {
    settle_timer_->stop();

    if (!enabled_) {
        // Normal clean stop (stop_process already emitted Disabled).
        return;
    }

    LOG_WARN(kHearthSupTag, QString("hearth exited (code %1)").arg(exit_code));

    if (retry_count_ >= kMaxRetries) {
        LOG_WARN(kHearthSupTag, "hearth kept crashing — giving up supervision");
        enabled_ = false;
        // Emit Crashed briefly so the status chip shows it, then Disabled after
        // a short delay so the user actually sees the message.
        set_state(State::Crashed);
        QTimer::singleShot(3000, this, [this]() { set_state(State::Disabled); });
        return;
    }

    set_state(State::Crashed);

    const int delay = std::min(kRetryBaseMs * (1 << retry_count_), kRetryMaxMs);
    ++retry_count_;
    LOG_INFO(kHearthSupTag, QString("restarting hearth in %1 ms (attempt %2/%3)")
                          .arg(delay).arg(retry_count_).arg(kMaxRetries));
    retry_timer_->start(delay);
}

} // namespace fincept
