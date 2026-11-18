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

// ── Service ─────────────────────────────────────────────────────────────────

class NewsNlpService : public QObject {
    Q_OBJECT
  public:
    using EntitiesCallback = std::function<void(bool, NerResult)>;
    /// `answer` is the script's whole answer (geolocated_articles, unique_locations,
    /// coverage). Always called once; on failure `error` says why.
    using GeoCallback = std::function<void(bool ok, QJsonObject answer, QString error)>;
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
