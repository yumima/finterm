#pragma once
// IpoWatchView — Bloomberg-style IPO research terminal.
//
// Single consolidated screen with three lenses on the same Nasdaq calendar
// dataset:
//   CALENDAR    — time-ordered, upcoming first, priced below (default)
//   PERFORMANCE — priced deals sorted by Day-1 pop %, color-coded
//   BOOKRUNNERS — grouped by lead underwriter (banker view)
//
// Layout (wide screens 2:1 and up):
//   ┌─ title bar (lens tabs + refresh) ──────────────────────────────┐
//   │ KPI strip (week count/$, month count/$, 30D pop, range mix)    │
//   │ Filter chip row (status, window, exchange, size) + search box  │
//   ├──────────────────────────────────────────────────┬─────────────┤
//   │ Workspace (table for active lens)                │ Detail rail │
//   └──────────────────────────────────────────────────┴─────────────┘
//
// The detail rail collapses automatically when the splitter is dragged to
// 0; the filter row stays compact so it works on 3:2 / 16:9 laptops too.

#include "core/result/Result.h"
#include "services/markets/MarketDataService.h"

#include <QDate>
#include <QHash>
#include <QJsonDocument>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTableWidgetItem;
class QVBoxLayout;

namespace fincept::screens::widgets {

class IpoWatchView : public QWidget {
    Q_OBJECT
  public:
    explicit IpoWatchView(QWidget* parent = nullptr);

  public slots:
    void refresh();

  private:
    /// One IPO event. Mix of as-filed and as-priced fields.
    struct Entry {
        QString company;
        QString ticker;
        QString exchange;
        QDate   date;             // expectedPriceDate or pricedDate
        QString date_raw;
        QString price_range;      // e.g. "$15.00-$17.00" or "$17.00" for priced
        QString final_price_raw;  // priced only — original string from API
        double  final_price = 0;  // priced only — parsed dollars
        QString shares_raw;
        double  shares       = 0;
        QString deal_size;        // display string ($170M / $1.2B)
        double  deal_size_dollars = 0;
        QString bookrunner;       // primary lead firm
        QStringList all_bookrunners;
        QString status;           // "upcoming" | "priced"
        // Performance enrichment via yfinance batch_quotes — lazy.
        bool   perf_fetched = false;
        double last_price   = 0;
        double pop_pct      = 0;  // (last - final) / final * 100, priced only
    };

    /// Filter state.
    enum Lens { LensCalendar = 0, LensPerformance, LensBookrunners, LensCount };
    enum TimeWindow { TW_AllUpcoming = 0, TW_ThisWeek, TW_30Days, TW_6Months, TW_12Months, TW_Past30Days };

    static const char* lens_label(Lens l);

    // ── Building ────────────────────────────────────────────────────────────
    void build_ui();
    void build_title_bar(QVBoxLayout* root);
    void build_kpi_strip(QVBoxLayout* root);
    void build_filter_row(QVBoxLayout* root);
    void build_workspace(QVBoxLayout* root);
    void build_detail_rail(QSplitter* splitter);
    void apply_styles();

    // ── Networking ──────────────────────────────────────────────────────────
    void fetch_month(const QString& yyyymm);
    void on_fetch_done();
    void enrich_priced_with_quotes(); // lazy: triggered when PERFORMANCE lens is opened

    // ── Rendering ───────────────────────────────────────────────────────────
    void render();                  // dispatch by active_lens_
    void render_calendar();
    void render_performance();
    void render_bookrunners();
    void render_kpis();
    void render_detail(const Entry* e); // nullptr clears the rail
    QVector<int> filtered_indices() const;     // applies filter chips + search
    bool entry_passes_filters(const Entry& e) const;

    // Helpers
    static QString format_money(double dollars);
    static QString format_deal_size(const QString& price_range, const QString& shares);
    static double  parse_price_mid(const QString& price_range, bool* ok = nullptr);
    static double  parse_shares(const QString& shares);

    // ── State ───────────────────────────────────────────────────────────────
    QVector<Entry> entries_;
    int pending_fetches_ = 0;
    Lens       active_lens_   = LensCalendar;
    TimeWindow time_window_   = TW_AllUpcoming;
    QString    status_filter_ = "all";     // all | upcoming | priced
    QString    exch_filter_   = "all";     // all | NYSE | NASDAQ | …
    QString    size_filter_   = "all";     // all | <100M | 100-500M | 500M+
    QString    search_query_;

    // ── Widgets ─────────────────────────────────────────────────────────────
    QLabel*       title_lbl_   = nullptr;
    QPushButton*  lens_btns_[LensCount] = {nullptr};
    QPushButton*  refresh_btn_ = nullptr;
    QLabel*       status_lbl_  = nullptr;

    // KPI strip cells (left to right)
    QWidget* kpi_strip_   = nullptr;
    QLabel*  kpi_week_    = nullptr;
    QLabel*  kpi_month_   = nullptr;
    QLabel*  kpi_pop_     = nullptr;
    QLabel*  kpi_above_   = nullptr;
    QLabel*  kpi_in_      = nullptr;
    QLabel*  kpi_below_   = nullptr;

    // Filter chips
    QWidget*   filter_row_  = nullptr;
    QComboBox* status_chip_ = nullptr;
    QComboBox* window_chip_ = nullptr;
    QComboBox* exch_chip_   = nullptr;
    QComboBox* size_chip_   = nullptr;
    QLineEdit* search_      = nullptr;

    // Workspace + detail
    QSplitter*    splitter_      = nullptr;
    QTableWidget* table_         = nullptr;  // single table; columns/rows change per lens
    QWidget*      detail_pane_   = nullptr;
    QLabel*       detail_html_   = nullptr;  // RichText label with all sections
};

} // namespace fincept::screens::widgets
