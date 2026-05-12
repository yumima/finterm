// src/screens/pre_ipo/CompanyDetailPanel.h
#pragma once
#include "screens/pre_ipo/PreIpoTypes.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::screens {

/// Center panel showing detailed information for a selected private company.
class CompanyDetailPanel : public QWidget {
    Q_OBJECT
  public:
    explicit CompanyDetailPanel(QWidget* parent = nullptr);

    void set_company(const pre_ipo::PrivateCompany& company);
    void clear();

  signals:
    /// Emitted when user clicks a public comparable ticker chip.
    void navigate_to_markets(QString ticker);

  private:
    void build_ui();
    QWidget* build_placeholder();
    QWidget* build_detail_view();

    void populate(const pre_ipo::PrivateCompany& c);

    QWidget* make_metric_tile(const QString& label, const QString& value,
                              const QString& color = {}) const;
    QWidget* make_tag_chip(const QString& text, bool clickable = false) const;
    void rebuild_rounds_table(const QVector<pre_ipo::PrimaryRound>& rounds);
    void rebuild_comps_chips(const QStringList& tickers);
    void rebuild_investors(const QStringList& investors);
    void rebuild_tags(const QStringList& tags);
    void rebuild_fund_marks(const QVector<pre_ipo::FundMark>& marks);
    void rebuild_analytics(const pre_ipo::PrivateCompany& c);

    // ── Stacked widget ────────────────────────────────────────────────────────
    QStackedWidget* stack_     = nullptr;
    QWidget*        placeholder_ = nullptr;
    QScrollArea*    detail_scroll_ = nullptr;
    QWidget*        detail_view_ = nullptr;

    // ── Header fields ─────────────────────────────────────────────────────────
    QLabel* name_lbl_       = nullptr;
    QLabel* sector_lbl_     = nullptr;
    QLabel* meta_lbl_       = nullptr;   // founded · HQ

    // ── Key metrics (compact label:value — no tile boxes) ────────────────────
    QLabel* val_lbl_       = nullptr;   // last valuation
    QLabel* round_lbl_     = nullptr;   // last round name + date
    QLabel* rev_lbl_       = nullptr;   // revenue estimate
    QLabel* emp_lbl_       = nullptr;   // employee count

    // ── IPO status ────────────────────────────────────────────────────────────
    QLabel* status_badge_  = nullptr;
    QLabel* window_lbl_    = nullptr;
    QLabel* s1_date_lbl_   = nullptr;

    // ── Share price section ───────────────────────────────────────────────────
    QWidget* price_section_     = nullptr;
    QLabel*  sec_price_lbl_     = nullptr;  // secondary market $/share
    QLabel*  implied_price_lbl_ = nullptr;  // from last round valuation
    QLabel*  formd_price_lbl_   = nullptr;  // Form D implied
    QLabel*  price_delta_lbl_   = nullptr;  // % change from last round

    // ── Rounds table ──────────────────────────────────────────────────────────
    QTableWidget* rounds_table_ = nullptr;

    // ── Investors ─────────────────────────────────────────────────────────────
    QWidget*     investors_container_ = nullptr;
    QVBoxLayout* investors_layout_    = nullptr;

    // ── Public comps ──────────────────────────────────────────────────────────
    QWidget*     comps_container_ = nullptr;
    QHBoxLayout* comps_layout_    = nullptr;

    // ── Tags ──────────────────────────────────────────────────────────────────
    QWidget*     tags_container_  = nullptr;
    QHBoxLayout* tags_layout_     = nullptr;

    // ── Description ───────────────────────────────────────────────────────────
    QLabel* desc_lbl_ = nullptr;

    // ── Fund marks (consensus + per-fund rows) ────────────────────────────────
    QWidget*      marks_section_   = nullptr;
    QLabel*       consensus_lbl_   = nullptr;
    QLabel*       dispersion_lbl_  = nullptr;
    QLabel*       smart_money_lbl_ = nullptr;
    QTableWidget* marks_table_     = nullptr;

    // ── Analytics KPI band ────────────────────────────────────────────────────
    QWidget* kpi_section_       = nullptr;
    QLabel*  kpi_readiness_lbl_ = nullptr;
    QLabel*  kpi_drift_lbl_     = nullptr;
    QLabel*  kpi_premium_lbl_   = nullptr;
    QLabel*  kpi_days_lbl_      = nullptr;
    QLabel*  kpi_raised_lbl_    = nullptr;
};

} // namespace fincept::screens
