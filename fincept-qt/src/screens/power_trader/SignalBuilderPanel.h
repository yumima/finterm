// src/screens/power_trader/SignalBuilderPanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

/// Signal Builder — Phase 2 of the congressional trading intelligence suite.
///
/// Wide-screen two-column layout:
///   LEFT  (320px): 8 factor weight sliders + preset selector
///   RIGHT (flex):  live preview table (re-scored trades) + selected trade breakdown
///
/// Factor weights are multipliers (0–200%, step 10%):
///   0%   = ignore this factor entirely
///   100% = standard weight
///   200% = double weight (amplify this factor)
///
/// Factors 1–4 are computed from available STOCK Act data.
/// Factors 5–8 show "Pending" until Congress.gov / LDA integration.
class SignalBuilderPanel : public QWidget {
    Q_OBJECT
  public:
    explicit SignalBuilderPanel(QWidget* parent = nullptr);

    void set_data(const power_trader::PowerTraderSummary& summary);

  signals:
    void member_selected(QString member_id);

  private slots:
    void on_weight_changed();
    void on_preset_clicked(const QString& preset_id);
    void on_save_preset();
    void on_trade_selected(int row);

  private:
    void build_ui();
    void build_factor_panel(QWidget* parent);
    void build_preset_bar(QWidget* parent);
    void refresh_preview();
    void update_breakdown(int row);
    void load_user_presets();
    void save_user_presets();
    power_trader::SignalPreset current_preset() const;
    void apply_preset(const power_trader::SignalPreset& p);

    // ── Factor sliders ────────────────────────────────────────────────────────
    struct FactorRow {
        QString id;
        QString label;
        QString description;
        bool    pending = false;   // true = no live data yet (slider disabled)
        QSlider*  slider  = nullptr;
        QLabel*   pct_lbl = nullptr;
        QLabel*   val_lbl = nullptr;  // shows current computed base value
    };
    QVector<FactorRow> factors_;

    // ── Preset bar ────────────────────────────────────────────────────────────
    QVector<power_trader::SignalPreset> presets_;
    QWidget* preset_bar_  = nullptr;
    QString  active_preset_id_ = "default";

    // ── Preview table ─────────────────────────────────────────────────────────
    QTableWidget* preview_table_   = nullptr;
    QLabel*       formula_lbl_     = nullptr;
    QWidget*      breakdown_panel_ = nullptr;
    QLabel*       bd_member_       = nullptr;
    QLabel*       bd_ticker_       = nullptr;
    QVector<QLabel*> bd_factor_lbls_;  // one per factor (shows contribution)

    // ── State ─────────────────────────────────────────────────────────────────
    power_trader::PowerTraderSummary summary_;
    QVector<power_trader::TradeFactorScores> base_scores_;
    bool updating_ = false;   // suppress re-entrant slider signals
};

} // namespace fincept::screens
