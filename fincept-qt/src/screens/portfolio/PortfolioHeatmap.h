// src/screens/portfolio/PortfolioHeatmap.h
#pragma once
#include "screens/portfolio/PortfolioTypes.h"

#include <QHash>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QWidget>

namespace fincept::screens {

/// 220px left sidebar with color-coded holding blocks, risk gauge, and top movers.
class PortfolioHeatmap : public QWidget {
    Q_OBJECT
  public:
    explicit PortfolioHeatmap(QWidget* parent = nullptr);

    void set_holdings(const QVector<portfolio::HoldingWithQuote>& holdings);
    void set_metrics(const portfolio::ComputedMetrics& metrics);
    void set_selected_symbol(const QString& symbol);
    void set_currency(const QString& currency);
    void refresh_theme();

  signals:
    void symbol_selected(QString symbol);
    void mode_changed(portfolio::HeatmapMode mode);
    // Emitted when the "PORTFOLIO" title in the pane header is clicked.
    // Receivers (PortfolioScreen) clear any selected symbol and switch the
    // right-pane chart/blotter back to portfolio-level view — same effect
    // as the former "← PORTFOLIO" back pill.
    void portfolio_view_requested();
    // Mirror PortfolioBlotter — same actions, same screen-level dialogs.
    void edit_transaction_requested(QString symbol);
    void delete_position_requested(QString symbol);

  private:
    void build_ui();
    /// Re-build the grid structure. Only called when the symbol set or order
    /// changes. Reuses block widgets keyed by symbol — only new symbols cause
    /// widget allocation; removed symbols are deleted.
    void rebuild_blocks();
    /// Update text + stylesheet on every existing block. Cheap — no layout
    /// touching, no widget allocation. Called on data refresh, mode change,
    /// AFT-fetch completion, theme change.
    void refresh_block_appearances();
    /// Restyle exactly the previously-selected and newly-selected blocks.
    /// Cheap — touches at most two widgets, no layout touching.
    void restyle_selection(const QString& prev, const QString& next);
    /// Create one block widget for the given symbol, wire its click +
    /// context-menu signals, and return it. Does NOT add to the layout.
    QPushButton* create_block_widget(const QString& symbol);
    /// Set the text + stylesheet on a block based on the current mode + data.
    void update_block_appearance(QPushButton* block, const portfolio::HoldingWithQuote& h);
    QColor block_color(const portfolio::HoldingWithQuote& h) const;
    void update_detail();
    void update_risk_gauge();
    void update_top_movers();

    // Mode buttons
    QPushButton* pnl_btn_    = nullptr;
    QPushButton* weight_btn_ = nullptr;
    QPushButton* day_btn_    = nullptr;
    QPushButton* aft_btn_    = nullptr;  // after-hours (pre/post market change)

    // AFT cache — populated on demand via the yfinance daemon's
    // `extended_hours` action when the user enters AFT mode. Map of
    // symbol → post/pre-market percent change. Missing-key vs zero-value
    // is meaningful — missing renders neutral gray in block_color().
    QHash<QString, double> aft_quotes_;
    quint64  aft_gen_    = 0;             // supersedes in-flight stale requests
    void fetch_aft_quotes();

    // Blocks container
    QWidget* blocks_container_ = nullptr;
    // Symbol → block widget. Persists across refreshes so we don't churn
    // QPushButton objects (with their stylesheets, signals, and context-menu
    // lambdas) every time data ticks or the user toggles a mode.
    QHash<QString, QPushButton*> block_widgets_;

    // Selected holding detail
    QWidget* detail_panel_ = nullptr;
    QLabel* detail_symbol_ = nullptr;
    QLabel* detail_price_ = nullptr;
    QLabel* detail_change_ = nullptr;
    QLabel* detail_qty_ = nullptr;
    QLabel* detail_cost_ = nullptr;
    QLabel* detail_mv_ = nullptr;
    QLabel* detail_pnl_ = nullptr;
    QLabel* detail_pnl_pct_ = nullptr;
    QLabel* detail_weight_ = nullptr;

    // Risk gauge
    QLabel* risk_label_ = nullptr;
    QWidget* risk_bar_ = nullptr;
    QLabel* risk_value_ = nullptr;

    // Top movers
    QLabel* top_gainer_ = nullptr;
    QLabel* top_loser_ = nullptr;

    // Quick stats
    QLabel* stat_holdings_ = nullptr;
    QLabel* stat_conc_ = nullptr;
    QLabel* stat_vol_ = nullptr;

    // State
    QVector<portfolio::HoldingWithQuote> holdings_;
    portfolio::ComputedMetrics metrics_;
    portfolio::HeatmapMode mode_ = portfolio::HeatmapMode::Pnl;
    QString selected_symbol_;
    QString currency_ = "USD";
};

} // namespace fincept::screens
