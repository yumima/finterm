// src/screens/pre_ipo/CompanyListPanel.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::screens {

/// Left panel: searchable, filterable list of private companies.
/// Each card shows name, sector, valuation badge, and IPO status badge.
class CompanyListPanel : public QWidget {
    Q_OBJECT
  public:
    explicit CompanyListPanel(QWidget* parent = nullptr);

    void set_companies(const QVector<pre_ipo::PrivateCompany>& companies);
    void set_selected_id(const QString& id);

  signals:
    void company_selected(QString id);

  protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

  private:
    void build_ui();
    void rebuild_list();
    void apply_filter();
    void set_active_filter(const QString& tag);

    QWidget* make_card(const pre_ipo::PrivateCompany& c) const;
    static QWidget* make_status_badge(pre_ipo::IpoStatus status, QWidget* parent);
    static QString valuation_label(double b);

    // ── Widgets ───────────────────────────────────────────────────────────────
    QLineEdit*   search_edit_     = nullptr;
    QWidget*     filter_bar_      = nullptr;
    QHBoxLayout* filter_layout_   = nullptr;
    QScrollArea* scroll_          = nullptr;
    QWidget*     list_container_  = nullptr;
    QVBoxLayout* list_layout_     = nullptr;

    // Filter chip buttons
    QVector<QPushButton*> filter_btns_;

    // ── State ─────────────────────────────────────────────────────────────────
    QVector<pre_ipo::PrivateCompany> all_companies_;
    QString search_text_;
    QString active_filter_; // "all", "watched", "filed", "ai", "fintech", "defense"
    QString selected_id_;
};

} // namespace fincept::screens
