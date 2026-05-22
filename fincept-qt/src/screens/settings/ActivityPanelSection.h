#pragma once
// ActivityPanelSection.h — in-flight + recent agent dispatches
// (Track 7A).
//
// A focused view over `agent_traces` that answers "what is the
// agent doing *right now* and what did it just finish".  Distinct
// from the AI System section's last-50 table — that's history;
// this is the active state surface.  Replit-Agent / Devin pattern,
// scoped to read-only for v1 (no cancel button yet).
//
// Refresh model: 3-second QTimer while the section is visible.
// Stops when hidden so we don't poll forever in the background.

#include "core/result/Result.h"
#include "storage/repositories/AgentTraceRepository.h"

#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

class ActivityPanelSection : public QWidget {
    Q_OBJECT
  public:
    explicit ActivityPanelSection(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private slots:
    void on_refresh();
    void on_view_trace(int row, int column);

  private:
    QLabel* summary_lbl_ = nullptr;
    QTableWidget* in_flight_table_ = nullptr;
    QTableWidget* recent_table_ = nullptr;
    QPushButton* refresh_btn_ = nullptr;
    QTimer* poll_timer_ = nullptr;

    void build_ui();
    void populate(const QVector<AgentTraceRow>& rows);
    static QString elapsed(const QString& started_at_iso);
    void show_trace_dialog(const AgentTraceRow& t);
};

} // namespace fincept::screens
