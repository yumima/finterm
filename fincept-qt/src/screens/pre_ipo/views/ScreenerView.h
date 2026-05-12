// src/screens/pre_ipo/views/ScreenerView.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QWidget>

class QLineEdit;
class QTableWidget;
class QPushButton;

namespace fincept::screens {

/// Universe screener — sortable, filterable table for the Scan workflow.
/// Every row is a private company with derived metrics: readiness score,
/// mark drift, fund count, cumulative raise, S-1 status. The user clicks any
/// row to drop into the Markets tab dossier.
class ScreenerView : public QWidget {
    Q_OBJECT
  public:
    explicit ScreenerView(QWidget* parent = nullptr);

    void set_companies(const QVector<pre_ipo::PrivateCompany>& companies);

  signals:
    void company_selected(QString id);

  private:
    void build_ui();
    void rebuild_table();
    bool passes_filter(const pre_ipo::PrivateCompany& c) const;

    QLineEdit*    search_         = nullptr;
    QTableWidget* table_          = nullptr;
    QPushButton*  filter_has_marks_ = nullptr;
    QPushButton*  filter_filed_     = nullptr;
    QPushButton*  filter_ai_        = nullptr;
    QPushButton*  filter_fintech_   = nullptr;

    QVector<pre_ipo::PrivateCompany> companies_;
    QString search_text_;
};

} // namespace fincept::screens
