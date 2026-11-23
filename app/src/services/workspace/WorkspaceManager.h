#pragma once
#include "core/result/Result.h"
#include "services/workspace/IWorkspaceParticipant.h"
#include "services/workspace/WorkspaceTypes.h"

#include <QHash>
#include <QMainWindow>
#include <QObject>
#include <QTimer>

#include <optional>

namespace finterm {

class DockScreenRouter;

/// Central workspace service — singleton QObject on the UI thread.
///
/// Lifecycle:
///   MainWindow wires it up via set_main_window() during startup.
///   load_last_workspace() is called after auth resolves.
///   save_workspace() is called in closeEvent().
class WorkspaceManager : public QObject {
    Q_OBJECT
  public:
    static WorkspaceManager& instance();

    /// Register a window. Multiple windows supported (multi-window mode).
    void set_main_window(QMainWindow* w);

    /// Unregister a closing window to prevent dangling pointers.
    void remove_window(QMainWindow* w);

    /// Register a window's screen router; the first one registered is the
    /// primary router whose active screen a workspace saves and restores.
    /// Unregisters itself when the router is destroyed.
    void set_router(DockScreenRouter* r);

    // ── Screen participation ──────────────────────────────────────────────────

    /// Called by screens in their constructor.
    void register_participant(const QString& screen_id, IWorkspaceParticipant* p);

    /// Called by screens in their destructor.
    void unregister_participant(const QString& screen_id);

    // ── Workspace operations ──────────────────────────────────────────────────

    /// The user's new / open: the workspace's screen is shown (as its tab).
    void new_workspace(const QString& name, const QString& description = {}, const QString& template_id = {});

    void open_workspace(const QString& path);

    /// Saves the workspace that is loaded; nothing loaded, nothing saved.
    void save_workspace();

    void save_workspace_as(const QString& new_name, const QString& path);

    void import_workspace(const QString& path);

    void export_workspace(const QString& destination_path);

    /// Resume the last-used workspace (sign-in, unlock). The window is
    /// already as it was left — its dock layout and tab views restored it —
    /// so a resume restores the screens' states, never which screen shows.
    void load_last_workspace();

    // ── Queries ───────────────────────────────────────────────────────────────

    Result<QVector<WorkspaceSummary>> list_workspaces() const;

    bool has_current_workspace() const { return current_workspace_.has_value(); }

    QString current_workspace_name() const;

  signals:
    void workspace_loaded(const WorkspaceDef& ws);
    void workspace_saved(const QString& id);
    void workspace_closed();
    void workspace_error(const QString& message);

  private:
    WorkspaceManager();

    /// Whether loading a workspace switches the window to its screen: yes when
    /// the user opens one; no when a session resumes (the dock owns that).
    enum class ScreenSwitch { Show, Keep };

    void create(const QString& name, const QString& description, const QString& template_id, ScreenSwitch sw);
    void open_file(const QString& path, ScreenSwitch sw);
    void capture_from_ui();
    void apply_to_ui(ScreenSwitch sw);
    void ensure_workspaces_dir() const;
    QString workspace_file_path(const QString& id) const;
    void save_last_id(const QString& id);
    void update_mru(const QString& id);

    std::optional<WorkspaceDef> current_workspace_;
    QHash<QString, IWorkspaceParticipant*> participants_;
    QHash<QString, QJsonObject> pending_states_; // states waiting for lazy screens

    // Multi-window aware: stores all registered windows and routers.
    // The first registered window is treated as primary for workspace operations.
    QList<QMainWindow*> windows_;
    QList<DockScreenRouter*> routers_;
    QTimer* autosave_timer_ = nullptr;
};

} // namespace finterm
