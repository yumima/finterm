#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace fincept {

/// Opt-in supervisor for the local hearth AI engine.
///
/// When "Manage local engine" is enabled in Settings → LLM Config, finterm
/// starts `hearth start` as a managed child process and keeps it alive while
/// the app is open. Off by default — users who run hearth themselves (or a
/// raw Ollama) are unaffected.
///
/// Usage:
///   HearthSupervisor::instance().set_enabled(true);  // called on Settings save
///   // Process is started immediately if the engine isn't already up.
///   // On app exit Qt's parent-child destruction stops the child.
///
/// Thread: all methods and signals are on the GUI thread.
class HearthSupervisor : public QObject {
    Q_OBJECT
public:
    static HearthSupervisor& instance();

    enum class State { Disabled, Starting, Running, Crashed };

    void set_enabled(bool enabled);
    bool is_enabled() const { return enabled_; }
    State state() const { return state_; }

    /// Resolved path to the hearth CLI binary. Resolution order:
    ///   1. env FINCEPT_HEARTH_BIN
    ///   2. ~/.hearth/bin/hearth  (pip install hearth installs here)
    ///   3. PATH lookup
    /// Returns empty string when not found.
    static QString hearth_binary();

signals:
    /// Emitted whenever state() changes.
    void state_changed(State state);

private:
    explicit HearthSupervisor(QObject* parent = nullptr);
    void start_process();
    void stop_process();
    void on_process_finished(int exit_code, QProcess::ExitStatus status);

    QProcess* process_ = nullptr;
    QTimer*   retry_timer_ = nullptr;
    State     state_ = State::Disabled;
    bool      enabled_ = false;
    int       retry_count_ = 0;
};

} // namespace fincept
