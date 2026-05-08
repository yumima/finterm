// src/screens/power_trader/RankingsPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QButtonGroup>
#include <QLabel>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Multi-dimension leaderboard panel for the POWER TRADER screen.
///
/// Layout (vertical):
///   Top   — 8 dimension pill buttons (scrollable row)
///   Main  — Ranked QTableWidget: RANK / NAME / PARTY / VALUE / BAR
///   Footer — summary label
class RankingsPanel : public QWidget {
    Q_OBJECT
  public:
    explicit RankingsPanel(QWidget* parent = nullptr);

    void set_data(const power_trader::PowerTraderSummary& summary);
    void refresh_theme();

  signals:
    void member_selected(QString member_id);

  private slots:
    void on_dimension_changed(power_trader::RankingDimension dim);

  private:
    void build_ui();
    void populate_table(const QVector<power_trader::RankedMember>& ranked);
    void apply_pill_styles();

    static QString format_value(double v, power_trader::RankingDimension dim);

    // Dimension pill buttons
    QButtonGroup*                          btn_group_    = nullptr;
    QVector<QPushButton*>                  dim_buttons_;
    power_trader::RankingDimension         current_dim_  = power_trader::RankingDimension::Alpha;

    // Ranked table
    QTableWidget*  table_        = nullptr;
    QLabel*        footer_label_ = nullptr;

    // ── Member detail card (right pane) ───────────────────────────────────────
    void build_detail_card(QWidget* parent, QVBoxLayout* vl);
    void populate_detail_card(const QString& member_id);

    QLabel*       card_name_     = nullptr;
    QLabel*       card_meta_     = nullptr;
    QLabel*       card_alpha_    = nullptr;
    QLabel*       card_return_   = nullptr;
    QLabel*       card_nw_       = nullptr;
    QLabel*       card_signal_   = nullptr;
    QLabel*       card_lag_      = nullptr;
    QLabel*       card_trades_   = nullptr;
    QLabel*       card_cmtes_    = nullptr;

    // Cached summary for re-ranking on dimension change
    power_trader::PowerTraderSummary summary_;
    bool has_data_ = false;
};

} // namespace fincept::screens
