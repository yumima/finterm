#include "python/PythonRunner.h"
#include "services/workflow/WorkflowService.h"

#include "core/logging/Logger.h"
#include "services/workflow/WorkflowExecutor.h"
#include "storage/repositories/WorkflowRepository.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace finterm::workflow {

WorkflowService& WorkflowService::instance() {
    static WorkflowService s;
    return s;
}

WorkflowService::WorkflowService() : QObject(nullptr) {}

void WorkflowService::save_workflow(const WorkflowDef& wf) {
    auto r = WorkflowRepository::instance().save(wf);
    if (r.is_err()) {
        LOG_ERROR("WorkflowService", QString("Save failed: %1").arg(QString::fromStdString(r.error())));
        emit save_failed(QString::fromStdString(r.error()));
        return;
    }
    emit workflow_saved(wf.id);
    LOG_INFO("WorkflowService", QString("Saved workflow: %1").arg(wf.name));
}

void WorkflowService::load_workflow(const QString& id) {
    auto r = WorkflowRepository::instance().load(id);
    if (r.is_err()) {
        LOG_ERROR("WorkflowService", QString("Load failed: %1").arg(QString::fromStdString(r.error())));
        emit workflow_load_failed(QString::fromStdString(r.error()));
        return;
    }
    emit workflow_loaded(r.value());
}

void WorkflowService::list_workflows() {
    auto r = WorkflowRepository::instance().list_all();
    if (r.is_err()) {
        LOG_ERROR("WorkflowService", QString("List failed: %1").arg(QString::fromStdString(r.error())));
        return;
    }

    // Convert WorkflowRow summaries to lightweight WorkflowDefs (no nodes/edges)
    QVector<WorkflowDef> workflows;
    for (const auto& row : r.value()) {
        WorkflowDef wf;
        wf.id = row.id;
        wf.name = row.name;
        wf.description = row.description;
        wf.created_at = row.created_at;
        wf.updated_at = row.updated_at;

        if (row.status == "idle")
            wf.status = WorkflowStatus::Idle;
        else if (row.status == "running")
            wf.status = WorkflowStatus::Running;
        else if (row.status == "completed")
            wf.status = WorkflowStatus::Completed;
        else if (row.status == "error")
            wf.status = WorkflowStatus::Error;
        else
            wf.status = WorkflowStatus::Draft;

        workflows.append(wf);
    }
    emit workflows_listed(workflows);
}

void WorkflowService::delete_workflow(const QString& id) {
    auto r = WorkflowRepository::instance().remove(id);
    if (r.is_err()) {
        LOG_ERROR("WorkflowService", QString("Delete failed: %1").arg(QString::fromStdString(r.error())));
        return;
    }
    emit workflow_deleted(id);
    LOG_INFO("WorkflowService", QString("Deleted workflow: %1").arg(id));
}

// ── Import/Export ──────────────────────────────────────────────────────

// ── Execution ──────────────────────────────────────────────────────────

void WorkflowService::execute_workflow(const WorkflowDef& wf) {
    if (executor_) {
        executor_->deleteLater();
        executor_ = nullptr;
    }

    executor_ = new WorkflowExecutor(this);

    connect(executor_, &WorkflowExecutor::execution_started, this, &WorkflowService::execution_started);
    connect(executor_, &WorkflowExecutor::node_started, this, &WorkflowService::node_execution_started);
    connect(executor_, &WorkflowExecutor::node_completed, this, &WorkflowService::node_execution_completed);
    connect(executor_, &WorkflowExecutor::execution_finished, this, [this](const WorkflowExecutionResult& result) {
        emit execution_finished(result);
        if (executor_) {
            executor_->deleteLater();
            executor_ = nullptr;
        }
    });

    executor_->execute(wf);
}

void WorkflowService::execute_from_node(const WorkflowDef& wf, const QString& start_node_id) {
    if (executor_) {
        executor_->deleteLater();
        executor_ = nullptr;
    }

    executor_ = new WorkflowExecutor(this);

    connect(executor_, &WorkflowExecutor::execution_started, this, &WorkflowService::execution_started);
    connect(executor_, &WorkflowExecutor::node_started, this, &WorkflowService::node_execution_started);
    connect(executor_, &WorkflowExecutor::node_completed, this, &WorkflowService::node_execution_completed);
    connect(executor_, &WorkflowExecutor::execution_finished, this, [this](const WorkflowExecutionResult& result) {
        emit execution_finished(result);
        if (executor_) {
            executor_->deleteLater();
            executor_ = nullptr;
        }
    });

    executor_->execute_from(wf, start_node_id);
}

void WorkflowService::stop_execution() {
    if (!executor_)
        return;
    executor_->stop();
    python::PythonRunner::instance().cancel_user_code(); // a code node could run on for an hour
}

bool WorkflowService::is_executing() const {
    return executor_ && executor_->is_running();
}

} // namespace finterm::workflow
