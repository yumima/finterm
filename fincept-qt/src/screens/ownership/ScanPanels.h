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

/// The largest 13F filers — the world's biggest asset managers' US-listed
/// stock books — ranked by size, each with what it did since its previous
/// filing. A row opens the filer's whole book; a ticker in the biggest-buy or
/// biggest-sell column opens that stock.
class LargestFundsPanel : public QWidget {
    Q_OBJECT
  public:
    explicit LargestFundsPanel(QWidget* parent = nullptr);
    void refresh();
  signals:
    void stock_activated(const QString& symbol);
    /// `quarter` (ISO) is the filing the row describes, so the book that
    /// opens matches the row's moves even when it is an EDGAR-pulled quarter.
    void firm_activated(const QString& cik, const QString& name, const QString& quarter);
  private:
    void render();
    QLabel* title_note_ = nullptr;
    QPushButton* pull_btn_ = nullptr;
    QLabel* pull_status_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

} // namespace fincept::screens
