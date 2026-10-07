// src/services/portfolio/PortfolioDates.h
//
// One date convention for the portfolio's return math.
//
// NAV snapshots are keyed by the LOCAL calendar date (QDate::currentDate()
// when build_summary writes them). Transactions are stamped in UTC by
// add_asset / sell_asset (QDateTime::currentDateTimeUtc() → "…T…Z"), so for
// a US user trading in the evening the UTC date is already TOMORROW. Keying a
// flow on transaction_date.left(10) put the purchase in the segment after the
// snapshot that already contained it: a phantom gain on the trade day and the
// matching phantom loss the next. Every consumer that buckets a transaction
// by date goes through transaction_local_date() instead, which converts any
// stored timestamp carrying a zone (Z or ±hh:mm) to the local date the
// snapshots use. Date-only strings (user-entered trade dates, imports) and
// zone-less timestamps are already local by convention and pass through.
// This is a read-time conversion — stored rows keep their original stamps.
//
// Header-only so the pure return/ledger units (and their tests) need nothing
// extra linked.

#pragma once

#include "screens/portfolio/PortfolioTypes.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace fincept::portfolio {

/// YYYY-MM-DD of `stamp` in the local time zone (see header comment).
inline QString local_date_of(const QString& stamp) {
    const QString s = stamp.trimmed();
    if (s.size() <= 10)
        return s.left(10);
    // Only a stamp with an explicit zone designator names an instant that can
    // land on a different local date. Check the tail rather than parsing every
    // row: "Z", or a "+hh:mm"/"-hh:mm"/"+hhmm" offset after the time part.
    const QString tail = s.mid(10);
    const bool has_zone = tail.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive) ||
                          tail.lastIndexOf(QLatin1Char('+')) > 0 ||
                          (tail.lastIndexOf(QLatin1Char('-')) > 0 && tail.contains(QLatin1Char(':')));
    if (!has_zone)
        return s.left(10);
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        return s.left(10);
    return dt.toLocalTime().date().toString(Qt::ISODate);
}

inline QString transaction_local_date(const Transaction& t) {
    return local_date_of(t.transaction_date);
}

/// v049 synthesized an opening BUY for every pre-ledger holding. When the
/// holding had no first_purchase_date, the row was dated at MIGRATION time —
/// a fabricated date, detectable as the synthesis marker plus a transaction
/// date on the day the row was created. Live snapshots from before that date
/// already contain the position, so the row is not an external flow; the
/// NAV backfill must agree (it may not reconstruct history before that date,
/// where the position's real holding period is unknown).
inline bool is_fabricated_opening(const Transaction& t) {
    return t.notes.contains(QLatin1String("synthesized from the holdings row")) &&
           t.transaction_date.left(10) == t.created_at.left(10);
}

/// Latest local date of a migration-dated opening BUY (empty when none).
inline QString fabricated_opening_cutoff(const QVector<Transaction>& txns) {
    QString cutoff;
    for (const auto& t : txns) {
        if (is_fabricated_opening(t)) {
            const QString d = transaction_local_date(t);
            if (d > cutoff)
                cutoff = d;
        }
    }
    return cutoff;
}

/// The snapshots the return/metric/chart math may use. A 'backfill'
/// (reconstructed) row dated before a migration-dated opening BUY was built
/// without that position — whose real holding period is unknown — while the
/// return math treats the position as already held there; reading it would
/// turn the whole position into one day's phantom gain. Such rows are
/// EXCLUDED at read time (kept on disk, so nothing is lost and the rule can
/// change). 'live' rows were real valuations that contained the position and
/// always stay. The backfill no longer writes such rows at all.
inline QVector<PortfolioSnapshot> usable_snapshots(const QVector<PortfolioSnapshot>& snaps,
                                                   const QVector<Transaction>& txns) {
    const QString cutoff = fabricated_opening_cutoff(txns);
    if (cutoff.isEmpty())
        return snaps;
    QVector<PortfolioSnapshot> out;
    out.reserve(snaps.size());
    for (const auto& s : snaps)
        if (s.source == QLatin1String("live") || s.snapshot_date.left(10) >= cutoff)
            out.append(s);
    return out;
}

} // namespace fincept::portfolio
