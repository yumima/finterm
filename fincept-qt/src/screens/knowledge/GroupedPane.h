#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QStackedWidget;
class QToolButton;
class QButtonGroup;

namespace fincept::knowledge {

/// One of the three top-level cockpit panes (BASICS, PRACTICE, …).
/// Hosts a header with segmented sub-tab buttons and a QStackedWidget
/// switching between member columns. Each member column is added with
/// addSubPane(label, widget, settings_key) — the active sub-tab index is
/// persisted under "knowledge/<group_id>/active_subtab" in QSettings, and
/// restored on construction (defaulting to the first sub-pane when no
/// prior selection exists).
///
/// Contained columns retain their state because they are kept alive in
/// the stack — switching tabs does not destroy them, so each column's
/// internal "last selected entry" survives sub-tab navigation.
class GroupedPane : public QWidget {
    Q_OBJECT
  public:
    explicit GroupedPane(const QString& group_id, const QString& title, QWidget* parent = nullptr);

    /// Add a sub-pane. Takes ownership of `widget`. `tab_label` is what
    /// renders on the segmented button (uppercase recommended).
    void addSubPane(const QString& tab_label, QWidget* widget);

    /// Number of sub-panes currently registered.
    int subPaneCount() const;

    /// The widget currently shown in the stack, or nullptr if empty.
    QWidget* currentSubPane() const;

    /// Show the sub-pane whose hosted widget == widget (no-op if not in stack).
    void showSubPane(QWidget* widget);

    /// Restore the persisted active sub-tab index (or 0 if none). Call after
    /// all sub-panes have been added.
    void restoreActiveSubTab();

  signals:
    /// Emitted when the user switches sub-tabs. `widget` is the now-active
    /// sub-pane; useful for the shell to refresh the breadcrumb / rail.
    void subPaneActivated(QWidget* widget);

  private:
    void setActiveIndex(int idx, bool persist);
    void restyleButtons();

    QString group_id_;
    QStackedWidget* stack_ = nullptr;
    QButtonGroup* btn_group_ = nullptr;
    QVector<QToolButton*> buttons_;
    QWidget* btn_row_ = nullptr;
};

} // namespace fincept::knowledge
