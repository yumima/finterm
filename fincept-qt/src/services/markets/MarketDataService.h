#pragma once
#include "core/result/Result.h"

#    include "datahub/Producer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVector>

#include <functional>

namespace fincept::services {

struct QuoteData {
    QString symbol;
    QString name;
    double price = 0;
    double change = 0;
    double change_pct = 0;
    double high = 0;
    double low = 0;
    double volume = 0;
    // Live order-book snapshot — populated when the upstream feed reports
    // it. yfinance's fast_info returns bid/ask best-effort: zeros outside
    // regular trading hours, illiquid tickers, or paid-feed-only exchanges.
    // Consumers should treat 0 as "unavailable" rather than a real quote.
    double bid = 0;
    double ask = 0;
    double bid_size = 0;
    double ask_size = 0;
};

struct OfficerInfo {
    QString name;
    QString title;
};

struct InfoData {
    QString symbol;
    QString name;
    QString sector;
    QString industry;
    QString country;
    QString currency;
    double market_cap = 0;
    double pe_ratio = 0;
    double forward_pe = 0;
    double price_to_book = 0;
    double dividend_yield = 0;
    double beta = 0;
    double week52_high = 0;
    double week52_low = 0;
    double avg_volume = 0;
    double eps = 0; // revenuePerShare as proxy
    double roe = 0;
    double profit_margin = 0;
    double debt_to_equity = 0;
    double current_ratio = 0;
    // Narrative + leadership — used by the IPO Watch detail rail to render
    // a "who's behind this company" section beyond pure numbers.
    QString description;     // longBusinessSummary
    QString website;
    int     employees = 0;
    QVector<OfficerInfo> officers;
};

struct HistoryPoint {
    qint64 timestamp = 0; // seconds since epoch
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    qint64 volume = 0;
};

struct TickerDef {
    QString symbol;
    QString name;
};

struct MarketCategory {
    QString category;
    QStringList tickers;
};

struct RegionalMarket {
    QString region;
    QVector<TickerDef> tickers;
};

/// Fetches market quotes via Python/yfinance.
/// Features:
///   - Request batching: collects symbols over a 100ms window, deduplicates, single Python call
///   - Quote caching: returns cached data immediately, refreshes in background
///   - DataHub producer: owns the `market:quote:*` topic family. Phase 2 —
///     see fincept-qt/docs/datahub-phases/phase-02-market-data-pilot.md.
class MarketDataService : public QObject
    , public fincept::datahub::Producer
{
    Q_OBJECT
  public:
    using QuoteCallback = std::function<void(bool, QVector<QuoteData>)>;

    static MarketDataService& instance();

    /// Register this service as a DataHub producer + install the default
    /// `market:quote:*` policy. Idempotent — safe if called more than once.
    /// Called from main.cpp after `datahub::register_metatypes()`.
    void ensure_registered_with_hub();

    // ── fincept::datahub::Producer ────────────────────────────────────────
    QStringList topic_patterns() const override;
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override;

    /// Fetch quotes — batched and cached. Callback receives filtered results for requested symbols.
    /// Phase 3+: prefer `DataHub::subscribe(this, "market:quote:<sym>", ...)` for streaming widgets.
    /// This callback API remains for one-shot reads (e.g. report builder snapshots).
    void fetch_quotes(const QStringList& symbols, QuoteCallback cb);

    /// Drop cached quote entries for the given symbols, forcing the next
    /// fetch_quotes() call to hit the data source. The DataHub min_interval
    /// (currently 2s) still rate-limits actual outbound calls, so rapid tab
    /// switching won't hammer yfinance. Pass an empty list to clear all.
    /// Use this on user-initiated refresh (tab activation, refresh button,
    /// portfolio selection change) so the user always feels the data is live.
    void invalidate_quotes(const QStringList& symbols = {});

    using NewsCallback = std::function<void(bool, QJsonArray)>;
    void fetch_news(const QString& symbol, int count, NewsCallback cb);

    /// Live order-book snapshot (bid/ask + sizes) for a single symbol.
    /// Separate from the batch quote path because it hits yfinance's
    /// fast_info endpoint per-symbol — calling it inside the batch loop
    /// would scale O(N) network round trips. Callers invoke this only
    /// when they actually need bid/ask (e.g. when the perf chart enters
    /// focus mode for one ticker).
    struct OrderbookData {
        QString symbol;
        double  bid       = 0;
        double  ask       = 0;
        double  bid_size  = 0;
        double  ask_size  = 0;
    };
    using OrderbookCallback = std::function<void(bool, OrderbookData)>;
    void fetch_orderbook(const QString& symbol, OrderbookCallback cb);

    /// Fetch full company info (P/E, 52W range, market cap, ratios, etc.)
    using InfoCallback = std::function<void(bool, InfoData)>;
    void fetch_info(const QString& symbol, InfoCallback cb);

    /// Minimal-profile batch lookup: one daemon round-trip for many symbols,
    /// returns sector / industry / longName / marketCap / website / country.
    /// Used by IPO Watch to enrich priced tickers without N round-trips.
    /// 7-day CacheManager TTL since these values change rarely.
    struct CompanyProfile {
        QString symbol;
        QString long_name;
        QString sector;
        QString industry;
        QString website;
        QString country;
        double  market_cap = 0;
        int     employees  = 0;
    };
    using ProfileCallback = std::function<void(bool, QVector<CompanyProfile>)>;
    void fetch_company_profiles(const QStringList& symbols, ProfileCallback cb);

    /// IPO Watch detail-rail aggregator. Single daemon round-trip returning
    /// quarterly financials, institutional/major holders, and recent news.
    /// Designed for lazy-fetch on row click — see IpoWatchView's NEWS /
    /// FINANCIALS / HOLDERS tabs.
    struct FinancialPoint  { QString date; double value = 0; };
    struct InstitutionalHolder {
        QString holder;
        double  pct    = 0; // 0..1
        double  shares = 0;
        double  value  = 0; // dollars
        QString date;
    };
    struct MajorHolderRow { QString label; double value = 0; };
    struct NewsItem {
        QString title;
        QString link;
        QString publisher;
        QString ts;
    };
    struct IpoExtras {
        QString symbol;
        QVector<FinancialPoint>     quarterly_revenue;
        QVector<FinancialPoint>     quarterly_net_income;
        QVector<InstitutionalHolder> institutional_holders;
        QVector<MajorHolderRow>     major_holders;
        QVector<NewsItem>           news;
    };
    using IpoExtrasCallback = std::function<void(bool, IpoExtras)>;
    void fetch_ipo_extras(const QString& symbol, IpoExtrasCallback cb);

    /// SEC EDGAR filings (free public API — no key required, but the
    /// `User-Agent: <name> <email>` header is mandated by SEC). Returns the
    /// 25 most-recent filings for the ticker (S-1, 10-K, 8-K, Form 4, etc.).
    struct SecFiling {
        QString form;          // "S-1" | "10-K" | "8-K" | "4" | …
        QString filing_date;   // "YYYY-MM-DD"
        QString accession;     // "0000320193-25-000005"
        QString primary_doc;   // filename within the accession folder
        QString url;           // pre-built EDGAR document URL
    };
    using SecFilingsCallback = std::function<void(bool, QVector<SecFiling>)>;
    void fetch_sec_filings(const QString& ticker, SecFilingsCallback cb);

    /// Wikipedia summary lookup — used by IPO Watch as a description fallback
    /// when yfinance's `longBusinessSummary` is empty (common for pre-IPO
    /// filings and freshly-listed deals). The REST `summary` endpoint returns
    /// the article's lead paragraph in plain text, no key needed.
    struct WikipediaSummary {
        QString title;
        QString description;   // short tagline ("American technology company")
        QString extract;       // first ~3 sentences of the article
        QString url;
    };
    using WikipediaCallback = std::function<void(bool, WikipediaSummary)>;
    void fetch_wikipedia_summary(const QString& title_query, WikipediaCallback cb);

    /// S-1 funding-history extractor — free, public surrogate for the
    /// Crunchbase/NPM "Financing History" panel. Downloads the company's
    /// latest S-1 (or S-1/A) document from SEC EDGAR, lifts the
    /// "Recent Sales of Unregistered Securities" section verbatim, and
    /// best-effort tags any round mentions it can identify by regex.
    /// Caller passes the S-1 document URL we already discovered via
    /// fetch_sec_filings — we don't re-walk the submissions API here.
    struct S1Funding {
        QString source_url;     // S-1 document URL on sec.gov
        QString section_text;   // cleaned "Recent Sales..." section
        // Best-effort structured rows extracted from the section. Always
        // partial; treat as hints, not the authoritative record.
        struct Round { QString date; QString amount; QString context; };
        QVector<Round> rounds;
    };
    using S1FundingCallback = std::function<void(bool, S1Funding)>;
    void fetch_s1_funding(const QString& s1_url, S1FundingCallback cb);

    /// Fetch historical OHLCV data. period: "1mo","3mo","6mo","1y","2y","5y"
    /// interval: "1d","1wk","1mo"
    /// Phase 3+: prefer `DataHub::subscribe(this, "market:history:<sym>:<period>:<interval>", ...)`
    /// for streaming chart widgets. Callback API remains for one-shot reads.
    using HistoryCallback = std::function<void(bool, QVector<HistoryPoint>)>;
    void fetch_history(const QString& symbol, const QString& period, const QString& interval, HistoryCallback cb);

    /// Fetch 5-day hourly sparkline data for multiple symbols in one Python call.
    /// Callback: map of symbol -> list of close prices (chronological).
    /// Phase 3+: prefer `DataHub::subscribe(this, "market:sparkline:<sym>", ...)` for live tables.
    using SparklineCallback = std::function<void(bool, QHash<QString, QVector<double>>)>;
    void fetch_sparklines(const QStringList& symbols, SparklineCallback cb);

    static QVector<MarketCategory> default_global_markets();
    static QVector<RegionalMarket> default_regional_markets();

    /// Default symbol lists for dashboard widgets
    static QStringList indices_symbols();
    static QStringList forex_symbols();
    static QStringList crypto_symbols();
    static QStringList commodity_symbols();
    static QStringList mover_symbols();
    static QStringList global_snapshot_symbols();

  private:
    MarketDataService();
    void flush_batch();

    /// Internal: publish the per-symbol result to the hub and clear
    /// in_flight for the matching topic. Called from inside `flush_batch`.
    void publish_quote_to_hub(const QuoteData& q);
    void publish_history_to_hub(const QString& symbol, const QString& period, const QString& interval,
                                const QVector<HistoryPoint>& points);
    void publish_sparkline_to_hub(const QString& symbol, const QVector<double>& points);

    // ── Batching ──
    struct PendingRequest {
        QStringList symbols;
        QuoteCallback cb;
    };
    QVector<PendingRequest> pending_;
    bool batch_scheduled_ = false;

    bool hub_registered_ = false;

    // ── Caching — delegated to CacheManager (SQLite-backed, so entries
    // persist across app restarts). Two parallel keys per symbol:
    //   "market:{sym}"      — 30s TTL, drives in-session freshness so the
    //                          watchlist/dashboard doesn't re-hit yfinance
    //                          on every redraw.
    //   "market_last:{sym}" — 7d TTL, used only as a cold-start fallback so
    //                          the user sees their last-known prices the
    //                          instant the app launches while the live
    //                          quote refresh runs in the background.
    static constexpr int kQuoteCacheTtlSec     = 30;
    static constexpr int kQuoteLastKnownTtlSec = 7 * 24 * 60 * 60;

    // SEC EDGAR's submissions API is keyed by CIK (numeric, zero-padded to 10
    // digits), not ticker. We resolve the ticker→CIK mapping by lazily
    // fetching company_tickers.json (~700KB) on the first SEC request and
    // caching it in memory for the rest of the session. SEC publishes the
    // file weekly so a per-session cache is plenty.
    QHash<QString, int>      sec_cik_map_;       // upper-case ticker → CIK
    bool                     sec_cik_loaded_   = false;
    bool                     sec_cik_loading_  = false;
    QVector<std::function<void(bool)>> sec_cik_waiters_;
};

} // namespace fincept::services
