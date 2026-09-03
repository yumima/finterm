#include "services/news/NewsService.h"

#include "services/news/NewsBriefCacheKey.h"
#include "services/news/NewsBriefPrompts.h"
#include "services/news/NewsBriefSelection.h"
#include "services/news/NewsCategories.h"
#include "services/news/NewsClusterService.h"

#include "ai_chat/Degeneracy.h"
#include "ai_chat/LlmService.h"
#include "core/logging/Logger.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"
#include "services/app_context/AppContextService.h"
#include "storage/cache/CacheManager.h"
#include "storage/repositories/PortfolioRepository.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent>

#    include "datahub/DataHub.h"
#    include "datahub/DataHubMetaTypes.h"

#include <QAtomicInt>
#include <QDateTime>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>

#ifdef HAS_QT_WEBSOCKETS
#    include <QtWebSockets/QWebSocket>
#endif

#include <algorithm>
#include <memory>

namespace fincept::services {

static constexpr int kFeedTransferTimeoutMs = 8000;   // 8s per RSS feed request

// Model role used for AI briefs (TL;DR / DIGEST). A hearth role name rather
// than a concrete model so it follows whatever the user maps it to.
constexpr const char* kBriefModelRole = "fast_chat";

// Bump when the brief prompts change in a way that alters output, so entries
// cached under the previous prompt are not served for up to the summary TTL.
//
// Only the POOL-level key needs this. The brief-level key hashes the finished
// prompt text, so a prompt edit changes it for free — this exists for the
// stage-0 key, which is deliberately computed before the prompts are built.
//
// 4: two prompts instead of one, written from ranked story blocks with article
// bodies rather than from the newest 35 headlines, with the breakdown's
// headings fixed by the selection instead of chosen by the model.
constexpr int kBriefPromptVersion = 4;
// Only retry a collapsed brief when the first attempt came back inside this.
// Slower than this and a second pass risks outliving the user's patience and
// the caller's own timeout; the error is the better answer. See
// news_run_brief_call for why chat() is not a single bounded request.
constexpr qint64 kBriefRetryBudgetMs = 45000;
static constexpr int kWsReconnectDelayMs    = 10000;  // 10s before WebSocket reconnect
static constexpr int kSummaryMaxChars       = 300;    // max chars for article summary

// Use a real browser User-Agent — Bloomberg, WSJ, FT and other major
// publishers reject "finterm/4.0" as scraper traffic. Browser UA
// gets us 200s on the same endpoints.
static constexpr const char* kBrowserUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// ── Singleton ───────────────────────────────────────────────────────────────

NewsService& NewsService::instance() {
    static NewsService s;
    return s;
}

NewsService::NewsService() {
    nam_ = new QNetworkAccessManager(this);
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(kArticleCacheTtlSec * 1000);
    connect(refresh_timer_, &QTimer::timeout, this,
            [this]() { fetch_all_news(true, [](bool, QVector<NewsArticle>) {}); });
}

// ── Fetch all RSS feeds in parallel ─────────────────────────────────────────

void NewsService::fetch_all_news(bool force, ArticlesCallback cb) {
    if (!force) {
        const QVariant cached = fincept::CacheManager::instance().get("news:articles");
        if (!cached.isNull()) {
            const QJsonArray arr = QJsonDocument::fromJson(cached.toString().toUtf8()).array();
            QVector<NewsArticle> articles;
            articles.reserve(arr.size());
            for (const auto& v : arr) {
                const QJsonObject o = v.toObject();
                NewsArticle a;
                a.id = o["id"].toString();
                a.time = o["time"].toString();
                a.headline = o["headline"].toString();
                a.summary = o["summary"].toString();
                a.source = o["source"].toString();
                a.region = o["region"].toString();
                a.category = o["category"].toString();
                a.link = o["link"].toString();
                a.sort_ts = o["sort_ts"].toVariant().toLongLong();
                a.tier = o["tier"].toInt(4);
                a.priority = priority_from_string(o["priority"].toString());
                a.sentiment = sentiment_from_string(o["sentiment"].toString());
                a.impact = impact_from_string(o["impact"].toString());
                a.lang = o["lang"].toString();
                for (const auto& t : o["tickers"].toArray())
                    a.tickers << t.toString();
                articles.append(a);
            }
            cb(true, articles);
            publish_articles_to_hub(articles);
            return;
        }
    }

    auto feeds = default_feeds();
    feed_count_ = feeds.size();

    // Shared state for collecting results from parallel requests
    struct FetchState {
        QMutex mutex;
        QVector<NewsArticle> all_articles;
        QAtomicInt remaining{0};
        ArticlesCallback callback;
        NewsService* service = nullptr;
    };

    auto state = std::make_shared<FetchState>();
    state->remaining.storeRelaxed(feeds.size());
    state->callback = std::move(cb);
    state->service = this;

    for (const auto& feed : feeds) {
        QNetworkRequest req(QUrl(feed.url));
        req.setHeader(QNetworkRequest::UserAgentHeader, kBrowserUserAgent);
        req.setRawHeader("Accept", "application/rss+xml, application/xml, text/xml, */*");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(kFeedTransferTimeoutMs);

        auto* reply = nam_->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, feed, state]() {
            reply->deleteLater();

            QVector<NewsArticle> articles;
            const int http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                const QByteArray trimmed = data.trimmed();
                // Cheap shape check: real RSS/Atom starts with <?xml or <rss or
                // <feed. HTML error pages (Akamai access-denied, Cloudflare
                // captcha) start with <html / <!doctype and would pass through
                // the parser silently producing 0 articles — flag them clearly.
                const bool looks_like_html =
                    trimmed.left(20).toLower().contains("<html") ||
                    trimmed.left(20).toLower().contains("<!doctype html");
                if (looks_like_html) {
                    LOG_WARN("NewsService",
                             QString("Feed %1 (%2) returned HTML (likely access-denied), %3 bytes")
                                 .arg(feed.id, feed.source).arg(data.size()));
                } else if (trimmed.startsWith('<')) {
                    articles = parse_rss_xml(data, feed);
                }
                if (articles.isEmpty() && !looks_like_html) {
                    LOG_WARN("NewsService", QString("Feed %1 (%2) returned %3 bytes but no parsed articles")
                                                .arg(feed.id, feed.source).arg(data.size()));
                }
            } else {
                LOG_WARN("NewsService", QString("Feed %1 (%2) failed: HTTP %3, err=%4")
                                            .arg(feed.id, feed.source).arg(http_code).arg(reply->errorString()));
            }

            {
                QMutexLocker lock(&state->mutex);
                state->all_articles.append(articles);
            }

            if (state->remaining.fetchAndSubRelaxed(1) == 1) {
                // Last feed done — sort by time descending
                auto& all = state->all_articles;
                std::sort(all.begin(), all.end(),
                          [](const NewsArticle& a, const NewsArticle& b) { return a.sort_ts > b.sort_ts; });

                QSet<QString> sources;
                for (const auto& a : all)
                    sources.insert(a.source);
                state->service->active_sources_ = sources.values();

                // Serialize to CacheManager
                QJsonArray arr;
                for (const auto& a : all) {
                    QJsonObject o;
                    o["id"] = a.id;
                    o["time"] = a.time;
                    o["headline"] = a.headline;
                    o["summary"] = a.summary;
                    o["source"] = a.source;
                    o["region"] = a.region;
                    o["category"] = a.category;
                    o["link"] = a.link;
                    o["sort_ts"] = static_cast<qint64>(a.sort_ts);
                    o["tier"] = a.tier;
                    o["priority"] = priority_string(a.priority);
                    o["sentiment"] = sentiment_string(a.sentiment);
                    o["impact"] = impact_string(a.impact);
                    o["lang"] = a.lang;
                    QJsonArray tickers;
                    for (const auto& t : a.tickers)
                        tickers.append(t);
                    o["tickers"] = tickers;
                    arr.append(o);
                }
                fincept::CacheManager::instance().put(
                    "news:articles", QVariant(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))),
                    kArticleCacheTtlSec, "news");

                LOG_INFO("NewsService",
                         QString("Fetched %1 articles from %2 sources").arg(all.size()).arg(sources.size()));

                state->callback(true, all);
                emit state->service->articles_updated(all);
                state->service->publish_articles_to_hub(all);
            }
        });
    }
}

// ── Progressive fetch — emits partial batches as each feed arrives ───────────
// First few fast feeds (Reuters, BBC) typically arrive in <300ms giving the
// screen something to render immediately while slower feeds trickle in.

void NewsService::fetch_all_news_progressive(bool force, ArticlesCallback final_cb) {
    if (!force) {
        const QVariant cached = fincept::CacheManager::instance().get("news:articles");
        if (!cached.isNull()) {
            const QJsonArray arr = QJsonDocument::fromJson(cached.toString().toUtf8()).array();
            QVector<NewsArticle> articles;
            articles.reserve(arr.size());
            for (const auto& v : arr) {
                const QJsonObject o = v.toObject();
                NewsArticle a;
                a.id = o["id"].toString();
                a.time = o["time"].toString();
                a.headline = o["headline"].toString();
                a.summary = o["summary"].toString();
                a.source = o["source"].toString();
                a.region = o["region"].toString();
                a.category = o["category"].toString();
                a.link = o["link"].toString();
                a.sort_ts = o["sort_ts"].toVariant().toLongLong();
                a.tier = o["tier"].toInt(4);
                a.priority = priority_from_string(o["priority"].toString());
                a.sentiment = sentiment_from_string(o["sentiment"].toString());
                a.impact = impact_from_string(o["impact"].toString());
                a.lang = o["lang"].toString();
                for (const auto& t : o["tickers"].toArray())
                    a.tickers << t.toString();
                articles.append(a);
            }
            final_cb(true, articles);
            emit articles_partial(articles, feed_count_, feed_count_);
            publish_articles_to_hub(articles);
            return;
        }
    }

    auto feeds = default_feeds();
    feed_count_ = feeds.size();
    const int total = feeds.size();

    struct FetchState {
        QMutex mutex;
        QVector<NewsArticle> all_articles;
        QAtomicInt remaining{0};
        QAtomicInt done{0};
        ArticlesCallback callback;
        NewsService* service = nullptr;
    };

    auto state = std::make_shared<FetchState>();
    state->remaining.storeRelaxed(total);
    state->callback = std::move(final_cb);
    state->service = this;

    for (const auto& feed : feeds) {
        QNetworkRequest req(QUrl(feed.url));
        req.setHeader(QNetworkRequest::UserAgentHeader, kBrowserUserAgent);
        req.setRawHeader("Accept", "application/rss+xml, application/xml, text/xml, */*");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(kFeedTransferTimeoutMs);

        auto* reply = nam_->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, feed, state, total, this]() {
            reply->deleteLater();

            QVector<NewsArticle> batch;
            const int http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray data = reply->readAll();
                const QByteArray trimmed = data.trimmed();
                const bool looks_like_html =
                    trimmed.left(20).toLower().contains("<html") ||
                    trimmed.left(20).toLower().contains("<!doctype html");
                if (looks_like_html) {
                    LOG_WARN("NewsService",
                             QString("Feed %1 (%2) returned HTML (likely access-denied), %3 bytes")
                                 .arg(feed.id, feed.source).arg(data.size()));
                } else if (trimmed.startsWith('<')) {
                    batch = parse_rss_xml(data, feed);
                }
                if (batch.isEmpty() && !looks_like_html) {
                    LOG_WARN("NewsService", QString("Feed %1 (%2) returned %3 bytes but no parsed articles")
                                                .arg(feed.id, feed.source).arg(data.size()));
                }
            } else {
                LOG_WARN("NewsService", QString("Feed %1 (%2) failed: HTTP %3, err=%4")
                                            .arg(feed.id, feed.source).arg(http_code).arg(reply->errorString()));
            }

            QVector<NewsArticle> snapshot;
            int feeds_done = 0;
            {
                QMutexLocker lock(&state->mutex);
                state->all_articles.append(batch);
                feeds_done = state->done.fetchAndAddRelaxed(1) + 1;
                // Partial snapshot sorted by time for progressive display
                snapshot = state->all_articles;
            }
            std::sort(snapshot.begin(), snapshot.end(),
                      [](const NewsArticle& a, const NewsArticle& b) { return a.sort_ts > b.sort_ts; });
            emit articles_partial(snapshot, feeds_done, total);
            // Progressive publish — each chunk fans out the accumulated
            // list. Hub's per-topic coalescing (news:general at 250ms)
            // throttles the UI repaint storm on cold-cache fills.
            publish_articles_to_hub(snapshot);

            if (state->remaining.fetchAndSubRelaxed(1) == 1) {
                // All feeds done — finalize cache
                auto& all = state->all_articles;
                std::sort(all.begin(), all.end(),
                          [](const NewsArticle& a, const NewsArticle& b) { return a.sort_ts > b.sort_ts; });

                QSet<QString> sources;
                for (const auto& a : all)
                    sources.insert(a.source);
                state->service->active_sources_ = sources.values();

                QJsonArray parr;
                for (const auto& a : all) {
                    QJsonObject o;
                    o["id"] = a.id;
                    o["time"] = a.time;
                    o["headline"] = a.headline;
                    o["summary"] = a.summary;
                    o["source"] = a.source;
                    o["region"] = a.region;
                    o["category"] = a.category;
                    o["link"] = a.link;
                    o["sort_ts"] = static_cast<qint64>(a.sort_ts);
                    o["tier"] = a.tier;
                    o["priority"] = priority_string(a.priority);
                    o["sentiment"] = sentiment_string(a.sentiment);
                    o["impact"] = impact_string(a.impact);
                    o["lang"] = a.lang;
                    QJsonArray tickers;
                    for (const auto& t : a.tickers)
                        tickers.append(t);
                    o["tickers"] = tickers;
                    parr.append(o);
                }
                fincept::CacheManager::instance().put(
                    "news:articles", QVariant(QString::fromUtf8(QJsonDocument(parr).toJson(QJsonDocument::Compact))),
                    kArticleCacheTtlSec, "news");

                LOG_INFO(
                    "NewsService",
                    QString("Progressive fetch complete: %1 articles, %2 sources").arg(all.size()).arg(sources.size()));

                state->callback(true, all);
                emit state->service->articles_updated(all);
                state->service->publish_articles_to_hub(all);
            }
        });
    }
}

// ── AI Analysis (local LLM engine) ──────────────────────────────────────────
//
// The old cloud endpoint (/news/analyze on fincept.in, credits-metered) is gone
// in the localhost build. We extract the article text and analyse it on-device
// through the local LLM (hearth), asking for structured JSON we map onto
// NewsAnalysis. Both the "Analyze" button and the analyze_news_article MCP tool
// flow through here, so this one change fixes both.

namespace {

// Prompt the local model for structured analysis as strict JSON. Long bodies
// are truncated to stay within the context budget.
QString news_build_analysis_prompt(const QString& title, const QString& body) {
    QString text = body;
    if (text.size() > 8000)
        text = text.left(8000) + "\n…[truncated]";
    return QString(
               "You are a financial news analyst. Analyse the article and reply with ONLY a JSON "
               "object (no markdown, no commentary) of exactly this shape:\n"
               "{\"summary\":\"2-3 sentences\","
               "\"sentiment\":{\"score\":-1..1,\"intensity\":0..1,\"confidence\":0..1},"
               "\"market_impact\":{\"urgency\":\"LOW|MEDIUM|HIGH\","
               "\"prediction\":\"negative|neutral|moderate_positive|positive\"},"
               "\"keywords\":[],\"topics\":[],\"key_points\":[],"
               "\"risk_signals\":{\"regulatory\":{\"level\":\"none|low|medium|high\",\"details\":\"\"},"
               "\"geopolitical\":{\"level\":\"\",\"details\":\"\"},"
               "\"operational\":{\"level\":\"\",\"details\":\"\"},"
               "\"market\":{\"level\":\"\",\"details\":\"\"}}}\n\n"
               "Title: %1\n\nArticle:\n%2")
        .arg(title.isEmpty() ? QStringLiteral("(untitled)") : title, text);
}

// One brief request, with the collapse retry.
//
// Time-gated, because chat() is not one request: it walks a quota-fallback
// chain of up to kMaxQuotaHops more on a 429, each with its own 120s ceiling.
// A first attempt that came back fast has room for a second; one that crawled
// does not, and the user is better served by the error. The failure is
// stochastic — same prompt, same model, and the next pass is normally clean —
// so rejecting on the first collapse spends a real request to display "AI
// brief unavailable", which is what the user actually saw.
ai_chat::LlmResponse news_run_brief_call(const QString& prompt, const ai_chat::PersonaScope& scope,
                                         const char* what) {
    QElapsedTimer clock;
    clock.start();
    auto resp = ai_chat::LlmService::instance().chat(prompt, {}, /*use_tools=*/false, scope);
    if (resp.success && ai_chat::looks_degenerate(resp.content.trimmed())) {
        if (clock.elapsed() > kBriefRetryBudgetMs) {
            LOG_WARN("NewsService", QString("brief %1 collapsed after %2ms — over the retry "
                                            "budget, giving up")
                                        .arg(QLatin1String(what))
                                        .arg(clock.elapsed()));
            return resp;
        }
        LOG_WARN("NewsService", QString("brief %1 collapsed, retrying once").arg(QLatin1String(what)));
        resp = ai_chat::LlmService::instance().chat(prompt, {}, /*use_tools=*/false, scope);
    }
    return resp;
}

// Map the model's JSON onto NewsAnalysis. Reasoning models can wrap the object
// in prose/fences, so slice the outermost {...} before parsing.
bool news_parse_analysis(const QString& content, NewsAnalysis& out) {
    const int s = content.indexOf('{');
    const int e = content.lastIndexOf('}');
    if (s < 0 || e <= s)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(content.mid(s, e - s + 1).toUtf8());
    if (!doc.isObject())
        return false;
    const QJsonObject a = doc.object();
    // Reject objects that aren't analysis-shaped (e.g. {} or an {"error":...}
    // object) so they route to the error path instead of rendering as blanks.
    if (!a.contains("summary") && !a.contains("sentiment") && !a.contains("key_points"))
        return false;
    const QJsonObject sent = a.value("sentiment").toObject();
    const QJsonObject mi = a.value("market_impact").toObject();
    const QJsonObject rs = a.value("risk_signals").toObject();
    out.summary = a.value("summary").toString();
    out.sentiment = {sent.value("score").toDouble(), sent.value("intensity").toDouble(),
                     sent.value("confidence").toDouble()};
    out.market_impact = {mi.value("urgency").toString(), mi.value("prediction").toString()};
    for (const auto& v : a.value("keywords").toArray())
        out.keywords << v.toString();
    for (const auto& v : a.value("topics").toArray())
        out.topics << v.toString();
    for (const auto& v : a.value("key_points").toArray())
        out.key_points << v.toString();
    auto sig = [&rs](const char* k) {
        const QJsonObject o = rs.value(QString::fromLatin1(k)).toObject();
        return RiskSignal{o.value("level").toString(), o.value("details").toString()};
    };
    out.regulatory = sig("regulatory");
    out.geopolitical = sig("geopolitical");
    out.operational = sig("operational");
    out.market = sig("market");
    return true;
}

} // namespace

void NewsService::analyze_article(const QString& url, AnalysisCallback cb) {
    extract_article_body(url, [this, cb](bool ok, QString title, QString text) {
        if (!ok || text.trimmed().isEmpty()) {
            LOG_WARN("NewsService", "analyze_article: could not extract article body for analysis");
            cb(false, {});
            return;
        }
        const QString prompt = news_build_analysis_prompt(title, text);
        // chat() is blocking + uses a synchronous HTTP loop, so run it off the
        // UI thread; the watcher's finished() lands back on the UI thread.
        auto* watcher = new QFutureWatcher<ai_chat::LlmResponse>(this);
        QObject::connect(watcher, &QFutureWatcher<ai_chat::LlmResponse>::finished, this,
                         [this, watcher, cb]() {
                             const ai_chat::LlmResponse resp = watcher->result();
                             watcher->deleteLater();
                             NewsAnalysis analysis;
                             if (!resp.success || !news_parse_analysis(resp.content, analysis)) {
                                 LOG_WARN("NewsService",
                                          "analyze_article: local analysis failed: "
                                              + (resp.success ? QStringLiteral("unparseable response")
                                                              : resp.error));
                                 cb(false, {});
                                 return;
                             }
                             cb(true, analysis);
                             emit this->analysis_ready(analysis);
                         });
        // Constrained JSON decoding, and no chain-of-thought. The parser here
        // takes the first '{' to the last '}' and needs valid JSON in between;
        // small local models emit nearly-valid JSON often enough — a dropped
        // comma between two fields — that analysis failed on some articles and
        // not others, with nothing to distinguish them. Measured against the
        // configured local model, roughly one request in three was unparseable
        // free-form and none were with response_format set.
        ai_chat::PersonaScope scope;
        scope.json_object = true;
        scope.think = false;   // a one-shot structured extraction, not reasoning
        watcher->setFuture(QtConcurrent::run([prompt, scope]() {
            return ai_chat::LlmService::instance().chat(prompt, {}, /*use_tools=*/false, scope);
        }));
    });
}

// ── Article body extraction (trafilatura via Python helper) ─────────────────
//
// NewsDetailPanel calls this on article click to populate the ARTICLE section
// below the action buttons. The Python helper does the network fetch and
// boilerplate strip in-process, so we don't have to ship a C++ readability
// implementation. Cached aggressively because article content doesn't change
// once published — see kArticleBodyTtlSec.

QString NewsService::article_body_cache_key(const QString& url) {
    // SHA1 hex collapses any URL to 40 chars.
    return QStringLiteral("news:body:")
           + QString::fromLatin1(
               QCryptographicHash::hash(url.trimmed().toUtf8(), QCryptographicHash::Sha1).toHex());
}

void NewsService::extract_article_body(const QString& url, BodyCallback cb) {
    if (url.trimmed().isEmpty()) {
        cb(false, {}, {});
        return;
    }

    const QString cache_key = article_body_cache_key(url);

    const QVariant cached = CacheManager::instance().get(cache_key);
    if (!cached.isNull()) {
        const auto obj = QJsonDocument::fromJson(cached.toString().toUtf8()).object();
        cb(true, obj.value("title").toString(), obj.value("text").toString());
        return;
    }

    // 25 s budget — most extracts finish in ~1–3 s, but a slow paywall page
    // or upstream DNS hiccup can stretch out. Stays under the default 30 s
    // process timeout in PythonRunner.
    constexpr int kExtractTimeoutMs = 25'000;

    python::PythonRunner::instance().run(
        QStringLiteral("extract_article.py"),
        QStringList{url},
        [cb, cache_key](python::PythonResult result) {
            if (!result.success || result.output.trimmed().isEmpty()) {
                LOG_WARN("NewsService",
                         "extract_article failed: exit=" + QString::number(result.exit_code)
                             + " err=" + result.error.left(200));
                cb(false, {}, {});
                return;
            }
            const auto doc = QJsonDocument::fromJson(result.output.toUtf8());
            if (!doc.isObject()) {
                cb(false, {}, {});
                return;
            }
            const auto obj = doc.object();
            if (!obj.value("success").toBool(false)) {
                cb(false, {}, {});
                return;
            }
            const QString title = obj.value("title").toString();
            const QString text  = obj.value("text").toString();
            // Cache the parsed object (not the raw stdout) so a malformed
            // future extractor version with extra fields doesn't pollute the
            // cache slot we read back.
            QJsonObject store;
            store["title"] = title;
            store["text"]  = text;
            const QString blob = QString::fromUtf8(QJsonDocument(store).toJson(QJsonDocument::Compact));
            CacheManager::instance().put(cache_key, QVariant(blob), kArticleBodyTtlSec, "news");
            cb(true, title, text);
        },
        /*on_line=*/{}, /*timeout_ms=*/kExtractTimeoutMs);
}
// ── Batch article-body extraction ───────────────────────────────────────────
//
// The brief needs the opening paragraphs of its top stories before it can
// write a word. Doing that through extract_article_body() once per URL is the
// wrong shape: PythonRunner caps concurrency at 3 processes, so eight URLs
// serialise into three waves, and every process in every wave re-pays the
// trafilatura/lxml import cost. One process fetching them on a thread pool
// costs the slowest single fetch instead.

void NewsService::extract_article_bodies(const QStringList& urls, BodiesCallback cb) {
    auto out = std::make_shared<QHash<QString, QString>>();

    // Split into hits and misses first. A story the reader already opened has
    // its body in the same cache slot the reading pane filled, so the common
    // case for a re-run of the brief is that nothing is sent to Python at all.
    QStringList misses;
    QSet<QString> seen;
    for (const QString& raw : urls) {
        const QString url = raw.trimmed();
        if (url.isEmpty() || seen.contains(url))
            continue;
        seen.insert(url);
        const QVariant cached = CacheManager::instance().get(article_body_cache_key(url));
        if (!cached.isNull()) {
            const QString text =
                QJsonDocument::fromJson(cached.toString().toUtf8()).object().value("text").toString();
            if (!text.isEmpty())
                out->insert(url, text);
            continue;
        }
        misses.append(url);
    }

    if (misses.isEmpty()) {
        cb(*out);
        return;
    }

    // Same 25 s budget as the single-URL path — the helper fetches in
    // parallel, so the batch takes about as long as its slowest member rather
    // than the sum. Stays under PythonRunner's default 30 s process timeout.
    constexpr int kExtractTimeoutMs = 25'000;

    python::PythonRunner::instance().run(
        QStringLiteral("extract_articles_batch.py"), misses,
        [cb, out](python::PythonResult result) {
            // A failed batch is not a failed brief: every story still has its
            // headline and RSS blurb. Hand back whatever the cache gave us.
            if (!result.success || result.output.trimmed().isEmpty()) {
                LOG_WARN("NewsService", "extract_articles_batch failed: exit="
                                            + QString::number(result.exit_code) + " err="
                                            + result.error.left(200));
                cb(*out);
                return;
            }
            const QJsonArray arr =
                QJsonDocument::fromJson(result.output.toUtf8()).object().value("results").toArray();
            int ok_count = 0;
            for (const auto& v : arr) {
                const QJsonObject o = v.toObject();
                const QString url = o.value("url").toString();
                const QString text = o.value("text").toString();
                if (!o.value("success").toBool(false) || text.isEmpty() || url.isEmpty())
                    continue;
                ++ok_count;
                out->insert(url, text);
                // Write through to the SHARED per-URL slot, in the same shape
                // extract_article_body() stores and reads. A body pulled for
                // the brief is the body the reading pane shows when the reader
                // clicks that story, and vice versa — one fetch serves both.
                QJsonObject store;
                store["title"] = o.value("title").toString();
                store["text"] = text;
                CacheManager::instance().put(
                    article_body_cache_key(url),
                    QVariant(QString::fromUtf8(QJsonDocument(store).toJson(QJsonDocument::Compact))),
                    kArticleBodyTtlSec, "news");
            }
            LOG_INFO("NewsService", QString("extract_articles_batch: %1/%2 bodies extracted")
                                        .arg(ok_count).arg(arr.size()));
            cb(*out);
        },
        /*on_line=*/{}, /*timeout_ms=*/kExtractTimeoutMs);
}

namespace {

/// Thread pool for brief generation, kept off QThreadPool::globalInstance().
///
/// Each half of a brief blocks a thread for the whole request — up to 120s per
/// hop, times the quota-fallback chain, plus a collapse retry. Two halves run
/// concurrently, and TL;DR and DIGEST are separate pipelines over different
/// pools, so a user who presses both has four threads parked on the network.
/// The global pool defaults to the core count and is what
/// NewsScreen::apply_filters_async and cluster_articles run on; on a four-core
/// box those briefs would hold the whole pool and the feed would stop
/// responding to filter changes for minutes.
///
/// Four threads: enough for both surfaces at once, and a fifth request queues
/// rather than adding another parked thread.
QThreadPool& brief_pool() {
    static QThreadPool pool;
    static bool configured = [] {
        pool.setMaxThreadCount(4);
        pool.setObjectName(QStringLiteral("news-brief"));
        return true;
    }();
    Q_UNUSED(configured)
    return pool;
}

} // namespace

// ── AI Headline Summarization ────────────────────────────────────────────────
//
// Five stages, event-loop driven end to end:
//
//   0. POOL CACHE   — an unchanged feed returns the previous brief immediately.
//   1. SELECT       — cluster the pool into stories, rank, take the top N with
//                     one slot guaranteed per category (worker thread; the
//                     clustering is O(n²) over several hundred articles).
//   2. ENRICH       — pull article bodies for the highest-ranked few, in one
//                     Python process.
//   3. BRIEF CACHE  — key on the finished prompt text, so a run whose SELECTION
//                     is unchanged reuses the brief even though the raw feed
//                     underneath it has moved on. This is what makes two briefs
//                     minutes apart agree with each other.
//   4. GENERATE     — the two halves concurrently, each with its own token
//                     budget, reassembled around the <<<CATEGORIES>>> marker
//                     the reading pane already splits on.
//
// `count` bounds the number of STORIES, not articles: deduplication happens
// before it applies, so 20 means twenty distinct stories rather than twenty
// rows off the top of a feed that may hold six copies of one of them.

void NewsService::summarize_headlines(const QVector<NewsArticle>& articles, int count, SummaryCallback cb) {
    if (articles.isEmpty()) {
        cb(false, {});
        return;
    }

    const QString pf_id = services::AppContextService::instance().snapshot().portfolio_id;

    // ── Stage 0: pool cache ─────────────────────────────────────────────────
    // Keyed on the whole input, so it can be checked before any work happens
    // — no clustering, no extraction, no model call. brief_cache::key hashes
    // the full sorted headline set, so an unchanged feed is an exact hit and a
    // changed one cannot collide with it.
    QStringList pool_headlines;
    pool_headlines.reserve(articles.size());
    for (const auto& a : articles)
        pool_headlines.append(a.headline);
    const QString pool_key = brief_cache::key(pool_headlines, pf_id, count, kBriefPromptVersion);
    {
        const QVariant cached = fincept::CacheManager::instance().get(pool_key);
        if (!cached.isNull()) {
            cb(true, cached.toString());
            return;
        }
    }

    // Holdings, read once here so the worker thread does not touch the DB.
    QStringList held;
    QSet<QString> held_set;
    if (!pf_id.isEmpty()) {
        const auto assets_r = PortfolioRepository::instance().get_assets(pf_id);
        if (assets_r.is_ok()) {
            for (const auto& a : assets_r.value()) {
                const QString s = a.symbol.trimmed().toUpper();
                if (!s.isEmpty() && !held_set.contains(s)) {
                    held_set.insert(s);
                    held.append(s);
                }
            }
        }
    }

    // ── Stage 1: cluster + select, off the UI thread ────────────────────────
    using news::brief_select::Story;
    news::brief_select::Config cfg;
    // A caller asking for fewer stories than there are categories is asking
    // for an incomplete breakdown; the lower bound keeps the brief worth
    // rendering. The upper bound is prompt size — past ~30 blocks the model
    // stops distinguishing them and the top-stories half degrades.
    if (count > 0)
        cfg.max_stories = std::clamp(count, 6, 30);

    auto* select_watcher = new QFutureWatcher<QVector<Story>>(this);
    QObject::connect(
        select_watcher, &QFutureWatcher<QVector<Story>>::finished, this,
        [this, select_watcher, cb, cfg, held, pool_key]() {
            const QVector<Story> stories = select_watcher->result();
            select_watcher->deleteLater();

            if (stories.isEmpty()) {
                LOG_WARN("NewsService", "summarize_headlines: selection produced no stories");
                cb(false, {});
                return;
            }

            // ── Stage 2: enrich the top stories with article bodies ─────────
            // Trimmed on the way out AND on the way back: Atom feeds store
            // link as an attribute and parse_rss_xml does not trim those, and
            // extract_article_bodies keys its result map by the trimmed URL.
            // Looking the body up under the untrimmed link fetched it, cached
            // it, counted it in the log — and then dropped it before the
            // prompt, silently, for exactly the stories chosen for enrichment.
            QStringList to_enrich;
            for (const Story& s : stories) {
                if (s.enrich && !s.link.trimmed().isEmpty())
                    to_enrich.append(s.link.trimmed());
            }

            extract_article_bodies(
                to_enrich, [this, stories, cb, cfg, held, pool_key](QHash<QString, QString> bodies) mutable {
                    QVector<Story> enriched = stories;
                    for (Story& s : enriched)
                        s.body = bodies.value(s.link.trimmed());

                    const QStringList categories = news::brief_select::categories_present(enriched, cfg);
                    const QString top_block = news::brief_select::render_stories(enriched);
                    const QString grouped_block =
                        news::brief_select::render_stories_by_category(enriched, categories);

                    QString portfolio_block;
                    if (!held.isEmpty()) {
                        QStringList in_news;
                        for (const Story& s : enriched) {
                            if (s.portfolio_hit)
                                in_news.append(s.portfolio_tickers.join(QStringLiteral(", ")) + " — "
                                               + s.headline);
                        }
                        portfolio_block = "Holdings: " + held.join(", ");
                        portfolio_block +=
                            in_news.isEmpty()
                                ? "\n(No holding appears directly in today's stories.)"
                                : "\nHoldings in today's stories:\n" + in_news.join("\n");
                    }

                    const QString top_prompt = news::brief_prompt::build_top(top_block, portfolio_block);
                    const QString breakdown_prompt =
                        categories.isEmpty() ? QString()
                                             : news::brief_prompt::build_breakdown(grouped_block, categories);

                    // ── Stage 3: brief cache, keyed on the prompts themselves ──
                    const QString content_key =
                        news::brief_select::brief_key(top_prompt, breakdown_prompt);
                    {
                        const QVariant cached = fincept::CacheManager::instance().get(content_key);
                        if (!cached.isNull()) {
                            const QString hit = cached.toString();
                            // Fill the pool slot too, so the next call over
                            // this same feed short-circuits at stage 0 instead
                            // of re-clustering to reach the same answer.
                            fincept::CacheManager::instance().put(pool_key, QVariant(hit),
                                                                  kSummaryCacheTtlSec, "news");
                            cb(true, hit);
                            return;
                        }
                    }

                    // ── Stage 4: generate both halves concurrently ──────────
                    ai_chat::PersonaScope scope;
                    // think=false: a short structured one-shot is exactly what
                    // the flag exists for. It is a request rather than a
                    // guarantee — Ollama's /v1 route drops it, see
                    // build_openai_request — so the token budgets below are
                    // sized to hold a chain-of-thought anyway.
                    scope.think = false;
                    // The whole point of a content-addressed cache is that the
                    // same stories give the same brief; at the provider's
                    // default temperature they instead give two differently
                    // worded reads of the same day and the reader cannot tell
                    // that apart from the news having changed.
                    scope.temperature = 0.0;
                    // Run briefs on the fast role rather than the configured
                    // chat model. Measured against hearth: fast_chat (qwen3:14b)
                    // returns 495 completion tokens in 30s, primary_chat
                    // (qwen3:30b-a3b) 1554 in 40s standalone — and far worse
                    // in-app, because 30b-a3b exceeds this GPU's VRAM and
                    // spills to CPU. It also ignores think:false. Resolve the
                    // bound model for the "news" role, falling back to the
                    // hearth alias only when we are actually on hearth: on a
                    // cloud provider that alias is a model name it never heard
                    // of. See AiRoles.h.
                    {
                        const auto target = ai_chat::LlmService::instance().scope_for_role(
                            QStringLiteral("news"), QString::fromLatin1(kBriefModelRole));
                        scope.provider = target.provider;
                        scope.model = target.model;
                        scope.api_key = target.api_key;
                        scope.base_url = target.base_url;
                    }

                    struct Job {
                        QString top;
                        QString breakdown;
                        bool top_ok = false;
                        int pending = 0;
                    };
                    auto job = std::make_shared<Job>();
                    job->pending = breakdown_prompt.isEmpty() ? 1 : 2;

                    // Both watchers are parented to this service and therefore
                    // fire on its thread, so `job` needs no lock.
                    const QString wanted_breakdown = breakdown_prompt;
                    auto finish = [job, cb, content_key, pool_key, wanted_breakdown]() {
                        if (--job->pending > 0)
                            return;
                        if (!job->top_ok) {
                            cb(false, {});
                            return;
                        }
                        // The breakdown is optional. A brief whose top half
                        // arrived is worth showing even if the second call
                        // failed — split() handles a brief with no marker, and
                        // half a brief beats "AI brief unavailable".
                        QString out = job->top;
                        const bool complete = job->breakdown.isEmpty() == wanted_breakdown.isEmpty();
                        if (!job->breakdown.isEmpty())
                            out += QStringLiteral("\n\n")
                                   + QString(news::kCategoryMarker) + QStringLiteral("\n")
                                   + job->breakdown;
                        // Only a COMPLETE brief earns the content-addressed
                        // slot. That key is a hash of the prompts, so caching a
                        // brief whose breakdown call errored, timed out or
                        // collapsed would pin the half-answer to this selection
                        // for the full hour and never retry the missing half —
                        // turning one transient failure into a permanently
                        // category-less brief. Show it now, ask again next time.
                        if (complete)
                            fincept::CacheManager::instance().put(content_key, QVariant(out),
                                                                  kBriefCacheTtlSec, "news");
                        fincept::CacheManager::instance().put(
                            pool_key, QVariant(out),
                            complete ? kSummaryCacheTtlSec : kPartialBriefCacheTtlSec, "news");
                        cb(true, out);
                    };

                    // Each half gets its own ceiling, sized to hold the answer
                    // AND — where reasoning cannot be suppressed — the model's
                    // chain-of-thought.
                    //
                    // The old 900 assumed think=false takes effect, and on
                    // Ollama's /v1 route it does not. Measured on this prompt:
                    // qwen3:14b (the model the news role resolves to) spent all
                    // 900 tokens reasoning and returned finish_reason=length
                    // with an EMPTY message; qwen3.5:9b did the same at 2000,
                    // because the reasoning simply expands to fill whatever it
                    // is given. That is the reported "brief keeps stopping
                    // after two categories", and no prompt or budget change can
                    // reach it — which is why these calls now route to Ollama's
                    // native API, where think=false actually holds (see
                    // LlmService::use_ollama_native).
                    //
                    // The headroom stays for the paths that route can't cover —
                    // a cloud reasoning model, or a local one behind a tool
                    // call. It costs nothing when unused: a model that finishes
                    // stops at its stop token, so only a request that genuinely
                    // needs the room spends it. What the tight cap bought was
                    // bounding a collapse, and looks_degenerate plus
                    // kBriefRetryBudgetMs already do that better than
                    // truncation does.
                    ai_chat::PersonaScope top_scope = scope;
                    top_scope.max_tokens = 1800;
                    auto* top_watcher = new QFutureWatcher<ai_chat::LlmResponse>(this);
                    QObject::connect(top_watcher, &QFutureWatcher<ai_chat::LlmResponse>::finished, this,
                                     [top_watcher, job, finish]() {
                                         const auto resp = top_watcher->result();
                                         top_watcher->deleteLater();
                                         const QString text = resp.content.trimmed();
                                         if (!resp.success || text.isEmpty()) {
                                             // An empty message on a SUCCESSFUL
                                             // request is its own diagnosis and
                                             // used to be logged as a blank
                                             // error: the model spent the whole
                                             // token budget reasoning and never
                                             // reached the answer. Name it, or
                                             // the next person reads "failed:"
                                             // with nothing after it.
                                             LOG_WARN("NewsService",
                                                      resp.success
                                                          ? QString("brief top half returned an "
                                                                    "empty message (%1 completion "
                                                                    "tokens) — raise the budget or "
                                                                    "suppress reasoning")
                                                                .arg(resp.completion_tokens)
                                                          : "brief top half failed: " + resp.error);
                                         } else if (ai_chat::looks_degenerate(text)) {
                                             // A collapsed response is worse
                                             // than none: it is cached, rendered
                                             // in full, and reads as though the
                                             // feed itself is broken.
                                             LOG_WARN("NewsService",
                                                      QString("brief top half discarded as "
                                                              "degenerate (%1 chars)")
                                                          .arg(text.size()));
                                         } else {
                                             job->top = text;
                                             job->top_ok = true;
                                         }
                                         finish();
                                     });
                    top_watcher->setFuture(QtConcurrent::run(&brief_pool(), [top_prompt, top_scope]() {
                        return news_run_brief_call(top_prompt, top_scope, "top half");
                    }));

                    if (breakdown_prompt.isEmpty())
                        return;

                    // Larger than the top half: eight sections of two
                    // 30-word sentences is more output than five bullets and a
                    // one-line read, and it is the half that was being cut.
                    ai_chat::PersonaScope cat_scope = scope;
                    cat_scope.max_tokens = 2400;
                    auto* cat_watcher = new QFutureWatcher<ai_chat::LlmResponse>(this);
                    QObject::connect(cat_watcher, &QFutureWatcher<ai_chat::LlmResponse>::finished, this,
                                     [cat_watcher, job, finish]() {
                                         const auto resp = cat_watcher->result();
                                         cat_watcher->deleteLater();
                                         const QString text = resp.content.trimmed();
                                         if (!resp.success || text.isEmpty()) {
                                             LOG_WARN("NewsService",
                                                      resp.success
                                                          ? QString("brief breakdown returned an "
                                                                    "empty message (%1 completion "
                                                                    "tokens) — raise the budget or "
                                                                    "suppress reasoning")
                                                                .arg(resp.completion_tokens)
                                                          : "brief breakdown failed: " + resp.error);
                                         } else if (ai_chat::looks_degenerate(text)) {
                                             LOG_WARN("NewsService",
                                                      QString("brief breakdown discarded as "
                                                              "degenerate (%1 chars)")
                                                          .arg(text.size()));
                                         } else {
                                             job->breakdown = text;
                                         }
                                         finish();
                                     });
                    cat_watcher->setFuture(QtConcurrent::run(&brief_pool(), [breakdown_prompt, cat_scope]() {
                        return news_run_brief_call(breakdown_prompt, cat_scope, "breakdown");
                    }));
                });
        });

    select_watcher->setFuture(QtConcurrent::run([articles, held_set, cfg]() {
        return news::brief_select::select_stories(cluster_articles(articles), held_set, cfg);
    }));
}

// ── WebSocket live feed ──────────────────────────────────────────────────────

#ifdef HAS_QT_WEBSOCKETS
void NewsService::connect_live_feed(const QString& ws_url) {
    if (live_ws_)
        return; // already connected

    live_ws_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(live_ws_, &QWebSocket::connected, this, [this]() {
        live_connected_ = true;
        LOG_INFO("NewsService", "WebSocket live feed connected");
    });

    connect(live_ws_, &QWebSocket::disconnected, this, [this]() {
        live_connected_ = false;
        LOG_WARN("NewsService", "WebSocket live feed disconnected");
        // Auto-reconnect after delay
        QTimer::singleShot(kWsReconnectDelayMs, this, [this]() {
            if (live_ws_ && !live_connected_)
                live_ws_->open(live_ws_->requestUrl());
        });
    });

    connect(live_ws_, &QWebSocket::textMessageReceived, this, [this](const QString& msg) {
        // Parse incoming JSON article
        auto doc = QJsonDocument::fromJson(msg.toUtf8());
        if (!doc.isObject())
            return;

        auto obj = doc.object();
        NewsArticle article;
        article.id = obj["id"].toString();
        article.headline = obj["headline"].toString(obj["title"].toString());
        article.summary = obj["summary"].toString(obj["description"].toString());
        article.source = obj["source"].toString();
        article.link = obj["link"].toString(obj["url"].toString());
        article.category = obj["category"].toString("MARKETS");
        article.sort_ts = obj["timestamp"].toInteger(QDateTime::currentSecsSinceEpoch());
        // EVENT-STAMP: a publication instant.
        article.time = QDateTime::fromSecsSinceEpoch(article.sort_ts).toString("MMM dd, HH:mm");
        article.tier = obj["tier"].toInt(2);

        if (article.headline.isEmpty())
            return;

        enrich_article(article);

        // Prepend to cached articles
        QVector<NewsArticle> updated;
        {
            const QVariant cv = fincept::CacheManager::instance().get("news:articles");
            if (!cv.isNull()) {
                const QJsonArray existing = QJsonDocument::fromJson(cv.toString().toUtf8()).array();
                updated.reserve(existing.size() + 1);
                for (const auto& v : existing) {
                    const QJsonObject o = v.toObject();
                    NewsArticle a;
                    a.id = o["id"].toString();
                    a.time = o["time"].toString();
                    a.headline = o["headline"].toString();
                    a.summary = o["summary"].toString();
                    a.source = o["source"].toString();
                    a.region = o["region"].toString();
                    a.category = o["category"].toString();
                    a.link = o["link"].toString();
                    a.sort_ts = o["sort_ts"].toVariant().toLongLong();
                    a.tier = o["tier"].toInt(4);
                    updated.append(a);
                }
            }
        }
        updated.prepend(article);
        QJsonArray narr;
        for (const auto& a : updated) {
            QJsonObject o;
            o["id"] = a.id;
            o["time"] = a.time;
            o["headline"] = a.headline;
            o["summary"] = a.summary;
            o["source"] = a.source;
            o["region"] = a.region;
            o["category"] = a.category;
            o["link"] = a.link;
            o["sort_ts"] = static_cast<qint64>(a.sort_ts);
            o["tier"] = a.tier;
            narr.append(o);
        }
        fincept::CacheManager::instance().put(
            "news:articles", QVariant(QString::fromUtf8(QJsonDocument(narr).toJson(QJsonDocument::Compact))),
            kArticleCacheTtlSec, "news");
        emit articles_partial(updated, 1, 1);
        LOG_INFO("NewsService", "Live article: " + article.headline.left(50));
    });

    // Live news WebSocket was served by the external stub (now removed).
    // Only connect if the caller supplies an explicit ws_url (user-configured provider).
    if (ws_url.isEmpty()) {
        LOG_INFO("NewsService", "Live WebSocket skipped — no provider configured");
        live_ws_->deleteLater();
        live_ws_ = nullptr;
        return;
    }
    live_ws_->open(QUrl(ws_url));
}

void NewsService::disconnect_live_feed() {
    if (!live_ws_)
        return;
    live_ws_->close();
    live_ws_->deleteLater();
    live_ws_ = nullptr;
    live_connected_ = false;
}

bool NewsService::is_live_connected() const {
    return live_connected_;
}
#else
// No WebSocket support — stubs
void NewsService::connect_live_feed(const QString&) {}
void NewsService::disconnect_live_feed() {}
bool NewsService::is_live_connected() const {
    return false;
}
#endif

// ── Auto-refresh ────────────────────────────────────────────────────────────

void NewsService::set_refresh_interval(int minutes) {
    refresh_timer_->setInterval(minutes * 60 * 1000);
}

void NewsService::start_auto_refresh() {
    refresh_timer_->start();
}
void NewsService::stop_auto_refresh() {
    refresh_timer_->stop();
}

// ── RSS XML parser ──────────────────────────────────────────────────────────

QVector<NewsArticle> NewsService::parse_rss_xml(const QByteArray& xml, const RSSFeed& feed) {
    QVector<NewsArticle> articles;
    QXmlStreamReader reader(xml);

    bool in_item = false;
    NewsArticle current;
    QString current_tag;
    int item_idx = 0;

    while (!reader.atEnd()) {
        auto token = reader.readNext();

        if (token == QXmlStreamReader::StartElement) {
            current_tag = reader.name().toString();

            if (current_tag == "item" || current_tag == "entry") {
                in_item = true;
                item_idx++;
                current = {};
                current.category = feed.category;
                current.source = feed.source;
                current.region = feed.region;
                current.tier = feed.tier;
                current.id = QString("%1-%2-%3").arg(feed.id).arg(QDateTime::currentMSecsSinceEpoch()).arg(item_idx);
            }

            // Atom <link href="..."/> or <link rel="alternate" href="..."/>
            if (in_item && current_tag == "link") {
                auto href = reader.attributes().value("href").toString();
                auto rel = reader.attributes().value("rel").toString();
                if (!href.isEmpty() && (rel.isEmpty() || rel == "alternate")) {
                    if (current.link.isEmpty())
                        current.link = href;
                }
            }
        } else if (token == QXmlStreamReader::Characters && in_item) {
            QString text = reader.text().toString().trimmed();
            if (text.isEmpty())
                continue;

            if (current_tag == "title" && current.headline.isEmpty()) {
                current.headline = text.left(200);
            } else if ((current_tag == "description" || current_tag == "summary" || current_tag == "encoded") &&
                       current.summary.isEmpty()) {
                current.summary = strip_html(text).left(kSummaryMaxChars);
            } else if (current_tag == "link" && current.link.isEmpty()) {
                current.link = text.trimmed();
            } else if ((current_tag == "guid" || current_tag == "id") && current.link.isEmpty()) {
                // guid/id often contains the article URL as fallback
                if (text.startsWith("http"))
                    current.link = text.trimmed();
            } else if (current_tag == "pubDate" || current_tag == "published" || current_tag == "updated" ||
                       current_tag == "date") {
                if (current.sort_ts == 0) {
                    QDateTime dt = QDateTime::fromString(text, Qt::RFC2822Date);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(text, Qt::ISODate);
                    if (!dt.isValid())
                        dt = QDateTime::fromString(text, "ddd, dd MMM yyyy HH:mm:ss");
                    if (dt.isValid()) {
                        current.sort_ts = dt.toSecsSinceEpoch();
                        current.time = dt.toString("MMM dd, HH:mm");
                    } else {
                        current.time = text.left(22);
                    }
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            QString tag = reader.name().toString();
            if ((tag == "item" || tag == "entry") && in_item) {
                in_item = false;
                if (current.headline.isEmpty())
                    continue;

                if (current.time.isEmpty())
                    current.time = QDateTime::currentDateTime().toString("MMM dd, HH:mm");
                if (current.sort_ts == 0)
                    current.sort_ts = QDateTime::currentSecsSinceEpoch();

                enrich_article(current);
                articles.append(std::move(current));
            }
        }
    }

    return articles;
}

// ── Strip HTML tags ─────────────────────────────────────────────────────────

QString NewsService::strip_html(const QString& html) {
    static const QRegularExpression re("<[^>]*>");
    QString out = html;
    // A SPACE, not nothing. Deleting the tag outright fuses the words either
    // side of it, and RSS <description> is full of block-level markup, so
    // "...two subpoenas</p><p>Sign up for..." arrived as "subpoenasSign up
    // for". Harmless-looking in a summary label, actively misleading in the
    // brief: the model is handed a token that is not a word and has to guess
    // at it. simplified() below collapses the doubled spaces this introduces
    // between adjacent tags.
    out.replace(re, QStringLiteral(" "));
    return out.simplified();
}

// ── Enrich article: sentiment, priority, category, tickers ──────────────────

void NewsService::enrich_article(NewsArticle& article) {
    // Build once — reused for all keyword checks, ticker regex, and classify_threat
    const QString combined = article.headline + " " + article.summary;
    const QString text = combined.toLower();

    // Priority
    if (text.contains("breaking") || text.contains("alert"))
        article.priority = Priority::FLASH;
    else if (text.contains("urgent") || text.contains("emergency"))
        article.priority = Priority::URGENT;
    else if (text.contains("announce") || text.contains("report"))
        article.priority = Priority::BREAKING;

    // Weighted sentiment
    struct WordWeight {
        const char* word;
        int weight;
    };

    static const WordWeight positives[] = {
        {"surge", 3},       {"soar", 3},       {"skyrocket", 3}, {"breakthrough", 3}, {"boom", 3},
        {"record high", 3}, {"rally", 2},      {"gain", 2},      {"rise", 2},         {"jump", 2},
        {"climb", 2},       {"spike", 2},      {"rebound", 2},   {"boost", 2},        {"beat", 2},
        {"exceed", 2},      {"upgrade", 2},    {"profit", 2},    {"growth", 2},       {"expand", 2},
        {"recover", 2},     {"victory", 2},    {"ceasefire", 2}, {"treaty", 2},       {"reform", 2},
        {"optimism", 2},    {"milestone", 2},  {"strong", 1},    {"robust", 1},       {"stellar", 1},
        {"buy", 1},         {"positive", 1},   {"success", 1},   {"win", 1},          {"approval", 1},
        {"deal", 1},        {"confidence", 1}, {"dividend", 1},  {"progress", 1},     {"improve", 1},
        {"hope", 1},        {"support", 1},    {"bolster", 1},   {"outperform", 1},   {"bullish", 1},
        {"upside", 1},      {"favorable", 1},  {"momentum", 1},  {"launch", 1},       {"unveil", 1},
    };

    static const WordWeight negatives[] = {
        {"crash", 3},      {"plunge", 3},    {"collapse", 3},   {"devastat", 3},  {"catastroph", 3}, {"invasion", 3},
        {"war crime", 3},  {"nuclear", 3},   {"bankruptcy", 3}, {"meltdown", 3},  {"fall", 2},       {"drop", 2},
        {"decline", 2},    {"tumble", 2},    {"slide", 2},      {"slump", 2},     {"miss", 2},       {"disappoint", 2},
        {"fail", 2},       {"recession", 2}, {"crisis", 2},     {"conflict", 2},  {"attack", 2},     {"kill", 2},
        {"sanction", 2},   {"tariff", 2},    {"escalat", 2},    {"layoff", 2},    {"downgrade", 2},  {"default", 2},
        {"fraud", 2},      {"scandal", 2},   {"coup", 2},       {"protest", 2},   {"disaster", 2},   {"worst", 1},
        {"weak", 1},       {"loss", 1},      {"deficit", 1},    {"fear", 1},      {"risk", 1},       {"threat", 1},
        {"warning", 1},    {"sell", 1},      {"debt", 1},       {"inflation", 1}, {"slowdown", 1},   {"bearish", 1},
        {"negative", 1},   {"volatile", 1},  {"uncertain", 1},  {"reject", 1},    {"ban", 1},        {"suspend", 1},
        {"investigat", 1}, {"probe", 1},     {"hack", 1},       {"leak", 1},      {"shortage", 1},   {"disrupt", 1},
        {"shrink", 1},
    };

    int pos = 0, neg = 0;
    for (const auto& [w, wt] : positives) {
        if (text.contains(w))
            pos += wt;
    }
    for (const auto& [w, wt] : negatives) {
        if (text.contains(w))
            neg += wt;
    }

    int net = pos - neg;
    if (net >= 1)
        article.sentiment = Sentiment::BULLISH;
    else if (net <= -1)
        article.sentiment = Sentiment::BEARISH;
    // else stays NEUTRAL

    // Impact
    int strength = std::abs(net);
    if (article.priority == Priority::FLASH || article.priority == Priority::URGENT || strength >= 6)
        article.impact = Impact::HIGH;
    else if (article.priority == Priority::BREAKING || strength >= 3)
        article.impact = Impact::MEDIUM;

    // Category refinement. The keyword table lives in NewsCategories.h so the
    // brief prompt and the brief renderer classify by the same rules this does
    // — the renderer has to decide which category a bullet belongs to when the
    // model merges two headings, and it would be guessing differently from the
    // classifier if it carried its own copy. No match leaves the feed's own
    // category in place, which is the behaviour the if-chain here had.
    if (const QString cat = news::classify(text); !cat.isEmpty())
        article.category = cat;

    // Extract tickers: uppercase 2-5 letter words
    static QRegularExpression ticker_re("\\b[A-Z]{2,5}\\b");
    static QSet<QString> common_words = {"THE",  "FOR",  "AND",  "BUT",  "NOT",  "FROM", "WITH", "THIS", "THAT", "HAVE",
                                         "WILL", "BEEN", "THEY", "WERE", "SAID", "HAS",  "ITS",  "NEW",  "ARE",  "WAS"};
    auto it = ticker_re.globalMatch(combined); // reuse already-built string
    QSet<QString> found;
    while (it.hasNext() && found.size() < 5) {
        auto m = it.next();
        QString t = m.captured();
        if (!common_words.contains(t))
            found.insert(t);
    }
    article.tickers = found.values();

    // Language detection — check for CJK, Cyrillic, Arabic, Devanagari characters
    auto detect_lang = [](const QString& s) -> QString {
        int cjk = 0, cyrillic = 0, arabic = 0, devanagari = 0;
        for (const auto& ch : s) {
            ushort u = ch.unicode();
            if (u >= 0x4e00 && u <= 0x9fff)
                cjk++;
            else if (u >= 0x3040 && u <= 0x30ff)
                return "ja"; // kana = definitely Japanese
            else if (u >= 0xac00 && u <= 0xd7af)
                return "ko"; // hangul = Korean
            else if (u >= 0x0400 && u <= 0x04ff)
                cyrillic++;
            else if (u >= 0x0600 && u <= 0x06ff)
                arabic++;
            else if (u >= 0x0900 && u <= 0x097f)
                devanagari++;
        }
        int total = s.size();
        if (total == 0)
            return "en";
        if (cjk * 10 > total)
            return "zh";
        if (cyrillic * 10 > total)
            return "ru";
        if (arabic * 10 > total)
            return "ar";
        if (devanagari * 10 > total)
            return "hi";
        return "en";
    };
    article.lang = detect_lang(article.headline);

    // Threat classification — pass pre-built text to avoid a 3rd toLower()
    article.threat = classify_threat(article, text);

    // Source credibility flag
    article.source_flag = source_flag_for(article.source);
}

// ── Threat classification with confidence ───────────────────────────────────

ThreatClassification NewsService::classify_threat(const NewsArticle& article) {
    // Convenience overload — builds text itself (used only outside enrich_article)
    return classify_threat(article, (article.headline + " " + article.summary).toLower());
}

ThreatClassification NewsService::classify_threat(const NewsArticle& article, const QString& text) {
    ThreatClassification tc;
    tc.category = "general";
    tc.confidence = 0.3; // base confidence from keyword matching

    // Critical — immediate, high-impact events
    struct PatternScore {
        const char* pattern;
        const char* category;
        ThreatLevel level;
        double conf;
    };
    static const PatternScore critical_patterns[] = {
        {"nuclear strike", "conflict", ThreatLevel::CRITICAL, 0.95},
        {"nuclear attack", "conflict", ThreatLevel::CRITICAL, 0.95},
        {"war declared", "conflict", ThreatLevel::CRITICAL, 0.95},
        {"market crash", "market", ThreatLevel::CRITICAL, 0.9},
        {"flash crash", "market", ThreatLevel::CRITICAL, 0.9},
        {"circuit breaker", "market", ThreatLevel::CRITICAL, 0.85},
        {"trading halt", "market", ThreatLevel::CRITICAL, 0.85},
        {"bank run", "market", ThreatLevel::CRITICAL, 0.9},
        {"sovereign default", "market", ThreatLevel::CRITICAL, 0.9},
        {"cyberattack", "cyber", ThreatLevel::HIGH, 0.8},
        {"data breach", "cyber", ThreatLevel::HIGH, 0.75},
        {"ransomware", "cyber", ThreatLevel::HIGH, 0.8},
    };

    // High — significant events
    static const PatternScore high_patterns[] = {
        {"invasion", "conflict", ThreatLevel::HIGH, 0.85},
        {"airstrike", "conflict", ThreatLevel::HIGH, 0.85},
        {"missile launch", "conflict", ThreatLevel::HIGH, 0.85},
        {"military deploy", "conflict", ThreatLevel::HIGH, 0.8},
        {"coup attempt", "conflict", ThreatLevel::HIGH, 0.85},
        {"martial law", "conflict", ThreatLevel::HIGH, 0.85},
        {"bankruptcy fil", "market", ThreatLevel::HIGH, 0.8},
        {"rate hike", "market", ThreatLevel::HIGH, 0.7},
        {"rate cut", "market", ThreatLevel::HIGH, 0.7},
        {"earnings miss", "market", ThreatLevel::HIGH, 0.75},
        {"profit warning", "market", ThreatLevel::HIGH, 0.75},
        {"downgrad", "market", ThreatLevel::HIGH, 0.7},
        {"sanction", "regulatory", ThreatLevel::HIGH, 0.7},
        {"embargo", "regulatory", ThreatLevel::HIGH, 0.75},
        {"earthquake", "natural", ThreatLevel::HIGH, 0.8},
        {"tsunami", "natural", ThreatLevel::HIGH, 0.85},
        {"hurricane", "natural", ThreatLevel::HIGH, 0.75},
        {"pandemic", "natural", ThreatLevel::HIGH, 0.8},
    };

    // Medium patterns
    static const PatternScore medium_patterns[] = {
        {"protest", "conflict", ThreatLevel::MEDIUM, 0.6},     {"riot", "conflict", ThreatLevel::MEDIUM, 0.7},
        {"tension", "conflict", ThreatLevel::MEDIUM, 0.5},     {"escalat", "conflict", ThreatLevel::MEDIUM, 0.65},
        {"tariff", "regulatory", ThreatLevel::MEDIUM, 0.65},   {"regulation", "regulatory", ThreatLevel::MEDIUM, 0.5},
        {"antitrust", "regulatory", ThreatLevel::MEDIUM, 0.6}, {"investigat", "regulatory", ThreatLevel::MEDIUM, 0.55},
        {"layoff", "market", ThreatLevel::MEDIUM, 0.6},        {"recession", "market", ThreatLevel::MEDIUM, 0.65},
        {"inflation", "market", ThreatLevel::MEDIUM, 0.55},    {"selloff", "market", ThreatLevel::MEDIUM, 0.6},
        {"sell-off", "market", ThreatLevel::MEDIUM, 0.6},      {"volatil", "market", ThreatLevel::MEDIUM, 0.5},
        {"wildfire", "natural", ThreatLevel::MEDIUM, 0.6},     {"flood", "natural", ThreatLevel::MEDIUM, 0.6},
    };

    // Check patterns in priority order — first critical, then high, then medium
    for (const auto& p : critical_patterns) {
        if (text.contains(p.pattern)) {
            tc.level = p.level;
            tc.category = p.category;
            tc.confidence = p.conf;
            return tc;
        }
    }
    for (const auto& p : high_patterns) {
        if (text.contains(p.pattern)) {
            tc.level = p.level;
            tc.category = p.category;
            tc.confidence = p.conf;
            return tc;
        }
    }
    for (const auto& p : medium_patterns) {
        if (text.contains(p.pattern)) {
            tc.level = p.level;
            tc.category = p.category;
            tc.confidence = p.conf;
            return tc;
        }
    }

    // Low: any negative sentiment article
    if (article.sentiment == Sentiment::BEARISH) {
        tc.level = ThreatLevel::LOW;
        tc.confidence = 0.4;
    }

    return tc;
}

// ── Source credibility ──────────────────────────────────────────────────────

SourceFlag NewsService::source_flag_for(const QString& source) {
    static const QMap<QString, SourceFlag> flags = {
        // State media
        {"XINHUA", SourceFlag::STATE_MEDIA},
        {"CGTN", SourceFlag::STATE_MEDIA},
        {"GLOBAL TIMES", SourceFlag::STATE_MEDIA},
        {"RT", SourceFlag::STATE_MEDIA},
        {"TASS", SourceFlag::STATE_MEDIA},
        {"SPUTNIK", SourceFlag::STATE_MEDIA},
        {"PRESS TV", SourceFlag::STATE_MEDIA},
        {"KCNA", SourceFlag::STATE_MEDIA},
        {"TRT WORLD", SourceFlag::STATE_MEDIA},
        {"AL ARABIYA", SourceFlag::STATE_MEDIA},
        // Caution — sensationalism or low editorial standards
        {"ZEROHEDGE", SourceFlag::CAUTION},
        {"INFOWARS", SourceFlag::CAUTION},
        {"DAILY MAIL", SourceFlag::CAUTION},
        {"NY POST", SourceFlag::CAUTION},
    };
    auto it = flags.find(source.toUpper());
    return it != flags.end() ? it.value() : SourceFlag::NONE;
}

QString NewsService::source_flag_label(SourceFlag flag) {
    switch (flag) {
        case SourceFlag::STATE_MEDIA:
            return "STATE MEDIA";
        case SourceFlag::CAUTION:
            return "CAUTION";
        default:
            return {};
    }
}

// ── Default RSS feeds ──────────────────────────────────────────────────────

QVector<RSSFeed> NewsService::default_feeds() {
    return {
        // Tier 1 — Wire Services & Regulators
        // Reuters discontinued public RSS in 2020 (feeds.reuters.com is dead).
        // We keep tier-1 coverage via AP, BBC, FT, Bloomberg, WSJ instead.
        {"ap-top", "AP Top News", "https://rsshub.app/apnews/topics/ap-top-news", "GEOPOLITICS", "GLOBAL", "AP", 1},
        {"sec-press", "SEC Press Releases", "https://www.sec.gov/news/pressreleases.rss", "REGULATORY", "US", "SEC", 1},
        {"fed-press", "Federal Reserve", "https://www.federalreserve.gov/feeds/press_all.xml", "REGULATORY", "US",
         "FEDERAL RESERVE", 1},
        {"un-news", "UN News", "https://news.un.org/feed/subscribe/en/news/all/rss.xml", "GEOPOLITICS", "GLOBAL", "UN",
         1},
        // (IMF News removed — endpoint serves an Akamai access-denied HTML page,
        //  not RSS. Fincept's macro coverage is already provided by Bloomberg /
        //  WSJ / Economist / IMF press is reachable via UN feeds.)

        // Tier 2 — Major Financial Media
        {"bloomberg-mkts", "Bloomberg Markets", "https://feeds.bloomberg.com/markets/news.rss", "MARKETS", "GLOBAL",
         "BLOOMBERG", 2},
        {"wsj-markets", "WSJ Markets", "https://feeds.a.dj.com/rss/RSSMarketsMain.xml", "MARKETS", "US", "WSJ", 2},
        {"wsj-world", "WSJ World", "https://feeds.a.dj.com/rss/RSSWorldNews.xml", "GEOPOLITICS", "GLOBAL", "WSJ", 2},
        {"marketwatch", "MarketWatch", "https://feeds.marketwatch.com/marketwatch/topstories/", "MARKETS", "US",
         "MARKETWATCH", 2},
        {"cnbc-finance", "CNBC Finance",
         "https://search.cnbc.com/rs/search/combinedcms/view.xml?partnerId=wrss01&id=100003114", "MARKETS", "US",
         "CNBC", 2},
        {"seekingalpha", "Seeking Alpha", "https://seekingalpha.com/market_currents.xml", "MARKETS", "US",
         "SEEKING ALPHA", 2},

        // Tier 2 — Global News
        {"bbc-world", "BBC World", "http://feeds.bbci.co.uk/news/world/rss.xml", "GEOPOLITICS", "GLOBAL", "BBC", 2},
        {"bbc-business", "BBC Business", "http://feeds.bbci.co.uk/news/business/rss.xml", "MARKETS", "GLOBAL", "BBC",
         2},
        {"aljazeera", "Al Jazeera", "https://www.aljazeera.com/xml/rss/all.xml", "GEOPOLITICS", "GLOBAL", "AL JAZEERA",
         2},
        {"nyt-world", "NYT World", "https://rss.nytimes.com/services/xml/rss/nyt/World.xml", "GEOPOLITICS", "GLOBAL",
         "NYT", 2},
        {"guardian-world", "Guardian World", "https://www.theguardian.com/world/rss", "GEOPOLITICS", "GLOBAL",
         "GUARDIAN", 2},
        {"france24", "France 24", "https://www.france24.com/en/rss", "GEOPOLITICS", "EU", "FRANCE 24", 2},

        // Tier 2 — Geopolitics & Defense
        {"foreignpolicy", "Foreign Policy", "https://foreignpolicy.com/feed/", "GEOPOLITICS", "GLOBAL",
         "FOREIGN POLICY", 2},
        // (defensenews.com/rss/ returns 404 — endpoint discontinued.)

        // Tier 2 — Energy & Commodities
        {"oilprice", "OilPrice.com", "https://oilprice.com/rss/main", "ENERGY", "GLOBAL", "OILPRICE", 2},
        // (Kitco RSS endpoint removed — kitco.com no longer publishes RSS.)

        // Tier 2 — Tech
        {"techcrunch", "TechCrunch", "https://techcrunch.com/feed/", "TECH", "GLOBAL", "TECHCRUNCH", 2},
        {"wired", "Wired", "https://www.wired.com/feed/rss", "TECH", "US", "WIRED", 2},

        // Tier 2 — Forex
        {"fxstreet", "FXStreet", "https://www.fxstreet.com/rss/news", "MARKETS", "GLOBAL", "FXSTREET", 2},

        // Tier 2 — China & Asia
        {"scmp", "South China Morning Post", "https://www.scmp.com/rss/91/feed", "GEOPOLITICS", "CHINA", "SCMP", 2},
        {"nikkei-asia", "Nikkei Asia", "https://asia.nikkei.com/rss/feed/nar", "MARKETS", "ASIA", "NIKKEI ASIA", 2},
        {"caixin", "Caixin Global", "https://www.caixinglobal.com/feed/", "MARKETS", "CHINA", "CAIXIN", 2},

        // Tier 2 — MENA
        {"middle-east-eye", "Middle East Eye", "https://www.middleeasteye.net/rss", "GEOPOLITICS", "MENA",
         "MIDDLE EAST EYE", 2},

        // ── Additional feeds (29→80+) ──────────────────────────────────────

        // Tier 1 — Wire (additional)
        // (Reuters tech RSS removed — feed discontinued.)

        // Tier 2 — Major Financial (additional)
        {"cnbc-world", "CNBC World",
         "https://search.cnbc.com/rs/search/combinedcms/view.xml?partnerId=wrss01&id=100727362", "MARKETS", "GLOBAL",
         "CNBC", 2},
        {"cnbc-tech", "CNBC Technology",
         "https://search.cnbc.com/rs/search/combinedcms/view.xml?partnerId=wrss01&id=19854910", "TECH", "US", "CNBC",
         2},
        {"investing-news", "Investing.com", "https://www.investing.com/rss/news.rss", "MARKETS", "GLOBAL",
         "INVESTING.COM", 2},
        {"economist", "The Economist", "https://www.economist.com/finance-and-economics/rss.xml", "ECONOMIC", "GLOBAL",
         "ECONOMIST", 2},

        // Tier 2 — Crypto
        {"coindesk", "CoinDesk", "https://www.coindesk.com/arc/outboundfeeds/rss/", "CRYPTO", "GLOBAL", "COINDESK", 2},
        {"cointelegraph", "CoinTelegraph", "https://cointelegraph.com/rss", "CRYPTO", "GLOBAL", "COINTELEGRAPH", 2},
        {"theblock", "The Block", "https://www.theblock.co/rss.xml", "CRYPTO", "GLOBAL", "THE BLOCK", 2},
        {"decrypt", "Decrypt", "https://decrypt.co/feed", "CRYPTO", "GLOBAL", "DECRYPT", 2},

        // Tier 1 — Central Banks & Regulators
        {"ecb-press", "ECB Press", "https://www.ecb.europa.eu/rss/press.html", "REGULATORY", "EU", "ECB", 1},
        {"boe-news", "Bank of England", "https://www.bankofengland.co.uk/rss/news", "REGULATORY", "UK", "BOE", 1},

        // Tier 2 — Commodities (additional)
        {"mining-com", "Mining.com", "https://www.mining.com/feed/", "MARKETS", "GLOBAL", "MINING.COM", 2},

        // Tier 2 — US Markets (additional)
        {"benzinga", "Benzinga", "https://www.benzinga.com/feed", "MARKETS", "US", "BENZINGA", 2},

        // Tier 2 — Europe
        {"dw-world", "Deutsche Welle", "https://rss.dw.com/rdf/rss-en-all", "GEOPOLITICS", "EU", "DW", 2},

        // Tier 2 — China & Europe (additional)
        {"scmp-biz", "SCMP Business", "https://www.scmp.com/rss/92/feed", "MARKETS", "CHINA", "SCMP", 2},
        {"guardian-biz", "Guardian Business", "https://www.theguardian.com/uk/business/rss", "MARKETS", "EU",
         "GUARDIAN", 2},
        {"euronews-biz", "Euronews Business", "https://www.euronews.com/rss?level=theme&name=business", "MARKETS", "EU",
         "EURONEWS", 2},
        {"channel-news-asia", "CNA", "https://www.channelnewsasia.com/rssfeeds/8395986", "MARKETS", "ASIA", "CNA", 2},

        // Tier 2 — Fintech
        {"finextra", "Finextra", "https://www.finextra.com/rss/headlines.aspx", "TECH", "GLOBAL", "FINEXTRA", 2},

        // Tier 3 — Economic / Macro
        {"zero-hedge", "ZeroHedge", "https://feeds.feedburner.com/zerohedge/feed", "ECONOMIC", "GLOBAL", "ZEROHEDGE",
         3},
        {"calculated-risk", "Calculated Risk", "https://feeds.feedburner.com/CalculatedRisk", "ECONOMIC", "US",
         "CALCULATED RISK", 3},
        {"wolfstreet", "Wolf Street", "https://wolfstreet.com/feed/", "ECONOMIC", "US", "WOLF STREET", 3},

        // Tier 3 — Defense & Security
        // (defenseone.com/rss/ returns 404 — endpoint discontinued.)
        {"bellingcat", "Bellingcat", "https://www.bellingcat.com/feed/", "GEOPOLITICS", "GLOBAL", "BELLINGCAT", 3},

        // Tier 3 — Tech (additional)
        {"arstechnica", "Ars Technica", "https://feeds.arstechnica.com/arstechnica/index", "TECH", "GLOBAL",
         "ARS TECHNICA", 3},
        {"theverge", "The Verge", "https://www.theverge.com/rss/index.xml", "TECH", "GLOBAL", "THE VERGE", 3},
        {"mit-tech", "MIT Tech Review", "https://www.technologyreview.com/feed/", "TECH", "GLOBAL", "MIT TECH REVIEW",
         3},

        // Tier 3 — ESG
        {"carbon-brief", "Carbon Brief", "https://www.carbonbrief.org/feed/", "ENERGY", "GLOBAL", "CARBON BRIEF", 3},

        // Tier 4 — Blogs & Aggregators
        {"hackernews", "Hacker News", "https://hnrss.org/frontpage", "TECH", "GLOBAL", "HACKER NEWS", 4},
        {"abnormal-returns", "Abnormal Returns", "https://abnormalreturns.com/feed/", "MARKETS", "US",
         "ABNORMAL RETURNS", 4},
        {"marginal-rev", "Marginal Revolution", "https://marginalrevolution.com/feed", "ECONOMIC", "GLOBAL",
         "MARGINAL REVOLUTION", 4},
    };
}

// ── Free helpers ────────────────────────────────────────────────────────────

QString priority_string(Priority p) {
    switch (p) {
        case Priority::FLASH:
            return "FLASH";
        case Priority::URGENT:
            return "URGENT";
        case Priority::BREAKING:
            return "BREAKING";
        case Priority::ROUTINE:
            return "ROUTINE";
    }
    return "ROUTINE";
}

QString sentiment_string(Sentiment s) {
    switch (s) {
        case Sentiment::BULLISH:
            return "BULLISH";
        case Sentiment::BEARISH:
            return "BEARISH";
        case Sentiment::NEUTRAL:
            return "NEUTRAL";
    }
    return "NEUTRAL";
}

QString impact_string(Impact i) {
    switch (i) {
        case Impact::HIGH:
            return "HIGH";
        case Impact::MEDIUM:
            return "MEDIUM";
        case Impact::LOW:
            return "LOW";
    }
    return "LOW";
}

QString priority_color(Priority p) {
    switch (p) {
        case Priority::FLASH:
            return "#dc2626";
        case Priority::URGENT:
            return "#d97706";
        case Priority::BREAKING:
            return "#ca8a04";
        case Priority::ROUTINE:
            return "#525252";
    }
    return "#525252";
}

QString sentiment_color(Sentiment s) {
    switch (s) {
        case Sentiment::BULLISH:
            return "#16a34a";
        case Sentiment::BEARISH:
            return "#dc2626";
        case Sentiment::NEUTRAL:
            return "#ca8a04";
    }
    return "#ca8a04";
}

QString relative_time(int64_t unix_ts) {
    if (unix_ts <= 0)
        return {};
    auto now = QDateTime::currentSecsSinceEpoch();
    auto d = now - unix_ts;
    if (d < 0)
        return "now";
    if (d < 60)
        return QString("%1s").arg(d);
    if (d < 3600)
        return QString("%1m").arg(d / 60);
    if (d < 86400)
        return QString("%1h").arg(d / 3600);
    return QString("%1d").arg(d / 86400);
}

QString threat_level_string(ThreatLevel t) {
    switch (t) {
        case ThreatLevel::CRITICAL:
            return "CRITICAL";
        case ThreatLevel::HIGH:
            return "HIGH";
        case ThreatLevel::MEDIUM:
            return "MEDIUM";
        case ThreatLevel::LOW:
            return "LOW";
        case ThreatLevel::INFO:
            return "INFO";
    }
    return "INFO";
}

QString threat_level_color(ThreatLevel t) {
    switch (t) {
        case ThreatLevel::CRITICAL:
            return "#dc2626";
        case ThreatLevel::HIGH:
            return "#f97316";
        case ThreatLevel::MEDIUM:
            return "#eab308";
        case ThreatLevel::LOW:
            return "#22c55e";
        case ThreatLevel::INFO:
            return "#525252";
    }
    return "#525252";
}

Priority priority_from_string(const QString& s) {
    if (s == "FLASH")
        return Priority::FLASH;
    if (s == "URGENT")
        return Priority::URGENT;
    if (s == "BREAKING")
        return Priority::BREAKING;
    return Priority::ROUTINE;
}

Sentiment sentiment_from_string(const QString& s) {
    if (s == "BULLISH")
        return Sentiment::BULLISH;
    if (s == "BEARISH")
        return Sentiment::BEARISH;
    return Sentiment::NEUTRAL;
}

Impact impact_from_string(const QString& s) {
    if (s == "HIGH")
        return Impact::HIGH;
    if (s == "MEDIUM")
        return Impact::MEDIUM;
    return Impact::LOW;
}

// ── DataHub producer wiring ─────────────────────────────────────────────────

QStringList NewsService::topic_patterns() const {
    return {QStringLiteral("news:general"),
            QStringLiteral("news:symbol:*"),
            QStringLiteral("news:category:*"),
            QStringLiteral("news:cluster:*")};
}

void NewsService::refresh(const QStringList& topics) {
    // Cluster topics are push-only — producer never pulls them.
    bool needs_general = false;
    for (const auto& t : topics) {
        if (t == QLatin1String("news:general") ||
            t.startsWith(QLatin1String("news:symbol:")) ||
            t.startsWith(QLatin1String("news:category:"))) {
            needs_general = true;
            break;
        }
    }
    if (!needs_general) return;

    // All non-cluster topics derive from the general feed; one fetch
    // fans out via publish_articles_to_hub.
    fetch_all_news_progressive(/*force=*/true, [](bool, QVector<NewsArticle>) {});
}

int NewsService::max_requests_per_sec() const {
    return 2;  // RSS aggregator pacing — generous but avoids request storms
}

void NewsService::ensure_registered_with_hub() {
    if (hub_registered_) return;
    auto& hub = fincept::datahub::DataHub::instance();
    hub.register_producer(this);

    // General feed — cache 5m, min refresh interval 30s, coalesce
    // progressive chunks to 250ms so cold-cache fills don't repaint in
    // a tight loop. `coalesce_within_ms` field arrived in Phase 4.
    fincept::datahub::TopicPolicy general;
    general.ttl_ms = 5 * 60 * 1000;
    general.min_interval_ms = 30 * 1000;
    general.coalesce_within_ms = 250;
    general.push_only = false;
    hub.set_policy_pattern(QStringLiteral("news:general"), general);

    // Per-symbol / per-category slices share the same TTL; they derive
    // from the same fetch so min_interval keeps producer pacing sane.
    fincept::datahub::TopicPolicy derived = general;
    hub.set_policy_pattern(QStringLiteral("news:symbol:*"), derived);
    hub.set_policy_pattern(QStringLiteral("news:category:*"), derived);

    // Server-assigned clusters — push-only, no scheduled refresh.
    fincept::datahub::TopicPolicy cluster_policy;
    cluster_policy.push_only = true;
    cluster_policy.ttl_ms = 0;
    cluster_policy.min_interval_ms = 0;
    hub.set_policy_pattern(QStringLiteral("news:cluster:*"), cluster_policy);

    hub_registered_ = true;
    LOG_INFO("NewsService",
             "Registered with DataHub (news:general, news:symbol:*, "
             "news:category:*, news:cluster:*)");
}

void NewsService::publish_articles_to_hub(const QVector<NewsArticle>& accumulated) {
    if (!hub_registered_) return;
    auto& hub = fincept::datahub::DataHub::instance();

    // Single canonical publish — the whole accumulated list on news:general.
    hub.publish(QStringLiteral("news:general"), QVariant::fromValue(accumulated));

    // Fan out per-symbol and per-category slices, but only for topics
    // that currently have subscribers — the hub is the authority on
    // who's listening. For now publish unconditionally; the hub's
    // push_only policy on symbol/category patterns caches last-known-
    // good even with no live subscribers, so future mounts get the
    // snapshot via peek(). This is cheap: lists are small and the
    // string splits are linear in article count.
    QHash<QString, QVector<NewsArticle>> by_symbol;
    QHash<QString, QVector<NewsArticle>> by_category;
    for (const auto& a : accumulated) {
        for (const auto& sym : a.tickers) {
            if (!sym.isEmpty())
                by_symbol[sym].append(a);
        }
        if (!a.category.isEmpty())
            by_category[a.category].append(a);
    }
    for (auto it = by_symbol.constBegin(); it != by_symbol.constEnd(); ++it) {
        hub.publish(QStringLiteral("news:symbol:") + it.key(),
                    QVariant::fromValue(it.value()));
    }
    for (auto it = by_category.constBegin(); it != by_category.constEnd(); ++it) {
        hub.publish(QStringLiteral("news:category:") + it.key(),
                    QVariant::fromValue(it.value()));
    }
}

} // namespace fincept::services
