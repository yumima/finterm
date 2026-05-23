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

#include "core/result/Result.h"
#include "storage/repositories/AgentTraceRepository.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace fincept::services {
struct SkillProposal;
}

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
    void on_trace_double_clicked(int row, int column);

  private:
    QLabel* spend_total_lbl_ = nullptr;
    QLabel* spend_by_agent_lbl_ = nullptr;
    QTableWidget* traces_table_ = nullptr;
    QTableWidget* killswitch_table_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QPushButton* disable_btn_ = nullptr;
    QPushButton* enable_btn_ = nullptr;
    QCheckBox* inline_completion_cb_ = nullptr;
    QLabel* status_lbl_ = nullptr;

    void build_ui();
    /// Both `load_spend` and `load_traces` take the same shared trace
    /// fetch from `reload()` — avoids a duplicate SQL read.
    void load_spend(const Result<QVector<AgentTraceRow>>& traces);
    void load_traces(const Result<QVector<AgentTraceRow>>& traces);
    void load_killswitch();
    void show_status(const QString& msg, bool error = false);

    /// Track 7C — propose a SKILL.md revision based on a turn the
    /// user has reviewed.  Spawns a background QtConcurrent::run
    /// for the LLM call so the UI thread stays responsive.
    void on_propose_skill_fix(const AgentTraceRow& trace);
    void show_proposal_dialog(const Result<services::SkillProposal>& r);
};

} // namespace fincept::screens
