#include "services/notebook/NotebookKernel.h"

#include "core/logging/Logger.h"
#include "python/PythonRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTimer>

#include <utility>

namespace finterm::services::notebook {

namespace {
constexpr int kPythonWaitMs = 250; // finterm's look for Python at start
} // namespace

NotebookKernel::NotebookKernel(QObject* parent) : QObject(parent) {}

NotebookKernel::~NotebookKernel() {
    end_process();
}

void NotebookKernel::set_state(State s) {
    if (state_ == s)
        return;
    state_ = s;
    emit state_changed();
}

void NotebookKernel::run(const QString& cell_id, int number, const QString& code) {
    waiting_.append({cell_id, number, code});
    QTimer::singleShot(0, this, &NotebookKernel::pump);
}

void NotebookKernel::pump() {
    if (!running_.isEmpty())
        return;
    if (waiting_.isEmpty()) {
        if (!proc_)
            set_state(State::Off); // what it was to be started for was dropped
        return;
    }
    if (!proc_) {
        start();
        return;
    }
    if (state_ == State::Starting)
        return; // "ready" comes back here
    const Cell cell = waiting_.takeFirst();
    running_ = cell.id;
    set_state(State::Busy);
    send({{"op", "run"}, {"id", cell.id}, {"n", cell.number}, {"code", cell.code}});
}

void NotebookKernel::start() {
    auto& runner = python::PythonRunner::instance();
    set_state(State::Starting);
    if (runner.is_initializing()) {
        QTimer::singleShot(kPythonWaitMs, this, &NotebookKernel::pump);
        return;
    }
    QString why;
    proc_ = runner.create_server(QStringLiteral("launch/finterm_kernel.py"), &why);
    if (!proc_) {
        running_ = waiting_.takeFirst().id;
        on_ended(why);
        return;
    }
    proc_->setParent(this);
    buf_.clear();
    stderr_.clear();
    connect(proc_, &QProcess::readyReadStandardOutput, this, &NotebookKernel::on_stdout);
    connect(proc_, &QProcess::readyReadStandardError, this, [this]() {
        stderr_ = (stderr_ + proc_->readAllStandardError()).right(8192);
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                on_stdout();
                stderr_ = (stderr_ + proc_->readAllStandardError()).right(8192);
                on_ended(status == QProcess::NormalExit
                             ? QStringLiteral("The notebook's Python ended (exit code %1)").arg(code)
                             : QStringLiteral("The notebook's Python crashed"));
            });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            on_ended(QStringLiteral("Python could not be started: ") + proc_->errorString());
    });
    LOG_INFO("Notebook", "Starting the kernel");
    proc_->start();
}

void NotebookKernel::send(const QJsonObject& msg) {
    if (proc_)
        proc_->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
}

void NotebookKernel::on_stdout() {
    if (!proc_)
        return;
    buf_ += proc_->readAllStandardOutput();
    qsizetype from = 0;
    QList<QJsonObject> events;
    for (qsizetype nl; (nl = buf_.indexOf('\n', from)) >= 0; from = nl + 1)
        events << QJsonDocument::fromJson(buf_.mid(from, nl - from)).object();
    buf_.remove(0, from);
    const auto* from_proc = proc_;
    for (const QJsonObject& e : std::as_const(events)) {
        if (proc_ != from_proc)
            return; // what an event was answered with restarted it
        on_event(e);
    }
}

void NotebookKernel::on_event(const QJsonObject& event) {
    const QString type = event.value("t").toString();
    const QString id = event.value("id").toString();
    if (type == QLatin1String("ready")) {
        version_ = event.value("python").toString();
        set_state(State::Idle);
        pump();
    } else if (type == QLatin1String("stream")) {
        emit output(id, {QStringLiteral("stream"), event.value("name").toString(), event.value("text").toString()});
    } else if (type == QLatin1String("result")) {
        emit output(id, {QStringLiteral("execute_result"), {}, event.value("text").toString()});
    } else if (type == QLatin1String("error")) {
        KernelOutput out{QStringLiteral("error")};
        out.error_name = event.value("ename").toString();
        out.error_value = event.value("evalue").toString();
        for (const auto& line : event.value("traceback").toArray())
            out.traceback << line.toString();
        emit output(id, out);
    } else if (type == QLatin1String("done") && id == running_) {
        const bool ok = event.value("ok").toBool();
        running_.clear();
        set_state(State::Idle);
        emit finished(id, true, ok);
        if (!ok)
            drop_waiting();
        pump();
    }
}

void NotebookKernel::drop_waiting() {
    const auto dropped = std::exchange(waiting_, {});
    for (const Cell& cell : dropped)
        emit finished(cell.id, false, false);
}

void NotebookKernel::fail_running(const QString& name, const QString& why) {
    if (running_.isEmpty())
        return;
    const QString id = std::exchange(running_, {});
    KernelOutput out{QStringLiteral("error")};
    out.error_name = name;
    out.error_value = why.section('\n', 0, 0);
    out.traceback = why.split('\n');
    emit output(id, out);
    emit finished(id, true, false);
}

/// The process is gone, or could not be started: whoever waits is told.
void NotebookKernel::on_ended(const QString& how) {
    LOG_WARN("Notebook", how);
    if (running_.isEmpty() && state_ == State::Starting && !waiting_.isEmpty())
        running_ = waiting_.takeFirst().id; // the cell it was started for
    const QString said = QString::fromUtf8(stderr_).trimmed();
    if (proc_) {
        proc_->disconnect(this);
        proc_->deleteLater();
        proc_ = nullptr;
    }
    set_state(State::Off);
    fail_running(QStringLiteral("KernelError"), said.isEmpty() ? how : how + '\n' + said);
    drop_waiting();
}

void NotebookKernel::interrupt() {
    drop_waiting();
    if (!running_.isEmpty())
        send({{"op", "interrupt"}});
}

void NotebookKernel::cancel(const QString& cell_id) {
    for (int i = 0; i < waiting_.size(); ++i) {
        if (waiting_[i].id == cell_id) {
            waiting_.removeAt(i);
            emit finished(cell_id, false, false);
            return;
        }
    }
    if (running_ == cell_id)
        interrupt();
}

bool NotebookKernel::holds(const QString& cell_id) const {
    if (running_ == cell_id)
        return true;
    for (const Cell& cell : waiting_)
        if (cell.id == cell_id)
            return true;
    return false;
}

void NotebookKernel::end_process() {
    if (!proc_)
        return;
    proc_->disconnect(this);
    // Killed, and gone before it is deleted: not waited for here.
    auto* proc = std::exchange(proc_, nullptr);
    proc->setParent(nullptr);
    QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, &QObject::deleteLater);
    if (proc->state() == QProcess::NotRunning)
        proc->deleteLater();
    else
        proc->kill();
}

void NotebookKernel::restart() {
    end_process();
    set_state(State::Off);
    fail_running(QStringLiteral("KernelRestarted"), QStringLiteral("The kernel was restarted"));
    drop_waiting();
}

} // namespace finterm::services::notebook
