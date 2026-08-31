// src/core/util/BarTime.h
#pragma once
#include <QDate>
#include <QDateTime>
#include <QTimeZone>

/// Decoding the two kinds of epoch second this codebase carries.
///
/// They are both `qint64`, they are both called `timestamp`, and they decode
/// DIFFERENTLY. That is the whole reason this header exists: the distinction
/// lived only in the programmer's head, and
/// `QDateTime::fromSecsSinceEpoch(x).date()` compiles and looks correct for
/// both. It has been wrong in five places across four files, twice in the same
/// file — once on a line whose neighbour twenty lines up got it right.
///
///   BAR stamps (Candle::timestamp, TechnicalsData::last_bar_ts) are a daily
///   bar's session, stamped at MIDNIGHT IN THE EXCHANGE'S OWN ZONE. The instant
///   means nothing; only the calendar date does. Reading it in UTC is a day
///   early for every exchange east of Greenwich (Tokyo midnight = 15:00 UTC the
///   previous day), and reading it in the viewer's local zone is a day early
///   for every viewer west of the exchange.
///
///   EVENT stamps (an earnings announcement, a news publication) carry a real
///   time of day and belong in a real timezone — ET for a US print. bar_date()
///   is WRONG for these; use QDateTime::fromSecsSinceEpoch(ts).toTimeZone(...).
///
/// Prefer `Candle::date()` over calling this directly — it is shorter, it reads
/// better, and it cannot be pointed at the wrong kind of stamp.
namespace fincept::core::bartime {

/// Calendar date of a bar stamped at midnight in its exchange's zone.
///
/// The +14h trick: a stamp is D 00:00 at UTC offset `o`, so the instant is
/// D 00:00 − o in UTC. Adding 14h lands inside D's UTC day for o in the
/// HALF-OPEN range (−10h, +14h] — at exactly −10h the sum is (D+1) 00:00:00 and
/// this would return D+1. No securities exchange sits at −10h (the westernmost,
/// Hawaii, has no equity venue), so it is exact for every real one — with no
/// timezone database lookup and no per-exchange table to keep current. The
/// endpoint is stated precisely because this header is now the single authority
/// the rest of the codebase is pointed at.
inline QDate bar_date(qint64 unix_secs) {
    return QDateTime::fromSecsSinceEpoch(unix_secs + 14 * 3600, QTimeZone::utc()).date();
}

} // namespace fincept::core::bartime
