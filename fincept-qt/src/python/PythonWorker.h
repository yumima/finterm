#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <functional>

namespace fincept::python {

/// Persistent yfinance worker — long-lived Python process running
/// `yfinance_data.py --daemon --socket <path>`. Avoids the 2–3 second
/// yfinance/pandas import cost that every `PythonRunner::run()` spawn pays.
///
/// IPC: Unix domain socket (QLocalSocket / AF_UNIX). The process stdin/stdout
/// is used only for the one-time ready handshake; all subsequent communication
/// flows through the socket, enabling concurrent requests without the
/// head-of-line blocking inherent in a pipe.
///
/// Daemon concurrency: ThreadPoolExecutor inside the Python daemon
/// (6 network workers + 2 compute workers). Multiple requests can be
/// in-flight simultaneously. Interactive requests (quote, info, historical,
/// compute_technicals, …) are sent to the daemon immediately. Background
/// batch actions (batch_all, batch_quotes, batch_sparklines) are deduplicated
/// — at most one of each is in-flight at a time; new submissions while one is
/// running are silently dropped (the refresh timer fires again in seconds).
class PythonWorker : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(bool ok, QJsonObject result, QString error)>;

    static PythonWorker& instance();

    bool is_ready() const { return ready_; }

    /// Begin spawning the persistent yfinance daemon now. The first
    /// submit() would do this anyway, but calling it at app start lets
    /// the ~1–3 s yfinance/pandas import cost overlap with the GUI's
    /// first paint instead of stalling the dashboard's first refresh.
    /// Idempotent; no-op if already starting or running.
    void warm_up() { ensure_started(); }

    void submit(const QString& action, const QJsonObject& payload, Callback cb,
                int timeout_ms = 0);

    static constexpr int kNetworkActionTimeoutMs = 10'000;
    static constexpr int kComputeActionTimeoutMs = 30'000;

    void stop();

  private:
    PythonWorker();
    ~PythonWorker() override;

    // Declare Pending before any method that uses it.
    struct Pending {
        QString     action;
        QJsonObject payload;
        Callback    cb;
        QTimer*     deadline = nullptr;
    };

    void ensure_started();
    void launch_process();
    void on_process_finished(int exit_code, QProcess::ExitStatus status);
    void on_process_error(QProcess::ProcessError err);
    void on_ready_read();          // stdout: ready handshake only
    void on_socket_data();         // socket: all response frames
    void try_drain_frames();       // parse frames from socket_buf_
    void dispatch_queued();        // called after socket connects
    void on_background_complete(); // send next queued background action
    void send_to_daemon(int id, Pending p);
    void fail_all_pending(const QString& reason);

    static bool is_background_action(const QString& action);

    QProcess*      proc_          = nullptr;
    QLocalSocket*  socket_        = nullptr;
    QString        socket_path_;
    bool           ready_         = false;
    bool           shutting_down_ = false;
    int            restart_count_ = 0;
    static constexpr int kMaxRestarts    = 5;
    static constexpr int kReadyTimeoutMs = 15'000;

    QHash<int, Pending>          in_flight_;
    QVector<QPair<int, Pending>> queue_;
    int next_id_ = 1;

    QByteArray read_buf_;    // stdout handshake buffer
    QByteArray socket_buf_;  // socket response buffer

    QTimer ready_watchdog_;
};

} // namespace fincept::python
