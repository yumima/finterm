// src/screens/portfolio/views/EconomicsView.h
#pragma once
#include "screens/portfolio/PortfolioTypes.h"

#include <QLabel>
#include <QTableWidget>
#include <QWidget>

namespace fincept::screens {

/// Economics view: per-holding contribution to value and P&L. The factor-
/// sensitivity section states that no factor model data exists (it used to
/// show numbers from an uncited constant sector-beta table).
class EconomicsView : public QWidget {
    Q_OBJECT
  public:
    explicit EconomicsView(QWidget* parent = nullptr);

    void set_data(const portfolio::PortfolioSummary& summary, const QString& currency);

  private:
    void build_ui();
    void update_indicators();

    // Macro indicators table
    QTableWidget* indicators_table_ = nullptr;

    portfolio::PortfolioSummary summary_;
    QString currency_;
};

} // namespace fincept::screens
