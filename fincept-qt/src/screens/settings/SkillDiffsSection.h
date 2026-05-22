#pragma once
// SkillDiffsSection.h — vendor-skill drift review panel (Track 1C UI).
//
// Counterpart to the nightly CI PR comments: lets the user run
// `vendor_skills_diff.py` from inside the app against a configured
// upstream clone and review the diff in an inbox-style table.
//
// Actions per row:
//   - View diff    side-by-side QPlainTextEdit of local vs upstream
//   - Accept upstream    overwrite the local SKILL.md with upstream
//                        (gated behind a confirm — same safety
//                        pattern as the Track 7C propose-fix flow)
//
// Upstream path is persisted to SettingsRepository so the user only
// configures once.  If the script's never been run / the path isn't
// configured, the table is empty and the user sees a hint banner.

#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

class SkillDiffsSection : public QWidget {
    Q_OBJECT
  public:
    explicit SkillDiffsSection(QWidget* parent = nullptr);

  private slots:
    void on_browse();
    void on_run_diff();
    void on_view_diff();
    void on_accept_upstream();
    void on_selection_changed();
    void on_diff_finished(int exit_code);

  private:
    QLineEdit* upstream_edit_      = nullptr;
    QPushButton* browse_btn_        = nullptr;
    QPushButton* run_btn_           = nullptr;
    QPushButton* view_diff_btn_     = nullptr;
    QPushButton* accept_btn_        = nullptr;
    QTableWidget* table_            = nullptr;
    QLabel* status_lbl_             = nullptr;
    QLabel* hint_lbl_               = nullptr;
    QJsonObject last_report_;       // raw output of vendor_skills_diff.py
    class QProcess* diff_proc_     = nullptr;   // null when no diff is running

    void build_ui();
    void load_upstream_path();
    void save_upstream_path(const QString& path);
    void populate_table(const QJsonObject& report);
    void show_status(const QString& msg, bool error = false);

    /// Resolve the row's local + upstream SKILL.md path pair.  Empty
    /// strings on failure (also surfaces to status).  Both paths are
    /// canonicalised and required to live under an allow-list root
    /// (local: the vendored skills tree; upstream: the configured
    /// upstream clone) to defeat path-traversal via tampered JSON.
    struct Paths { QString local; QString upstream; QString name; };
    Paths paths_for_row(int row);
};

} // namespace fincept::screens
