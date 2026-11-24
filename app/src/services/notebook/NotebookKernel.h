#pragma once
// NotebookKernel.h — the Python process a notebook's cells run in
// (scripts/launch/finterm_kernel.py): one per notebook, started by its first
// cell, the cells in order, what one defines there for the next.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace finterm::services::notebook {

struct KernelOutput {
    QString type{}; ///< "stream", "execute_result" or "error"
    QString name{}; ///< stream: "stdout" or "stderr"
    QString text{}; ///< stream, execute_result
    QString error_name{};
    QString error_value{};
    QStringList traceback{};
};

class NotebookKernel : public QObject {
    Q_OBJECT
  public:
    enum class State {
        Off, ///< no process: the next cell starts one
        Starting,
        Idle,
        Busy,
    };

    explicit NotebookKernel(QObject* parent = nullptr);
    ~NotebookKernel() override;

    State state() const { return state_; }
    QString python_version() const { return version_; }

    /// Run `code` after the cells already waiting. `number` names it in
    /// tracebacks ("<cell 3>"). Nothing is emitted before this returns.
    void run(const QString& cell_id, int number, const QString& code);

    /// End the running cell (KeyboardInterrupt); those waiting are dropped. A
    /// cell in C code may not stop for it — restart() does.
    void interrupt();

    /// End the process: its variables go, the running cell ends with an error.
    void restart();

    /// Drop the cell if it waits, or interrupt it if it runs.
    void cancel(const QString& cell_id);

    /// The cell waits or runs.
    bool holds(const QString& cell_id) const;

  signals:
    void state_changed();
    void output(const QString& cell_id, const finterm::services::notebook::KernelOutput& out);
    /// `ran`: false for one dropped from the queue. After an error the cells
    /// waiting are dropped: they were written for what it should have made.
    void finished(const QString& cell_id, bool ran, bool ok);

  private:
    struct Cell {
        QString id;
        int number = 0;
        QString code;
    };

    void pump();
    void start();
    void on_stdout();
    void on_event(const QJsonObject& event);
    void on_ended(const QString& how);
    void send(const QJsonObject& msg);
    void drop_waiting();
    void fail_running(const QString& name, const QString& why);
    void end_process();
    void set_state(State s);

    QProcess* proc_ = nullptr;
    QByteArray buf_;
    QByteArray stderr_; ///< what a process that failed said last
    QList<Cell> waiting_;
    QString how_;        ///< how the cells' process ended, as it says
    QString running_; ///< the cell the process has; empty when none
    QString version_;
    State state_ = State::Off;
};

} // namespace finterm::services::notebook
