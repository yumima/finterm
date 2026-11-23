#pragma once
// FuturesHub — the ONE owner of the futures reads the MARKETS screens draw: a
// DataHub producer, so a curve shown by three widgets on two screens is one
// fetch, refreshed while anything shows it and not after.
//
//   fut:curve:<ROOT>          every listed month, one batch quote (fetch_curve);
//                             the front month then moves with the live tape
//                             between reads (a print newer than the read stands)
//   fut:past:<ROOT>           where those months settled a week and a month ago
//   fut:cot:<CFTC>            CFTC Commitments of Traders, three years, weekly
//   fut:spread:<NEAR>|<FAR>   six months of a calendar spread (far_sym − near_sym)
//
// A failed read is published as the topic's error; the last good value stays.

#include "datahub/Producer.h"
#include "services/futures/FuturesMath.h"

#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QStringList>

namespace finterm::services {
struct StreamTick;
}

namespace finterm::services::futures {

class FuturesHub : public QObject, public datahub::Producer {
    Q_OBJECT
  public:
    static FuturesHub& instance();
    void ensure_registered_with_hub();

    static QString curve_topic(const QString& root) { return QStringLiteral("fut:curve:") + root; }
    static QString past_topic(const QString& root) { return QStringLiteral("fut:past:") + root; }
    static QString cot_topic(const QString& cftc) { return QStringLiteral("fut:cot:") + cftc; }
    static QString spread_topic(const QString& near_sym, const QString& far_sym) {
        return QStringLiteral("fut:spread:") + near_sym + QLatin1Char('|') + far_sym;
    }

    QStringList topic_patterns() const override;
    void refresh(const QStringList& topics) override;
    int max_requests_per_sec() const override { return 4; }

  private:
    FuturesHub();
    void refresh_curve(const QString& root);
    void refresh_past(const QString& root);
    void refresh_cot(const QString& cftc);
    void refresh_spread(const QString& near_sym, const QString& far_sym);
    void on_ticks(const QVector<StreamTick>& ticks);
    void sync_stream();

    QHash<QString, Curve> curves_;          ///< last curve by root (the tape is laid on these)
    QHash<QString, CurveContract> live_;    ///< newest stream print of each front month, by root
    QHash<QString, qint64> past_failed_at_; ///< a refused history read backs off five minutes
    QSet<QString> running_;
    bool registered_ = false;
};

} // namespace finterm::services::futures

Q_DECLARE_METATYPE(finterm::services::futures::Curve)
Q_DECLARE_METATYPE(QVector<finterm::services::futures::PastCurve>)
Q_DECLARE_METATYPE(finterm::services::futures::CotReport)
Q_DECLARE_METATYPE(finterm::services::futures::SpreadHistory)
