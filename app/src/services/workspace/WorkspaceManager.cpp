#include "services/workspace/WorkspaceManager.h"

#include "app/DockScreenRouter.h"
#include "core/config/AppPaths.h"
#include "core/logging/Logger.h"
#include "core/symbol/SymbolContext.h"
#include "screens/dashboard/canvas/GridLayout.h"
#include "services/workspace/WorkspaceSerializer.h"
#include "services/workspace/WorkspaceTemplates.h"
#include "storage/repositories/SettingsRepository.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QUuid>

namespace finterm {

namespace {

/// Writes `grid` into the key DashboardScreen loads its grid from (an empty
/// grid — the blank template — leaves the user's as it is).
void seed_dashboard_grid(const screens::GridLayout& grid) {
    if (grid.items.isEmpty())
        return;
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << grid.cols << grid.row_h << grid.margin;
    stream << static_cast<int>(grid.items.size());
    for (const auto& item : grid.items) {
        stream << item.id << item.instance_id;
        stream << item.cell.x << item.cell.y << item.cell.w << item.cell.h;
        stream << item.cell.min_w << item.cell.min_h;
    }
    SettingsRepository::instance().set("dashboard_canvas_layout", QString::fromLatin1(data.toBase64()), "dashboard");
}

} // namespace

// ── Singleton ─────────────────────────────────────────────────────────────────

WorkspaceManager& WorkspaceManager::instance() {
    static WorkspaceManager inst;
    return inst;
}

WorkspaceManager::WorkspaceManager() {
    autosave_timer_ = new QTimer(this);
    autosave_timer_->setInterval(5 * 60 * 1000); // 5 minutes
    connect(autosave_timer_, &QTimer::timeout, this, [this]() {
        if (current_workspace_.has_value())
            save_workspace();
    });
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void WorkspaceManager::set_main_window(QMainWindow* w) {
    if (w && !windows_.contains(w))
        windows_.append(w);
}

void WorkspaceManager::remove_window(QMainWindow* w) {
    windows_.removeAll(w);
}

void WorkspaceManager::set_router(DockScreenRouter* r) {
    if (!r || routers_.contains(r))
        return;
    routers_.append(r);
    connect(r, &QObject::destroyed, this, [this, r]() { routers_.removeAll(r); });
}

// ── Participant registration ──────────────────────────────────────────────────

void WorkspaceManager::register_participant(const QString& screen_id, IWorkspaceParticipant* p) {
    participants_[screen_id] = p;

    // If a pending state exists, deliver it immediately
    if (pending_states_.contains(screen_id)) {
        p->restore_state(pending_states_.take(screen_id));
        LOG_DEBUG("WorkspaceManager", QString("Delivered pending state to: %1").arg(screen_id));
    }
}

void WorkspaceManager::unregister_participant(const QString& screen_id) {
    participants_.remove(screen_id);
}

// ── Workspace operations ──────────────────────────────────────────────────────

void WorkspaceManager::new_workspace(const QString& name, const QString& description, const QString& template_id) {
    create(name, description, template_id, ScreenSwitch::Show);
}

void WorkspaceManager::open_workspace(const QString& path) {
    open_file(path, ScreenSwitch::Show);
}

void WorkspaceManager::create(const QString& name, const QString& description, const QString& template_id,
                              ScreenSwitch sw) {
    ensure_workspaces_dir();

    WorkspaceDef ws = WorkspaceTemplates::make(template_id);
    ws.metadata.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ws.metadata.name = name;
    ws.metadata.description = description;
    ws.metadata.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    ws.metadata.updated_at = ws.metadata.created_at;
    // A template's dashboard grid is the dashboard's from now on — only when
    // the user creates the workspace. A workspace never captures the grid, so
    // writing its stored copy on an open or a resume put the template's grid
    // back over the user's own.
    if (sw == ScreenSwitch::Show)
        seed_dashboard_grid(ws.dashboard_layout);

    current_workspace_ = std::move(ws);
    pending_states_.clear();

    apply_to_ui(sw);

    emit workspace_closed();
    emit workspace_loaded(*current_workspace_);

    // On disk and the last-used at once: a resume before the first autosave
    // finds it, rather than making yet another "Default".
    save_workspace();
    autosave_timer_->start();
    LOG_INFO("WorkspaceManager", QString("New workspace: %1").arg(name));
}

void WorkspaceManager::open_file(const QString& path, ScreenSwitch sw) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit workspace_error(QString("Cannot open file: %1").arg(path));
        return;
    }
    QByteArray data = f.readAll();
    f.close();

    auto result = WorkspaceSerializer::from_json(QJsonDocument::fromJson(data));
    if (result.is_err()) {
        emit workspace_error(QString::fromStdString(result.error()));
        return;
    }

    // Save current workspace before switching
    if (current_workspace_.has_value())
        save_workspace();

    current_workspace_ = std::move(result.value());
    pending_states_.clear();

    apply_to_ui(sw);

    save_last_id(current_workspace_->metadata.id);
    update_mru(current_workspace_->metadata.id);

    emit workspace_loaded(*current_workspace_);
    autosave_timer_->start();

    LOG_INFO("WorkspaceManager", QString("Opened workspace: %1").arg(current_workspace_->metadata.name));
}

void WorkspaceManager::save_workspace() {
    // Before sign-in / unlock nothing is loaded yet. A save is never a load:
    // making (and applying) a workspace here switched the window to the
    // blank template's DASHBOARD behind the lock screen, beside the screen
    // the dock had just restored — and wrote a new "Default" file per start.
    if (!current_workspace_.has_value())
        return;

    ensure_workspaces_dir();
    capture_from_ui();

    current_workspace_->metadata.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonDocument doc = WorkspaceSerializer::to_json(*current_workspace_);
    QString path = workspace_file_path(current_workspace_->metadata.id);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit workspace_error(QString("Cannot write workspace: %1").arg(path));
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();

    save_last_id(current_workspace_->metadata.id);
    emit workspace_saved(current_workspace_->metadata.id);
    LOG_INFO("WorkspaceManager", QString("Saved workspace: %1").arg(current_workspace_->metadata.name));
}

void WorkspaceManager::save_workspace_as(const QString& new_name, const QString& path) {
    if (!current_workspace_.has_value())
        return;

    capture_from_ui();

    WorkspaceDef ws = *current_workspace_;
    ws.metadata.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ws.metadata.name = new_name;
    ws.metadata.updated_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonDocument doc = WorkspaceSerializer::to_json(ws);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit workspace_error(QString("Cannot write workspace: %1").arg(path));
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();

    // Switch to the new workspace
    current_workspace_ = std::move(ws);
    save_last_id(current_workspace_->metadata.id);
    emit workspace_saved(current_workspace_->metadata.id);
    emit workspace_loaded(*current_workspace_);
    LOG_INFO("WorkspaceManager", QString("Saved workspace as: %1").arg(new_name));
}

void WorkspaceManager::import_workspace(const QString& path) {
    auto summary = WorkspaceSerializer::summary_from_file(path);
    if (summary.is_err()) {
        emit workspace_error(QString::fromStdString(summary.error()));
        return;
    }

    ensure_workspaces_dir();

    // Assign new UUID to avoid collision
    QString new_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString dest = workspace_file_path(new_id);

    QFile::copy(path, dest);

    // Patch the id in the copied file
    QFile f(dest);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        f.close();
        auto result = WorkspaceSerializer::from_json(QJsonDocument::fromJson(data));
        if (result.is_ok()) {
            WorkspaceDef ws = result.value();
            ws.metadata.id = new_id;
            QFile fw(dest);
            if (fw.open(QIODevice::WriteOnly | QIODevice::Truncate))
                fw.write(WorkspaceSerializer::to_json(ws).toJson(QJsonDocument::Indented));
        }
    }

    open_workspace(dest);
    LOG_INFO("WorkspaceManager", QString("Imported workspace from: %1").arg(path));
}

void WorkspaceManager::export_workspace(const QString& destination_path) {
    if (!current_workspace_.has_value())
        return;
    capture_from_ui();

    QJsonDocument doc = WorkspaceSerializer::to_json(*current_workspace_);
    QFile f(destination_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit workspace_error(QString("Cannot export to: %1").arg(destination_path));
        return;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    LOG_INFO("WorkspaceManager", QString("Exported workspace to: %1").arg(destination_path));
}

void WorkspaceManager::load_last_workspace() {
    auto id_result = SettingsRepository::instance().get("workspace.last_id");
    // An unlock in the same session: that workspace is live — its screens
    // and symbols are newer than its file (a ticker changed since the last
    // save). Re-reading the file would put them back.
    if (current_workspace_.has_value() && id_result.is_ok() && id_result.value() == current_workspace_->metadata.id)
        return;
    if (id_result.is_ok() && !id_result.value().isEmpty()) {
        QString path = workspace_file_path(id_result.value());
        if (QFile::exists(path)) {
            open_file(path, ScreenSwitch::Keep);
            return;
        }
    }
    // No saved workspace — create default
    create(QStringLiteral("Default"), {}, {}, ScreenSwitch::Keep);
}

// ── Queries ───────────────────────────────────────────────────────────────────

Result<QVector<WorkspaceSummary>> WorkspaceManager::list_workspaces() const {
    ensure_workspaces_dir();
    QDir dir(AppPaths::workspaces());
    QVector<WorkspaceSummary> result;

    const auto entries = dir.entryInfoList({"*.fwsp"}, QDir::Files, QDir::Time);
    for (const auto& fi : entries) {
        auto s = WorkspaceSerializer::summary_from_file(fi.absoluteFilePath());
        if (s.is_ok())
            result.append(s.value());
    }
    return Result<QVector<WorkspaceSummary>>::ok(std::move(result));
}

QString WorkspaceManager::current_workspace_name() const {
    return current_workspace_.has_value() ? current_workspace_->metadata.name : QString{};
}

// ── Private helpers ───────────────────────────────────────────────────────────

void WorkspaceManager::capture_from_ui() {
    if (!current_workspace_.has_value())
        return;

    // Window geometry — use primary window (first registered)
    if (!windows_.isEmpty())
        current_workspace_->window_geometry_base64 = windows_.first()->saveGeometry().toBase64();

    // Active screen — the primary router's tab (not a pane beside it: ER
    // beside Portfolio is Portfolio's view).
    if (!routers_.isEmpty()) {
        auto* router = routers_.first();
        const QString tab = router->primary_id();
        current_workspace_->active_screen = router->is_open(tab) ? tab : router->current_screen_id();
    }

    // Per-screen states from participants
    current_workspace_->screen_states.clear();
    for (auto it = participants_.cbegin(); it != participants_.cend(); ++it) {
        WorkspaceScreenState ss;
        ss.screen_id = it.key();
        ss.state = it.value()->save_state();
        current_workspace_->screen_states.append(ss);
    }

    // Symbol context (group→symbol assignments) is a singleton, captured once
    // per workspace save. Empty object if the user hasn't touched any groups.
    current_workspace_->symbol_context = SymbolContext::instance().to_json();
}

void WorkspaceManager::apply_to_ui(ScreenSwitch sw) {
    if (!current_workspace_.has_value())
        return;
    const WorkspaceDef& ws = *current_workspace_;

    // Queue pending screen states for lazy-constructed screens
    for (const auto& ss : ws.screen_states) {
        if (participants_.contains(ss.screen_id)) {
            // Screen already constructed — deliver immediately
            participants_[ss.screen_id]->restore_state(ss.state);
        } else {
            // Screen not yet constructed — queue for when it registers
            pending_states_[ss.screen_id] = ss.state;
        }
    }

    // The workspace the user opened: its window geometry and its screen (as
    // that screen's tab). A resumed session (sign-in, unlock) keeps the
    // window as the session restored it — geometry per window, the dock and
    // each tab's view — which is what the user left.
    if (sw == ScreenSwitch::Show && !windows_.isEmpty() && !ws.window_geometry_base64.isEmpty()) {
        // Never clobber maximized/fullscreen; skip an unchanged geometry (a
        // restoreGeometry is a WM configure round-trip — a visible flash).
        auto* win = windows_.first();
        const Qt::WindowStates st = win->windowState();
        const bool preserve_size = st.testFlag(Qt::WindowMaximized) || st.testFlag(Qt::WindowFullScreen);
        if (!preserve_size) {
            const QByteArray saved = QByteArray::fromBase64(ws.window_geometry_base64.toLatin1());
            if (win->saveGeometry() != saved)
                win->restoreGeometry(saved);
        }
    }

    if (sw == ScreenSwitch::Show && !routers_.isEmpty() && !ws.active_screen.isEmpty())
        routers_.first()->go_to(ws.active_screen);

    // Restore symbol group context last so every subscriber has been
    // constructed (or at least queued) before signals fan out.
    SymbolContext::instance().from_json(ws.symbol_context);
}

void WorkspaceManager::ensure_workspaces_dir() const {
    QDir().mkpath(AppPaths::workspaces());
}

QString WorkspaceManager::workspace_file_path(const QString& id) const {
    return AppPaths::workspaces() + "/" + id + ".fwsp";
}

void WorkspaceManager::save_last_id(const QString& id) {
    SettingsRepository::instance().set("workspace.last_id", id, "workspace");
}

void WorkspaceManager::update_mru(const QString& id) {
    auto r = SettingsRepository::instance().get("workspace.mru_list");
    QStringList mru;
    if (r.is_ok() && !r.value().isEmpty())
        mru = r.value().split(',', Qt::SkipEmptyParts);

    mru.removeAll(id);
    mru.prepend(id);
    if (mru.size() > 5)
        mru = mru.mid(0, 5);

    SettingsRepository::instance().set("workspace.mru_list", mru.join(','), "workspace");
}

} // namespace finterm
