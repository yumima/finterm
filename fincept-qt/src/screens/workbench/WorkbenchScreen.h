#pragma once
// WorkbenchScreen.h — AI Workbench, single-screen left-nav (Track 13).
//
// Consolidates the AI surface (chat / agents / teams / workflows /
// tools / servers / profiles / system) behind one screen.  The
// design-doc target is for the existing chat_mode + agent_config +
// mcp_servers screens to live here as panels; this revision ships
// the scaffold and embeds the System + Schedule panels (AiSystemSection
// and SchedulerSection respectively) so the work from Tracks 10, 14
// is visible end-to-end.  Other sections are placeholders pointing
// the user at their dedicated screens for now.
//
// Lives in MainWindow's master_stack alongside chat_mode and the
// dock surface; `show_workbench()` on MainWindow flips the stack to
// this widget.

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

#include <vector>

namespace fincept::screens {

class WorkbenchScreen : public QWidget {
    Q_OBJECT
  public:
    explicit WorkbenchScreen(QWidget* parent = nullptr);

  signals:
    /// Emitted when the user wants to leave the Workbench (e.g. via
    /// the back button).  MainWindow flips the master_stack back.
    void exit_requested();

  private:
    QStackedWidget* sections_ = nullptr;
    std::vector<QPushButton*> nav_btns_;

    void build_ui();

    // Each section is built lazily as the user lands on the tab.
    // Returns a widget the QStackedWidget owns.
    QWidget* build_section_placeholder(const QString& name,
                                       const QString& description,
                                       const QString& cta_text);
    QWidget* build_chat_section();
    QWidget* build_agents_section();
    QWidget* build_teams_section();
    QWidget* build_workflows_section();
    QWidget* build_tools_section();
    QWidget* build_servers_section();
    QWidget* build_profiles_section();
    QWidget* build_system_section();
};

} // namespace fincept::screens
