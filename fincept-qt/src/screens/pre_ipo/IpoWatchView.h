#pragma once
// IpoWatchView — Bloomberg-style IPO research terminal.
//
// Lenses on one deal dataset (scripts/ipo_calendar.py — Nasdaq's calendar
// merged per deal, staged on EDGAR, with a projected pricing date):
//   CALENDAR    — the pipeline by projected pricing date, priced below (default)
//   PERFORMANCE — priced deals sorted by return since IPO
//   WATCHLIST / LOCKUPS / PRIVATE / SIGNALS — see the Lens enum
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
#include "screens/pre_ipo/PreIpoTypes.h"
#include "services/markets/MarketDataService.h"

#include <QDate>
#include <QJsonObject>
#include <QHash>
#include <QJsonDocument>
#include <QSet>

#include <QDeadlineTimer>

#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <limits>

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
    /// One registration-ladder filing on EDGAR (S-1, S-1/A, 8-A12B, CERT,
    /// EFFECT, 424B4, RW, DRS…), newest first.
    struct LadderStep {
        QString form;
        QDate   date;
        QString url;   // EDGAR document, empty when the accession is unknown
    };

    /// When the deal is expected to price, and what that rests on. Kinds
    /// (see scripts/ipo_calendar.py for the measurement behind each):
    ///   scheduled — Nasdaq's expected price date
    ///   imminent  — 8-A / CERT / EFFECT on file; date + p10–p90 window
    ///   estimate  — comparables window [lo, hi], point = median; `share` of
    ///               comparable filers that priced at all
    ///   long_shot — under 10% of comparables priced; no date
    ///   overdue / stalled — past its window and not priced
    ///   dormant   — no registration filing for 120 days
    ///   none      — too few comparables
    ///   priced / withdrawn — terminal
    struct Projection {
        QString kind;
        QDate   date, lo, hi;
        double  share = std::numeric_limits<double>::quiet_NaN();
        QString basis;
        int     comps_at_risk = 0, comps_priced = 0, comps_withdrawn = 0, comps_pending = 0;
        int     age_days = -1;
        bool firm() const { return kind == "scheduled" || kind == "imminent"; }
    };

    /// One IPO DEAL — Nasdaq's buckets merged by dealID into one lifecycle
    /// row (scripts/ipo_calendar.py). Reading the buckets as separate rows
    /// showed a deal that priced in June as a live "filed" entry from May.
    struct Entry {
        QString deal_id;
        QString company;
        QString ticker;
        QString exchange;
        // `date` is the event that defines the row for its status — expected
        // (upcoming), first public filing (filed), pricing (priced),
        // withdrawal (withdrawn). The explicit fields below say which is which.
        QDate   date;
        QString date_raw;
        QDate   filed_date;       // first public S-1/F-1 (Nasdaq filedDate)
        QDate   expected_date;    // Nasdaq expectedPriceDate (upcoming only)
        QDate   priced_date;
        QDate   withdrawn_date;
        bool    priced_from_edgar    = false;  // 424B4 seen before Nasdaq moved it
        bool    withdrawn_from_edgar = false;  // RW seen before Nasdaq moved it
        QString price_range;      // e.g. "15.00-17.00" or "17.00" for priced
        bool    range_from_overview = false;   // filed deal: range from Nasdaq's deal page
        QString final_price_raw;  // priced only
        double  final_price = 0;  // priced only — parsed dollars
        QString shares_raw;
        double  shares       = 0;
        double  overallot    = 0; // greenshoe shares, 0 = unknown
        QString deal_size;        // display string ($170M / $1.2B)
        double  deal_size_dollars = 0;
        QString bookrunner;       // Nasdaq's feed carries none; kept for search
        QStringList all_bookrunners;
        QString status;           // "upcoming" | "filed" | "priced" | "withdrawn"
        bool    spac = false;
        // EDGAR (enrich pass)
        QString cik;
        int     lockup_days = 0;  // prospectus term; 0 = not on file
        QString stage;            // on_file | listing | certified | effective | priced | withdrawn | unknown
        QDate   stage_date;
        QDate   last_filing_date;
        QVector<LadderStep> ladder;
        bool    edgar_known = false;
        bool    edgar_failed = false;   // SEC was asked and did not answer (no stale copy)
        bool    edgar_skipped = false;  // first filed too long ago to be read
        Projection proj;
        /// The date the pipeline is ordered and windowed by: the projected
        /// pricing date (or the window start) for a pending deal; the event
        /// date otherwise.
        QDate when() const;
        bool pending() const { return status == "upcoming" || status == "filed"; }
        // Performance enrichment via yfinance batch_quotes — pre-fetched on load.
        bool   perf_fetched = false;
        double last_price   = 0;
        double pop_pct      = 0;  // (last - final) / final * 100, priced only
        // Company profile via yfinance batch_info — pre-fetched on load too.
        bool    profile_fetched = false;
        QString sector;
        QString industry;
        QString website;
    };

    /// Lock-up expiry of a recent operating-company IPO, from the deal's own
    /// term (Nasdaq overview) or the 180-day convention where none is on file.
    struct Lockup {
        QString ticker, company;
        QDate   priced, expiry;
        int     days = 0;
        bool    assumed = false;
        double  shares_offered = 0, final_price = 0;
    };

    /// Filter state.
    /// BOOKRUNNERS lens removed — Nasdaq's public IPO calendar doesn't carry
    /// `leadFirmName`. We keep the field on the Entry struct for a future
    /// SEC EDGAR-driven source but the lens shipped only "Unknown" rows.
    /// LOCKUPS shows lock-up expiries of operating-company IPOs priced in the
    /// last ~13 months, from each deal's own prospectus term (Nasdaq deal
    /// overview). It used Finnhub's /stock/lockup, which is premium-only
    /// (302) — the lens was permanently empty behind a "no entries" message.
    ///
    /// PRIVATE shows SEC Form D filers (Rule 506(b)/(c)/504 fundraises) so
    /// pre-S-1 private companies — SpaceX, Anthropic, OpenAI, Anduril,
    /// Qumulo, etc. — surface in IPO Watch's search and can be starred.
    /// Backed by PreIpoService::recent_form_d(); no live quote (private
    /// companies don't trade publicly).
    ///
    /// SIGNALS is a cross-universe "what just changed" feed of the derived
    /// signals PreIpoService::signal_list() computes (consensus mark moves,
    /// S-1 amendment bursts, IPO-readiness jumps). Rows click through to the
    /// private dossier.
    enum Lens { LensCalendar = 0, LensPerformance, LensWatchlist, LensLockups,
                LensPrivate, LensSignals, LensCount };
    enum TimeWindow { TW_AllUpcoming = 0, TW_ThisWeek, TW_30Days, TW_3Months,
                      TW_6Months, TW_12Months, TW_Past30Days };
    // Tab indexes for the two detail-rail tab bars — kept in sync with the
    // addTab order in build_detail_rail. Use these instead of magic numbers
    // so reorders are caught at compile time.
    enum FactTab  { FT_Deal = 0, FT_Business, FT_Leadership, FT_Fundamentals,
                    FT_News, FT_Holders, FT_Filings, FT_Funding, FT_Pipeline };
    enum ChartTab { CT_Price = 0, CT_Revenue, CT_NetIncome, CT_Margins,
                    CT_Range, CT_Lockup, CT_Timeline };

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
    /// scripts/ipo_calendar.py: `calendar` (Nasdaq only, seconds) renders
    /// first; `enrich` (EDGAR stage + projections + lock-ups; minutes cold,
    /// seconds warm) replaces it when it lands.
    void load_calendar(bool enrich);
    void apply_calendar(const QJsonObject& root, bool enriched);
    static Entry entry_from_json(const QJsonObject& o);
    void on_fetch_done();
    void enrich_priced_with_quotes(); // queues priced tickers for quotes (pop_pct)
    void enrich_with_profiles();      // queues priced tickers for profiles (sector/industry)
    // Yahoo is asked for IPO tickers in chunks, one at a time (run_pump).
    struct ChunkPump {
        QStringList   queue;       // waiting, in order
        QSet<QString> queued;      // = queue, for O(1) membership
        QSet<QString> requested;   // asked (or waiting) — not re-queued; cleared on refresh / wake
        QSet<QString> inflight;    // the chunk being asked now
        bool    busy = false;      // a chunk is out, or the gap after it runs
        quint64 gen = 0;           // bumped on send and on reply/give-up: a stale reply or watchdog is ignored
        QDeadlineTimer cooldown{0};   // after a refusal: no chunk before it expires
        bool    cooldown_wake = false; // a resume is already scheduled
        bool add(const QString& ticker);   // false when already asked / waiting
    };
    /// fetch(syms, done) asks for one chunk; done(refused, retry) reports it:
    /// `retry` tickers are asked again later, `refused` stops the chain.
    using ChunkDone  = std::function<void(bool refused, const QSet<QString>& retry)>;
    using ChunkFetch = std::function<void(const QStringList& syms, ChunkDone done)>;
    void run_pump(ChunkPump* p, const char* what, ChunkFetch fetch, std::function<void()> on_drain);

    // ── Rendering ───────────────────────────────────────────────────────────
    void render();                  // dispatch by active_lens_
    void render_calendar();
    void render_performance();
    void render_watchlist();
    void render_lockups();          // Finnhub-sourced — see Lens enum
    void render_private();          // private-company dossier list (PreIpoService::companies)
    void show_add_company_dialog(); // user-add a private company → PreIpoService::add_user_company
    void render_signals();          // cross-universe derived-signal feed (signal_list)
    /// Detail-rail render for a private company, keyed by PrivateCompany::id.
    /// Pulled out separately because the per-Entry render_detail() assumes
    /// Nasdaq-calendar columns we don't have for private companies; this
    /// version surfaces the full dossier (valuation, rounds, fund marks, SPV
    /// interest, S-1 pipeline) from PreIpoService::company(id) inside the rail.
    void render_detail_private(const QString& company_id);
    /// Weight the three panes for the current subject type. No-op once the
    /// user has dragged a divider — see the implementation comment.
    void apply_pane_weights(bool chart_heavy);
    void render_kpis();
    void render_detail(const Entry* e); // nullptr clears the rail
    const Entry* detail_entry() const;  // the open dossier's row, by deal id
    void rerender_detail();
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
    void    rebuild_price_chart(const Entry& e);     // mutates price_chart_view_
    void    rebuild_revenue_chart(const Entry& e);   // mutates revenue_chart_view_
    void    rebuild_netincome_chart(const Entry& e); // mutates netincome_chart_view_
    void    rebuild_margins_chart(const Entry& e);   // mutates margins_chart_view_
    /// Private-company $/share trend from N-PORT fund marks, drawn into the
    /// PRICE chart host. Returns true iff a trend line was plotted (≥2 quarters);
    /// false when only a placeholder was shown (caller hides the tab).
    bool    rebuild_fundmark_chart(const pre_ipo::PrivateCompany& c);

    /// Holds the EDGAR dossier read until the reader stops moving. This pane
    /// renders on every selection change, so arrowing down a sixty-row list
    /// would fire one ~40-request read per row — thousands of SEC requests,
    /// and every Python slot in the app occupied for minutes, for companies
    /// nobody stopped on.
    class QTimer* dossier_debounce_ = nullptr;
    QString       dossier_pending_id_;

    // Lazy fetchers fired from render_detail when their tab data is missing.
    void fetch_ipo_extras_for_detail(const Entry& e);
    void fetch_sec_filings_for_detail(const Entry& e);
    void fetch_wikipedia_for_detail(const Entry& e);
    /// Wikipedia summary for private companies — reused by
    /// render_detail_private. `title` is the lookup name (tries a suffix-
    /// stripped variant on miss for filings whose Form D legal name doesn't
    /// match the common Wikipedia title, e.g. "Space Exploration Technologies
    /// Corp." → "SpaceX"); `company_id` is re-rendered on success so the cache
    /// stays name-keyed while routing stays id-keyed.
    void fetch_wikipedia_for_private(const QString& title, const QString& company_id);
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
    /// Private companies are keyed in starred_ by PrivateCompany::id, but
    /// legacy stars (pre-dossier UI) used the Form D legal name. Match
    /// tolerantly against id, display name, and aliases so old watchlists
    /// keep working. Toggling normalizes to the id (see the PRIVATE/WATCHLIST
    /// click handlers).
    bool private_starred(const pre_ipo::PrivateCompany& c) const;
    /// Flip a private company's watch state, clearing any legacy name/alias
    /// keys and persisting the id form.
    void toggle_private_star(const QString& company_id);

    // Helpers
    /// EXPECTED cell: the projected pricing date in the typography of its
    /// basis (firm dates bold, estimates "~" and muted, no-date kinds say
    /// why), tooltip = the basis. Sorts by when().
    QTableWidgetItem* make_when_item(const Entry& e) const;
    QTableWidgetItem* make_status_item(const Entry& e) const;
    static QString stage_label(const Entry& e);
    static QString status_label(const Entry& e);
    static QString status_color(const Entry& e);
    static QString format_money(double dollars);
    /// Bare magnitude for quantities that are NOT money (share counts).
    /// Passing a share count to format_money prefixes it with '$'.
    static QString format_count(double n);
    static QString format_deal_size(const QString& price_range, const QString& shares);
    static double  parse_price_mid(const QString& price_range, bool* ok = nullptr);
    static double  parse_shares(const QString& shares);

    // ── State ───────────────────────────────────────────────────────────────
    QVector<Entry> entries_;
    QVector<Lockup> lockups_;
    bool    calendar_inflight_ = false;
    bool    enrich_inflight_   = false;
    bool    enriched_          = false;   // EDGAR stage present in entries_
    quint64 calendar_gen_      = 0;       // bumped per refresh; stale fast replies dropped
    quint64 enrich_applied_gen_ = 0;      // gen at which the last enrich reply was applied
    ChunkPump quote_pump_, profile_pump_;
    QStringList calendar_errors_;
    QString status_text() const;
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
    QHash<QString, qint64>                                 filings_failed_at_; // retry after a minute
    QSet<QString>                                          filings_retry_pending_; // one 61 s re-render each
    quint64                                                filings_gen_ = 0;       // bumped on a wake reset
    bool filings_failed(const QString& ticker) const;
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
    /// Currently shown in the detail rail: a ticker for a listed entry, or a
    /// PrivateCompany::id for a private dossier. Every path that blanks the
    /// rail clears it, which is what makes it safe to route asynchronous
    /// per-company updates through.
    /// True once the user has dragged a splitter handle. Their layout wins
    /// from then on; automatic weighting stops.
    bool splitter_user_sized_ = false;
    QString detail_symbol_;
    QString detail_deal_id_;   // the open dossier's deal (tickers repeat across re-filings)
    // Starred-ticker set, persisted under SettingsRepository key
    // `ipo_watch.starred`. Each entry is either the ticker (preferred) or
    // company name for upcoming deals with no ticker yet — see starred_key().
    QSet<QString> starred_;
    Lens       active_lens_   = LensCalendar;
    TimeWindow time_window_   = TW_AllUpcoming;
    // Default to UPCOMING because the screen's primary value is forward
    // pipeline research — past priced deals are the COMPS context, not the
    // headline. User can flip to ALL / PRICED via the status chip.
    QString    status_filter_ = "upcoming"; // all | upcoming (pipeline) | priced | withdrawn
    QString    exch_filter_   = "all";     // all | NYSE | NASDAQ | …
    QString    size_filter_   = "all";     // all | <100M | 100-500M | 500M+
    QString    type_filter_   = "all";     // all | operating | spac
    /// Exchange chip match. Nasdaq's feed names NYSE American "NYSE MKT", so
    /// "AMEX" never matched by substring and "NYSE" swallowed it.
    bool exchange_matches(const QString& exch) const;
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
    QComboBox* type_chip_   = nullptr;
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
    QWidget*      page_price_chart_host_     = nullptr; // QVBoxLayout host for the QChartView
    QWidget*      page_revenue_chart_host_   = nullptr; // QVBoxLayout host for revenue bar chart
    QWidget*      page_netincome_chart_host_ = nullptr; // QVBoxLayout host for net-income bar chart
    QWidget*      page_margins_chart_host_   = nullptr; // QVBoxLayout host for multi-line margin chart
    QChartView*   price_chart_view_          = nullptr; // recreated each render
    QChartView*   revenue_chart_view_        = nullptr;
    QChartView*   netincome_chart_view_      = nullptr;
    QChartView*   margins_chart_view_        = nullptr;
    QLabel*       page_range_    = nullptr;
    QLabel*       page_lockup_   = nullptr;
    QLabel*       page_timeline_ = nullptr;

    QLabel*       right_top_     = nullptr;  // sector heat + comps
    QLabel*       right_bottom_  = nullptr;  // research links
};

} // namespace fincept::screens::widgets
