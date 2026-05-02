#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <functional>

namespace fincept::python {

/// Persistent yfinance worker — long-lived Python process running
/// `yfinance_data.py --daemon`. Avoids the 2–3 second yfinance/pandas import
/// cost that every `PythonRunner::run()` spawn pays.
///
/// Scope: yfinance_data.py ONLY. Do NOT route agent/framework/numpy1 scripts
/// through here — those need PythonRunner's per-script venv routing (numpy1
/// vs numpy2 selection in PythonRunner.cpp::select_venv_for_script).
///
/// Hosted actions (see `_daemon_dispatch` in yfinance_data.py): batch_quotes,
/// batch_sparklines, batch_all, historical_period, quote, info, news,
/// financial_ratios, portfolio_nav_history, extended_hours, compute_technicals.
///
/// Caveat — daemon is single-threaded. A long-running action blocks every
/// other request until it returns. compute_technicals is CPU-bound but bounded
/// (<1s for 252-bar series). If you add a new action that can hang, either
/// move it to a fresh PythonRunner spawn or pass a `timeout_ms` to submit()
/// (which fails the C++ callback at the deadline; the Python action keeps
/// running, but the UI no longer hangs waiting for it).
///
/// Framing: 4-byte big-endian length prefix, UTF-8 JSON body. Matches
/// `run_daemon()` in scripts/yfinance_data.py.
///
/// Requests carry a monotonically increasing `id`. Callbacks are keyed by id
/// and invoked on the Qt event loop when the matching response arrives.
///
/// Lifecycle: `start()` is lazy — call from the first producer. Restart on
/// unexpected exit is automatic (up to a retry cap). `stop()` closes stdin
/// and waits briefly for a clean exit.
class PythonWorker : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(bool ok, QJsonObject result, QString error)>;

    static PythonWorker& instance();

    /// True once the daemon has sent its `{"ready": true}` handshake and
    /// is accepting requests. Requests submitted before this queue up and
    /// dispatch once ready.
    bool is_ready() const { return ready_; }

    /// Submit one request. `action` maps to the daemon's dispatch table
    /// (batch_all, batch_quotes, batch_sparklines, historical_period, ...).
    /// `payload` is action-specific. Callback is invoked on the hub thread.
    /// Safe to call before the daemon is ready — the request queues.
    ///
    /// `timeout_ms` defaults to 0 (no timeout, wait forever). Pass a
    /// positive value to cap how long the C++ callback will wait. On
    /// timeout the callback fires with `ok=false, err="timeout (Nms)"`,
    /// the request is erased from the in-flight table, and any subsequent
    /// daemon response for the same id is ignored. The Python action keeps
    /// running (single-threaded daemon — we can't preempt it) but the UI
    /// no longer pretends to be loading.
    void submit(const QString& action, const QJsonObject& payload, Callback cb,
                int timeout_ms = 0);

    // Suggested per-action timeouts. Network-bound actions (yfinance HTTP
    // round-trip) get the smaller cap; compute-bound ones get the larger.
    static constexpr int kNetworkActionTimeoutMs = 10'000;
    static constexpr int kComputeActionTimeoutMs = 30'000;

    /// Graceful shutdown — sent automatically on app exit via destructor.
    void stop();

  private:
    PythonWorker();
    ~PythonWorker() override;

    void ensure_started();
    void launch_process();
    void on_process_finished(int exit_code, QProcess::ExitStatus status);
    void on_process_error(QProcess::ProcessError err);
    void on_ready_read();
    void try_drain_frames();
    void dispatch_queued();
    void fail_all_pending(const QString& reason);

    QProcess* proc_ = nullptr;
    bool ready_ = false;
    bool shutting_down_ = false;
    int restart_count_ = 0;
    static constexpr int kMaxRestarts = 5;
    static constexpr int kReadyTimeoutMs = 15'000;  // import yfinance+pandas

    // Pending request tracking. Keyed by request id for response correlation.
    struct Pending {
        QString action;
        QJsonObject payload;
        Callback cb;
        QTimer* deadline = nullptr;  // null when timeout_ms was 0
    };
    QHash<int, Pending> in_flight_;
    // Requests submitted before the daemon was ready, or queued during restart.
    QVector<QPair<int, Pending>> queue_;
    int next_id_ = 1;

    // Read buffer — frames may span multiple readyRead() signals.
    QByteArray read_buf_;

    // Ready-wait watchdog — if the daemon hasn't handshaked within the
    // timeout, kill it and restart.
    QTimer ready_watchdog_;
};

} // namespace fincept::python
