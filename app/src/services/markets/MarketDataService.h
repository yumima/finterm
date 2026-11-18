#pragma once
#include "core/result/Result.h"
#include "core/util/BarTime.h"
#include "services/markets/Bars.h"
#include "datahub/Producer.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <cmath>
#include <functional>
#include <limits>

namespace finterm::services {

/// "The vendor did not supply this number." Distinct from a real 0.
inline constexpr double kUnknownNum = std::numeric_limits<double>::quiet_NaN();

struct QuoteData {
    QString symbol;
    QString name;
    double price = 0;
    // change / change_pct / volume are NaN (kUnknownNum) when the feed had no
    // previous close / volume — render placeholder(), never a flat 0.00%.
    double change = 0;
    double change_pct = 0;
    double high = 0;
    double low = 0;
    double volume = 0;
    // The session's open and the close `change` is measured against — NaN
    // when the feed did not say. prev_close is the one stored baseline: a
    // live print re-derives change / change_pct from it (with_live_price).
    double open = kUnknownNum;
    double prev_close = kUnknownNum;
    // Live order-book snapshot — populated when the upstream feed reports
    // it. yfinance's fast_info returns bid/ask best-effort: zeros outside
    // regular trading hours, illiquid tickers, or paid-feed-only exchanges.
    // Consumers should treat 0 as "unavailable" rather than a real quote.
    double bid = 0;
    double ask = 0;
    double bid_size = 0;
    double ask_size = 0;

    // Lets DataHub skip re-publishing a byte-identical quote (no-change
    // dirty-check). An unchanged tick (common off-hours / illiquid symbols)
    // carries the same bytes from the daemon, so member-wise equality holds.
    // INVARIANT: this is only safe because every published QuoteData is a
    // freshly-parsed value (QVariant::fromValue deep-copies it) — never an
    // in-place-mutated, retained instance. Do NOT add operator== to a payload
    // type whose producer mutates and re-publishes the same instance, or the
    // dirty-check could wrongly skip a real update.
    // NaN-aware: an absent open / prev_close / change is NaN, and NaN != NaN
    // would make every unchanged quote look new — every tick republished and
    // every subscriber repainted.
    bool operator==(const QuoteData& o) const {
        auto same = [](double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); };
        return symbol == o.symbol && name == o.name && same(price, o.price) && same(change, o.change) &&
               same(change_pct, o.change_pct) && same(high, o.high) && same(low, o.low) && same(volume, o.volume) &&
               same(open, o.open) && same(prev_close, o.prev_close) && same(bid, o.bid) && same(ask, o.ask) &&
               same(bid_size, o.bid_size) && same(ask_size, o.ask_size);
    }
};

struct OfficerInfo {
    QString name;
    QString title;
};

/// Numeric fields default to kUnknownNum (NaN), not 0.
///
/// Both producers behind this struct send JSON null for a field Yahoo did
/// not supply — get_info has never defaulted its numerics, and
/// get_financial_ratios no longer does. Read with a bare toDouble() those
/// became 0.0, indistinguishable from a real zero: the IPO Watch rail
/// reported a 0.00% profit margin, and a peer chart drew a zero-height bar,
/// for companies the vendor simply had no figure for.
struct InfoData {
    QString symbol;
    QString name;
    QString sector;
    QString industry;
    QString country;
    QString currency; // empty when Yahoo gave none — never a default
    // Did the profile lookup (get_info) answer? Empty fields mean "Yahoo has
    // none" only when it did. info_transient: it was refused or not answered
    // (auth, rate limit, timeout) — the blanks say nothing about the symbol.
    bool info_answered = false;
    bool info_transient = false;
    double market_cap = kUnknownNum;
    double pe_ratio = kUnknownNum;
    double forward_pe = kUnknownNum;
    double price_to_book = kUnknownNum;
    double dividend_yield = kUnknownNum;
    double beta = kUnknownNum;
    double week52_high = kUnknownNum;
    double week52_low = kUnknownNum;
    double avg_volume = kUnknownNum;
    double eps = kUnknownNum; // revenuePerShare as proxy
    double roe = kUnknownNum;
    double profit_margin = kUnknownNum;
    double debt_to_equity = kUnknownNum;
    double current_ratio = kUnknownNum;
    // Narrative + leadership — used by the IPO Watch detail rail to render
    // a "who's behind this company" section beyond pure numbers.
    QString description; // longBusinessSummary
    QString website;
    int employees = 0;
    QVector<OfficerInfo> officers;
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
///     see app/docs/datahub-phases/phase-02-market-data-pilot.md.
/// One trade print from the push price stream, as it arrived — every session.
/// MarketDataService lays REGULAR-session prints on market:quote:* itself;
/// extended-hours and futures-contract prints are for the surfaces that own
/// those pairings (AFT%, FUT%), which take them through stream_ticks().
struct StreamTick {
    QString symbol;
    double price = 0;
    double change = kUnknownNum; ///< Yahoo's change vs ITS previous close; price − change = that close
    /// 0 pre-market, 1 regular (futures: any Globex hours), 2 post-market,
    /// 4 overnight. Proto3 omits 0, so an absent field reads as pre-market.
    int market_hours = 0;
    qint64 time_ms = 0;
};

class MarketDataService : public QObject, public finterm::datahub::Producer {
    Q_OBJECT
  public:
    /// (ok, quotes, error): `error` is the vendor's own reason when !ok — a
    /// rate limit or refusal must not read as "no such symbol".
    using QuoteCallback = std::function<void(bool, QVector<QuoteData>, const QString&)>;

    static MarketDataService& instance();

    /// Register this service as a DataHub producer + install the default
    /// `market:quote:*` policy. Idempotent — safe if called more than once.
    /// Called from main.cpp after `datahub::register_metatypes()`.
    void ensure_registered_with_hub();

    // ── finterm::datahub::Producer ────────────────────────────────────────
    QStringList topic_patterns() const override;
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override;

    /// Fetch quotes — batched and cached. Callback receives filtered results for requested symbols.
    /// Phase 3+: prefer `DataHub::subscribe(this, "market:quote:<sym>", ...)` for streaming widgets.
    /// This callback API remains for one-shot reads (e.g. report builder snapshots).
    /// On a failed fetch the callback normally gets ok=true with whatever
    /// stale cached quotes exist. stale_on_failure=false reports the failure
    /// instead (ok=false, no rows) — for a caller pacing its own asks, which
    /// must see a refusal to stop.
    void fetch_quotes(const QStringList& symbols, QuoteCallback cb, bool stale_on_failure = true);
    /// When `symbol`'s quote was fetched (the cache entry's write time) —
    /// kept off QuoteData so an unchanged tick stays byte-identical for the
    /// hub's no-change check. Invalid when no quote is held.
    QDateTime quote_as_of(const QString& symbol) const;
    /// A quote as JSON for tools and workflows — one shape everywhere; an
    /// unknown figure is null, never 0.
    static QJsonObject quote_json(const QuoteData& q);

    /// Real day's top gainers + losers via yfinance's predefined screeners.
    /// Returns both sides in one daemon round-trip. Used by TopMoversWidget;
    /// the prior implementation ranked a hardcoded 12-ticker watchlist, which
    /// produced visibly wrong "biggest gainer" rows on quiet days.
    struct TopMovers {
        QVector<QuoteData> gainers;
        QVector<QuoteData> losers;
        /// The first `n` of each side.
        TopMovers truncated(int n) const {
            TopMovers t{gainers.mid(0, n), losers.mid(0, n)};
            return t;
        }
    };
    using TopMoversCallback = std::function<void(bool, TopMovers)>;
    /// The day's movers, `count` (≤ kMoversMax) per side. Screens subscribe
    /// to the hub topic kMoversTopic instead; this one-shot form (the MCP
    /// tool) reads the same fetch and cache.
    void fetch_top_movers(int count, TopMoversCallback cb);
    static constexpr int kMoversMax = 25;
    static inline const QString kMoversTopic = QStringLiteral("market:movers");

    /// Drop cached quote entries for the given symbols, forcing the next
    /// fetch_quotes() call to hit the data source. The DataHub min_interval
    /// (currently 2s) still rate-limits actual outbound calls, so rapid tab
    /// switching won't hammer yfinance. Pass an empty list to clear all.
    /// Use this on user-initiated refresh (tab activation, refresh button,
    /// portfolio selection change) so the user always feels the data is live.
    void invalidate_quotes(const QStringList& symbols = {});

    /// ok=false carries why (the daemon reports a refused request — e.g. a
    /// Yahoo rate limit — as {"error": ..., "articles": []}; read as an empty
    /// success it showed as "no news").
    using NewsCallback = std::function<void(bool, QJsonArray, QString)>;
    void fetch_news(const QString& symbol, int count, NewsCallback cb);

    /// Live order-book snapshot (bid/ask + sizes) for a single symbol.
    /// Separate from the batch quote path because it hits yfinance's
    /// fast_info endpoint per-symbol — calling it inside the batch loop
    /// would scale O(N) network round trips. Callers invoke this only
    /// when they actually need bid/ask (e.g. when the perf chart enters
    /// focus mode for one ticker).
    struct OrderbookData {
        QString symbol;
        double bid = 0;
        double ask = 0;
        double bid_size = 0;
        double ask_size = 0;
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
        double market_cap = 0;
        int employees = 0;
        bool known_none = false; // Yahoo answered "unknown ticker" (404) — no profile exists
        bool refused = false;    // Yahoo refused / did not answer for this ticker — ask again
        bool failed = false;     // the lookup errored (not Yahoo's answer) — not cached
    };
    /// ok=false when the lookup failed — cached rows may still come along.
    using ProfileCallback = std::function<void(bool, QVector<CompanyProfile>)>;
    void fetch_company_profiles(const QStringList& symbols, ProfileCallback cb);

    /// IPO Watch detail-rail aggregator. Single daemon round-trip returning
    /// quarterly financials, institutional/major holders, and recent news.
    /// Designed for lazy-fetch on row click — see IpoWatchView's NEWS /
    /// FINANCIALS / HOLDERS tabs.
    struct FinancialPoint {
        QString date;
        double value = 0;
    };
    struct InstitutionalHolder {
        QString holder;
        double pct = 0; // 0..1
        double shares = 0;
        double value = 0; // dollars
        QString date;
    };
    struct MajorHolderRow {
        QString label;
        double value = 0;
    };
    struct NewsItem {
        QString title;
        QString link;
        QString publisher;
        QString ts;
    };
    struct IpoExtras {
        QString symbol;
        QVector<FinancialPoint> quarterly_revenue;
        QVector<FinancialPoint> quarterly_net_income;
        // Annual series — same source EquityResearch's FinancialsTab uses
        // for its YoY labels. ~4 years of history per yfinance.
        QVector<FinancialPoint> annual_revenue;
        QVector<FinancialPoint> annual_net_income;
        QVector<FinancialPoint> annual_gross_profit;
        QVector<FinancialPoint> annual_operating_income;
        QVector<InstitutionalHolder> institutional_holders;
        QVector<MajorHolderRow> major_holders;
        QVector<NewsItem> news;
    };
    using IpoExtrasCallback = std::function<void(bool, IpoExtras)>;
    void fetch_ipo_extras(const QString& symbol, IpoExtrasCallback cb);

    /// SEC EDGAR filings (free public API — no key required, but the
    /// `User-Agent: <name> <email>` header is mandated by SEC). Returns the
    /// 25 most-recent filings for the ticker (S-1, 10-K, 8-K, Form 4, etc.).
    struct SecFiling {
        QString form;        // "S-1" | "10-K" | "8-K" | "4" | …
        QString filing_date; // "YYYY-MM-DD"
        QString accession;   // "0000320193-25-000005"
        QString primary_doc; // filename within the accession folder
        QString url;         // pre-built EDGAR document URL
    };
    using SecFilingsCallback = std::function<void(bool, QVector<SecFiling>)>;
    void fetch_sec_filings(const QString& ticker, SecFilingsCallback cb);

    /// Wikipedia summary lookup — used by IPO Watch as a description fallback
    /// when yfinance's `longBusinessSummary` is empty (common for pre-IPO
    /// filings and freshly-listed deals). The REST `summary` endpoint returns
    /// the article's lead paragraph in plain text, no key needed.
    struct WikipediaSummary {
        QString title;
        QString description; // short tagline ("American technology company")
        QString extract;     // first ~3 sentences of the article
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
        QString source_url;   // S-1 document URL on sec.gov
        QString section_text; // cleaned "Recent Sales..." section
        // Additional standardized prospectus sections — each is the cleaned
        // plain-text excerpt, lifted verbatim from the S-1. Empty when the
        // section isn't present (common for shell-company SPAC S-1s).
        QString principal_stockholders; // 5%+ holders — closest free cap-table analog
        QString use_of_proceeds;        // how the IPO funds will be spent
        QString underwriters;           // bookrunner roster + allocation %
        QString selected_financials;    // multi-year revenue / net income table
        QStringList risk_factors;       // top-level risk headings (≤15)
        // Best-effort structured rows extracted from section_text. Always
        // partial; treat as hints, not the authoritative record.
        struct Round {
            QString date;
            QString amount;
            QString context;
        };
        QVector<Round> rounds;
    };
    using S1FundingCallback = std::function<void(bool, S1Funding)>;
    void fetch_s1_funding(const QString& s1_url, S1FundingCallback cb);

    /// One price series, as every reader in the app gets it.
    struct BarsReply {
        bool ok = false;            ///< an answer (bars may be empty: "no history"); false = failed
        QVector<HistoryPoint> bars; ///< priced bars only (bars::parse)
        QJsonArray rows;            ///< the same bars as the daemon's rows (technicals / TALipp read these)
        QDateTime as_of;            ///< when fetched (a cache hit keeps its original time)
        QString error;              ///< the vendor's own reason when !ok
    };
    using BarsCallback = std::function<void(const BarsReply&)>;
    /// The ONE price-history read: `historical_period` through one parser
    /// (bars::parse), one cache entry per (symbol, period, interval, adjusted)
    /// and concurrent asks for the same series coalesced onto one request.
    /// A failed or empty reply is never cached. period: "1d" … "max" or
    /// "range:YYYY-MM-DD:YYYY-MM-DD"; interval: "1m" … "1mo".
    /// Streaming chart widgets subscribe to market:history:<sym>:<period>:<interval>
    /// instead — the same parser and cache.
    /// One company's yfinance `info` answer, as every reader gets it.
    struct InfoReply {
        bool ok = false;
        QJsonObject info;     ///< the daemon's info dict (each reader parses its own view)
        QDateTime as_of;      ///< when fetched (a cache hit keeps its original time)
        QString error;        ///< the vendor's reason when !ok
        bool transient = false; ///< a refusal / no answer — ask again later, never "no such company"
    };
    using InfoReplyCallback = std::function<void(const InfoReply&)>;
    /// The ONE company-info read: the daemon's `info`, one cache entry per
    /// symbol (kInfoTtlSec), concurrent asks coalesced, one retry when the
    /// daemon call itself timed out, a failure never cached. Equity Research's
    /// StockInfo, fetch_info()'s InfoData, the portfolio fundamentals and the
    /// workflow nodes are all views of this one answer.
    void fetch_info_raw(const QString& symbol, InfoReplyCallback cb);
    static constexpr int kInfoTtlSec = 600;

    /// Split/dividend-adjusted prices, or the prints that actually traded.
    /// A strong enum, not a bool: a stray integer argument must not compile.
    enum class Adjust { Adjusted, Raw };
    void fetch_bars(const QString& symbol, const QString& period, const QString& interval, BarsCallback cb,
                    Adjust adjust = Adjust::Adjusted);

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

    /// Stream `symbols` while `owner` lives (or until it sets another list;
    /// an empty list withdraws). For prints that are not market:quote:*
    /// topics — e.g. the pinned contract behind an index's FUT% — and for
    /// extended-hours surfaces that must keep streaming whatever else is on
    /// screen. Ticks arrive through stream_ticks().
    void set_stream_extras(QObject* owner, const QStringList& symbols);

    /// Whether the hub's market:quote:<symbol> was polled or ticked in THIS
    /// process (not a disk-hydrated quote from an earlier session).
    bool has_live_quote(const QString& symbol) const { return polled_syms_.contains(symbol); }

  signals:
    /// Every push-stream print of a tick batch (fast-lane quotes excluded),
    /// all sessions, at most once per flush (~4/s).
    void stream_ticks(const QVector<finterm::services::StreamTick>& ticks);

  private:
    /// Every shared read answers on a later turn of the event loop, cache hit
    /// or not: a caller that sets up state after the call (a counter, a
    /// guard, a render already running) must never see its callback inside it.
    void later(std::function<void()> f) { QTimer::singleShot(0, this, std::move(f)); }
    QHash<QString, QVector<BarsCallback>> bars_waiters_; // series key → callers waiting on its request
    QHash<QString, QVector<InfoReplyCallback>> info_waiters_; // symbol → callers waiting on its info
    void request_info(const QString& symbol, int tries_left);
    /// The one movers fetch (kMoversMax per side): cached kMoversTtlSec,
    /// concurrent callers coalesced onto the request already out.
    void fetch_movers_all(std::function<void(bool, const TopMovers&, const QString&)> cb, bool fresh = false);
    QVector<std::function<void(bool, const TopMovers&, const QString&)>> movers_waiters_;
    MarketDataService();
    void flush_batch();

    /// Internal: publish the per-symbol result to the hub and clear
    /// in_flight for the matching topic. Called from inside `flush_batch`.
    /// `polled` = a quote fetched this process (a poll), as opposed to a
    /// persisted one replayed by cold-start hydration — only a polled quote
    /// is a baseline live ticks may be laid on (a hydrated one can be a prior
    /// session's, with that session's previous close).
    void publish_quote_to_hub(const QuoteData& q, bool polled);
    void publish_history_to_hub(const QString& symbol, const QString& period, const QString& interval,
                                const QVector<HistoryPoint>& points);
    void publish_sparkline_to_hub(const QString& symbol, const QVector<double>& points);

    /// Read every `market_last:*` entry from CacheManager and publish it
    /// into DataHub so widgets subscribing for the first time after launch
    /// get an instant initial value. Invoked via QTimer::singleShot(0) from
    /// ensure_registered_with_hub() — runs off the startup critical path
    /// so the lock screen renders before this disk-read pass.
    ///
    /// Publishing is chunked: at most kHydrateChunkSize quotes per
    /// event-loop turn, with the rest deferred via QTimer::singleShot(0).
    /// This keeps each chunk's main-thread cost low (~1-2 ms) so input
    /// events (PIN typing) and other queued events (video frames) can
    /// interleave between chunks instead of waiting behind the whole
    /// hundreds-of-quotes burst.
    void hydrate_quotes_from_cache();

    /// Writes BOTH quote cache entries for one symbol: the 30s "market:" hot
    /// entry and the 7d "market_last:" cold-start fallback.
    ///
    /// This exists as one function because it was two. Both call sites built
    /// the same payload independently, and when the `cached_at` stamp that
    /// gates cold-start hydration was added to one of them, the other kept
    /// writing the same key unstamped — and since a missing stamp reads as 0,
    /// the gate treats it as "no age known" and publishes regardless. Opening
    /// the portfolio screen (which goes through the on-demand batch path) was
    /// enough to un-stamp a symbol and restore the stale-price bug the stamp
    /// was added to fix. One writer, one format, one stamp.
    static void store_quote_entries(const QString& symbol, const QJsonObject& full);
    void hydrate_next_chunk();
    static constexpr int kHydrateChunkSize = 25;
    // Decoded payload waiting to be published. Built once in
    // hydrate_quotes_from_cache(), drained in chunks.
    QVector<QuoteData> hydrate_pending_;
    int hydrate_pending_total_ = 0;

    /// Steady-state refresh chunking. The Producer::refresh() callback can
    /// arrive with 50–100 quotes plus sparklines + histories in one shot;
    /// publishing them all synchronously costs ~1–4 ms each (cache write +
    /// hub fan-out + signal emission), so a 100-quote burst monopolised the
    /// main thread for ~100–300 ms — long enough to drop video frames and
    /// stall a PIN keystroke. The Python callback now parses into these
    /// pending queues and `drain_refresh_chunk()` publishes up to
    /// kRefreshChunkSize items per event-loop turn, leaving room for paint
    /// events and input between chunks. Same architectural pattern as
    /// hydrate_quotes_from_cache(): bound the worst-case slice, not just
    /// the total work.
    void drain_refresh_chunk();
    static constexpr int kRefreshChunkSize = 15;
    struct PendingSparkline {
        QString symbol;
        QVector<double> points;
    };
    struct PendingHistory {
        QString symbol;
        QString period;
        QString interval;
        QJsonArray rows;              // priced rows (bars::priced_rows) — what the shared cache holds
        QVector<HistoryPoint> points; // bars::parse(rows)
    };
    QVector<QuoteData> refresh_pending_quotes_;
    // symbol -> absolute position in refresh_pending_quotes_ (minus _base_ =
    // index), so a newer print replaces a waiting one in O(1).
    QHash<QString, qint64> refresh_pending_quote_at_;
    qint64 refresh_pending_quote_base_ = 0; // count taken from the front
    QVector<PendingSparkline> refresh_pending_sparks_;
    QVector<PendingHistory> refresh_pending_hists_;
    bool refresh_drain_scheduled_ = false;
    // Totals only used for the "drain complete" log line, so we report the
    // burst size the user sees instead of "0 quotes" once the queue drains.
    int refresh_quotes_total_ = 0;
    int refresh_sparks_total_ = 0;
    int refresh_hists_total_ = 0;
    qint64 refresh_started_ms_ = 0;

    // ── Batching ──
    struct PendingRequest {
        QStringList symbols;
        QuoteCallback cb;
        bool stale_on_failure = true;
    };
    QVector<PendingRequest> pending_;
    bool batch_scheduled_ = false;

    bool hub_registered_ = false;

    // ── Push price stream (yfinance_data._QuoteStreamer) ──────────────────
    // Every market:quote:* topic with a subscriber is streamed; each tick moves
    // the cached quote's price and the values derived from it, so everything on
    // screen keeps pace with the source between the 30s polls. Polling stays
    // the source of truth for every other field and for symbols Yahoo never
    // ticks (indices, most futures).
    struct LastTick {
        double price = 0;
        double volume = 0;   ///< NaN when the tick carried none
        qint64 time_ms = 0;  ///< exchange timestamp of the tick (ordering)
        qint64 seen_ms = 0;  ///< when it arrived (freshness vs a poll)
        /// Fast-lane quotes only (indices, ROOT=F): the previous close from the
        /// SAME snapshot as the price, so the change is derived from that pair.
        /// NaN for push-stream prints (they ride the polled quote's baseline).
        double prev_close = std::numeric_limits<double>::quiet_NaN();
    };
    void init_stream();
    void sync_stream(bool force);
    void apply_ticks(const QJsonArray& ticks);
    /// `q` with `price` as its last price: change / change_pct re-derived from
    /// `prev_close` when given, else from q's OWN baseline (price − change =
    /// previous close); high/low extended.
    static QuoteData with_live_price(QuoteData q, double price, double volume,
                                     double prev_close = std::numeric_limits<double>::quiet_NaN());
    QHash<QObject*, QStringList> stream_extras_;
    QSet<QObject*> extras_watched_; ///< owners whose destroyed() is connected
    QHash<QString, LastTick> last_tick_;
    QHash<QString, qint64> last_raw_ms_; ///< newest push print per symbol, any session (ordering)
    QSet<QString> spark_live_;      ///< sparklines whose last point is today's in-progress bar
    QHash<QString, qint64> session_start_ms_; ///< first regular print after a quiet 30 min (local ms)
    QHash<QString, qint64> last_print_ms_;    ///< when a NEW regular print last arrived (not a re-send)
    QSet<QString> polled_syms_; ///< hub holds a quote polled THIS process (tick baseline)
    /// fetch_quotes() answers from the poll cache, which ticks never touch:
    /// for a symbol streaming now, hand out the hub's tick-refined quote
    /// instead, or a screen rebuilding from fetch_quotes (Portfolio, every
    /// 20s) snaps back to the older poll price.
    void overlay_live(QVector<QuoteData>& quotes) const;
    QStringList stream_syms_;              ///< the set the daemon was last sent
    QTimer* stream_sync_timer_ = nullptr;  ///< debounces topic_active/topic_idle bursts
    bool stream_unavailable_ = false;      ///< this daemon can't stream; retried on restart
    static constexpr int kTickWinsOverPollMs = 15'000;
    /// A fast-lane snapshot's previous close stays the baseline for a poll of
    /// the same symbol this long (it is re-sent only when it changes, and a
    /// poll's own baseline — e.g. ROOT=F's daily-bar close vs Yahoo's settle —
    /// can differ from it: the change must not flip between the two).
    static constexpr qint64 kSnapshotPrevMs = 10 * 60'000;

    // ── Caching — delegated to CacheManager (SQLite-backed, so entries
    // persist across app restarts). Two parallel keys per symbol:
    //   "market:{sym}"      — 30s TTL, drives in-session freshness so the
    //                          watchlist/dashboard doesn't re-hit yfinance
    //                          on every redraw.
    //   "market_last:{sym}" — 7d TTL, used only as a cold-start fallback so
    //                          the user sees their last-known prices the
    //                          instant the app launches while the live
    //                          quote refresh runs in the background.
    static constexpr int kQuoteCacheTtlSec = 30;
    /// Oldest a cached quote may be and still be published at cold start.
    /// The 7-day fallback TTL is right for "better than nothing" during a
    /// rate-limited session, and wrong for presenting a price as current.
    ///
    /// Four days, not two: a market closed Friday 17:00 reopens Monday 09:00,
    /// which is ~64h, so a 48h bound disabled hydration every Monday — and
    /// Friday's close IS the market price on Monday morning, exactly the value
    /// worth showing. Four days also covers the long weekends the US calendar
    /// produces (Good Friday→Monday, Thanksgiving, Christmas/New Year abutting
    /// a weekend).
    ///
    /// It does NOT cover every market this app quotes: Shanghai closes ~8-9
    /// consecutive days for Chinese New Year, so 000001.SS hydrates blank at
    /// cold start midway through that holiday even though its last close is
    /// still the live price. That is deliberate rather than overlooked.
    /// Hydration is a latency optimisation, not a data source — the live fetch
    /// lands seconds later with the same close — so the two failure modes are
    /// wildly asymmetric: a suppressed entry costs a briefly empty cell, while
    /// a published one costs a stale price wearing a Fresh badge. Erring short
    /// is the right side of that trade. Fixing it properly needs a per-symbol
    /// market calendar (sessions elapsed, not hours elapsed), not a bigger
    /// constant — a bigger constant just moves the US regression back in.
    static constexpr qint64 kHydrateMaxAgeSec = 4 * 24 * 3600;
    static constexpr int kQuoteLastKnownTtlSec = 7 * 24 * 60 * 60;

    // SEC EDGAR's submissions API is keyed by CIK (numeric, zero-padded to 10
    // digits), not ticker. We resolve the ticker→CIK mapping by lazily
    // fetching company_tickers.json (~700KB) on the first SEC request and
    // caching it in memory for the rest of the session. SEC publishes the
    // file weekly so a per-session cache is plenty.
    QHash<QString, int> sec_cik_map_; // upper-case ticker → CIK
    bool sec_cik_loaded_ = false;
    bool sec_cik_loading_ = false;
    QVector<std::function<void(bool)>> sec_cik_waiters_;
};

} // namespace finterm::services
