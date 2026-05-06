#include "python/PythonWorker.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonSetupManager.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace fincept::python {

namespace {

QByteArray encode_frame(const QJsonObject& obj) {
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const quint32 n = static_cast<quint32>(body.size());
    QByteArray out;
    out.reserve(4 + body.size());
    out.append(static_cast<char>((n >> 24) & 0xff));
    out.append(static_cast<char>((n >> 16) & 0xff));
    out.append(static_cast<char>((n >> 8)  & 0xff));
    out.append(static_cast<char>(n         & 0xff));
    out.append(body);
    return out;
}

} // namespace

// ── Singleton ─────────────────────────────────────────────────────────────────

PythonWorker& PythonWorker::instance() {
    static PythonWorker s;
    return s;
}

PythonWorker::PythonWorker() {
    ready_watchdog_.setSingleShot(true);
    ready_watchdog_.setInterval(kReadyTimeoutMs);
    connect(&ready_watchdog_, &QTimer::timeout, this, [this]() {
        if (ready_) return;
        LOG_WARN("PythonWorker", "Daemon handshake timed out — killing and restarting");
        if (proc_) proc_->kill();
    });
}

PythonWorker::~PythonWorker() {
    stop();
}

// ── Background action classification ─────────────────────────────────────────

// static
bool PythonWorker::is_background_action(const QString& action) {
    return action == "batch_all" || action == "batch_quotes" || action == "batch_sparklines";
}

// ── submit ────────────────────────────────────────────────────────────────────

void PythonWorker::submit(const QString& action, const QJsonObject& payload, Callback cb,
                          int timeout_ms) {
    ensure_started();

    // ── Background deduplication ─────────────────────────────────────────────
    // batch_all/batch_quotes/batch_sparklines are idempotent refresh sweeps.
    // Only the latest payload matters; stale queued or in-flight copies are
    // discarded so the daemon queue never grows under slow-symbol conditions.
    if (is_background_action(action)) {
        // Already in-flight: daemon is working on it — drop new submission.
        for (const auto& p : in_flight_) {
            if (p.action == action) {
                if (cb) cb(false, {}, "deduped: already in-flight");
                return;
            }
        }
        // Already queued: replace with latest payload (keep same queue slot).
        for (auto& entry : queue_) {
            if (entry.second.action == action) {
                if (entry.second.deadline) entry.second.deadline->deleteLater();
                if (entry.second.cb) entry.second.cb(false, {}, "deduped: superseded");
                entry.second.payload = payload;
                entry.second.cb      = std::move(cb);
                entry.second.deadline = nullptr;
                if (timeout_ms > 0) {
                    auto* t = new QTimer(this);
                    t->setSingleShot(true);
                    t->setInterval(timeout_ms);
                    const int eid = entry.first;
                    connect(t, &QTimer::timeout, this, [this, eid, action, timeout_ms]() {
                        auto it = in_flight_.find(eid);
                        if (it == in_flight_.end()) return;
                        Pending ep = std::move(it.value());
                        in_flight_.erase(it);
                        LOG_WARN("PythonWorker",
                                 QString("action '%1' (id=%2) timed out after %3ms")
                                     .arg(action).arg(eid).arg(timeout_ms));
                        if (ep.deadline) ep.deadline->deleteLater();
                        if (ep.cb) ep.cb(false, {}, QString("timeout (%1ms)").arg(timeout_ms));
                        on_background_complete();
                    });
                    entry.second.deadline = t;
                }
                return;
            }
        }
    }

    const int id = next_id_++;
    Pending p;
    p.action  = action;
    p.payload = payload;
    p.cb      = std::move(cb);

    if (timeout_ms > 0) {
        p.deadline = new QTimer(this);
        p.deadline->setSingleShot(true);
        p.deadline->setInterval(timeout_ms);
        connect(p.deadline, &QTimer::timeout, this, [this, id, action, timeout_ms]() {
            auto it = in_flight_.find(id);
            if (it == in_flight_.end()) return;
            Pending tp = std::move(it.value());
            in_flight_.erase(it);
            LOG_WARN("PythonWorker",
                     QString("action '%1' (id=%2) timed out after %3ms — "
                             "daemon may still be working on it; UI callback failed")
                         .arg(action).arg(id).arg(timeout_ms));
            if (tp.deadline) tp.deadline->deleteLater();
            if (tp.cb) tp.cb(false, {}, QString("timeout (%1ms)").arg(timeout_ms));
            // If it was a background action, unblock the next queued background.
            if (is_background_action(action)) on_background_complete();
        });
    }

    bool socket_live = socket_ && socket_->state() == QLocalSocket::ConnectedState;

    if (!socket_live) {
        // Queue while socket is connecting. Interactive requests jump in front
        // of any pending background actions so they reach the daemon first.
        if (!is_background_action(action)) {
            int insert_at = 0;
            for (int i = 0; i < queue_.size(); ++i) {
                if (is_background_action(queue_[i].second.action)) { insert_at = i; break; }
                insert_at = i + 1;
            }
            queue_.insert(insert_at, {id, std::move(p)});
        } else {
            queue_.append({id, std::move(p)});
        }
        return;
    }

    // Socket is live. Interactive requests go immediately; background requests
    // are gated — only one of each type may be in-flight at a time.
    if (is_background_action(action)) {
        for (const auto& existing : in_flight_) {
            if (is_background_action(existing.action)) {
                queue_.append({id, std::move(p)});
                return;
            }
        }
    }

    send_to_daemon(id, std::move(p));
}

// ── send_to_daemon ────────────────────────────────────────────────────────────

void PythonWorker::send_to_daemon(int id, Pending p) {
    QJsonObject req;
    req["id"]      = id;
    req["action"]  = p.action;
    req["payload"] = p.payload;
    if (p.deadline) p.deadline->start();
    in_flight_.insert(id, std::move(p));
    socket_->write(encode_frame(req));
}

// ── stop ──────────────────────────────────────────────────────────────────────

void PythonWorker::stop() {
    shutting_down_ = true;
    if (socket_ && socket_->state() == QLocalSocket::ConnectedState) {
        QJsonObject req;
        req["id"]      = 0;
        req["action"]  = QStringLiteral("shutdown");
        req["payload"] = QJsonObject{};
        socket_->write(encode_frame(req));
        socket_->waitForBytesWritten(1'000);
        socket_->disconnectFromServer();
    }
    if (proc_ && proc_->state() == QProcess::Running) {
        proc_->closeWriteChannel();
        proc_->waitForFinished(2'000);
        if (proc_->state() != QProcess::NotRunning)
            proc_->kill();
    }
    if (!socket_path_.isEmpty())
        QFile::remove(socket_path_);
    fail_all_pending(QStringLiteral("worker shutting down"));
}

// ── ensure_started / launch_process ──────────────────────────────────────────

void PythonWorker::ensure_started() {
    if (proc_ && proc_->state() != QProcess::NotRunning)
        return;
    launch_process();
}

void PythonWorker::launch_process() {
    auto& runner = PythonRunner::instance();

    const QString scripts_dir = runner.scripts_dir();
    const QString script_path = scripts_dir + "/yfinance_data.py";
    if (!QFileInfo::exists(script_path)) {
        LOG_WARN("PythonWorker", "yfinance_data.py not found — worker disabled");
        return;
    }

    QString python_exe =
        PythonSetupManager::instance().python_path(QStringLiteral("venv-numpy2"));
    if (!QFileInfo::exists(python_exe))
        python_exe = runner.python_path();
    if (python_exe.isEmpty()) {
        LOG_WARN("PythonWorker", "No Python interpreter resolved — worker disabled");
        return;
    }

    // Clean up from previous run
    if (socket_) {
        socket_->disconnect();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    if (proc_) {
        proc_->deleteLater();
        proc_ = nullptr;
    }
    socket_buf_.clear();
    read_buf_.clear();

    // Socket path in the app's runtime directory (already created at startup).
    socket_path_ = fincept::AppPaths::runtime() + "/yfinance.sock";
    QFile::remove(socket_path_); // remove any stale file from a previous crash

    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(runner.build_python_env());
    proc_->setWorkingDirectory(scripts_dir);
    proc_->setReadChannel(QProcess::StandardOutput);

#ifdef _WIN32
    proc_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpa) {
        cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif

    connect(proc_, &QProcess::readyReadStandardOutput, this, &PythonWorker::on_ready_read);
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &PythonWorker::on_process_finished);
    connect(proc_, &QProcess::errorOccurred, this, &PythonWorker::on_process_error);
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        if (!proc_) return;
        const QByteArray err = proc_->readAllStandardError();
        if (!err.isEmpty())
            LOG_WARN("PythonWorker", QString("stderr: %1").arg(QString::fromUtf8(err).trimmed()));
    });

    ready_ = false;
    LOG_INFO("PythonWorker", QString("Launching daemon: %1 %2 --daemon --socket %3")
                                  .arg(python_exe).arg(script_path).arg(socket_path_));
    proc_->start(python_exe, {script_path, "--daemon", "--socket", socket_path_});
    ready_watchdog_.start();
}

// ── on_process_finished ───────────────────────────────────────────────────────

void PythonWorker::on_process_finished(int exit_code, QProcess::ExitStatus status) {
    ready_ = false;
    ready_watchdog_.stop();
    const QString reason = QString("daemon exited (code=%1 status=%2)").arg(exit_code).arg(status);
    LOG_INFO("PythonWorker", reason);

    // Disconnect socket — it points to a dead process.
    if (socket_) {
        socket_->disconnect();
        socket_->deleteLater();
        socket_ = nullptr;
    }

    for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
        if (it.value().deadline) it.value().deadline->deleteLater();
        if (it.value().cb) it.value().cb(false, {}, reason);
    }
    in_flight_.clear();

    if (shutting_down_ || QCoreApplication::closingDown())
        return;

    if (restart_count_ >= kMaxRestarts) {
        LOG_ERROR("PythonWorker",
                  QString("Restart cap (%1) reached — pending requests will fail")
                      .arg(kMaxRestarts));
        fail_all_pending(QStringLiteral("worker restart cap reached"));
        return;
    }
    ++restart_count_;
    LOG_INFO("PythonWorker", QString("Restarting daemon (attempt %1/%2)")
                                  .arg(restart_count_).arg(kMaxRestarts));
    launch_process();
}

void PythonWorker::on_process_error(QProcess::ProcessError err) {
    LOG_WARN("PythonWorker", QString("QProcess error: %1 (%2)")
                                  .arg(err)
                                  .arg(proc_ ? proc_->errorString() : QString()));
}

// ── Ready handshake (stdout) ──────────────────────────────────────────────────

void PythonWorker::on_ready_read() {
    if (!proc_) return;
    read_buf_.append(proc_->readAllStandardOutput());

    // Parse the single ready frame from stdout.
    while (read_buf_.size() >= 4) {
        const quint8 b0 = static_cast<quint8>(read_buf_.at(0));
        const quint8 b1 = static_cast<quint8>(read_buf_.at(1));
        const quint8 b2 = static_cast<quint8>(read_buf_.at(2));
        const quint8 b3 = static_cast<quint8>(read_buf_.at(3));
        const quint32 n = (static_cast<quint32>(b0) << 24) |
                          (static_cast<quint32>(b1) << 16) |
                          (static_cast<quint32>(b2) << 8)  |
                           static_cast<quint32>(b3);
        if (n > 64u * 1024u * 1024u) { read_buf_.clear(); return; }
        if (static_cast<quint32>(read_buf_.size()) < 4 + n) return;

        const QByteArray body = read_buf_.mid(4, static_cast<int>(n));
        read_buf_.remove(0, 4 + static_cast<int>(n));

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();

        if (!ready_ && obj.value("ready").toBool()) {
            ready_ = true;
            ready_watchdog_.stop();
            restart_count_ = 0;
            LOG_INFO("PythonWorker", QString("Daemon ready (pid=%1) — connecting socket %2")
                                          .arg(obj.value("pid").toInt())
                                          .arg(socket_path_));

            // Connect to the Unix domain socket the daemon bound.
            socket_ = new QLocalSocket(this);
            connect(socket_, &QLocalSocket::readyRead,   this, &PythonWorker::on_socket_data);
            connect(socket_, &QLocalSocket::connected,   this, [this]() {
                LOG_INFO("PythonWorker", "Socket connected — dispatching queued requests");
                dispatch_queued();
            });
            connect(socket_, &QLocalSocket::errorOccurred, this,
                    [this](QLocalSocket::LocalSocketError err) {
                        LOG_WARN("PythonWorker",
                                 QString("Socket error %1: %2").arg(err)
                                     .arg(socket_ ? socket_->errorString() : QString()));
                    });
            socket_->connectToServer(socket_path_);
            return; // ready frame processed — nothing more expected on stdout
        }
    }
}

// ── Socket data ───────────────────────────────────────────────────────────────

void PythonWorker::on_socket_data() {
    if (!socket_) return;
    socket_buf_.append(socket_->readAll());
    try_drain_frames();
}

void PythonWorker::try_drain_frames() {
    while (socket_buf_.size() >= 4) {
        const quint8 b0 = static_cast<quint8>(socket_buf_.at(0));
        const quint8 b1 = static_cast<quint8>(socket_buf_.at(1));
        const quint8 b2 = static_cast<quint8>(socket_buf_.at(2));
        const quint8 b3 = static_cast<quint8>(socket_buf_.at(3));
        const quint32 n = (static_cast<quint32>(b0) << 24) |
                          (static_cast<quint32>(b1) << 16) |
                          (static_cast<quint32>(b2) << 8)  |
                           static_cast<quint32>(b3);
        if (n > 64u * 1024u * 1024u) {
            LOG_ERROR("PythonWorker",
                      QString("Frame size %1 exceeds 64 MB — resetting socket stream").arg(n));
            socket_buf_.clear();
            if (socket_) socket_->disconnectFromServer();
            return;
        }
        if (static_cast<quint32>(socket_buf_.size()) < 4 + n) return;

        const QByteArray body = socket_buf_.mid(4, static_cast<int>(n));
        socket_buf_.remove(0, 4 + static_cast<int>(n));

        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            LOG_WARN("PythonWorker",
                     QString("Bad JSON frame: %1 (%2 bytes)").arg(pe.errorString()).arg(body.size()));
            continue;
        }
        const QJsonObject obj = doc.object();
        const int id = obj.value("id").toInt();

        auto it = in_flight_.find(id);
        if (it == in_flight_.end()) {
            LOG_DEBUG("PythonWorker", QString("Unknown response id=%1 — ignoring").arg(id));
            continue;
        }
        Pending p = std::move(it.value());
        in_flight_.erase(it);
        if (p.deadline) { p.deadline->stop(); p.deadline->deleteLater(); }

        const bool    ok  = obj.value("ok").toBool();
        const QString err = obj.value("error").toString();
        QJsonObject   result_obj;
        const QJsonValue rv = obj.value("result");
        if (rv.isObject()) result_obj = rv.toObject();
        else result_obj["_value"] = rv;

        const bool was_background = is_background_action(p.action);
        if (p.cb) p.cb(ok, result_obj, err);

        // Background slot just freed — send next queued background action.
        if (was_background) on_background_complete();
    }
}

// ── dispatch_queued / on_background_complete ──────────────────────────────────

void PythonWorker::dispatch_queued() {
    if (!socket_ || socket_->state() != QLocalSocket::ConnectedState) return;

    // Send ALL interactive requests immediately (daemon handles concurrently).
    // Send at most ONE background action (gate opens after response arrives).
    bool bg_sent = false;
    QVector<QPair<int, Pending>> remaining;

    for (auto& entry : queue_) {
        if (!is_background_action(entry.second.action)) {
            send_to_daemon(entry.first, std::move(entry.second));
        } else if (!bg_sent) {
            send_to_daemon(entry.first, std::move(entry.second));
            bg_sent = true;
        } else {
            remaining.append(std::move(entry));
        }
    }
    queue_ = std::move(remaining);
}

void PythonWorker::on_background_complete() {
    // A background slot is now free — send the next queued background action.
    if (!socket_ || socket_->state() != QLocalSocket::ConnectedState) return;
    for (int i = 0; i < queue_.size(); ++i) {
        if (is_background_action(queue_[i].second.action)) {
            auto entry = std::move(queue_[i]);
            queue_.removeAt(i);
            send_to_daemon(entry.first, std::move(entry.second));
            return;
        }
    }
}

// ── fail_all_pending ──────────────────────────────────────────────────────────

void PythonWorker::fail_all_pending(const QString& reason) {
    for (auto& entry : queue_) {
        if (entry.second.deadline) entry.second.deadline->deleteLater();
        if (entry.second.cb) entry.second.cb(false, {}, reason);
    }
    queue_.clear();
    for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
        if (it.value().deadline) it.value().deadline->deleteLater();
        if (it.value().cb) it.value().cb(false, {}, reason);
    }
    in_flight_.clear();
}

} // namespace fincept::python
