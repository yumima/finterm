// src/services/markets/IndexFutures.h
#pragma once
#include "services/markets/LivePrice.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cmath>
#include <functional>
#include <limits>

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
    QString index; // "^GSPC"
    QString proxy; // "ES=F" — the continuous symbol; the quote reads one contract month
    QString root;  // "ES" — what the reader calls it
    QString name;  // "E-mini S&P 500"
};

/// The future quoted for an index, or nullptr when it has none.
const IndexFuture* index_future_for(const QString& index);

struct IndexFutureQuote {
    QString index;
    QString contract;  // the contract month quoted, "ESZ26.CME"
    double price = 0;  // last print
    double change = 0; // since the previous close
    double pct = std::numeric_limits<double>::quiet_NaN(); // NaN = pending (see is_pending)
    /// Pending because Yahoo REFUSED the quote (rate limit) — not declined on
    /// purpose (dead feed, waiting for the reopen). Only a refusal keeps the
    /// number the stream already filled; a declined row goes back to a dash.
    bool refused = false;
    QString reason; ///< why a pending row has no number (the daemon's words)
    double ref_price = 0; // the contract's previous close (Yahoo's own "previous close")
    QDate session_date;   // the CME trading day of the last print — the close is the one before it
    qint64 ts = 0;        // end of the last bar
};

/// Every index future in ONE request whatever `indices` holds, so all callers
/// send an identical payload and share the daemon's cached answer; the result
/// is filtered to `indices`. `cb` runs on the GUI thread unless `ctx` died
/// first; `ok` false carries the error, and the map holds only the indices
/// that came back with a sound number.
void fetch_index_futures(const QStringList& indices, QObject* ctx,
                         std::function<void(bool ok, QHash<QString, IndexFutureQuote>, QString error)> cb);

/// The CME trading day an instant belongs to: the ET date, a session that
/// opens at 18:00 ET belonging to the next day, weekends to Monday.
inline QDate cme_trading_day(qint64 secs) {
    // EVENT-STAMP: a print is an instant, placed on the CME session clock.
    const QDateTime et = QDateTime::fromSecsSinceEpoch(secs).toTimeZone(QTimeZone("America/New_York"));
    QDate d = et.date();
    if (et.time() >= QTime(18, 0))
        d = d.addDays(1);
    while (d.dayOfWeek() >= 6)
        d = d.addDays(1);
    return d;
}

/// Lay a live print of `contract` on `q`: true when it moved it.
///
/// A push print carries Yahoo's change against the contract's own previous
/// close, so price and close arrive as a PAIR from one print — the quote takes
/// both, never this price against a close from elsewhere. That also fills a
/// quote the fetch could not (pending: Yahoo refused the batch quote) and
/// carries it across the 18:00 ET reopen, where the close rolls with the
/// prints. Older prints than the one shown (a reconnect replay) are ignored.
inline bool apply_future_tick(IndexFutureQuote& q, const QString& contract, double price, double change,
                              qint64 time_ms) {
    if (contract != q.contract || !(price > 0) || !std::isfinite(change))
        return false;
    const double ref = price - change;
    if (!(ref > 0))
        return false;
    const qint64 ts = time_ms / 1000;
    if (ts > 0 && ts < q.ts)
        return false;
    if (price == q.price && std::abs(ref - q.ref_price) <= 1e-9 * ref)
        return false;
    q.price = price;
    q.ref_price = ref;
    q.change = change;
    q.pct = change / ref * 100.0;
    if (ts > 0) {
        q.ts = ts;
        q.session_date = cme_trading_day(ts);
    }
    return true;
}

/// True for a row that names its contract but has no number yet (the fetch
/// was refused or the contract hasn't printed): a dash, filled by its prints.
inline bool is_pending(const IndexFutureQuote& q) {
    return !std::isfinite(q.pct) || !(q.price > 0);
}

/// "ESZ26" from "ESZ26.CME".
QString contract_label(const IndexFutureQuote& q);

/// Tooltips shared by the dashboard column and the chart popup, so the two
/// can't describe the same number differently.
QString index_future_tooltip(const IndexFutureQuote& q, const QString& label);
QString index_future_unavailable_tooltip(const QString& index, const QString& label);

} // namespace fincept::services
