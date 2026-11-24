#pragma once
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>

namespace finterm::python {

struct PythonResult {
    bool success = false;
    QString output;
    QString error;
    int exit_code = -1;
};

/// Runs Python scripts as subprocesses and returns JSON output.
/// Locates python from venv or system PATH.
/// Limits concurrent processes to avoid overwhelming the system.
class PythonRunner : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(PythonResult)>;
    /// Per-line streaming callback. Invoked on the thread that owns PythonRunner
    /// (main thread in practice) as each complete `\n`-terminated line arrives.
    /// `is_stderr` is true for stderr lines, false for stdout.
    using StreamCallback = std::function<void(QString line, bool is_stderr)>;

    // Default per-process hard timeout. Scripts that make network calls
    // (benchmark fetches, etc.) can hang indefinitely on
    // DNS/connection failure. When the deadline fires the process is killed
    // and the callback is called with success=false so callers show an error
    // rather than loading forever. Override per call via run(..., timeout_ms).
    static constexpr int kProcessTimeoutMs = 30'000;

    /// The same for run_job(): an hour, then it is taken to hang.
    static constexpr int kJobTimeoutMs = 3'600'000;

    static PythonRunner& instance();

    /// `args` become the script's sys.argv. They reach it over stdin
    /// (launch/finterm_run.py), never on the command line, so they may be secret and
    /// of any length.
    ///
    /// Run a script asynchronously. Callback invoked on Qt event loop.
    /// Requests are queued if max concurrency is reached.
    /// Optional `on_line` delivers each complete stdout/stderr line as it arrives.
    /// Optional `timeout_ms` overrides the default per-process timeout. Use
    /// for scripts that legitimately need to run longer than the default
    /// (e.g. network-heavy scrapers); 0 disables the timeout entirely.
    void run(const QString& script, const QStringList& args, Callback cb, StreamCallback on_line = {},
             int timeout_ms = kProcessTimeoutMs);

    /// Same as run(), for a short query a reader is actively waiting on — a
    /// click whose answer comes from a local index in a second or two.
    /// These skip the ordinary queue and have a lane of their own beyond the
    /// normal concurrency limit, so they never wait behind network scrapers
    /// that can hold every ordinary slot for 30 seconds at a time. Not for
    /// anything slow: a long job here would block the next click instead.
    void run_interactive(const QString& script, const QStringList& args, Callback cb,
                         int timeout_ms = kProcessTimeoutMs);

    /// Same as run(), for what takes minutes: training a model, an agent's
    /// research. Jobs have slots of their own: in the ordinary ones they would
    /// be killed at the default timeout, or hold up every screen's data.
    void run_job(const QString& script, const QStringList& args, Callback cb, StreamCallback on_line = {},
                 int timeout_ms = kJobTimeoutMs);

    /// Start a script that runs until it ends by itself or is stopped: a
    /// server, a job of hours. Holds no slot and has no deadline; it ends with
    /// the app. `on_exit` is called once, with what it wrote to stderr last.
    /// Its environment carries none of the stored credentials — such a script
    /// may run code a model wrote. Returns 0 when it cannot be started (then
    /// `on_exit` has been called), else a handle for stop_background().
    quint64 start_background(const QString& script, const QStringList& args, StreamCallback on_line, Callback on_exit);
    /// Ask it to end, and make it when it has not after `grace_ms`.
    void stop_background(quint64 handle, int grace_ms = 10'000);

    /// Run Python code from a temp file; returns stdout/stderr. `cb` may be
    /// called at once when it cannot start.
    /// finterm's own snippet: in the ordinary slots, with their deadline.
    void run_code(const QString& code, Callback cb);
    /// The user's (a workflow's code node): takes what it takes, in slots of its
    /// own, so it neither waits for the jobs nor holds them up.
    void run_user_code(const QString& code, Callback cb);
    /// End all of it, running or waiting; each is answered with an error.
    void cancel_user_code();

    /// A script that is spoken to over stdin (the notebook's kernel): not
    /// started, its stdin left open, in the Cells folder. Holds no slot and has
    /// no deadline; ends with the app. Null when it cannot run, `why` saying it.
    QProcess* create_server(const QString& script, QString* why);

    /// Looking for Python at start is not over: is_available() can't say yet.
    bool is_initializing() const { return !python_init_done_; }

    /// Resolve path to python executable
    QString python_path() const;

    /// Resolve path to scripts directory
    QString scripts_dir() const;

    /// Build the standard Python process environment used by all finagent/Python
    /// subprocess spawns. Sets PYTHONIOENCODING, PYTHONDONTWRITEBYTECODE,
    /// PYTHONUNBUFFERED, FINTERM_DATA_DIR, FINAGENT_DATA_DIR,
    /// FINAGENT_RUNTIME_CACHE_SIZE, and PYTHONPATH (derived from scripts_dir()).
    /// Callers that run a script inside a sub-package may still need to prepend
    /// a "parent-of-pkg" directory to PYTHONPATH themselves — this helper only
    /// sets the shared base env.
    QProcessEnvironment build_python_env() const;

    /// Where the data folders are, for any Python process (part of the above):
    /// finterm_paths resolves the active profile's from it. No keyring read.
    static void insert_data_dirs(QProcessEnvironment& env);

    /// The folder a Python process runs in: what a library writes to "here" is
    /// the user's, and the scripts' folder is replaced by an update. Scripts
    /// start in Scripts; code the user or an agent writes runs in Cells, apart:
    /// a "pandas.py" saved there must not be what `python -m` finds first.
    enum class WorkDir { Scripts, Cells };
    static QString work_dir(WorkDir which = WorkDir::Scripts);

    /// Check if python is available
    bool is_available() const;

  public:
    /// Locate the bundled scripts directory (exe-relative, then the dev tree).
    /// Static so startup migrations can find it without constructing the runner.
    static QString find_scripts_dir();

  private:
    struct QueuedRequest {
        QString script;
        QStringList args;
        Callback cb;
        StreamCallback on_line;
        int timeout_ms = kProcessTimeoutMs;
    };

    PythonRunner();
    QString find_python_sync() const;
    void find_python_async();
    void start_next();
    void queue_code(const QString& code, Callback cb, QQueue<QueuedRequest>& queue, int timeout_ms);
    /// A process running venv's Python, with what every one of them needs. Not started.
    QProcess* new_process(const QString& venv, const QStringList& args, const QProcessEnvironment& env, WorkDir dir);
    /// The process for a script: program, launcher arguments, folder, and its
    /// arguments written to stdin once it runs. Not started.
    QProcess* make_process(const QString& script, const QStringList& args, const QProcessEnvironment& env);

    QString python_path_;
    QString scripts_dir_;
    bool python_init_done_ = false;

    // Concurrency limiter
    static constexpr int DEFAULT_MAX_CONCURRENT = 3;
    int max_concurrent_ = DEFAULT_MAX_CONCURRENT;
    int active_count_ = 0;
    // The interactive lane: run_interactive() requests, which start ahead of
    // the ordinary queue and may use this many slots beyond max_concurrent_.
    static constexpr int kInteractiveLanes = 1;
    int interactive_active_ = 0;
    // run_job() requests: these many at a time, beside all of the above.
    static constexpr int kJobLanes = 2;
    int job_active_ = 0;
    // run_user_code() requests.
    static constexpr int kCodeLanes = 2;
    int code_active_ = 0;

    QQueue<QueuedRequest> queue_;
    QQueue<QueuedRequest> interactive_queue_;
    QQueue<QueuedRequest> job_queue_;
    QQueue<QueuedRequest> code_queue_;

    // Incremental output buffering per process
    struct ProcessBuffers {
        QByteArray stdout_buf;
        QByteArray stderr_buf;
        int stdout_streamed = 0; // offset into stdout_buf already streamed
        int stderr_streamed = 0;
        StreamCallback on_line;
        bool user_code = false;
        qint64 start_ms = 0;             // spawn timestamp for duration diagnostics
        QTimer* timeout_timer = nullptr; // killed + nulled in finished/error handlers
    };
    QHash<QProcess*, ProcessBuffers> proc_buffers_;

    quint64 next_background_ = 1;
    QHash<quint64, QProcess*> background_;
};

/// Extract last JSON line/block from Python output
QString extract_json(const QString& output);

/// Why a run failed, one wording for every caller: the script's own
/// {"error": ...} on stdout first (scripts often print it and exit non-zero
/// with an empty stderr), then stderr, then "Script failed (exit code N)".
/// Empty for a successful run whose answer carries no "error".
QString failure_message(const PythonResult& result);

} // namespace finterm::python
