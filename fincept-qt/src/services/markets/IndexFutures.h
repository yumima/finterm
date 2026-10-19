// src/services/markets/IndexFutures.h
#pragma once
#include <QDate>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace fincept::services {

/// The futures quote for a cash index — ES for the S&P 500, NQ for the
/// Nasdaq, … — the way a futures board shows it: last price and change since
/// the contract's previous close, live day and night.
///
/// It is also the only honest after-hours number for an index. A cash index
/// stops calculating at the bell and Yahoo restamps the closing level (a fake
/// +0.00%), while its future keeps trading.
///
/// Only indices with a future on the feed are covered. Yahoo carries no
/// Eurex/ICE/HKEX/CFFEX index futures, so FTSE, DAX, CAC, EURO STOXX, Hang
/// Seng and the China indices have no futures quote and get none.
struct IndexFuture {
    QString index;       // "^GSPC"
    QString proxy;       // "ES=F" — the continuous symbol; the quote reads one contract month
    QString root;        // "ES" — what the reader calls it
    QString name;        // "E-mini S&P 500"
};

/// The future quoted for an index, or nullptr when it has none.
const IndexFuture* index_future_for(const QString& index);

struct IndexFutureQuote {
    QString index;
    QString contract;      // the contract month quoted, "ESZ26.CME"
    double price = 0;      // last print
    double change = 0;     // since the previous close
    double pct = 0;
    double ref_price = 0;  // the contract's previous close (Yahoo's own "previous close")
    QDate ref_date;        // the CME trading day that close ended
    qint64 ts = 0;         // end of the last bar
};

/// Every index future in ONE request whatever `indices` holds, so all callers
/// send an identical payload and share the daemon's cached answer; the result
/// is filtered to `indices`. `cb` runs on the GUI thread unless `ctx` died
/// first; `ok` false carries the error, and the map holds only the indices
/// that came back with a sound number.
void fetch_index_futures(const QStringList& indices, QObject* ctx,
                         std::function<void(bool ok, QHash<QString, IndexFutureQuote>, QString error)> cb);

/// "ESZ26" from "ESZ26.CME".
QString contract_label(const IndexFutureQuote& q);

/// Tooltips shared by the dashboard column and the chart popup, so the two
/// can't describe the same number differently.
QString index_future_tooltip(const IndexFutureQuote& q, const QString& label);
QString index_future_unavailable_tooltip(const QString& index, const QString& label);

} // namespace fincept::services
