#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#    include "datahub/Producer.h"

class QWebSocket;

#include <atomic>
#include <cstdint>
#include <functional>

namespace fincept::services {

// ── Data types ──────────────────────────────────────────────────────────────

enum class Priority { FLASH, URGENT, BREAKING, ROUTINE };
enum class Sentiment { BULLISH, BEARISH, NEUTRAL };
enum class Impact { HIGH, MEDIUM, LOW };
enum class ThreatLevel { CRITICAL, HIGH, MEDIUM, LOW, INFO };

/// Source credibility flags.
enum class SourceFlag {
    NONE = 0,
    STATE_MEDIA = 1, // Government-controlled outlet
    CAUTION = 2,     // Known for sensationalism or low editorial standards
};

struct ThreatClassification {
    ThreatLevel level = ThreatLevel::INFO;
    QString category; // "conflict", "cyber", "natural", "market", "regulatory", "general"
    // No confidence: keyword matching has no calibrated probability behind it.
};

struct NewsArticle {
    QString id;
    QString time;
    Priority priority = Priority::ROUTINE;
    QString category;
    QString headline;
    QString summary;
    QString source;
    QString region;
    Sentiment sentiment = Sentiment::NEUTRAL;
    Impact impact = Impact::LOW;
    QStringList tickers;
    QString link;
    int64_t sort_ts = 0; // unix seconds
    int tier = 4;        // 1=wire, 2=major, 3=specialty, 4=blog
    ThreatClassification threat;
    SourceFlag source_flag = SourceFlag::NONE;
    QString lang; // ISO language code (e.g., "en", "fr", "ar")
};

struct SentimentAnalysis {
    double score = 0;
    double intensity = 0;
    double confidence = 0;
};

struct MarketImpactData {
    QString urgency;    // LOW/MEDIUM/HIGH
    QString prediction; // negative/neutral/moderate_positive/positive
};

struct RiskSignal {
    QString level;
    QString details;
};

struct NewsAnalysis {
    SentimentAnalysis sentiment;
    MarketImpactData market_impact;
    QStringList keywords;
    QStringList topics;
    QStringList key_points;
    QString summary;
    RiskSignal regulatory;
    RiskSignal geopolitical;
    RiskSignal operational;
    RiskSignal market;
};

// ── RSS Feed definition ─────────────────────────────────────────────────────

struct RSSFeed {
    QString id;
    QString name;
    QString url;
    QString category;
    QString region;
    QString source;
    int tier = 3;
};

// ── AI Summarization ────────────────────────────────────────────────────────

struct HeadlineSummary {
    QString summary;
    int64_t cached_at = 0;
    QString headline_signature; // hash of headlines used to generate
};

// ── Service ─────────────────────────────────────────────────────────────────

/// Phase 5 — DataHub producer for `news:general`, `news:symbol:*`,
/// `news:category:*`, `news:cluster:*`. Existing `articles_updated`
/// / `articles_partial` Qt signals remain live in parallel with hub
/// publishes so consumers can migrate incrementally.
class NewsService : public QObject
    , public fincept::datahub::Producer
{
    Q_OBJECT
  public:
    using ArticlesCallback = std::function<void(bool ok, QVector<NewsArticle>)>;
    using AnalysisCallback = std::function<void(bool ok, NewsAnalysis)>;
    using SummaryCallback = std::function<void(bool ok, QString summary)>;
    /// (ok, title, body_text). Body text is plain text, paragraphs separated
    /// by `\n\n`. Title may be empty if the extractor couldn't recover it.
    using BodyCallback = std::function<void(bool ok, QString title, QString body)>;

    static NewsService& instance();

    /// Register with the hub + install news:* policies. Idempotent.
    /// Called from main.cpp after `datahub::register_metatypes()`.
    void ensure_registered_with_hub();

    // ── fincept::datahub::Producer ────────────────────────────────────────
    QStringList topic_patterns() const override;
    /// Refresh splits by prefix: `news:general` → fetch_all_news(true);
    /// `news:symbol:<sym>` / `news:category:<cat>` derive from the
    /// general fetch + filter; `news:cluster:*` is push-only.
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override;  // RSS — cap at 2/s

    void fetch_all_news(bool force, ArticlesCallback cb);
    void analyze_article(const QString& url, AnalysisCallback cb);

    /// Pull the cleaned body text out of an article URL via the
    /// extract_article.py helper (trafilatura → readability → bs4 fallback).
    /// Cached per URL hash for kArticleBodyTtlSec since article content is
    /// effectively immutable once published. Callback fires on the Qt event
    /// loop with (ok, title, body). On failure body is empty and ok=false.
    void extract_article_body(const QString& url, BodyCallback cb);

    /// url -> body text, for the URLs that yielded one. Missing key = the
    /// extractor could not read that article; callers carry on without it.
    using BodiesCallback = std::function<void(QHash<QString, QString>)>;

    /// Extract several article bodies in one Python process.
    ///
    /// Shares the per-URL cache with extract_article_body(), so a story the
    /// reader has already opened costs nothing here and vice versa. Only the
    /// cache misses are sent to the helper. Callback fires on the Qt event
    /// loop, and fires even when every URL fails — with an empty map — so a
    /// caller sequencing work behind it cannot be stranded.
    void extract_article_bodies(const QStringList& urls, BodiesCallback cb);

    /// Build the AI news brief over `articles`.
    ///
    /// `count` bounds the number of distinct STORIES the brief is written
    /// from, not the number of articles read: the pool is clustered first, so
    /// six outlets carrying one story cost one slot rather than six. Every
    /// category present is guaranteed a slot before the rest are filled on
    /// rank, so the breakdown covers the day rather than the loudest sector.
    ///
    /// Cached twice — once against the input pool, once against the generated
    /// prompt — so an unchanged feed and an unchanged SELECTION both return
    /// the previous brief verbatim. Callback fires on the Qt event loop.
    void summarize_headlines(const QVector<NewsArticle>& articles, int count, SummaryCallback cb);

    int feed_count() const { return feed_count_; }
    QStringList active_sources() const { return active_sources_; }

    void set_refresh_interval(int minutes);
    void start_auto_refresh();
    void stop_auto_refresh();

    void fetch_all_news_progressive(bool force, ArticlesCallback final_cb);

    /// Connect to a WebSocket endpoint for live breaking news push.
    /// If url is empty, uses default. Emits articles_partial on new data.
    void connect_live_feed(const QString& ws_url = {});
    void disconnect_live_feed();
    bool is_live_connected() const;

    /// Source credibility lookup.
    static SourceFlag source_flag_for(const QString& source);
    static QString source_flag_label(SourceFlag flag);

    /// Threat level helpers.
    static ThreatClassification classify_threat(const NewsArticle& article);
    /// Overload that accepts pre-built lowercased text to avoid redundant allocation.
    static ThreatClassification classify_threat(const NewsArticle& article, const QString& text);

  signals:
    void articles_updated(QVector<NewsArticle> articles);
    void analysis_ready(NewsAnalysis analysis);
    void articles_partial(QVector<NewsArticle> articles, int feeds_done, int feeds_total);

  private:
    NewsService();

    static QVector<RSSFeed> default_feeds();
    static QVector<NewsArticle> parse_rss_xml(const QByteArray& xml, const RSSFeed& feed);
    static void enrich_article(NewsArticle& article);
    static QString strip_html(const QString& html);

    /// Cache slot for one article's extracted body. Hashed because raw URLs
    /// carry query strings and tracking params that bloat the key and include
    /// characters SQLite handles awkwardly. Shared by the single-URL and batch
    /// extraction paths so a body fetched by either serves both.
    static QString article_body_cache_key(const QString& url);

    QNetworkAccessManager* nam_ = nullptr;
    QTimer* refresh_timer_ = nullptr;
    static constexpr int kArticleCacheTtlSec = 600; // 10 min
    static constexpr int kSummaryCacheTtlSec = 600;
    // Article bodies are static once published, so cache aggressively. The
    // first click on a story pays the ~1-3s extraction cost; every re-open
    // (same session or after restart, since CacheManager persists) hits
    // memory and renders instantly.
    static constexpr int kArticleBodyTtlSec = 7 * 24 * 60 * 60; // 7 days
    // The brief cache is addressed by the prompt text, so a hit is always the
    // brief those exact stories produced — reuse stays correct however old the
    // entry is. An hour, rather than the pool cache's ten minutes, because the
    // point of that tier is that a feed which keeps re-delivering the same top
    // stories keeps showing the same brief instead of rewording it hourly.
    static constexpr int kBriefCacheTtlSec = 60 * 60;
    // A brief that lost one of its two halves is worth showing but not worth
    // keeping: short enough that the next press retries the missing half,
    // long enough to absorb a double-click.
    static constexpr int kPartialBriefCacheTtlSec = 60;
    int feed_count_ = 0;
    QStringList active_sources_;

    // WebSocket live feed
    QWebSocket* live_ws_ = nullptr;
    bool live_connected_ = false;

    /// Publish `news:general` + fan out `news:symbol:<sym>` and
    /// `news:category:<cat>` derived slices. Called from both the
    /// progressive partial-snapshot path and the final fetch path —
    /// subscribers see the same accumulated list each time, so the
    /// progressive-publish pattern (see DATAHUB_ARCHITECTURE.md §11)
    /// collapses to a series of full-list republishes.
    void publish_articles_to_hub(const QVector<NewsArticle>& accumulated);
    bool hub_registered_ = false;
};

// ── Helpers ─────────────────────────────────────────────────────────────────

QString priority_string(Priority p);
QString sentiment_string(Sentiment s);
QString impact_string(Impact i);
QString priority_color(Priority p);
QString sentiment_color(Sentiment s);
QString relative_time(int64_t unix_ts);
QString threat_level_string(ThreatLevel t);
QString threat_level_color(ThreatLevel t);

// Round-trip helpers (string → enum)
Priority priority_from_string(const QString& s);
Sentiment sentiment_from_string(const QString& s);
Impact impact_from_string(const QString& s);

} // namespace fincept::services

#include <QMetaType>
Q_DECLARE_METATYPE(fincept::services::NewsArticle)
Q_DECLARE_METATYPE(QVector<fincept::services::NewsArticle>)
