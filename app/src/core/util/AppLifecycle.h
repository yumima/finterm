// src/core/util/AppLifecycle.h
#pragma once
// The app's helper processes live and die with it.
//
// Two gaps this closes (2026-11-16):
//  * `finterm.sh stop` (and a logout, a system shutdown, `kill`) sends
//    SIGTERM, which Qt does not handle: the app died on the spot, none of its
//    clean-up ran, and the helpers it had started were left to notice on
//    their own — the data daemon does (its socket closes), the local model
//    server and the exchange feeds did not.
//  * A crash or SIGKILL of the app leaves the same orphans.

class QProcess;

namespace finterm::lifecycle {

/// SIGTERM, SIGINT and SIGHUP become an ordinary quit: the event loop ends,
/// aboutToQuit runs and every helper is stopped in order. A second signal
/// while that is under way ends the process at once. No-op on Windows, where
/// console close and logoff reach the app as Qt events already.
void quit_on_terminate_signals();

/// Call before `proc->start()`. The child is ended by the system when the app
/// ends, however it ends — on Linux through PR_SET_PDEATHSIG, on Windows
/// through a job object that kills its members when the app's handle closes.
/// (macOS has no equivalent; there the app's own clean-up stops it.)
///
/// Linux delivers the death signal when the THREAD that started the child
/// exits, so it is applied only to a child started from the GUI thread, the
/// one thread that lives as long as the app.
void bind_to_app(QProcess* proc);

} // namespace finterm::lifecycle
