#pragma once
// The three market-wide scans on the OWNERSHIP screen. Every row is a stock,
// and every row opens Equity Research — the screen exists to hand the reader
// a ticker they were not already thinking of.
//
//   InsiderBuysPanel  open-market insider purchases, last N days, one row per
//                     issuer with the cluster stated as a count (OpenInsider)
//   ShortRankPanel    every stock on the latest FINRA settlement date, ranked
//                     by short interest ÷ 13F shares (Drechsler & Drechsler)
//   MoversPanel       holder-base change between the two indexed quarters —
//                     breadth (Chen, Hong & Stein), new, closed, focused net
//
// Each panel states its date and its lag in the section title, because the
// three sources run on three clocks and the reader must not have to guess.

#include "screens/ownership/OwnershipTypes.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace fincept::screens {

class InsiderBuysPanel : public QWidget {
    Q_OBJECT
  public:
    explicit InsiderBuysPanel(QWidget* parent = nullptr);
    /// Load (or reload) with the current controls. Cheap: a local query.
    void refresh();
  signals:
    void stock_activated(const QString& symbol);
  private:
    void render();
    QLabel* title_note_ = nullptr;
    QComboBox* window_ = nullptr;
    QSpinBox* min_insiders_ = nullptr;
    QComboBox* min_value_ = nullptr;
    QCheckBox* exclude_ten_pct_ = nullptr;
    QCheckBox* exclude_plan_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

class ShortRankPanel : public QWidget {
    Q_OBJECT
  public:
    explicit ShortRankPanel(QWidget* parent = nullptr);
    void refresh();
  signals:
    void stock_activated(const QString& symbol);
  private:
    void render();
    QLabel* title_note_ = nullptr;
    QComboBox* sort_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

class MoversPanel : public QWidget {
    Q_OBJECT
  public:
    explicit MoversPanel(QWidget* parent = nullptr);
    void refresh();
  signals:
    void stock_activated(const QString& symbol);
  private:
    void render();
    QLabel* title_note_ = nullptr;
    QComboBox* sort_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

class FirmDetailPanel;

/// The largest 13F filers — the world's biggest asset managers' US-listed
/// stock books — ranked by size, each with what it did since its previous
/// filing. A row shows the filer's whole book in the pane to its right, so the
/// ranking stays in view while the reader moves from fund to fund; a ticker in
/// the biggest-buy or biggest-sell column, or a holding in the book, opens
/// that stock.
class LargestFundsPanel : public QWidget {
    Q_OBJECT
  public:
    explicit LargestFundsPanel(QWidget* parent = nullptr);
    void refresh();
  signals:
    void stock_activated(const QString& symbol);
  private:
    void render();
    QLabel* title_note_ = nullptr;
    QPushButton* pull_btn_ = nullptr;
    QLabel* pull_status_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
    // The book of the fund picked in the table. The book loads at the quarter
    // the row describes, so it matches the row's moves even for an
    // EDGAR-pulled quarter.
    FirmDetailPanel* detail_ = nullptr;
    QString selected_cik_;   // re-selected after a reload re-fills the table
};

} // namespace fincept::screens
