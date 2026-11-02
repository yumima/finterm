#include "python/PythonWorker.h"

#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "python/PythonRunner.h"
#include "python/PythonSetupManager.h"

#include <algorithm>
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
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

// static
void PythonWorker::answer(Pending& p, bool ok, const QJsonObject& result, const QString& err) {
    // The caller, then every request merged into it (Background merging).
    if (p.cb) p.cb(ok, result, err);
    for (auto& cb : p.also)
        if (cb) cb(ok, result, err);
}

// static
QJsonObject PythonWorker::merge_background_payload(const QJsonObject& a, const QJsonObject& b) {
    // List fields (symbols, quotes, sparklines, histories): the union, first
    // seen order. Anything else: the newer value.
    auto key = [](const QJsonValue& v) {
        return v.isString() ? v.toString()
                            : QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact));
    };   // strings (symbols) as-is; history request objects serialized once each
    QJsonObject out = a;
    for (auto it = b.begin(); it != b.end(); ++it) {
        const QJsonValue av = out.value(it.key());
        if (it.value().isArray() && av.isArray()) {
            QJsonArray merged = av.toArray();
            QSet<QString> have;
            for (const auto& v : merged) have.insert(key(v));
            for (const auto& v : it.value().toArray()) {
                const QString k = key(v);
                if (!have.contains(k)) { have.insert(k); merged.append(v); }
            }
            out[it.key()] = merged;
        } else {
            out[it.key()] = it.value();
        }
    }
    return out;
}

QTimer* PythonWorker::make_deadline(int id, const QString& action, int timeout_ms) {
    // Started in send_to_daemon (queue wait doesn't count against the budget).
    auto* t = new QTimer(this);
    t->setSingleShot(true);
    t->setInterval(timeout_ms);
    connect(t, &QTimer::timeout, this, [this, id, action]() {
        auto it = in_flight_.find(id);
        if (it == in_flight_.end()) return;
        Pending tp = std::move(it.value());
        in_flight_.erase(it);
        const int ms = tp.deadline ? tp.deadline->interval() : 0;
        LOG_WARN("PythonWorker",
                 QString("action '%1' (id=%2) timed out after %3ms — "
                         "daemon may still be working on it; UI callback failed")
                     .arg(action).arg(id).arg(ms));
        if (tp.deadline) tp.deadline->deleteLater();
        // A timeout is no answer: back off BEFORE the callbacks, so one that
        // re-submits synchronously meets it instead of the free slot.
        if (is_background_action(action)) bg_backoff_.setRemainingTime(kBackgroundBackoffMs);
        answer(tp, false, {}, QString("timeout (%1ms)").arg(ms));
        // If it was a background action, unblock the next queued background
        // (after the back-off: a timeout is no answer).
        if (is_background_action(action)) on_background_complete(/*failed=*/true);
    });
    return t;
}

// ── submit ────────────────────────────────────────────────────────────────────

void PythonWorker::submit(const QString& action, const QJsonObject& payload, Callback cb,
                          int timeout_ms) {
    ensure_started();

    // ── Background merging ───────────────────────────────────────────────────
    // batch_all/batch_quotes/batch_sparklines share ONE background slot. A
    // submission used to be failed when one of the same action was in flight
    // ("deduped: already in-flight") or replaced a queued one ("superseded")
    // — but two callers' symbol sets differ (the hub's 127-quote tick, a
    // 7-quote portfolio group, two fetch_quotes callers), so the hub then
    // waited out its 30 s refresh timeout with the price unchanged and a
    // fetch_quotes caller fell back to stale prices. Now at most ONE request
    // per action waits behind the slot, and a later one merges into it: the
    // union of their lists, every callback answered with the merged result
    // (each caller keeps only its own symbols). Nothing is dropped and the
    // queue cannot grow. The merged request is one download of the union —
    // no more Yahoo work than the separate requests it replaces.
    if (is_background_action(action)) {
        for (auto& entry : queue_) {
            if (entry.second.action != action) continue;
            entry.second.payload = merge_background_payload(entry.second.payload, payload);
            if (cb) {
                if (!entry.second.cb) entry.second.cb = std::move(cb);
                else entry.second.also.append(std::move(cb));   // flat, not nested
            }
            // The merged request carries both callers' work, so it gets both
            // budgets (capped): one shared 10 s for the union failed every
            // merged caller together. A queued request with NO deadline keeps
            // none — its caller asked to wait without a limit, and a later
            // caller must not impose one. The timer starts at dispatch.
            if (timeout_ms > 0 && entry.second.deadline) {
                const int cur = entry.second.deadline->interval();
                // Never below what the queued caller already had.
                entry.second.deadline->setInterval(std::max(cur, std::min(cur + timeout_ms, kMaxMergedTimeoutMs)));
            }
            return;
        }
        // Nothing of this action queued: fall through — the gate below
        // queues it behind an in-flight background request, or sends it.
    }

    const int id = next_id_++;
    Pending p;
    p.action  = action;
    p.payload = payload;
    p.cb      = std::move(cb);
    if (timeout_ms > 0) p.deadline = make_deadline(id, action, timeout_ms);

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
        // Backing off after a refusal, or older background requests still
        // waiting: wait in line (no jumping the queue from a callback).
        bool older_waiting = false;
        for (const auto& e : queue_)
            if (is_background_action(e.second.action)) { older_waiting = true; break; }
        if (!bg_backoff_.hasExpired() || older_waiting) {
            queue_.append({id, std::move(p)});
            return;
        }
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
    // Start the deadline timer here — not at submit() time — so queue
    // wait time doesn't count against the caller's timeout budget.
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

    // Socket path in the app's runtime directory (already created at startup),
    // named by THIS process. It used to be one fixed name, and a second
    // instance — another profile, or a duplicate that slipped past the
    // single-instance guard — deleted the first one's socket on its way up and
    // again on its way down. The first instance kept its established
    // connection and nothing on screen said anything; every reconnect after
    // that failed. A per-process name means one instance can only ever touch
    // its own file. Sockets left by crashed processes are swept here, and
    // only when their owner is verifiably gone.
    const QString runtime_dir = fincept::AppPaths::runtime();
    socket_path_ = QStringLiteral("%1/yfinance-%2.sock")
                       .arg(runtime_dir)
                       .arg(QCoreApplication::applicationPid());
    QFile::remove(socket_path_); // our own stale file from a previous crash
    for (const QString& name : QDir(runtime_dir).entryList({QStringLiteral("yfinance-*.sock")},
                                                           QDir::System | QDir::Files)) {
        const QString pid = name.mid(9, name.size() - 14);   // yfinance-<pid>.sock
        bool ok = false;
        const qint64 owner = pid.toLongLong(&ok);
        if (!ok || owner == QCoreApplication::applicationPid())
            continue;
#ifdef Q_OS_LINUX
        // A pid with no /proc entry is dead; anything else is left alone.
        if (!QFileInfo::exists(QStringLiteral("/proc/%1").arg(owner)))
            QFile::remove(runtime_dir + "/" + name);
#endif
    }

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

    // Taken out now, answered LAST — after the restart decision: a callback
    // that submits again would otherwise launch a daemon from inside the
    // loop, which the launch below then tears down (and it would mutate
    // in_flight_ mid-iteration).
    auto lost = std::move(in_flight_);
    in_flight_.clear();
    auto answer_lost = [this, &lost, &reason]() {
        for (auto it = lost.begin(); it != lost.end(); ++it) {
            if (it.value().deadline) it.value().deadline->deleteLater();
            answer(it.value(), false, {}, reason);
        }
    };

    if (shutting_down_ || QCoreApplication::closingDown()) {
        answer_lost();
        return;
    }

    if (restart_count_ >= kMaxRestarts) {
        LOG_ERROR("PythonWorker",
                  QString("Restart cap (%1) reached — pending requests will fail")
                      .arg(kMaxRestarts));
        fail_all_pending(QStringLiteral("worker restart cap reached"));
        answer_lost();
        return;
    }
    ++restart_count_;
    LOG_INFO("PythonWorker", QString("Restarting daemon (attempt %1/%2)")
                                  .arg(restart_count_).arg(kMaxRestarts));
    launch_process();
    answer_lost();
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
        // A REFUSED sweep (all or part — the daemon's one rule,
        // yfinance_data._is_refusal_result, sent as the frame's "refused"):
        // the next one waits. Plain "no data" is not a refusal.
        const bool refused = was_background && obj.value(QStringLiteral("refused")).toBool();
        // Set BEFORE the callback: one that re-submits a background action
        // synchronously must meet the back-off, not slip into the free slot.
        if (refused) bg_backoff_.setRemainingTime(kBackgroundBackoffMs);
        answer(p, ok, result_obj, err);

        // Background slot just freed — send next queued background action.
        if (was_background) on_background_complete(refused);
    }
}

// ── dispatch_queued / on_background_complete ──────────────────────────────────

void PythonWorker::dispatch_queued() {
    if (!socket_ || socket_->state() != QLocalSocket::ConnectedState) return;

    // Send ALL interactive requests immediately (daemon handles concurrently).
    // Send at most ONE background action (gate opens after response arrives),
    // and none while backing off (on_background_complete sends it then).
    bool bg_sent = !bg_backoff_.hasExpired();
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

void PythonWorker::on_background_complete(bool failed) {
    // A background slot is now free — send the next queued background action.
    // After a failed one, not at once: a refused sweep is not cached by the
    // daemon, so sending the next straight away re-downloads into the same
    // 429 back to back. Interactive requests are not held.
    if (failed)
        bg_backoff_.setRemainingTime(kBackgroundBackoffMs);   // monotonic: no wall-clock steps
    if (!bg_backoff_.hasExpired()) {
        // Re-armed for what is left: a coarse timer may fire a little early,
        // and returning without re-arming would strand the queue for good.
        QTimer::singleShot(std::max<qint64>(bg_backoff_.remainingTime(), 1) + 5, Qt::PreciseTimer, this,
                           [this]() { on_background_complete(false); });
        return;
    }
    if (!socket_ || socket_->state() != QLocalSocket::ConnectedState) return;
    for (const auto& existing : in_flight_)
        if (is_background_action(existing.action)) return;   // slot already taken
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
    // Taken out BEFORE any callback runs: a callback may submit again, which
    // would merge into (or append to) the very containers being walked.
    auto queued = std::move(queue_);
    queue_.clear();
    auto inflight = std::move(in_flight_);
    in_flight_.clear();
    for (auto& entry : queued) {
        if (entry.second.deadline) entry.second.deadline->deleteLater();
        answer(entry.second, false, {}, reason);
    }
    for (auto it = inflight.begin(); it != inflight.end(); ++it) {
        if (it.value().deadline) it.value().deadline->deleteLater();
        answer(it.value(), false, {}, reason);
    }
}

} // namespace fincept::python
