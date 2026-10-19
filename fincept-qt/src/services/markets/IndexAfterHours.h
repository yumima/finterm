// src/services/markets/IndexAfterHours.h
#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace fincept::services {

/// After-hours for a cash index, implied by its future.
///
/// A cash index stops calculating at the bell, so it has no after-hours prints
/// of its own — Yahoo restamps the closing level, which reads as +0.00% for
/// ever. What trades after the close is the index future; its move since the
/// moment the CASH index closed (measured against the future's own price at
/// that instant, so the futures/cash basis cancels) is the after-hours move.
///
/// Only indices with a future on the feed are covered. Yahoo carries no
/// Eurex/ICE/HKEX/CFFEX index futures, so FTSE, DAX, CAC, EURO STOXX, Hang
/// Seng and the China indices have no honest after-hours number and get none.
struct IndexProxy {
    QString index;   // "^GSPC"
    QString proxy;   // "ES=F"
    QString proxy_name;
    QString tz;      // the cash market's zone
    QString open;    // "HH:MM" local
    QString close;   // "HH:MM" local
    QString lunch = {};        // "HH:MM-HH:MM" midday break, when the market has one
    QString early_close = {};  // "HH:MM" of the market's half-day close, when it has them
};

/// The future an index's after-hours is read from, or nullptr when it has none.
const IndexProxy* index_proxy_for(const QString& index);

struct IndexAfterHoursQuote {
    QString index;
    QString proxy;
    QString contract;        // the specific contract both prints came from ("ESZ26.CME")
    double pct = 0;          // the future's move since the cash close, percent
    double ref_price = 0;    // the future at the cash close
    double ext_price = 0;    // the future's latest print (or at the open)
    qint64 ref_ts = 0;       // the cash close, epoch seconds
    qint64 ext_ts = 0;       // end of the future's bar the move runs to
    QString session;         // "POST" same evening, "PRE" toward / at the next open
    bool cash_open = false;  // the cash market is trading: the number is frozen at the open
};

/// Every index with a proxy is fetched in ONE request whatever `indices` holds,
/// so all callers send an identical payload and share the daemon's cached
/// answer; the result is filtered to `indices`. `cb` runs on the GUI thread unless `ctx` died first; `ok`
/// false carries the error, and the map holds only the indices that came back
/// with a sound number.
void fetch_index_after_hours(const QStringList& indices, QObject* ctx,
                             std::function<void(bool ok, QHash<QString, IndexAfterHoursQuote>, QString error)> cb);

/// Cell text and tooltip, shared by the dashboard column and the chart popup
/// so the two can't describe the same number differently.
QString index_after_hours_tooltip(const IndexAfterHoursQuote& q, const QString& label);
QString index_after_hours_unavailable_tooltip(const QString& index, const QString& label);

} // namespace fincept::services
