// src/core/util/AppLifecycle.cpp
#include "core/util/AppLifecycle.h"

#include <QCoreApplication>
#include <QProcess>
#include <QThread>

#if defined(Q_OS_UNIX)
#    include <QSocketNotifier>

#    include <csignal>
#    include <cstdlib>
#    include <fcntl.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif
#if defined(Q_OS_LINUX)
#    include <sys/prctl.h>
#endif
#if defined(Q_OS_WIN)
#    include <Windows.h>
#endif

namespace finterm::lifecycle {

#if defined(Q_OS_UNIX)
namespace {
int g_signal_pipe[2] = {-1, -1};
volatile std::sig_atomic_t g_quitting = 0;

void on_signal(int sig) {
    // Async-signal-safe only: a write, or _exit on the second signal.
    if (g_quitting)
        _exit(128 + sig);
    g_quitting = 1;
    const char b = 1;
    [[maybe_unused]] const auto n = ::write(g_signal_pipe[0], &b, 1);
}
} // namespace

void quit_on_terminate_signals() {
    // CLOEXEC: the app's helpers must not inherit the pipe. Set with fcntl —
    // SOCK_CLOEXEC is a Linux flag and macOS has no such thing.
    if (g_signal_pipe[0] != -1 || ::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signal_pipe) != 0)
        return;
    for (int fd : g_signal_pipe)
        ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
    auto* notifier = new QSocketNotifier(g_signal_pipe[1], QSocketNotifier::Read, QCoreApplication::instance());
    QObject::connect(notifier, &QSocketNotifier::activated, notifier, [notifier]() {
        notifier->setEnabled(false);
        char b = 0;
        [[maybe_unused]] const auto n = ::read(g_signal_pipe[1], &b, 1);
        QCoreApplication::quit();
    });
    struct sigaction sa = {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP}) {
        // A signal the launcher chose to ignore stays ignored: `nohup finterm`
        // must survive its terminal closing.
        struct sigaction old = {};
        if (::sigaction(sig, nullptr, &old) == 0 && old.sa_handler == SIG_IGN)
            continue;
        ::sigaction(sig, &sa, nullptr);
    }
}
#else
void quit_on_terminate_signals() {}
#endif

void bind_to_app(QProcess* proc) {
    if (!proc)
        return;
    const bool gui_thread = QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
    if (!gui_thread) {
        // Linux would end it when this short-lived thread does. Unbound: it
        // relies on the owner's own clean-up. Said once, so it is not silent.
        static bool said = false;
        if (!said) {
            said = true;
            qWarning("AppLifecycle: a helper process started off the GUI thread is not bound to the app's life");
        }
        return;
    }
#if defined(Q_OS_LINUX)
    const pid_t parent = ::getpid();
    proc->setChildProcessModifier([parent]() {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        // The app may already have died between fork and here: then no signal
        // would ever come, so leave now.
        if (::getppid() != parent)
            _exit(0);
    });
#elif defined(Q_OS_WIN)
    static HANDLE job = []() -> HANDLE {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (!j)
            return nullptr;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            CloseHandle(j);
            return nullptr;
        }
        return j; // held for the app's life; the system closes it when the app ends
    }();
    if (!job)
        return;
    QObject::connect(proc, &QProcess::started, proc, [proc]() {
        const auto pid = static_cast<DWORD>(proc->processId());
        if (HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, pid)) {
            AssignProcessToJobObject(job, h);
            CloseHandle(h);
        }
    });
#endif
}

} // namespace finterm::lifecycle
