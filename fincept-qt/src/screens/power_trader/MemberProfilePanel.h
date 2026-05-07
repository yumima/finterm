// src/screens/power_trader/MemberProfilePanel.h
#pragma once
#include "screens/power_trader/PowerTraderTypes.h"

#include <QDate>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVector>
#include <QWidget>

namespace fincept::screens {

// ── NavChart ──────────────────────────────────────────────────────────────────
/// Custom QPainter line chart for estimated portfolio NAV series.
/// No QtCharts dependency — draws directly with QPainter.
class NavChart : public QWidget {
    Q_OBJECT
public:
    explicit NavChart(QWidget* parent = nullptr);

    void set_series(const QVector<power_trader::NavPoint>& series);
    void set_period(const QString& period); // "3M", "6M", "1Y", "ALL"

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QVector<power_trader::NavPoint> full_series_;
    QVector<power_trader::NavPoint> visible_series_;
    QString period_ = QStringLiteral("1Y");
    QPoint  hover_pos_;
    bool    hover_active_ = false;

    void rebuild_visible();
    QVector<power_trader::NavPoint> filtered_series(const QString& period) const;
};

// ── SectorPieChart ────────────────────────────────────────────────────────────
/// QPainter donut chart for sector allocation.
class SectorPieChart : public QWidget {
    Q_OBJECT
public:
    struct Slice { QString sector; double pct; QColor color; double corr_score; };
    explicit SectorPieChart(QWidget* parent = nullptr);
    void set_slices(const QVector<Slice>& slices);
    QSize sizeHint() const override { return {220, 220}; }
    QSize minimumSizeHint() const override { return {160, 160}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVector<Slice> slices_;
};

// ── MemberProfilePanel ────────────────────────────────────────────────────────
/// Full-detail profile panel for a single Congress member.
/// Displayed inside the POWER TRADER screen when a member row is clicked.
/// The widget IS a QScrollArea — the outer scroll area is the top-level widget.
class MemberProfilePanel : public QWidget {
    Q_OBJECT
public:
    explicit MemberProfilePanel(QWidget* parent = nullptr);

    void set_member(const power_trader::CongressMember& member);
    void clear();
    void refresh_theme();

signals:
    void navigate_to_markets(QString ticker);

private:
    void build_ui();
    void populate(const power_trader::CongressMember& member,
                  const power_trader::MemberPortfolio& portfolio,
                  const QVector<power_trader::CommitteeInsiderSignal>& insider_signals,
                  const QVector<power_trader::PoliticalTrade>& trades);
    void show_placeholder();

    // ── Sections ──────────────────────────────────────────────────────────────
    void build_header_bar(QWidget* parent, QVBoxLayout* vl);
    void build_stat_tiles(QWidget* parent, QVBoxLayout* vl);
    void build_chart_section(QWidget* parent, QVBoxLayout* vl);
    void build_holdings_table(QWidget* parent, QVBoxLayout* vl);
    void build_trades_section(QWidget* parent, QVBoxLayout* vl);
    void build_ranking_section(QWidget* parent, QVBoxLayout* vl);
    void build_committees_section(QWidget* parent, QVBoxLayout* vl);
    void build_sector_section(QWidget* parent, QVBoxLayout* vl);
    void build_insights_section(QWidget* parent, QVBoxLayout* vl);

    void populate_header(const power_trader::CongressMember& m,
                         const power_trader::MemberPortfolio& p);
    void populate_stat_tiles(const power_trader::CongressMember& m,
                             const power_trader::MemberPortfolio& p);
    void populate_chart(const power_trader::MemberPortfolio& p,
                        const QVector<power_trader::CommitteeInsiderSignal>& insider_signals,
                        const QStringList& committees);
    void populate_holdings(const power_trader::MemberPortfolio& p);
    void populate_trades(const QVector<power_trader::PoliticalTrade>& trades);
    void populate_rankings(const QString& member_id);
    void populate_committees(const power_trader::CongressMember& m,
                             const QVector<power_trader::CommitteeInsiderSignal>& insider_sigs,
                             const QVector<power_trader::PoliticalTrade>& trades);
    void populate_sector(const power_trader::MemberPortfolio& portfolio,
                         const QVector<power_trader::CommitteeInsiderSignal>& insider_sigs);
    void populate_insights(const power_trader::CongressMember& m,
                           const power_trader::MemberPortfolio& portfolio,
                           const QVector<power_trader::PoliticalTrade>& trades);

    static QString fmt_dollar(double v);   // also used by NavChart (via free fn)
    static QString fmt_pct(double v, bool show_sign = true);

    // ── Top-level layout ──────────────────────────────────────────────────────
    QWidget*      placeholder_   = nullptr;
    QScrollArea*  scroll_area_   = nullptr;
    QWidget*      content_       = nullptr;

    // ── Section 1: header bar ─────────────────────────────────────────────────
    QLabel* name_label_          = nullptr;
    QLabel* party_badge_         = nullptr;
    QLabel* meta_label_          = nullptr;
    QLabel* net_worth_label_     = nullptr;
    QWidget* rank_chips_         = nullptr;    // container for top-3 rank badges

    // ── Section 2: stat tiles ─────────────────────────────────────────────────
    QLabel* tile_portfolio_val_  = nullptr;
    QLabel* tile_ytd_return_     = nullptr;
    QLabel* tile_alpha_          = nullptr;
    QLabel* tile_cmte_trades_    = nullptr;

    // ── Section 3: chart + committee exposure ─────────────────────────────────
    NavChart*  nav_chart_        = nullptr;
    QWidget*   period_bar_       = nullptr;    // row of 3M/6M/1Y/ALL buttons
    QWidget*   cmte_exposure_    = nullptr;    // right panel container
    QSplitter* chart_splitter_   = nullptr;

    // ── Section 4: holdings table ─────────────────────────────────────────────
    QTableWidget* holdings_table_ = nullptr;

    // ── Section 5: recent trades ──────────────────────────────────────────────
    QTableWidget* trades_table_   = nullptr;

    // ── Section 6: ranking comparison ─────────────────────────────────────────
    QWidget* rank_cards_row_      = nullptr;

    // ── Section 7: committees ─────────────────────────────────────────────────
    QWidget* committees_container_ = nullptr;

    // ── Section 8: sector allocation ─────────────────────────────────────────
    SectorPieChart* sector_pie_   = nullptr;
    QWidget*       sector_legend_ = nullptr;
    QLabel*        insider_score_ = nullptr;  // "Insider Index: 74/100"

    // ── Section 9: trader insights ────────────────────────────────────────────
    QWidget* insights_container_  = nullptr;

    // ── State ─────────────────────────────────────────────────────────────────
    power_trader::CongressMember current_member_;
};

} // namespace fincept::screens
