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

// ── Signal Types ────────────────────────────────────────────────────────────

struct CorrelationSignal {
    QString type; // velocity_spike, keyword_spike, triangulation, etc.
    QString category;
    QString detail;
    QString severity; // critical, high, medium, low
    double value = 0;
    QStringList sources;
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
    using DeviationCallback = std::function<void(bool, QVector<QPair<QString, double>>)>;

    static NewsCorrelationService& instance();

    /// Detect correlation signals from articles.
    void detect_signals(const QVector<NewsArticle>& articles, SignalsCallback cb);

    /// Score current category counts against the per-category baseline.
    void detect_deviations(const QMap<QString, int>& current_counts, DeviationCallback cb);

  private:
    NewsCorrelationService();

    QMap<QString, CategoryBaseline> baselines_;
};

} // namespace finterm::services
