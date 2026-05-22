#pragma once
// SchedulerSection.h — agent_schedule CRUD panel (Track 10 finish).
//
// Pairs with AgentScheduler (services/agents/AgentScheduler.{h,cpp}) +
// the v027 agent_schedule table.  Shows entries the scheduler is
// watching and lets the user add / remove / toggle / fire them.
//
// Lives under Settings alongside AiSystemSection.  The eventual
// Workbench → Schedule tab (Track 13 full) reuses the same widget.

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

class SchedulerSection : public QWidget {
    Q_OBJECT
  public:
    explicit SchedulerSection(QWidget* parent = nullptr);

    void reload();

  private slots:
    void on_add();
    void on_remove();
    void on_toggle_enabled();
    void on_run_now();
    void on_refresh();
    void on_selection_changed();

  private:
    QTableWidget* table_ = nullptr;
    QPushButton* add_btn_ = nullptr;
    QPushButton* remove_btn_ = nullptr;
    QPushButton* toggle_btn_ = nullptr;
    QPushButton* run_now_btn_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    void build_ui();
    void show_status(const QString& msg, bool error = false);
    QString selected_id() const;
    bool selected_enabled() const;
};

} // namespace fincept::screens
