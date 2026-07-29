// src/screens/dashboard/widgets/EarningsCalendarWidget.h
#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"
#include "screens/portfolio/PortfolioTypes.h"

#include <QComboBox>
#include <QDate>
#include <QFrame>
#include <QHash>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>

namespace fincept::screens::widgets {

/// Earnings Calendar Widget — two views over upcoming earnings prints:
///
///   THIS WEEK  every company reporting between today and Friday, from the
///              Nasdaq public earnings calendar (no API key), ranked by market
///              cap so the names that move the tape are at the top. Rows you
///              hold are marked.
///   PORTFOLIO  the next print for each holding across the selected
///              portfolios, from yfinance's per-ticker earnings dates, with
///              each name's weight in the combined book. A report three weeks
///              out still shows up — the week view would miss it.
///
/// The estimate is coloured by expected growth (consensus vs the year-ago
/// quarter) and carries a badge with last quarter's surprise. Nothing is
/// coloured "beat/miss" before the company has actually reported.
///
/// View and portfolio selection are per-instance config, so two tiles can
/// watch different books.
class EarningsCalendarWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit EarningsCalendarWidget(const QJsonObject& cfg = {}, QWidget* parent = nullptr);

    QJsonObject config() const override;
    void apply_config(const QJsonObject& cfg) override;

  protected:
    void on_theme_changed() override;
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

  private:
    enum class Mode { ThisWeek, Portfolio };

    struct Entry {
        QDate date;
        QString symbol;
        QString company;
        QString when;    // "BMO" / "AMC" (week view) or "in 5d" (portfolio view)
        double eps_est = 0;
        bool has_est = false;
        double eps_ly = 0; // year-ago quarter's EPS — the growth comparison
        bool has_ly = false;
        // Last quarter that was actually reported: what it printed, what was
        // expected of it, and the resulting surprise. All three belong to that
        // past quarter — none of them relates to the estimate shown alongside.
        double surprise_pct = 0;
        bool has_surprise = false;
        QDate prev_date;
        double prev_actual = 0;
        double prev_estimate = 0;
        bool has_prev_values = false;
        // Which consensus panel the estimate/year-ago pair came from. Nasdaq
        // and Yahoo publish different numbers for the same print, so a row must
        // never mix them — see growth_verdict() in the .cpp.
        bool est_from_yf = false;
        double rank = 0; // market cap — orders the week view within a day
        QString fiscal_quarter;
        int num_ests = 0;
        QString ly_report_date;
        bool held = false;
        double weight = 0; // % of the combined selected book
        QStringList portfolios;
    };

    // Per-portfolio holdings snapshot, keyed by portfolio id. Kept per source
    // so a re-emit for one portfolio can't double-count the others.
    struct HoldingRow {
        QString symbol;
        double market_value = 0;
    };

    void build_body();
    void build_portfolio_selector();
    void apply_styles();
    void set_mode(Mode m);
    void refresh_data();
    void populate();
    void show_status(const QString& text);

    // ── This week (Nasdaq calendar) ──────────────────────────────────────────
    void load_week();
    void fetch_day(const QDate& date, qint64 epoch);
    void week_fetches_done();

    // ── Holdings (both views) ────────────────────────────────────────────────
    void load_holdings();
    void on_portfolios_loaded(QVector<portfolio::Portfolio> portfolios);
    void on_summary_loaded(const portfolio::PortfolioSummary& summary);
    void rebuild_held_index();
    void on_holdings_ready();

    // ── Per-symbol earnings dates (yfinance) ─────────────────────────────────
    void fetch_symbol_earnings(const QStringList& symbols, qint64 epoch, bool portfolio_view);
    void apply_symbol_result(const QString& symbol, const QJsonObject& result, bool portfolio_view);

    // ── Portfolio selector ───────────────────────────────────────────────────
    void rebuild_portfolio_model();
    void on_portfolio_item_changed(QStandardItem* item);
    void update_selector_text();
    QStringList effective_portfolio_ids() const;

    Mode mode_ = Mode::ThisWeek;

    QPushButton* week_btn_ = nullptr;
    QPushButton* portfolio_btn_ = nullptr;
    QComboBox* portfolio_combo_ = nullptr;
    QStandardItemModel* portfolio_model_ = nullptr;
    QWidget* header_widget_ = nullptr;
    QFrame* header_sep_ = nullptr;
    QScrollArea* scroll_area_ = nullptr;
    QVBoxLayout* list_layout_ = nullptr;
    QLabel* status_label_ = nullptr;
    QVector<QLabel*> header_labels_;
    QLabel* surprise_header_ = nullptr;
    QLabel* weight_header_ = nullptr; // hidden in the week view
    QTimer* selection_debounce_ = nullptr;

    QVector<Entry> entries_;
    QString note_;

    // Fetch epoch: bumped on every (re)load so a fan-out still in flight when
    // the user flips views or portfolios can't paint over the newer request.
    qint64 epoch_ = 0;
    int pending_days_ = 0;
    int pending_symbols_ = 0;
    int ok_days_ = 0;
    int ok_symbols_ = 0;
    bool week_rows_ready_ = false;

    QVector<portfolio::Portfolio> portfolios_;
    QStringList selected_ids_;
    bool all_portfolios_ = false;
    bool portfolios_loaded_ = false;
    bool suppress_item_signal_ = false;
    bool loaded_once_ = false;

    QHash<QString, QVector<HoldingRow>> holdings_by_portfolio_;
    QSet<QString> awaiting_summaries_;
    // symbol → combined market value across the selected portfolios
    QHash<QString, double> held_value_;
    QHash<QString, QStringList> held_portfolios_;
    double held_total_mv_ = 0;

    // Nasdaq returns ~300 rows a day, nearly all micro-caps. Keep the biggest
    // names — populate() says how many it dropped rather than truncating
    // silently.
    static constexpr int kMaxWeekRows = 120;
    // One yfinance call per symbol — bound the fan-out on large books.
    static constexpr int kMaxSymbolFetches = 50;
};

} // namespace fincept::screens::widgets
