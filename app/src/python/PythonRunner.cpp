#include "python/PythonRunner.h"

#include "core/config/AppIdentity.h"
#include "core/config/AppPaths.h"
#include "core/util/AppLifecycle.h"
#include "core/keys/CredentialCatalogue.h"
#include "core/logging/Logger.h"
#include "python/PythonSetupManager.h"
#include "storage/secure/SecureStorage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QPointer>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QTimer>

#include <memory>

namespace finterm::python {

// ── Credential catalogue ─────────────────────────────────────────────────────
// Generated from the scripts that actually read the keys — see
// core/keys/CredentialCatalogue.h and gen_credential_catalogue.py.
//
// This was a hand-maintained list of 18 while the scripts read 142 and
// SettingsScreen offered 7. Because the allow-list below also decides what
// survives the credential strip, every unlisted key was DELETED from the
// child environment: a user who exported EIA_API_KEY had it removed before
// the script ran, and the panel told them it was not configured.
//
// build_python_env() pulls each from SecureStorage and injects it, so a
// script reading os.environ.get("FRED_API_KEY") works whether or not the
// user has it in their shell. SecureStorage wins over an inherited value.
static const QStringList& managed_credential_keys() {
    return keys::env_names();
}

// ── Sensitive shell-env stripping ────────────────────────────────────────────
// After SecureStorage injection, drop any *other* credential-shaped variable
// the user may have inherited from their shell. This prevents unrelated keys
// (e.g. a developer's GITHUB_TOKEN, AWS_SECRET_ACCESS_KEY) from leaking into
// Python subprocesses where they'd be visible via /proc/<pid>/environ on
// Linux or process inspection tools elsewhere.
//
// Suffix list narrowed vs. PR #214: we omit bare _TOKEN because legitimate
// non-credential vars use it (CSRF_TOKEN, GITHUB_TOKEN for tooling). The
// managed_credential_keys() allow-list is checked first so any key the user
// configured in Settings is preserved, regardless of suffix.
static void strip_unmanaged_credentials(QProcessEnvironment& env, const QStringList& managed) {
    static const QStringList kSuffixes = {
        "_API_KEY", "_SECRET", "_SECRET_KEY", "_ACCESS_TOKEN", "_AUTH_TOKEN", "_PASSWORD", "_PRIVATE_KEY",
    };
    const QStringList all_keys = env.keys();
    for (const QString& k : all_keys) {
        if (managed.contains(k))
            continue; // we just injected this one — keep it
        const QString upper = k.toUpper();
        for (const QString& sfx : kSuffixes) {
            if (upper.endsWith(sfx)) {
                env.remove(k);
                break;
            }
        }
    }
}

// Scripts that require NumPy 1.x environment
static const QStringList kNumpy1Scripts = {
    "vectorbt", "backtesting", "gluonts", "functime", "financepy", "ffn",
};

PythonRunner& PythonRunner::instance() {
    static PythonRunner s;
    return s;
}

PythonRunner::PythonRunner() {
    scripts_dir_ = find_scripts_dir();
    LOG_INFO("Python", "Scripts: " + scripts_dir_);

    // Try fast, non-blocking venv detection first
    python_path_ = find_python_sync();

    if (!python_path_.isEmpty()) {
        python_init_done_ = true;
        LOG_INFO("Python", "Python: " + python_path_);
    } else {
        // System python detection requires a process spawn — do it async
        find_python_async();
    }
}

bool PythonRunner::is_available() const {
    return !python_path_.isEmpty();
}

QString PythonRunner::python_path() const {
    return python_path_;
}
QString PythonRunner::scripts_dir() const {
    return scripts_dir_;
}

// ── Standard Python environment ──────────────────────────────────────────────
// Shared by PythonRunner::run() and external direct-QProcess callers
// (e.g., AgentService's stdin/stream paths). Anything every Python spawn needs
// — encoding pins, FINTERM_DATA_DIR, FINAGENT_DATA_DIR, base PYTHONPATH — lives
// here. Script-specific path additions (parent-of-pkg) stay in run().
void PythonRunner::insert_data_dirs(QProcessEnvironment& env) {
    env.insert("FINTERM_DATA_DIR", PythonSetupManager::instance().install_dir());
    // FINTERM_USER_DIR: the active profile's user/ folder — where every
    // script keeps PERSONAL data (finterm_paths.user_root()). FINTERM_DATA_DIR
    // above stays the app root for system data (cache, market_data, models).
    env.insert("FINTERM_USER_DIR", finterm::AppPaths::user());
    // The user's database folder exactly as the app resolves it (old layout
    // until LayoutMigrations has moved it) — so scripts and app never disagree.
    env.insert("FINTERM_USER_DATA_DIR", finterm::AppPaths::data());
    // The active profile's regenerable cache (finterm_paths.cache_dir()).
    env.insert("FINTERM_CACHE_DIR", finterm::AppPaths::cache());
}

QString PythonRunner::work_dir(WorkDir which) {
    const QString dir = finterm::AppPaths::features() +
                        (which == WorkDir::Cells ? QStringLiteral("/work/cells") : QStringLiteral("/work/scripts"));
    QDir().mkpath(dir);
    return dir;
}

QProcessEnvironment PythonRunner::build_python_env() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONDONTWRITEBYTECODE", "1");
    env.insert("PYTHONUNBUFFERED", "1");

    insert_data_dirs(env);

    // Which vendor answers daily bars and quotes before Yahoo (price_sources.py;
    // Settings › Data Sources). A local QSettings, not AppConfig's shared one:
    // this can run off the GUI thread, and a QSettings instance is not shared-safe.
    const QString source = QSettings(AppIdentity::kOrg, AppIdentity::kApp)
                               .value(QStringLiteral("market_data/price_source"), QStringLiteral("yahoo"))
                               .toString();
    env.insert("FINTERM_PRICE_SOURCE", source);

    // FINAGENT_DATA_DIR: per-persona agent memory is personal, so it lives in
    // user/agents. Always overwrite so we match what resources.py expects
    // regardless of what the user shell inherited.
    env.insert("FINAGENT_DATA_DIR", finterm::AppPaths::agents() + "/finagent");

    // FINAGENT_RUNTIME_CACHE_SIZE: respect user override if already set; otherwise
    // leave unset so persona_registry.py uses its own default (8).
    // (No insert here by design.)

    // Base PYTHONPATH: scripts_dir on front, preserve whatever the user had.
#ifdef _WIN32
    const QChar kPathSep = ';';
#else
    const QChar kPathSep = ':';
#endif
    const QString existing_pypath = env.value("PYTHONPATH");
    QString new_pypath = existing_pypath.isEmpty() ? scripts_dir_ : (scripts_dir_ + kPathSep + existing_pypath);
    env.insert("PYTHONPATH", new_pypath);

    // Inject credentials stored via SettingsScreen → SecureStorage. SecureStorage
    // is the source of truth: any value found there overrides whatever the user's
    // shell happened to have. Without this, Python scripts like fred_data.py that
    // read os.environ.get("FRED_API_KEY") only worked when the user happened to
    // export the variable in their shell before launching the app.
    int injected = 0;
    for (const QString& key : managed_credential_keys()) {
        auto r = SecureStorage::instance().retrieve(key);
        if (r.is_ok() && !r.value().isEmpty()) {
            env.insert(key, r.value());
            ++injected;
        }
    }

    // Strip any other credential-shaped variable the user inherited from their
    // shell. The allow-list above is preserved; everything else matching the
    // sensitive suffixes is removed before the subprocess sees it.
    strip_unmanaged_credentials(env, managed_credential_keys());

    if (injected > 0) {
        LOG_DEBUG("Python", QString("Injected %1 credentials from SecureStorage").arg(injected));
    }

    return env;
}

// ── Venv routing ─────────────────────────────────────────────────────────────
// Routes scripts to the correct venv based on their name.

static QString select_venv_for_script(const QString& script) {
    QString lower = script.toLower();
    for (const auto& keyword : kNumpy1Scripts) {
        if (lower.contains(keyword)) {
            return "venv-numpy1";
        }
    }
    return "venv-numpy2";
}

// ── Find Python ──────────────────────────────────────────────────────────────

// Fast, non-blocking detection: checks only file existence (no process spawns)
QString PythonRunner::find_python_sync() const {
    // 1. Use UV-managed venvs from PythonSetupManager (preferred)
    auto& setup = PythonSetupManager::instance();
    QString venv2_py = setup.python_path("venv-numpy2");
    if (QFileInfo::exists(venv2_py)) {
        LOG_INFO("Python", "Using UV-managed venv-numpy2: " + venv2_py);
        return venv2_py;
    }

    QString venv1_py = setup.python_path("venv-numpy1");
    if (QFileInfo::exists(venv1_py)) {
        LOG_INFO("Python", "Using UV-managed venv-numpy1: " + venv1_py);
        return venv1_py;
    }

    // 2. Check venv next to executable (dev / portable)
    QString exe_dir = QCoreApplication::applicationDirPath();
    for (const auto& venv : {"venv-numpy2", "venv-numpy1", "venv", ".venv"}) {
#ifdef _WIN32
        QString candidate = exe_dir + "/" + venv + "/Scripts/python.exe";
#else
        QString candidate = exe_dir + "/" + venv + "/bin/python3";
#endif
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return {}; // No venv found — need async system python check
}

// Async detection: spawns `python --version` without blocking the UI thread
void PythonRunner::find_python_async() {
#ifdef _WIN32
    QString sys_python = "python";
#else
    QString sys_python = "python3";
#endif

    auto* proc = new QProcess(this);

#ifdef _WIN32
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpa) {
        cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, sys_python](int exit_code, QProcess::ExitStatus) {
                proc->deleteLater();
                if (exit_code == 0) {
                    python_path_ = sys_python;
                    LOG_INFO("Python", "System Python found: " + sys_python);
                } else {
                    LOG_WARN("Python", "No Python interpreter found");
                }
                python_init_done_ = true;

                // Drain any requests that were queued while we were detecting
                start_next();
            });

    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        proc->deleteLater();
        LOG_WARN("Python", "No Python interpreter found (process error)");
        python_init_done_ = true;
        start_next(); // answers what was queued meanwhile: there is no Python
    });

    proc->start(sys_python, {"--version"});
}

// ── Find Scripts Directory ───────────────────────────────────────────────────

QString PythonRunner::find_scripts_dir() {
    QString exe_dir = QCoreApplication::applicationDirPath();

    // Check relative to exe: scripts/, ../scripts/, etc.
    QStringList candidates = {
        exe_dir + "/scripts",
        exe_dir + "/../scripts",
        exe_dir + "/../../scripts",
        exe_dir + "/../../../scripts",
        exe_dir + "/../../../../scripts", // deep dev: build/Release/../../scripts
        exe_dir + "/resources/scripts",
    };

    // Also check project root (for dev: app/scripts)
    QDir dir(exe_dir);
    while (dir.cdUp()) {
        if (QFileInfo::exists(dir.filePath("scripts/yfinance_data.py"))) {
            return dir.filePath("scripts");
        }
        if (dir.isRoot())
            break;
    }

    for (const auto& c : candidates) {
        if (QFileInfo::exists(c + "/yfinance_data.py")) {
            return QDir::cleanPath(c);
        }
    }

    // Last resort: current directory
    if (QFileInfo::exists("scripts/yfinance_data.py")) {
        return QDir::cleanPath("scripts");
    }

    LOG_WARN("Python", "Scripts directory not found");
    return exe_dir + "/scripts";
}

// ── Run Script ───────────────────────────────────────────────────────────────

void PythonRunner::run(const QString& script, const QStringList& args, Callback cb, StreamCallback on_line,
                       int timeout_ms) {
    if (python_init_done_ && python_path_.isEmpty()) {
        cb({false, {}, "Python not available — run first-time setup from the app", -1});
        return;
    }

    QString script_path = scripts_dir_ + "/" + script;
    if (!QFileInfo::exists(script_path)) {
        cb({false, {}, "Script not found: " + script_path, -1});
        return;
    }

    // Queue the request and start if under concurrency limit
    queue_.enqueue({script, args, std::move(cb), std::move(on_line), timeout_ms});
    start_next();
}

void PythonRunner::run_interactive(const QString& script, const QStringList& args, Callback cb, int timeout_ms) {
    if (python_init_done_ && python_path_.isEmpty()) {
        cb({false, {}, "Python not available — run first-time setup from the app", -1});
        return;
    }
    if (!QFileInfo::exists(scripts_dir_ + "/" + script)) {
        cb({false, {}, "Script not found: " + scripts_dir_ + "/" + script, -1});
        return;
    }
    interactive_queue_.enqueue({script, args, std::move(cb), {}, timeout_ms});
    start_next();
}

void PythonRunner::run_job(const QString& script, const QStringList& args, Callback cb, StreamCallback on_line,
                           int timeout_ms) {
    if (python_init_done_ && python_path_.isEmpty()) {
        cb({false, {}, "Python not available — run first-time setup from the app", -1});
        return;
    }
    if (!QFileInfo::exists(scripts_dir_ + "/" + script)) {
        cb({false, {}, "Script not found: " + scripts_dir_ + "/" + script, -1});
        return;
    }
    job_queue_.enqueue({script, args, std::move(cb), std::move(on_line), timeout_ms});
    start_next();
}

void PythonRunner::run_code(const QString& code, Callback cb) {
    queue_code(code, std::move(cb), queue_, kProcessTimeoutMs);
}

void PythonRunner::run_user_code(const QString& code, Callback cb) {
    queue_code(code, std::move(cb), job_queue_, kJobTimeoutMs);
}

void PythonRunner::queue_code(const QString& code, Callback cb, QQueue<QueuedRequest>& queue, int timeout_ms) {
    if (python_init_done_ && python_path_.isEmpty()) {
        cb({false, {}, "Python not available — run first-time setup from the app", -1});
        return;
    }

    // The user's code, so a file only they can read, under a name nothing else
    // takes (the old one, the time in ms, was shared by two cells run together).
    QString temp_path;
    {
        QTemporaryFile file(QDir::tempPath() + "/finterm_cell_XXXXXX.py");
        file.setAutoRemove(false); // removed when the cell ends
        if (!file.open() || file.write(code.toUtf8()) < 0 || !file.flush()) {
            cb({false, {}, "Failed to create temp script file", -1});
            return;
        }
        temp_path = file.fileName();
    }

    // Queue as a special request — use the temp file path directly
    queue.enqueue({"__code__:" + temp_path, {}, std::move(cb), {}, timeout_ms});
    start_next();
}

QProcess* PythonRunner::create_server(const QString& script, QString* why) {
    const QString path = scripts_dir_ + "/" + script;
    if (python_path_.isEmpty()) {
        *why = QStringLiteral("Python not available — run first-time setup from the app");
        return nullptr;
    }
    if (!QFileInfo::exists(path)) {
        *why = QStringLiteral("Script not found: ") + path;
        return nullptr;
    }
    return new_process(QStringLiteral("venv-numpy2"), {path}, build_python_env(), WorkDir::Cells);
}

// JSON would carry half a surrogate pair (a headline cut inside an emoji) as it
// is, and Python then fails to encode it: it becomes a replacement character.
static QStringList well_formed(const QStringList& args) {
    QStringList out;
    for (const QString& a : args)
        out << QString::fromUtf8(a.toUtf8());
    return out;
}

QProcess* PythonRunner::new_process(const QString& venv, const QStringList& args, const QProcessEnvironment& env,
                                    WorkDir dir) {
    QString python_exe = python_path_;
    const QString venv_py = PythonSetupManager::instance().python_path(venv);
    if (QFileInfo::exists(venv_py))
        python_exe = venv_py;

    auto* proc = new QProcess(this);
    // A script can run for an hour (the 13F sync): it ends with the app,
    // however the app ends, rather than writing on as an orphan.
    lifecycle::bind_to_app(proc);
    proc->setProgram(python_exe);
    proc->setArguments(args);

    // NOTE: scripts_dir_ is on PYTHONPATH (build_python_env). Never add a
    // script's own parent folder to it: a sub-package sharing a library's name
    // would hide the library ("edgar" once did, for every SEC tool).
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(work_dir(dir));

#ifdef _WIN32
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* cpa) {
        cpa->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#endif
    return proc;
}

QProcess* PythonRunner::make_process(const QString& script, const QStringList& args, const QProcessEnvironment& env) {
    const bool is_code = script.startsWith("__code__:");
    // The arguments go over stdin, never on the command line, which every
    // account on the machine can read: they carry API keys, a wallet's
    // private key, the user's positions. finterm_run.py takes them from
    // there and runs the script as if they had been its argv.
    QByteArray input;
    QStringList command{script.mid(9)};
    if (!is_code) {
        command = {scripts_dir_ + "/launch/finterm_run.py", script};
        input = QJsonDocument(QJsonArray::fromStringList(well_formed(args))).toJson(QJsonDocument::Compact) + '\n';
    }
    // The right venv for the script; code always runs in venv-numpy2.
    auto* proc = new_process(is_code ? QStringLiteral("venv-numpy2") : select_venv_for_script(script), command, env,
                             is_code ? WorkDir::Cells : WorkDir::Scripts);
    connect(proc, &QProcess::started, proc, [proc, input]() {
        if (!input.isEmpty())
            proc->write(input);
        proc->closeWriteChannel(); // nothing more will come: a script that reads on gets EOF
    });
    return proc;
}

quint64 PythonRunner::start_background(const QString& script, const QStringList& args, StreamCallback on_line,
                                       Callback on_exit) {
    if (python_path_.isEmpty()) {
        on_exit({false, {}, "Python not available — run first-time setup from the app", -1});
        return 0;
    }
    if (!QFileInfo::exists(scripts_dir_ + "/" + script)) {
        on_exit({false, {}, "Script not found: " + scripts_dir_ + "/" + script, -1});
        return 0;
    }

    QProcessEnvironment env = build_python_env();
    for (const QString& key : managed_credential_keys())
        env.remove(key);

    auto* proc = make_process(script, args, env);
    const quint64 handle = next_background_++;
    background_.insert(handle, proc);

    // Whole lines as they come; of stderr, the end for the exit's report.
    auto out = std::make_shared<QByteArray>();
    auto err = std::make_shared<QByteArray>();
    auto on_line_shared = std::make_shared<StreamCallback>(std::move(on_line));
    auto drain = [on_line_shared](QByteArray& buf, bool is_stderr) {
        for (int nl; (nl = buf.indexOf('\n')) >= 0;) {
            const QString line = QString::fromUtf8(buf.left(nl)).trimmed();
            buf.remove(0, nl + 1);
            if (*on_line_shared && !line.isEmpty())
                (*on_line_shared)(line, is_stderr);
        }
    };
    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, out, drain]() {
        out->append(proc->readAllStandardOutput());
        drain(*out, false);
    });
    connect(proc, &QProcess::readyReadStandardError, this, [proc, err]() {
        err->append(proc->readAllStandardError());
        *err = err->right(8192);
    });
    auto exit_shared = std::make_shared<Callback>(std::move(on_exit));
    auto done = std::make_shared<bool>(false);
    auto finish = [this, handle, proc, exit_shared, done](PythonResult r) {
        if (*done)
            return;
        *done = true;
        background_.remove(handle);
        proc->deleteLater();
        if (*exit_shared)
            (*exit_shared)(std::move(r));
    };
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, out, err, drain, finish](int code, QProcess::ExitStatus status) {
                out->append(proc->readAllStandardOutput() + '\n');
                drain(*out, false);
                err->append(proc->readAllStandardError());
                finish({status == QProcess::NormalExit && code == 0, {}, QString::fromUtf8(err->right(8192)), code});
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, finish](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish({false, {}, "Process error: " + proc->errorString(), -1});
    });
    LOG_INFO("Python", "Starting in the background: " + script);
    proc->start();
    return handle;
}

void PythonRunner::stop_background(quint64 handle, int grace_ms) {
    QPointer<QProcess> proc = background_.value(handle);
    if (!proc)
        return;
#ifdef Q_OS_WIN
    Q_UNUSED(grace_ms);
    proc->kill(); // a console process has no window to ask
#else
    proc->terminate();
    QTimer::singleShot(grace_ms, this, [proc]() {
        if (proc && proc->state() != QProcess::NotRunning)
            proc->kill();
    });
#endif
}

void PythonRunner::start_next() {
    // Don't start if Python detection is still in progress
    if (!python_init_done_)
        return;

    for (;;) {
        // Interactive requests first: their own lane, else any free ordinary
        // slot. Jobs have a lane of their own and only that.
        QueuedRequest req;
        int* slots_held = &active_count_; // the pool the process holds a slot of, and gives it back to
        if (!interactive_queue_.isEmpty() &&
            (interactive_active_ < kInteractiveLanes || active_count_ < max_concurrent_)) {
            req = interactive_queue_.dequeue();
            if (interactive_active_ < kInteractiveLanes)
                slots_held = &interactive_active_;
        } else if (!queue_.isEmpty() && active_count_ < max_concurrent_) {
            req = queue_.dequeue();
        } else if (!job_queue_.isEmpty() && job_active_ < kJobLanes) {
            req = job_queue_.dequeue();
            slots_held = &job_active_;
        } else {
            break;
        }

        if (python_path_.isEmpty()) {
            // Python became unavailable — fail the request
            req.cb({false, {}, "Python not available", -1});
            continue;
        }

        ++*slots_held;

        // Determine if this is inline code or a script file
        const bool is_code = req.script.startsWith("__code__:");
        const QString temp_file = is_code ? req.script.mid(9) : QString(); // strip "__code__:" prefix
        auto* proc = make_process(req.script, req.args, build_python_env());

        // Initialize incremental output buffers (carry stream callback from the request)
        proc_buffers_[proc] = {};
        proc_buffers_[proc].on_line = req.on_line;
        proc_buffers_[proc].start_ms = QDateTime::currentMSecsSinceEpoch();

        // Helper: drain complete \n-terminated lines from a buffer starting at `offset`,
        // invoke `on_line(line, is_stderr)` per line, advance `offset`. Trailing
        // partial line (no \n yet) stays in the buffer for the next read.
        auto drain_lines = [this, proc](bool is_stderr) {
            auto& bufs = proc_buffers_[proc];
            if (!bufs.on_line)
                return;
            QByteArray& buf = is_stderr ? bufs.stderr_buf : bufs.stdout_buf;
            int& off = is_stderr ? bufs.stderr_streamed : bufs.stdout_streamed;
            while (true) {
                int nl = buf.indexOf('\n', off);
                if (nl < 0)
                    break;
                // Line is buf[off..nl) — exclude the trailing \n; also trim \r for CRLF
                QByteArray line = buf.mid(off, nl - off);
                if (line.endsWith('\r'))
                    line.chop(1);
                off = nl + 1;
                bufs.on_line(QString::fromUtf8(line), is_stderr);
            }
        };

        // Buffer stdout/stderr incrementally. After each append, drain complete lines
        // to the optional stream callback. The full buffer is still available for the
        // `finished` handler's authoritative parse.
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, drain_lines]() {
            proc_buffers_[proc].stdout_buf.append(proc->readAllStandardOutput());
            drain_lines(false);
        });
        connect(proc, &QProcess::readyReadStandardError, this, [this, proc, drain_lines]() {
            proc_buffers_[proc].stderr_buf.append(proc->readAllStandardError());
            drain_lines(true);
        });

        // finished() answers, with whatever was printed; errorOccurred only for a
        // process that never started (a killed or crashed one gets both signals,
        // errorOccurred first). `done`: the callback is called once.
        auto shared_cb = std::make_shared<Callback>(std::move(req.cb));
        auto done = std::make_shared<bool>(false);
        const QString script_name = req.script;

        connect(
            proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, shared_cb, done, script_name, is_code, temp_file, slots_held](int exit_code, QProcess::ExitStatus status) {
                if (*done)
                    return; // it never started: errorOccurred answered
                *done = true;
                const int timed_out_s = proc->property("timed_out_s").toInt();

                // Collect any remaining buffered data
                auto& bufs = proc_buffers_[proc];
                bufs.stdout_buf.append(proc->readAllStandardOutput());
                bufs.stderr_buf.append(proc->readAllStandardError());

                const qint64 duration_ms = bufs.start_ms > 0 ? QDateTime::currentMSecsSinceEpoch() - bufs.start_ms : 0;

                // Stop the hard-timeout timer — process finished normally (or was killed).
                if (bufs.timeout_timer) {
                    bufs.timeout_timer->stop();
                    bufs.timeout_timer->deleteLater();
                }

                QString stdout_str = QString::fromUtf8(bufs.stdout_buf);
                QString stderr_str = QString::fromUtf8(bufs.stderr_buf);

                proc_buffers_.remove(proc);
                proc->deleteLater();

                if (is_code && !temp_file.isEmpty())
                    QFile::remove(temp_file);

                PythonResult result;
                result.exit_code = exit_code;

                if (is_code) {
                    result.success = (exit_code == 0 && status == QProcess::NormalExit);
                    result.output = std::move(stdout_str);
                    result.error = std::move(stderr_str);
                } else {
                    QString json_out = extract_json(stdout_str);
                    result.success = (exit_code == 0 && status == QProcess::NormalExit && !json_out.isEmpty());
                    result.output = json_out.isEmpty() ? std::move(stdout_str) : std::move(json_out);
                    result.error = std::move(stderr_str);
                }

                // What ended it goes first; what it had printed is still there.
                const QString ended_by = timed_out_s > 0 ? QString("No answer after %1 s").arg(timed_out_s)
                                         : status != QProcess::NormalExit
                                             ? QStringLiteral("Process error: %1").arg(proc->errorString())
                                             : QString();
                if (!ended_by.isEmpty())
                    result.error = result.error.isEmpty() ? ended_by : ended_by + '\n' + result.error;

                if (!result.success && !is_code) {
                    LOG_ERROR("Python", QString("Script %1 failed in %2ms (exit=%3): %4")
                                            .arg(script_name)
                                            .arg(duration_ms)
                                            .arg(exit_code)
                                            .arg(result.error.left(200)));
                } else if (!is_code) {
                    LOG_DEBUG("Python", QString("Script %1 finished in %2ms").arg(script_name).arg(duration_ms));
                }

                (*shared_cb)(std::move(result));
                --*slots_held;
                start_next();
            });

        connect(proc, &QProcess::errorOccurred, this,
                [this, proc, shared_cb, done, is_code, temp_file, slots_held](QProcess::ProcessError error) {
                    if (*done)
                        return; // finished() already handled this — skip
                    if (error != QProcess::FailedToStart)
                        return; // finished() follows, and has what it printed
                    *done = true;

                    const QString error_msg = proc->errorString();
                    if (proc_buffers_.contains(proc) && proc_buffers_[proc].timeout_timer) {
                        proc_buffers_[proc].timeout_timer->stop();
                        proc_buffers_[proc].timeout_timer->deleteLater();
                    }
                    proc_buffers_.remove(proc);
                    proc->deleteLater();
                    if (is_code && !temp_file.isEmpty())
                        QFile::remove(temp_file);

                    (*shared_cb)({false, {}, "Process error: " + error_msg, -1});
                    --*slots_held;
                    start_next();
                });

        const bool in_job = slots_held == &job_active_;
        const bool in_lane = slots_held == &interactive_active_;
        LOG_INFO("Python", QString("Running (%1/%2 active, %3 queued%4): %5 %6")
                               .arg(*slots_held)
                               .arg(in_job ? kJobLanes : in_lane ? kInteractiveLanes : max_concurrent_)
                               .arg((in_job ? job_queue_ : in_lane ? interactive_queue_ : queue_).size())
                               .arg(in_job    ? QStringLiteral(", job lane")
                                    : in_lane ? QStringLiteral(", interactive lane")
                                              : QString(),
                                    proc->program(), script_name));
        proc->start();

        // Hard timeout — kill hanging scripts so they don't hold a concurrency
        // slot forever (slow network calls, yf.download on bad tickers).
        // Per-request override (req.timeout_ms) so legitimately long-running
        // scrapers (e.g. Senate eFD with many per-PTR HTTP round-trips) can
        // declare their own ceiling. 0 disables the timeout.
        // Use QPointer so the lambda is safe if the QProcess is destroyed before
        // the timer fires (OS address reuse would make a raw pointer check unreliable).
        const int timeout_ms = req.timeout_ms;
        if (timeout_ms > 0) {
            auto* tmr = new QTimer(this);
            tmr->setSingleShot(true);
            tmr->setInterval(timeout_ms);
            QPointer<QProcess> proc_guard(proc);
            connect(tmr, &QTimer::timeout, this, [this, proc_guard, script_name, timeout_ms]() {
                if (!proc_guard || !proc_buffers_.contains(proc_guard.data()))
                    return;
                LOG_WARN("Python", QString("Script %1 timed out after %2s — killing process")
                                       .arg(script_name)
                                       .arg(static_cast<double>(timeout_ms) / 1000.0, 0, 'f', 0));
                proc_guard->setProperty("timed_out_s", timeout_ms / 1000);
                proc_guard->kill(); // triggers finished(), which calls cb + start_next()
            });
            proc_buffers_[proc].timeout_timer = tmr;
            tmr->start();
        }
    }
}

// ── Extract JSON ─────────────────────────────────────────────────────────────

QString extract_json(const QString& output) {
    // Find the first '{' or '[' and return everything from that point.
    // Scripts print multi-line indented JSON, so we cannot take a single line.
    int brace = output.indexOf('{');
    int bracket = output.indexOf('[');

    int start = -1;
    if (brace >= 0 && bracket >= 0)
        start = qMin(brace, bracket);
    else if (brace >= 0)
        start = brace;
    else if (bracket >= 0)
        start = bracket;

    if (start < 0)
        return {};

    return output.mid(start).trimmed();
}

QString failure_message(const PythonResult& result) {
    const QJsonObject answer = QJsonDocument::fromJson(extract_json(result.output).toUtf8()).object();
    QString msg = answer.value(QStringLiteral("error")).toString().trimmed();
    if (msg.isEmpty() && !result.success)
        msg = result.error.trimmed();
    if (msg.isEmpty() && !result.success)
        msg = QStringLiteral("Script failed (exit code %1)").arg(result.exit_code);
    return msg;
}

} // namespace finterm::python
