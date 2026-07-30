// src/screens/equity_research/EquityEarningsTab.h
#pragma once
#include "services/equity/EarningsSignal.h"
#include "services/equity/EquityResearchModels.h"
#include "services/query/QueryStore.h"
#include "ui/widgets/LoadingOverlay.h"

#include <QLabel>
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
/// an oracle, and the tab says so.
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

    // Setup card
    QLabel* setup_move_ = nullptr;
    QLabel* setup_beat_ = nullptr;
    QLabel* setup_reaction_ = nullptr;
    QLabel* setup_runup_ = nullptr;

    // Scorecard rows are rebuilt per symbol.
    QVBoxLayout* score_rows_layout_ = nullptr;
    QLabel* caveats_label_ = nullptr;

    QTableWidget* history_table_ = nullptr;
    QLabel* history_summary_ = nullptr;
    QTableWidget* trend_table_ = nullptr;
    QTableWidget* revisions_table_ = nullptr;
    QTableWidget* estimates_table_ = nullptr;
    QTableWidget* growth_table_ = nullptr;

    // Shown instead of the panels when the symbol has no earnings at all
    // (ETFs, indices, funds) or the fetch failed.
    QLabel* message_label_ = nullptr;
    QWidget* content_widget_ = nullptr;
    ui::LoadingOverlay* loading_overlay_ = nullptr;
};

} // namespace fincept::screens
