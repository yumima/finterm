#pragma once
#include "services/news/NewsService.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace fincept::services {

// ── Signal Types ────────────────────────────────────────────────────────────

struct CorrelationSignal {
    QString type; // velocity_spike, keyword_spike, triangulation, etc.
    QString category;
    QString detail;
    QString severity; // critical, high, medium, low
    double value = 0;
    QStringList sources;
};

struct FocalPoint {
    double lat = 0;
    double lon = 0;
    int event_count = 0;
    QMap<QString, int> categories;
    int source_count = 0;
    QString severity;
    QStringList headlines;
};

struct CategoryBaseline {
    QVector<int> hourly_counts;
    double mean = 0;
    double stddev = 0;
    int sample_size = 0;
};

// ── Service ─────────────────────────────────────────────────────────────────

class NewsCorrelationService : public QObject {
    Q_OBJECT
  public:
    using SignalsCallback = std::function<void(bool, QVector<CorrelationSignal>)>;
    using FocalCallback = std::function<void(bool, QVector<FocalPoint>)>;
    using BaselineCallback = std::function<void(bool, QMap<QString, CategoryBaseline>)>;
    using DeviationCallback = std::function<void(bool, QVector<QPair<QString, double>>)>;

    static NewsCorrelationService& instance();

    /// Detect correlation signals from articles.
    void detect_signals(const QVector<NewsArticle>& articles, SignalsCallback cb);

    /// Detect geographic focal points from geolocated articles.
    void detect_focal_points(const QJsonArray& geolocated_articles, FocalCallback cb);

    /// Baseline management (persist to SQLite via Python).
    void update_baseline(const QMap<QString, int>& current_counts, BaselineCallback cb);
    void detect_deviations(const QMap<QString, int>& current_counts, DeviationCallback cb);

    /// Cached data.
    const QVector<CorrelationSignal>& cached_signals() const { return signals_cache_; }

  private:
    NewsCorrelationService();

    QVector<CorrelationSignal> signals_cache_;
    QMap<QString, CategoryBaseline> baselines_;
};

} // namespace fincept::services
