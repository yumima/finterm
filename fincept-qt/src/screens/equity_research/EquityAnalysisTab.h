// src/screens/equity_research/EquityAnalysisTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QLabel>
#include <QWidget>

#include <cmath>

namespace fincept::screens {

class EquityAnalysisTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityAnalysisTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);

  private:
    void apply_info_state(const services::query::QueryStore::State& s);
    void build_ui();
    static QString fmt(double v, int decimals = 2);
    static QString fmt_large(double v);
    static QString fmt_pct(double v);

    QString current_symbol_;

    // Financial Health
    QLabel* cash_val_ = nullptr;
    QLabel* debt_val_ = nullptr;
    QLabel* fcf_val_ = nullptr;
    QLabel* ocf_val_ = nullptr;

    // Enterprise Value
    QLabel* ev_val_ = nullptr;
    QLabel* ev_rev_val_ = nullptr;
    QLabel* ev_ebitda_val_ = nullptr;
    QLabel* book_val_ = nullptr;

    // Revenue & Profits
    QLabel* rev_val_ = nullptr;
    QLabel* rev_share_val_ = nullptr;
    QLabel* gp_val_ = nullptr;
    QLabel* ebitda_m_val_ = nullptr;

    // Key Ratios
    QLabel* pe_val_ = nullptr;
    QLabel* peg_val_ = nullptr;
    QLabel* roe_val_ = nullptr;
    QLabel* roa_val_ = nullptr;
    QLabel* beta_val_ = nullptr;
    QLabel* short_rat_val_ = nullptr;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
