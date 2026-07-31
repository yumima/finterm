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
/// Three horizons, in the order a analyst reads them:
///   PAST     what the company has actually delivered, and how the stock
///            traded on each print (surprise + close-to-close reaction);
///   CURRENT  where consensus sits today and which way it is being revised;
///   FUTURE   the forward estimates and the growth they imply.
///
/// On top of those sits a rules-based BUY / HOLD / SELL scorecard
/// (services::equity::evaluate_earnings) whose every leg is shown with the
/// number it came from — the verdict is a summary of the panels below it, not
/// an oracle, and the tab says so. The ×weights on those legs move with the
/// days left before the print, so the card states which way they have rotated
/// rather than leaving a reader to wonder why the same leg is worth more on
/// one symbol than another.
class EquityEarningsTab : public QWidget {
    Q_OBJECT
  public:
    explicit EquityEarningsTab(QWidget* parent = nullptr);
    void set_symbol(const QString& symbol);

  private:
    void build_ui();
    void apply_state(const services::query::QueryStore::State& s);
    void populate(const services::equity::EarningsAnalysis& a);
    void show_message(const QString& text);

    // Section builders — each returns a panel added to the scroll column.
    QWidget* build_summary_row();
    QWidget* build_scorecard();
    /// PAST section: the quarter table and, beside it, the same quarters
    /// plotted against what the stock did on each print.
    QWidget* build_history_panel();
    QWidget* build_current_panel();
    QWidget* build_future_panel();

    void fill_history(const services::equity::EarningsAnalysis& a,
                      const services::equity::EarningsVerdict& v);
    void fill_trend(const services::equity::EarningsAnalysis& a);
    void fill_revisions(const services::equity::EarningsAnalysis& a);
    void fill_estimates(const services::equity::EarningsAnalysis& a);
    void fill_growth(const services::equity::EarningsAnalysis& a);
    void fill_scorecard(const services::equity::EarningsVerdict& v);
    /// Write today's reading of the scorecard to the ledger, and settle any
    /// earlier readings whose print has since happened.
    void record_and_resolve(const services::equity::EarningsAnalysis& a,
                            const services::equity::EarningsVerdict& v);
    void fill_track_record(const services::equity::EarningsAnalysis& a);
    QWidget* build_track_record_panel();

    QString current_symbol_;
    QString currency_ = QStringLiteral("USD");

    // Next-report card
    QLabel* next_date_ = nullptr;
    QLabel* next_countdown_ = nullptr;
    QLabel* next_confirmed_ = nullptr;
    QLabel* next_eps_ = nullptr;
    QLabel* next_eps_range_ = nullptr;
    QLabel* next_rev_ = nullptr;
    QLabel* next_yoy_ = nullptr;

    // Verdict card
    QLabel* verdict_badge_ = nullptr;
    QLabel* verdict_score_ = nullptr;
    QLabel* verdict_confidence_ = nullptr;
    QLabel* verdict_headline_ = nullptr;
    // Why the ×weights on the breakdown below differ from one symbol to the
    // next: they rotate with the time left before the print.
    QLabel* verdict_horizon_ = nullptr;
    // The composite split into its two halves — how the business reads, and
    // how much of that is already in the price.
    QLabel* verdict_axes_ = nullptr;

    // Setup card
    QLabel* setup_move_ = nullptr;
    QLabel* setup_beat_ = nullptr;
    QLabel* setup_reaction_ = nullptr;
    QLabel* setup_runup_ = nullptr;
    QLabel* setup_runup20_ = nullptr;
    QLabel* setup_spread_ = nullptr;

    // Scorecard rows are rebuilt per symbol.
    QVBoxLayout* score_rows_layout_ = nullptr;
    QLabel* caveats_label_ = nullptr;

    QTableWidget* history_table_ = nullptr;
    QLabel* history_summary_ = nullptr;

    // Reaction chart + its metric switch. Each button shows that metric's
    // measured correlation with the next-session move, so the choice is
    // informed rather than blind.
    EarningsReactionChart* reaction_chart_ = nullptr;
    QHash<services::equity::ReactionMetric, QPushButton*> metric_buttons_;
    QLabel* correlation_note_ = nullptr;
    services::equity::ReactionMetric selected_metric_ = services::equity::ReactionMetric::QoQ;
    void set_metric(services::equity::ReactionMetric m);
    void fill_correlations(const services::equity::EarningsAnalysis& a);
    QTableWidget* trend_table_ = nullptr;
    QTableWidget* revisions_table_ = nullptr;
    QTableWidget* estimates_table_ = nullptr;
    QTableWidget* growth_table_ = nullptr;
    QTableWidget* track_table_ = nullptr;
    QLabel* track_note_ = nullptr;

    // Shown instead of the panels when the symbol has no earnings at all
    // (ETFs, indices, funds) or the fetch failed.
    QLabel* message_label_ = nullptr;
    QWidget* content_widget_ = nullptr;
    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
