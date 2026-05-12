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

    void rebuild_rounds_table(const QVector<pre_ipo::PrimaryRound>& rounds);
    void rebuild_comps_chips(const QStringList& tickers);
    void rebuild_investors(const QStringList& investors);
    void rebuild_tags(const QStringList& tags);
    void rebuild_fund_marks(const QVector<pre_ipo::FundMark>& marks);
    void rebuild_analytics(const pre_ipo::PrivateCompany& c);

    // ── Stacked widget ────────────────────────────────────────────────────────
    // detail_view_ is hosted directly in the stack — no outer QScrollArea.
    // The dossier is sized so a populated company fits in the typical center
    // pane without a page-level scrollbar; data-heavy tiles (rounds table,
    // fund-mark table, investors list, about) get internal scroll instead.
    QStackedWidget* stack_     = nullptr;
    QWidget*        placeholder_ = nullptr;
    QWidget*        detail_view_ = nullptr;

    // ── Header fields ─────────────────────────────────────────────────────────
    QLabel* name_lbl_       = nullptr;
    QLabel* sector_lbl_     = nullptr;
    QLabel* meta_lbl_       = nullptr;   // founded · HQ

    // ── Key metrics (compact label:value — no tile boxes) ────────────────────
    // Each metric has both a *label* and *value* pointer because the labels
    // are repurposed per company depending on which data source has rows.
    // For Form-D companies we show {Valuation, Last Round, Cum. Raised,
    // Last Filing}; for N-PORT-only companies (OpenAI/Anthropic/etc.) we
    // show {Consensus Mark, Mark As Of, Funds Tracking, Mark Drift} —
    // populated unconditionally so the dossier is never "two empty boxes".
    QLabel* val_lbl_label_  = nullptr;
    QLabel* val_lbl_       = nullptr;
    QLabel* round_lbl_label_ = nullptr;
    QLabel* round_lbl_     = nullptr;
    QLabel* rev_lbl_label_  = nullptr;
    QLabel* rev_lbl_       = nullptr;
    QLabel* emp_lbl_label_  = nullptr;
    QLabel* emp_lbl_       = nullptr;

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
    // investors_card_ is the surrounding tile (hidden when investor list is
    // empty so a sparse company doesn't waste a half-row of vertical space).
    QWidget*     investors_card_      = nullptr;
    QWidget*     investors_container_ = nullptr;
    QVBoxLayout* investors_layout_    = nullptr;
    QScrollArea* investors_scroll_    = nullptr;

    // ── Public comps ──────────────────────────────────────────────────────────
    QWidget*     comps_section_   = nullptr;  // header + chip row; hidden when empty
    QWidget*     comps_container_ = nullptr;
    QHBoxLayout* comps_layout_    = nullptr;

    // ── Tags ──────────────────────────────────────────────────────────────────
    QWidget*     tags_section_   = nullptr;  // header + chip row; hidden when empty
    QWidget*     tags_container_ = nullptr;
    QHBoxLayout* tags_layout_    = nullptr;

    // ── Description ───────────────────────────────────────────────────────────
    QWidget* desc_section_ = nullptr;  // header + scroll-wrapped label; hidden when empty
    QLabel*  desc_lbl_     = nullptr;

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
