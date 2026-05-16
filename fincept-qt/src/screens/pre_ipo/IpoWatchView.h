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
#include <QSet>
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
class QTabWidget;
class QVBoxLayout;
QT_FORWARD_DECLARE_CLASS(QChartView)

namespace fincept::screens::widgets {

class IpoWatchView : public QWidget {
    Q_OBJECT
  public:
    explicit IpoWatchView(QWidget* parent = nullptr);

  public slots:
    void refresh();

  protected:
    /// Click handler for KPI cells — wraps each cell as a left-click target
    /// that switches the time-window filter (e.g. clicking "THIS WEEK"
    /// selects TW_ThisWeek). Cells store the target window in property
    /// "kpiTw"; the filter is also written through to window_chip_ so the
    /// chip and KPI stay in sync.
    bool eventFilter(QObject* obj, QEvent* ev) override;

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
        QString status;           // "upcoming" | "filed" | "priced"
        // For filed entries, `date` carries the filedDate. We expose both
        // semantic names so the detail rail can label the field correctly.
        QDate   filed_date;       // mirror of date when status == "filed"
        // Performance enrichment via yfinance batch_quotes — pre-fetched on load.
        bool   perf_fetched = false;
        double last_price   = 0;
        double pop_pct      = 0;  // (last - final) / final * 100, priced only
        // Company profile via yfinance batch_info — pre-fetched on load too.
        // Used for SECTOR column and same-sector comps in the detail rail.
        bool    profile_fetched = false;
        QString sector;
        QString industry;
        QString website;
    };

    /// Filter state.
    /// BOOKRUNNERS lens removed — Nasdaq's public IPO calendar doesn't carry
    /// `leadFirmName`. We keep the field on the Entry struct for a future
    /// SEC EDGAR-driven source but the lens shipped only "Unknown" rows.
    enum Lens { LensCalendar = 0, LensPerformance, LensWatchlist, LensCount };
    enum TimeWindow { TW_AllUpcoming = 0, TW_ThisWeek, TW_30Days, TW_3Months,
                      TW_6Months, TW_12Months, TW_Past30Days };
    // Tab indexes for the two detail-rail tab bars — kept in sync with the
    // addTab order in build_detail_rail. Use these instead of magic numbers
    // so reorders are caught at compile time.
    enum FactTab  { FT_Deal = 0, FT_Business, FT_Leadership, FT_Fundamentals,
                    FT_News, FT_Holders, FT_Filings, FT_Funding, FT_Pipeline };
    enum ChartTab { CT_Price = 0, CT_Revenue, CT_Range, CT_Lockup, CT_Timeline };

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
    void enrich_priced_with_quotes(); // single batch_quotes, populates pop_pct
    void enrich_with_profiles();      // single batch_info, populates sector/industry

    // ── Rendering ───────────────────────────────────────────────────────────
    void render();                  // dispatch by active_lens_
    void render_calendar();
    void render_performance();
    void render_watchlist();
    void render_kpis();
    void render_detail(const Entry* e); // nullptr clears the rail
    // Per-section detail rail builders — each populates one tab page or pane.
    // Split out so render_detail isn't a 200-line monolith and so individual
    // sections can be unit-tested or reused later.
    QString build_deal_html(const Entry& e) const;
    QString build_business_html(const Entry& e, const services::InfoData& info, bool have_info) const;
    QString build_leadership_html(const services::InfoData& info, bool have_info) const;
    QString build_fundamentals_html(const services::InfoData& info, bool have_info, const Entry& e) const;
    QString build_pipeline_html(const Entry& e) const;
    QString build_range_html(const Entry& e) const;
    QString build_lockup_html(const Entry& e) const;
    QString build_timeline_html(const Entry& e) const;
    QString build_sector_comps_html(const Entry& e) const;
    QString build_links_html(const Entry& e, const services::InfoData& info, bool have_info) const;
    QString build_news_html(const Entry& e) const;
    QString build_holders_html(const Entry& e) const;
    QString build_filings_html(const Entry& e) const;
    QString build_funding_html(const Entry& e) const;
    void    rebuild_price_chart(const Entry& e);   // mutates price_chart_view_
    void    rebuild_revenue_chart(const Entry& e); // mutates revenue_chart_view_

    // Lazy fetchers fired from render_detail when their tab data is missing.
    void fetch_ipo_extras_for_detail(const Entry& e);
    void fetch_sec_filings_for_detail(const Entry& e);
    void fetch_wikipedia_for_detail(const Entry& e);
    void fetch_s1_funding_for_detail(const Entry& e);
    QVector<int> filtered_indices() const;     // applies filter chips + search
    /// `apply_status` — set false from the PERFORMANCE lens, which is by
    /// definition "priced only" and shouldn't be silently emptied when the
    /// default status chip is UPCOMING.
    bool entry_passes_filters(const Entry& e, bool apply_status = true) const;

    // ── Detail-rail enrichment ──────────────────────────────────────────────
    /// Kick off a one-shot fetch_history for the priced entry's ticker so the
    /// detail rail can render an inline price-since-IPO sparkline. The fetch
    /// is gated by history_cache_ so opening the same detail twice is free.
    void fetch_history_for_detail(const Entry& e);

    // ── Watchlist ───────────────────────────────────────────────────────────
    /// Stable key for the starred set — prefer the ticker, fall back to
    /// company name so upcoming deals with no ticker yet can still be saved.
    static QString starred_key(const Entry& e);
    void load_starred();
    void save_starred();
    void toggle_star(const Entry& e);
    bool is_starred(const Entry& e) const { return starred_.contains(starred_key(e)); }

    // Helpers
    static QString format_money(double dollars);
    static QString format_deal_size(const QString& price_range, const QString& shares);
    static double  parse_price_mid(const QString& price_range, bool* ok = nullptr);
    static double  parse_shares(const QString& shares);

    // ── State ───────────────────────────────────────────────────────────────
    QVector<Entry> entries_;
    int pending_fetches_ = 0;
    // Lazy per-symbol info cache for the detail rail. fetch_info returns
    // P/E, margins, growth, beta, analyst targets, etc. — too expensive to
    // pre-fetch for every entry but quick on a single row click.
    QHash<QString, services::InfoData> info_cache_;
    // Heavier per-symbol enrichment caches — populated lazily on row click,
    // not pre-fetched. NEWS / FINANCIALS / HOLDERS tabs share `ipo_extras_`;
    // FILINGS uses `filings_cache_`; Wikipedia fallback for the BUSINESS
    // tab lives in `wiki_cache_`.
    QHash<QString, services::MarketDataService::IpoExtras> ipo_extras_;
    QSet<QString>                                          ipo_extras_inflight_;
    QHash<QString, QVector<services::MarketDataService::SecFiling>> filings_cache_;
    QSet<QString>                                          filings_inflight_;
    QHash<QString, services::MarketDataService::WikipediaSummary> wiki_cache_;
    QSet<QString>                                          wiki_inflight_;
    // "wiki tried but failed" set — keep so we don't repeatedly hit the
    // REST API for pre-IPO companies that simply have no Wikipedia page.
    QSet<QString>                                          wiki_misses_;
    // Parsed S-1 "Recent Sales of Unregistered Securities" sections — the
    // free, public surrogate for the funding-round history that NPM / Hiive
    // sell. Keyed by ticker; misses kept separately so we don't re-parse
    // huge S-1 documents when the section is genuinely absent.
    QHash<QString, services::MarketDataService::S1Funding> s1_cache_;
    QSet<QString>                                          s1_inflight_;
    QSet<QString>                                          s1_misses_;
    // Lazy per-symbol price history cache for the detail rail's price-since-IPO
    // sparkline. Keyed by ticker. Presence in `history_cache_` means the fetch
    // returned (even if empty = "yfinance had nothing"); presence in
    // `history_inflight_` means the request is mid-flight and we shouldn't
    // re-issue it. They are mutually exclusive: callback drops from inflight
    // and writes to the cache atomically.
    QHash<QString, QVector<double>> history_cache_;
    QSet<QString>                   history_inflight_;
    QString detail_symbol_; // currently shown in the detail rail
    // Starred-ticker set, persisted under SettingsRepository key
    // `ipo_watch.starred`. Each entry is either the ticker (preferred) or
    // company name for upcoming deals with no ticker yet — see starred_key().
    QSet<QString> starred_;
    Lens       active_lens_   = LensCalendar;
    TimeWindow time_window_   = TW_AllUpcoming;
    // Default to UPCOMING because the screen's primary value is forward
    // pipeline research — past priced deals are the COMPS context, not the
    // headline. User can flip to ALL / PRICED via the status chip.
    QString    status_filter_ = "upcoming"; // all | upcoming | priced
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

    // ── Detail rail (3-pane Bloomberg-style) ────────────────────────────────
    // Outer splitter children: [table_, middle_pane_, right_pane_]
    // middle_pane_ vertical: [tabs_facts_, tabs_charts_]
    // right_pane_  vertical: [right_top_, right_bottom_]
    QWidget*      middle_pane_   = nullptr;
    QWidget*      right_pane_    = nullptr;
    QLabel*       header_lbl_    = nullptr;  // company name + chips above the tabs
    /// "Open in Equity Research →" — only visible when a priced row is
    /// selected. Publishes the same EventBus pair PortfolioBlotter uses
    /// (`nav.split_alongside` + `equity_research.load_symbol`), so the full
    /// Equity Research screen docks alongside IPO Watch with the ticker
    /// already loaded.
    QPushButton*  er_btn_        = nullptr;

    QTabWidget*   tabs_facts_    = nullptr;
    QLabel*       page_deal_     = nullptr;
    QLabel*       page_business_ = nullptr;
    QLabel*       page_leader_   = nullptr;
    QLabel*       page_fund_     = nullptr;
    QLabel*       page_pipeline_ = nullptr;
    QLabel*       page_news_     = nullptr;
    QLabel*       page_holders_  = nullptr;
    QLabel*       page_filings_  = nullptr;
    QLabel*       page_funding_  = nullptr;

    QTabWidget*   tabs_charts_   = nullptr;
    QWidget*      page_price_chart_host_   = nullptr; // QVBoxLayout host for the QChartView
    QWidget*      page_revenue_chart_host_ = nullptr; // QVBoxLayout host for revenue bar chart
    QChartView*   price_chart_view_        = nullptr; // recreated each render
    QChartView*   revenue_chart_view_      = nullptr;
    QLabel*       page_range_    = nullptr;
    QLabel*       page_lockup_   = nullptr;
    QLabel*       page_timeline_ = nullptr;

    QLabel*       right_top_     = nullptr;  // sector heat + comps
    QLabel*       right_bottom_  = nullptr;  // research links
};

} // namespace fincept::screens::widgets
