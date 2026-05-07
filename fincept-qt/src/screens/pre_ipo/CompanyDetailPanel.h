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
    void rebuild_rounds_table(const QVector<pre_ipo::FundingRound>& rounds);
    void rebuild_comps_chips(const QStringList& tickers);
    void rebuild_investors(const QStringList& investors);
    void rebuild_tags(const QStringList& tags);

    // ── Stacked widget ────────────────────────────────────────────────────────
    QStackedWidget* stack_     = nullptr;
    QWidget*        placeholder_ = nullptr;
    QScrollArea*    detail_scroll_ = nullptr;
    QWidget*        detail_view_ = nullptr;

    // ── Header fields ─────────────────────────────────────────────────────────
    QLabel* name_lbl_       = nullptr;
    QLabel* sector_lbl_     = nullptr;
    QLabel* meta_lbl_       = nullptr;   // founded · HQ

    // ── Metric tiles row ──────────────────────────────────────────────────────
    QWidget* metrics_row_   = nullptr;
    QWidget* tile_val_      = nullptr;
    QWidget* tile_round_    = nullptr;
    QWidget* tile_rev_      = nullptr;
    QWidget* tile_emp_      = nullptr;

    // ── IPO status section ────────────────────────────────────────────────────
    QLabel* status_badge_   = nullptr;
    QLabel* window_lbl_     = nullptr;
    QLabel* s1_date_lbl_    = nullptr;

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
};

} // namespace fincept::screens
