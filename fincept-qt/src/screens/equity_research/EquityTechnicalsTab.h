// src/screens/equity_research/EquityTechnicalsTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

class EquityTechnicalsTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityTechnicalsTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);

  private slots:
    void on_technicals_loaded(services::equity::TechnicalsData data);

  private:
    void build_ui();
    void populate(const services::equity::TechnicalsData& data);
    void clear_sections();
    void switch_period(QPushButton* btn, const QString& period);

    static QString signal_text(services::equity::TechSignal s);
    static const char* signal_color(services::equity::TechSignal s);
    static QString interpretation(const QString& col_key, double value);
    static QString period_btn_style_active();
    static QString period_btn_style_inactive();

    QString current_symbol_;
    QString current_period_ = "1y";

    // Period buttons
    QPushButton* btn_1m_ = nullptr;
    QPushButton* btn_3m_ = nullptr;
    QPushButton* btn_6m_ = nullptr;
    QPushButton* btn_1y_ = nullptr;
    QPushButton* btn_5y_ = nullptr;
    QPushButton* active_period_btn_ = nullptr;

    // Rating panel
    QLabel* rating_label_ = nullptr;
    QProgressBar* gauge_bar_ = nullptr;
    QLabel* strong_buy_count_ = nullptr;
    QLabel* buy_count_ = nullptr;
    QLabel* neutral_count_ = nullptr;
    QLabel* sell_count_ = nullptr;
    QLabel* strong_sell_count_ = nullptr;
    QLabel* total_label_ = nullptr;

    // Key indicators container
    QWidget* key_container_ = nullptr;

    // Category sections container
    QWidget* sections_container_ = nullptr;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
