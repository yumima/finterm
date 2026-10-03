// src/screens/equity_research/EquityEarningsTab.h
#pragma once
#include "screens/equity_research/EarningsReactionChart.h"
#include "services/equity/EarningsSignal.h"
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace fincept::screens {

/// ER "Earnings" tab — the pre-report desk view.
///
/// Leads with the three questions anyone asks before a print, each answered
/// only as far as the evidence goes (services::equity::evaluate_outlook):
///
///   WILL THEY BEAT?  a calibrated probability, the four ways the print can
///                    land, and which way the street is moving its number;
///   HOW BIG A MOVE?  the one-session size forecast with the ranges it
///                    actually achieves, beside what options are pricing;
///   WHICH WAY?       deliberately no call — what each outcome has meant for
///                    the price, and what is already priced in.
///
/// Below that: the history (chart with the forecast band, quarter table), this
/// name's own forecast record, and the consensus detail the answers draw on.
class EquityEarningsTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityEarningsTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);
    /// When the displayed analysis was actually fetched upstream. Invalid when
    /// nothing is displayed. Feeds the screen's freshness chip.
    QDateTime data_as_of() const { return data_fetched_at_; }

  private:
    void build_ui();
    void apply_state(const services::query::QueryStore::State& s);
    void populate(const services::equity::EarningsAnalysis& a);
    void show_message(const QString& text);

    // Section builders.
    QWidget* build_next_report();
    QWidget* build_answers_row();
    QWidget* build_chart_panel();
    QWidget* build_history_panel();
    QWidget* build_current_panel();
    QWidget* build_future_panel();

    void fill_next_report(const services::equity::EarningsAnalysis& a,
                          const services::equity::EarningsOutlook& o);
    void fill_beat(const services::equity::EarningsOutlook& o);
    void fill_size(const services::equity::EarningsAnalysis& a,
                   const services::equity::EarningsOutlook& o);
    void fill_direction(const services::equity::EarningsOutlook& o);
    void fill_history(const services::equity::EarningsAnalysis& a,
                      const services::equity::EarningsOutlook& o,
                      const services::equity::ForecastRecord& r);
    void fill_record(const services::equity::EarningsAnalysis& a,
                     const services::equity::ForecastRecord& r);
    void fill_trend(const services::equity::EarningsAnalysis& a);
    void fill_revisions(const services::equity::EarningsAnalysis& a);
    void fill_estimates(const services::equity::EarningsAnalysis& a);
    /// Write today's outlook to the ledger, and settle any earlier readings
    /// whose print has since happened. The ledger is what lets the beat
    /// probability and the options-implied move be checked on prints that
    /// were genuinely in the future when they were written down.
    void record_and_resolve(const services::equity::EarningsAnalysis& a,
                            const services::equity::EarningsOutlook& o);

    QString current_symbol_;
    QDateTime data_fetched_at_;
    QString currency_ = QStringLiteral("USD");

    // Next report strip
    QLabel* next_date_ = nullptr;
    QLabel* next_countdown_ = nullptr;
    QLabel* next_confirmed_ = nullptr;
    QLabel* next_eps_ = nullptr;
    QLabel* next_eps_range_ = nullptr;
    QLabel* next_rev_ = nullptr;
    QLabel* next_yoy_ = nullptr;
    QLabel* next_analysts_ = nullptr;
    QLabel* headline_ = nullptr;

    // 1 · Will they beat?
    QLabel* beat_value_ = nullptr;
    QLabel* beat_sub_ = nullptr;
    QWidget* beat_strip_ = nullptr;      // stacked bar of the four outcomes
    QLabel* beat_strip_legend_ = nullptr;
    QLabel* beat_drift_ = nullptr;

    // 2 · How big a move?
    QLabel* size_value_ = nullptr;
    QLabel* size_dollars_ = nullptr;
    QLabel* size_ranges_ = nullptr;
    QLabel* size_implied_ = nullptr;
    QLabel* size_history_ = nullptr;

    // 3 · Which way?
    QTableWidget* scenario_table_ = nullptr;
    QLabel* scenario_note_ = nullptr;
    QLabel* priced_in_ = nullptr;
    QLabel* caveats_label_ = nullptr;

    // History
    EarningsReactionChart* reaction_chart_ = nullptr;
    QHash<services::equity::ReactionMetric, QPushButton*> metric_buttons_;
    services::equity::ReactionMetric selected_metric_ = services::equity::ReactionMetric::Surprise;
    void set_metric(services::equity::ReactionMetric m);
    void fill_correlations(const services::equity::EarningsAnalysis& a);
    QLabel* chart_note_ = nullptr;
    QTableWidget* history_table_ = nullptr;
    QLabel* history_summary_ = nullptr;

    // Forecast record
    QLabel* record_size_ = nullptr;
    QLabel* record_beat_ = nullptr;
    QLabel* record_ledger_ = nullptr;

    QTableWidget* trend_table_ = nullptr;
    QTableWidget* revisions_table_ = nullptr;
    QTableWidget* estimates_table_ = nullptr;

    // Shown instead of the panels when the symbol has no earnings at all
    // (ETFs, indices, funds) or the fetch failed.
    QLabel* message_label_ = nullptr;
    QWidget* content_widget_ = nullptr;
    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
