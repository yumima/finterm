#pragma once
// DataHealth — what data is failing to load right now, and why.
//
// Every DataHub error (a quote, a chart, futures, market conditions) and any
// failure another surface reports lands here, classified (core/errors/
// DataFailure). A success for the same key clears it; anything not seen again
// for 10 minutes ages out. The status bar shows the result (READY, or what is
// failing), and its panel lists each problem with the reason and what to do —
// so "failed to load" always comes with a why.

#include "core/errors/DataFailure.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVector>

class QTimer;

namespace finterm::services {

class DataHealth : public QObject {
    Q_OBJECT
  public:
    static DataHealth& instance();

    /// Follow the DataHub. Call once at startup.
    void start();

    /// A failure outside the DataHub. `key` identifies the request (a later
    /// report_success(key) clears it); `what` is how the user knows it
    /// ("Earnings calendar", "News: Reuters").
    void report_failure(const QString& key, const QString& what, const QString& error,
                        const QString& source_hint = {});
    void report_success(const QString& key);

    /// One problem: one source failing one way, and what it hit.
    struct Problem {
        data_failure::Failure failure;
        QStringList items;     ///< "Quotes: AAPL, MSFT", "Charts: NVDA" …
        QStringList keys;      ///< the DataHub topics among them, to retry
        QDateTime last;
        int count = 0;         ///< failures seen (not distinct items)
        int affected = 0;      ///< distinct requests failing
    };
    /// Current problems, the most widespread first.
    QVector<Problem> problems() const;
    bool any() const { return !entries_.isEmpty(); }

    /// What a DataHub topic is, for people: {"Quotes", "AAPL"}.
    static QPair<QString, QString> describe_topic(const QString& topic);

  signals:
    void changed();

  private:
    DataHealth();
    struct Entry {
        QString group;  ///< "Quotes"
        QString item;   ///< "AAPL"
        data_failure::Failure failure;
        QDateTime last;
        int count = 0;
        bool hub = false;
    };
    void record(const QString& key, Entry e);
    void expire();
    void notify_soon();

    QHash<QString, Entry> entries_;
    QTimer* expire_timer_ = nullptr;
    QTimer* notify_timer_ = nullptr;
    bool started_ = false;
};

} // namespace finterm::services
