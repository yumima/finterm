// src/screens/power_trader/InsiderWatchPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// "INSIDER WATCH" tab — multi-factor insider-trading score for every member.
///
/// Wide-screen layout:
///   Top    — score legend / methodology note
///   Left   (45%) — ranked watch list table with score components
///   Right  (55%) — selected member detail: score breakdown + evidence bullets
class InsiderWatchPanel : public QWidget {
    Q_OBJECT
  public:
    explicit InsiderWatchPanel(QWidget* parent = nullptr);

    void set_data(const QVector<power_trader::InsiderWatchEntry>& entries);

  signals:
    void member_selected(QString member_id);

  private slots:
    void on_entry_selected(int row);

  private:
    void build_ui();
    void populate_watch_list();
    void show_entry(const power_trader::InsiderWatchEntry& e);

    // ── Watch list (left) ────────────────────────────────────────────────────
    QTableWidget* watch_table_      = nullptr;

    // ── Detail (right) ───────────────────────────────────────────────────────
    QLabel*       detail_name_      = nullptr;
    QLabel*       detail_score_     = nullptr;
    QLabel*       detail_meta_      = nullptr;
    QTableWidget* score_breakdown_  = nullptr;
    QWidget*      evidence_widget_  = nullptr;
    QVBoxLayout*  evidence_layout_  = nullptr;

    QVector<power_trader::InsiderWatchEntry> entries_;
    int selected_row_ = -1;
};

} // namespace fincept::screens
