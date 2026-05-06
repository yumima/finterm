#include "python/PythonWorker.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonSetupManager.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>

namespace fincept::python {

namespace {

/// Pack a 4-byte big-endian length prefix followed by the JSON payload bytes.
/// Mirrors `_daemon_write_frame` on the Python side.
QByteArray encode_frame(const QJsonObject& obj) {
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const quint32 n = static_cast<quint32>(body.size());
    QByteArray out;
    out.reserve(4 + body.size());
    out.append(static_cast<char>((n >> 24) & 0xff));
    out.append(static_cast<char>((n >> 16) & 0xff));
    out.append(static_cast<char>((n >> 8) & 0xff));
    out.append(static_cast<char>(n & 0xff));
    out.append(body);
    return out;
}

} // namespace

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
        if (proc_) {
            proc_->kill();
        }
    });
}

PythonWorker::~PythonWorker() {
    stop();
}

// static
bool PythonWorker::is_background_action(const QString& action) {
    return action == "batch_all" || action == "batch_quotes" || action == "batch_sparklines";
}

void PythonWorker::submit(const QString& action, const QJsonObject& payload, Callback cb,
                          int timeout_ms) {
    ensure_started();

    // ── Background deduplication ─────────────────────────────────────────────
    // batch_all / batch_quotes / batch_sparklines are idempotent refresh
    // sweeps. Only the latest payload matters. If one is already queued or
    // in-flight, discard the new submission — the refresh timer fires again
    // soon. Without this, slow symbols (FCASH, BRK.B) cause queue growth
    // that blocks interactive equity-research requests for minutes.
    if (is_background_action(action)) {
        // In-flight: daemon already working on it — skip entirely.
        for (const auto& p : in_flight_) {
            if (p.action == action) {
                if (cb) cb(false, {}, "deduped: already in-flight");
                return;
            }
        }
        // Queued: replace with the latest payload so stale data is never sent.
        for (auto& entry : queue_) {
            if (entry.second.action == action) {
                if (entry.second.deadline) entry.second.deadline->deleteLater();
                if (entry.second.cb) entry.second.cb(false, {}, "deduped: superseded");
                entry.second.payload = payload;
                entry.second.cb = std::move(cb);
                // Rebuild deadline timer for the new call site's timeout.
                entry.second.deadline = nullptr;
                if (timeout_ms > 0) {
                    // Timer will be started in try_send_next() when this
                    // entry is actually dispatched to the daemon.
                    entry.second.deadline = new QTimer(this);
                    entry.second.deadline->setSingleShot(true);
                    entry.second.deadline->setInterval(timeout_ms);
                    const int eid = entry.first;
                    const QString eact = action;
                    connect(entry.second.deadline, &QTimer::timeout, this,
                            [this, eid, eact, timeout_ms]() {
                                auto it = in_flight_.find(eid);
                                if (it == in_flight_.end()) return;
                                Pending ep = std::move(it.value());
                                in_flight_.erase(it);
                                LOG_WARN("PythonWorker",
                                         QString("action '%1' (id=%2) timed out after %3ms")
                                             .arg(eact).arg(eid).arg(timeout_ms));
                                if (ep.deadline) ep.deadline->deleteLater();
                                if (ep.cb) ep.cb(false, {}, QString("timeout (%1ms)").arg(timeout_ms));
                                try_send_next();
                            });
                }
                return;
            }
        }
    }

    const int id = next_id_++;
    Pending p;
    p.action = action;
    p.payload = payload;
    p.cb = std::move(cb);

    // Deadline timer — started in try_send_next() when the request is
    // actually dispatched (so queue wait time doesn't count against timeout).
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
                     QString("action '%1' (id=%2) timed out after %3ms — daemon "
                             "may still be working on it; UI callback failed")
                         .arg(action).arg(id).arg(timeout_ms));
            if (tp.deadline) tp.deadline->deleteLater();
            if (tp.cb) tp.cb(false, QJsonObject{}, QString("timeout (%1ms)").arg(timeout_ms));
            try_send_next(); // unblock the queue after timeout
        });
    }

    // ── Priority insertion ───────────────────────────────────────────────────
    // Interactive requests (equity research, news, financials) are inserted
    // before any background batch actions so they reach the daemon first.
    // Background actions go to the back.
    if (!is_background_action(action)) {
        // Find the first background action in the queue and insert before it.
        int insert_pos = queue_.size();
        for (int i = 0; i < queue_.size(); ++i) {
            if (is_background_action(queue_[i].second.action)) {
                insert_pos = i;
                break;
            }
        }
        queue_.insert(insert_pos, {id, std::move(p)});
    } else {
        queue_.append({id, std::move(p)});
    }

    try_send_next();
}

void PythonWorker::try_send_next() {
    if (!ready_ || !proc_ || proc_->state() != QProcess::Running)
        return;
    if (!in_flight_.isEmpty())
        return; // daemon is busy — wait for the response
    if (queue_.isEmpty())
        return;

    auto [id, p] = std::move(queue_.front());
    queue_.removeFirst();

    QJsonObject req;
    req["id"] = id;
    req["action"] = p.action;
    req["payload"] = p.payload;
    if (p.deadline) p.deadline->start(); // start now, not when queued
    in_flight_.insert(id, std::move(p));
    proc_->write(encode_frame(req));
}

void PythonWorker::stop() {
    shutting_down_ = true;
    if (!proc_)
        return;
    if (proc_->state() == QProcess::Running) {
        QJsonObject req;
        req["id"] = 0;
        req["action"] = QStringLiteral("shutdown");
        req["payload"] = QJsonObject{};
        proc_->write(encode_frame(req));
        proc_->closeWriteChannel();
        proc_->waitForFinished(2'000);  // destructor-only blocking call (P1 allows)
        if (proc_->state() != QProcess::NotRunning)
            proc_->kill();
    }
    fail_all_pending(QStringLiteral("worker shutting down"));
}

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

    // Always use venv-numpy2 — yfinance lives there. Fall back to the
    // PythonRunner's resolved interpreter if the venv isn't present.
    QString python_exe =
        PythonSetupManager::instance().python_path(QStringLiteral("venv-numpy2"));
    if (!QFileInfo::exists(python_exe))
        python_exe = runner.python_path();
    if (python_exe.isEmpty()) {
        LOG_WARN("PythonWorker", "No Python interpreter resolved — worker disabled");
        return;
    }

    if (proc_) {
        proc_->deleteLater();
        proc_ = nullptr;
    }
    proc_ = new QProcess(this);
    proc_->setProcessEnvironment(runner.build_python_env());
    proc_->setWorkingDirectory(scripts_dir);
    proc_->setReadChannel(QProcess::StandardOutput);

#ifdef _WIN32
    proc_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpa) {
        cpa->flags |= 0x08000000;  // CREATE_NO_WINDOW
    });
#endif

    connect(proc_, &QProcess::readyReadStandardOutput, this, &PythonWorker::on_ready_read);
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &PythonWorker::on_process_finished);
    connect(proc_, &QProcess::errorOccurred, this, &PythonWorker::on_process_error);
    // Drain stderr to the log so Python import/runtime errors surface.
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        if (!proc_) return;
        const QByteArray err = proc_->readAllStandardError();
        if (!err.isEmpty()) {
            LOG_WARN("PythonWorker", QString("stderr: %1").arg(QString::fromUtf8(err).trimmed()));
        }
    });

    read_buf_.clear();
    ready_ = false;
    LOG_INFO("PythonWorker", QString("Launching daemon: %1 %2 --daemon")
                                  .arg(python_exe).arg(script_path));
    proc_->start(python_exe, {script_path, QStringLiteral("--daemon")});
    ready_watchdog_.start();
}

void PythonWorker::on_process_finished(int exit_code, QProcess::ExitStatus status) {
    ready_ = false;
    ready_watchdog_.stop();
    const QString reason = QString("daemon exited (code=%1 status=%2)").arg(exit_code).arg(status);
    LOG_INFO("PythonWorker", reason);

    // Fail any in-flight requests; queued requests are preserved for the
    // next restart so callers don't observe spurious failures.
    for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
        if (it.value().deadline) it.value().deadline->deleteLater();
        if (it.value().cb) it.value().cb(false, {}, reason);
    }
    in_flight_.clear();

    if (shutting_down_ || QCoreApplication::closingDown()) {
        return;
    }

    if (restart_count_ >= kMaxRestarts) {
        LOG_ERROR("PythonWorker",
                  QString("Restart cap (%1) reached — giving up, pending requests will fail")
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

void PythonWorker::on_ready_read() {
    if (!proc_) return;
    read_buf_.append(proc_->readAllStandardOutput());
    try_drain_frames();
}

void PythonWorker::try_drain_frames() {
    while (read_buf_.size() >= 4) {
        const quint8 b0 = static_cast<quint8>(read_buf_.at(0));
        const quint8 b1 = static_cast<quint8>(read_buf_.at(1));
        const quint8 b2 = static_cast<quint8>(read_buf_.at(2));
        const quint8 b3 = static_cast<quint8>(read_buf_.at(3));
        const quint32 n =
            (static_cast<quint32>(b0) << 24) | (static_cast<quint32>(b1) << 16) |
            (static_cast<quint32>(b2) << 8)  |  static_cast<quint32>(b3);
        if (n > 64u * 1024u * 1024u) {
            LOG_ERROR("PythonWorker", QString("Frame size %1 exceeds 64MB cap — resetting stream").arg(n));
            read_buf_.clear();
            if (proc_) proc_->kill();
            return;
        }
        if (static_cast<quint32>(read_buf_.size()) < 4 + n) {
            return;  // wait for more bytes
        }
        const QByteArray body = read_buf_.mid(4, static_cast<int>(n));
        read_buf_.remove(0, 4 + static_cast<int>(n));

        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            LOG_WARN("PythonWorker",
                     QString("Bad JSON frame: %1 (%2 bytes)").arg(pe.errorString()).arg(body.size()));
            continue;
        }
        const QJsonObject obj = doc.object();

        // Ready handshake
        if (!ready_ && obj.value("ready").toBool()) {
            ready_ = true;
            ready_watchdog_.stop();
            restart_count_ = 0;  // clean handshake resets the retry budget
            LOG_INFO("PythonWorker", QString("Daemon ready (pid=%1)")
                                          .arg(obj.value("pid").toInt()));
            dispatch_queued();
            continue;
        }

        // Response
        const int id = obj.value("id").toInt();
        auto it = in_flight_.find(id);
        if (it == in_flight_.end()) {
            LOG_DEBUG("PythonWorker", QString("Unknown response id=%1 — ignoring").arg(id));
            continue;
        }
        Pending p = std::move(it.value());
        in_flight_.erase(it);
        // Cancel the deadline timer — response beat the timeout.
        if (p.deadline) {
            p.deadline->stop();
            p.deadline->deleteLater();
        }
        const bool ok = obj.value("ok").toBool();
        const QString err = obj.value("error").toString();
        QJsonObject result_obj;
        // Result can be any JSON type; wrap non-object results in a container
        // under "_value" so callers always receive an object.
        const QJsonValue rv = obj.value("result");
        if (rv.isObject()) {
            result_obj = rv.toObject();
        } else {
            result_obj["_value"] = rv;
        }
        if (p.cb) p.cb(ok, result_obj, err);
        try_send_next(); // daemon is free — dispatch the next queued request
    }
}

void PythonWorker::dispatch_queued() {
    // Daemon just became ready — start draining the queue one item at a time.
    try_send_next();
}

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
