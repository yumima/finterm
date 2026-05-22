#pragma once
// AiSystemSection.h — observability + safety panel for the AI stack
// (Track 13 minimum-viable; the full Workbench → System tab lands
// later).
//
// Surfaces what the safety / observability tracks already wrote:
//   - Recent agent_traces (Track 14 #38)
//   - Daily spend total + per-agent breakdown (Track 14 #41)
//   - Tool kill-switch entries with toggle (Track 14 #39)
//
// Lives inside SettingsScreen alongside the other sections; the
// dedicated Workbench screen with left-nav consolidation is the
// follow-up.

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

class AiSystemSection : public QWidget {
    Q_OBJECT
  public:
    explicit AiSystemSection(QWidget* parent = nullptr);

    /// Refresh every panel from disk.  Cheap to call (3 indexed
    /// SQLite reads + a SUM aggregate); safe to wire to a manual
    /// refresh button or a periodic timer.
    void reload();

  private slots:
    void on_refresh();
    void on_disable_tool();
    void on_enable_tool();

  private:
    QLabel* spend_total_lbl_ = nullptr;
    QLabel* spend_by_agent_lbl_ = nullptr;
    QTableWidget* traces_table_ = nullptr;
    QTableWidget* killswitch_table_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QPushButton* disable_btn_ = nullptr;
    QPushButton* enable_btn_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    void build_ui();
    void load_spend();
    void load_traces();
    void load_killswitch();
    void show_status(const QString& msg, bool error = false);
};

} // namespace fincept::screens
