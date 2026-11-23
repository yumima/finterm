#pragma once
// ConditionService — the ONE owner of every market-condition reading the app
// draws (Markets ▸ OVERVIEW, the dashboard's condition widgets, the chart
// dialog's condition strip). A DataHub producer, so the hub's scheduler keeps
// each topic fresh while anything subscribes to it and stops when nothing does.
//
// Topics (payload: QJsonObject, the same object that is persisted):
//   cond:stats:<SYM>   readings from the symbol's 5-year DAILY bars —
//                      MarketDataService::fetch_bars(sym, "5y", "1d"), the same
//                      series (and cache entry) as the chart dialog's 5Y view.
//                      {as_of, last, sma20/50/200, vs200, p1y, p5y, rsi, adx,
//                       pdi, mdi, ret1m, regime, closes:[[date, close]…], t}
//   cond:fred:<KEY>    a FRED key (conditions::parse_fred_key) through
//                      EconomicsService + fred_data.py (keyless public CSV):
//                      {points:[[date, value]…], t}
//   cond:ust           the US Treasury daily par yield curve, every tenor,
//                      ~2 years of business days, newest first — the same
//                      script and cache entry as Markets ▸ WORLD's 2Y / 2s10s:
//                      {rows:[{date, m1…y30}], t}
//   cond:shiller:CAPE  Shiller's CAPE, monthly (market_valuation_data.py cape):
//                      {points:[[date, value]…], t}
//   cond:mstr          Strategy's bitcoin holdings from its latest 8-K BTC
//                      Update (market_valuation_data.py mstr_btc, SEC EDGAR)
//   cond:kalshi:<SER>  Kalshi's open events for a series ticker (public API,
//                      the Kalshi client the prediction screen uses):
//                      {events:[{ticker, title, close, outcomes:[{label, p}]}], t}
//
// `t` is when the answer was fetched (ms since epoch). Every answer is also
// written to cache.db (kPersistTtlSec), and last_known() hands it back on a
// cold start — the widgets paint the last answer at once and mark it with its
// age, while the hub fetches a fresh one. A failed refresh never replaces a
// good answer; it is published as the topic's error.

#include "datahub/Producer.h"

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <functional>

namespace finterm::services::prediction::kalshi_ns {
class KalshiRestClient;
}

namespace finterm::services {

class ConditionService : public QObject, public datahub::Producer {
    Q_OBJECT
  public:
    static ConditionService& instance();
    void ensure_registered_with_hub();

    static QString stats_topic(const QString& sym) { return QStringLiteral("cond:stats:") + sym; }
    static QString fred_topic(const QString& key) { return QStringLiteral("cond:fred:") + key; }
    static QString kalshi_topic(const QString& series) { return QStringLiteral("cond:kalshi:") + series; }
    static QString shiller_topic(const QString& name) { return QStringLiteral("cond:shiller:") + name; }
    /// Strategy's bitcoin holdings (its weekly 8-K): {btc, as_of, filed, url, t}.
    static inline const QString kMstrTopic = QStringLiteral("cond:mstr");
    static inline const QString kUstTopic = QStringLiteral("cond:ust");

    /// The newest answer known for `topic`: the hub's value, else the one
    /// persisted by an earlier run. Empty object when there is none.
    QJsonObject last_known(const QString& topic) const;

    /// A chartable series for MarketChartDialog: "FRED:<key>",
    /// "UST:<tenor>" (m3, y2, y10 …), "UST:<long>-<short>" (spread, bp) or
    /// "CORR:<A>|<B>|<window>" (rolling correlation of daily changes).
    /// Answers (ok, points oldest first, error) on a later turn: the last
    /// known copy at once and, when the hub's is not fresh, the fresh one
    /// after it (the caller redraws). FRED / UST only; CORR answers once.
    static bool is_derived(const QString& key);
    /// The next refresh of these topics downloads, even when the persisted
    /// readings are current (a REFRESH the reader pressed).
    void mark_forced(const QStringList& topics);
    using SeriesCallback = std::function<void(bool, QVector<QPair<QDate, double>>, QString)>;
    void fetch_series(const QString& key, SeriesCallback cb);
    /// Display name for a derived key ("2s10s", "HY OAS"…); empty = none known.
    static QString derived_label(const QString& key);

    // ── datahub::Producer ──────────────────────────────────────────────────
    QStringList topic_patterns() const override;
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override { return 4; }

    static constexpr int kPersistTtlSec = 60 * 24 * 3600;

  private:
    ConditionService();
    void publish(const QString& topic, QJsonObject payload);
    void pump_stats();
    void refresh_stats(const QString& sym);
    void refresh_fred(const QString& key);
    void refresh_ust();
    void refresh_shiller(const QString& name);
    void refresh_mstr();
    bool mstr_running_ = false;
    QSet<QString> shiller_running_;
    void pump_kalshi();
    void answer_once(const QString& topic, std::function<QVector<QPair<QDate, double>>(const QJsonObject&)> build,
                     SeriesCallback cb);

    QStringList stats_queue_;
    QSet<QString> stats_running_;
    QSet<QString> fred_running_;
    bool ust_running_ = false;
    QStringList kalshi_queue_;
    QString kalshi_current_;
    int kalshi_seq_ = 0; ///< bumped per request; a timeout for an older one is a no-op
    prediction::kalshi_ns::KalshiRestClient* kalshi_ = nullptr;
    bool registered_ = false;
    void pump_stats_later(int ms);
    static bool stats_current(const QJsonObject& held);
    bool stats_paused_ = false;
    QSet<QString> forced_; ///< symbols whose next read must download
    static constexpr int kStatsFreshSec = 6 * 3600;
    static constexpr int kStatsGapMs = 1'500;
    static constexpr int kRateLimitPauseMs = 5 * 60'000;
};

} // namespace finterm::services
