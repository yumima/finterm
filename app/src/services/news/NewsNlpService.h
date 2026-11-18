#pragma once
#include "services/news/NewsService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace finterm::services {

// ── NER Result Types ────────────────────────────────────────────────────────

struct EntityResult {
    QString id;                                 // article id
    QVector<QPair<QString, QString>> countries; // (name, code)
    QStringList organizations;
    QStringList people;
    QStringList tickers;
};

struct TopEntity {
    QString name;
    int count = 0;
};

struct NerResult {
    QVector<EntityResult> per_article;
    QVector<TopEntity> top_countries;
    QVector<TopEntity> top_organizations;
    QVector<TopEntity> top_people;
};

// ── Geolocation Types ───────────────────────────────────────────────────────

struct GeoLocation {
    QString name;
    QString code;
    double lat = 0;
    double lon = 0;
    QString type; // "city" or "country"
};

struct ArticleGeo {
    QString id;
    QVector<GeoLocation> locations;
    double primary_lat = 0;
    double primary_lon = 0;
};

/// One answer of news_geolocation.py extract_and_geocode.
struct GeoResult {
    QVector<ArticleGeo> articles; // only the articles a location was found in
    QJsonObject raw;              // the script's whole answer (also unique_locations, coverage)
};

// ── Service ─────────────────────────────────────────────────────────────────

class NewsNlpService : public QObject {
    Q_OBJECT
  public:
    using EntitiesCallback = std::function<void(bool, NerResult)>;
    /// On failure `error` carries the script's own message (or stderr / exit code).
    using GeoCallback = std::function<void(bool ok, GeoResult result, QString error)>;
    using TranslateCallback = std::function<void(bool, QString translated, QString detected_lang)>;
    using SemanticClustersCallback = std::function<void(bool, QJsonArray clusters)>;

    static NewsNlpService& instance();

    /// Extract entities (countries, orgs, people, tickers) from articles.
    void extract_entities(const QVector<NewsArticle>& articles, EntitiesCallback cb);

    /// Semantic clustering via TF-IDF cosine similarity.
    void cluster_semantic(const QVector<NewsArticle>& articles, SemanticClustersCallback cb);

    /// Geolocate articles — extract locations and return coordinates. The one
    /// runner of news_geolocation.py; an article needs an id and a headline
    /// (summary optional). GeopoliticsService geolocates headlines through it.
    void geolocate_articles(const QVector<NewsArticle>& articles, GeoCallback cb);

    /// Translate text to target language.
    void translate_text(const QString& text, const QString& target_lang, TranslateCallback cb);

    /// Cached NER results (populated after extract_entities completes).
    const NerResult& cached_ner() const { return ner_cache_; }

  private:
    NewsNlpService();

    QJsonArray articles_to_json(const QVector<NewsArticle>& articles) const;

    NerResult ner_cache_;
};

} // namespace finterm::services
