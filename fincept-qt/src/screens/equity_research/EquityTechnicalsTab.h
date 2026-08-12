// src/screens/equity_research/EquityTechnicalsTab.h
#pragma once
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QDateTime>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

namespace fincept::screens {

class EquityTechnicalsTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityTechnicalsTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);
    /// When the displayed data was actually fetched upstream (QueryStore's
    /// State::fetched_at). Invalid when nothing is displayed. The screen's
    /// freshness chip prefers this over its signal-arrival fallback, which a
    /// cache hit restamps as "just now".
    QDateTime data_as_of() const { return data_fetched_at_; }
    /// Re-warm the entry this tab is actually displaying — its own (symbol,
    /// period, interval), not the daily default. The screen calls this on tab
    /// activation; calling fetch_technicals(symbol) there instead silently
    /// refreshed a daily entry nobody was looking at while a weekly selection
    /// aged toward its stale window.
    void refresh();

  private:
    void apply_technicals_state(const services::query::QueryStore::State& s);

    void build_ui();
    void populate(const services::equity::TechnicalsData& data);
    void clear_sections();
    /// Blank every panel back to the empty state (rating dash, zero counts).
    /// Used when an error would otherwise leave another symbol's rating on
    /// screen under the current symbol's header.
    void reset_panels();
    /// Render the "which bar, fetched when, stale or not" line under the
    /// rating — the only honest answer to "is this current?".
    void update_as_of(const services::equity::TechnicalsData& data,
                      const services::query::QueryStore::State& s);
    void switch_period(QPushButton* btn, const QString& period);
    /// Switch the bar interval the indicators are derived from ("1d" / "1wk").
    void switch_interval(QPushButton* btn, const QString& interval);
    /// (Re)bind the technicals subscription to the current symbol/period/interval.
    void rebind();

    static QString signal_text(services::equity::TechSignal s);
    /// Tooltip explaining why a displayed indicator is excluded from the tally.
    static QString non_voting_note(const QString& col_key);
    static const char* signal_color(services::equity::TechSignal s);
    static QString interpretation(const QString& col_key, double value);
    static QString col_key_for(const QString& name);
    static QString indicator_help(const QString& col_key);
    static QString period_btn_style_active();
    static QString period_btn_style_inactive();

    QString current_symbol_;
    /// QueryStore key of the data the panels currently render — stamped on
    /// every successful delivery, compared by the error path.
    /// current_technicals_key_ moves ahead of it on set_symbol() /
    /// switch_period() / switch_interval(); if the fetch for the new key fails
    /// while the panels still show the old one, keeping them up would present
    /// another symbol's — or another interval's — rating under the newly
    /// active header and buttons.
    QString displayed_key_;
    /// The as-of line without its STALE suffix, kept so the error path can
    /// re-render "refresh failed" over a line that promised "refreshing…".
    QString as_of_base_;
    /// Backing for data_as_of().
    QDateTime data_fetched_at_;
    QString current_period_ = "1y";
    // Bar interval the indicators are computed on. Unlike the period, this
    // genuinely changes the verdict — weekly bars describe the multi-month
    // trend where daily bars describe the multi-week one.
    QString current_interval_ = "1d";
    // The QueryStore key the current subscription is bound to. Tracked so
    // set_symbol() / switch_period() can drop the prior subscription before
    // rebinding — without this, a late resolve for the previous (symbol,
    // period) combination would still deliver and overwrite the panels.
    QString current_technicals_key_;

    // Period buttons
    QPushButton* btn_1m_ = nullptr;
    QPushButton* btn_3m_ = nullptr;
    QPushButton* btn_6m_ = nullptr;
    QPushButton* btn_1y_ = nullptr;
    QPushButton* btn_5y_ = nullptr;
    QPushButton* active_period_btn_ = nullptr;
    QPushButton* btn_daily_ = nullptr;
    QPushButton* btn_weekly_ = nullptr;
    QPushButton* active_interval_btn_ = nullptr;

    // Rating panel
    QLabel* rating_label_ = nullptr;
    QProgressBar* gauge_bar_ = nullptr;
    QLabel* strong_buy_count_ = nullptr;
    QLabel* buy_count_ = nullptr;
    QLabel* neutral_count_ = nullptr;
    QLabel* sell_count_ = nullptr;
    QLabel* strong_sell_count_ = nullptr;
    QLabel* total_label_ = nullptr;
    // Per-bucket contributions behind the verdict, so the number is arguable
    // rather than an oracle.
    QLabel* basis_label_ = nullptr;
    // States how well the panel does at describing the past and at predicting
    // the future. Both numbers, plainly, under the verdict. See signal_text().
    QLabel* accuracy_label_ = nullptr;
    // Which bar the verdict describes and when the data was fetched — with a
    // still-forming marker for today's bar / the current week, and a STALE
    // marker when the store served past-TTL data. See update_as_of().
    QLabel* as_of_label_ = nullptr;
    // Only visible when an indicator stage was lost on the Python side.
    QLabel* warning_label_ = nullptr;

    // Key indicators container
    QWidget* key_container_ = nullptr;

    // Category sections container
    QWidget* sections_container_ = nullptr;

    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
