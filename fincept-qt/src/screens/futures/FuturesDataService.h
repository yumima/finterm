#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

namespace fincept::screens::futures {

struct FuturesQuote {
    QString symbol;
    QString name;
    QString asset_class;
    double  last = 0.0;
    double  change = 0.0;
    double  change_pct = 0.0;
    double  volume = 0.0;
    double  open_interest = 0.0;
    double  high = 0.0;
    double  low = 0.0;
};

struct TermStructurePoint {
    QString month;     // contract month label (e.g. "JUN26")
    double  last = 0.0;
    double  change = 0.0;
    double  volume = 0.0;
    double  open_interest = 0.0;
};

struct HistoryPoint {
    qint64 timestamp = 0;
    double open = 0, high = 0, low = 0, close = 0, volume = 0;
};

struct SpreadResult {
    FuturesQuote leg1;
    FuturesQuote leg2;
    double       spread = 0.0;
    double       spread_pct = 0.0;
};

/// Wraps yfinance/stooq invocations behind typed callbacks. All calls are
/// async; callbacks fire on the Qt event loop.
///
/// Source selection: `set_source("yahoo"|"stooq")` toggles the underlying
/// data source globally. Yahoo path uses the persistent PythonWorker
/// daemon (fast steady-state). Stooq path spawns stooq_data.py per call
/// (CSV is small, ~300ms wall time even with cold spawn).
class FuturesDataService : public QObject {
    Q_OBJECT
  public:
    static FuturesDataService& instance();

    /// "yahoo" or "stooq". Default is "yahoo".
    void    set_source(const QString& source) { source_ = source; }
    QString source() const { return source_; }

    using QuotesCallback   = std::function<void(bool ok, QVector<FuturesQuote>, QString source)>;
    using TermCallback     = std::function<void(bool ok, QVector<TermStructurePoint>, QString source)>;
    using HistoryCallback  = std::function<void(bool ok, QVector<HistoryPoint>, QString source)>;
    using HeatmapCallback  = std::function<void(bool ok, QHash<QString, QVector<FuturesQuote>>, QString source)>;
    using SpreadCallback   = std::function<void(bool ok, SpreadResult, QString source)>;
    using ChinaListCallback = std::function<void(bool ok, QJsonArray, QString source)>;

    void fetch_quotes(const QStringList& symbols, QuotesCallback cb);
    void fetch_term_structure(const QString& product, int n_months, TermCallback cb);
    void fetch_history(const QString& symbol, const QString& period, HistoryCallback cb);
    void fetch_heatmap(HeatmapCallback cb);
    void fetch_spread(const QString& leg1, const QString& leg2, SpreadCallback cb);
    void fetch_china_main(const QString& exchange, ChinaListCallback cb);

  private:
    FuturesDataService() = default;

    void run_router(const QStringList& args, std::function<void(bool, QJsonObject, QString)> cb);

    void fetch_quotes_yahoo(const QStringList& symbols, QuotesCallback cb);
    void fetch_quotes_stooq(const QStringList& symbols, QuotesCallback cb);
    void fetch_history_yahoo(const QString& symbol, const QString& period, HistoryCallback cb);
    void fetch_history_stooq(const QString& symbol, const QString& period, HistoryCallback cb);

    QString source_ = "yahoo";
};

} // namespace fincept::screens::futures
