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
///   is WRONG for these; use market_date_et() below, and never hand-roll the
///   conversion: doing so is how the rule came to have five copies.
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

/// ── The EVENT half ──────────────────────────────────────────────────────────
///
/// The calendar date an event instant falls on, in US market time. This is the
/// counterpart to bar_date() above, and it lives here for the same reason: the
/// rule was open-coded at five sites — twice as an identical `market_today()`
/// and three times as a `QTimeZone et("America/New_York")` lambda — which is
/// precisely how bar_date's own rule came to be wrong in eight places. A rule
/// with five copies has five chances to drift.
///
/// Use this for announcements, report dates and countdowns. Do NOT use it for a
/// bar stamp: those carry no meaningful time of day, and converting one through
/// a real timezone is the mistake bar_date() exists to prevent.
/// Named `_et` on purpose. The zone is hard-coded, and the callers are NOT
/// US-only — the ER screen loads foreign listings too, and a Tokyo
/// announcement decoded in ET puts a countdown and the ±5-day match window a
/// day out. That limitation predates this header, but the locals it replaced
/// were called `et`, which admitted it; a generically-named shared helper
/// would let the next caller inherit the assumption without ever seeing it.
///
/// Fixing it properly needs each symbol's exchange, which is not available
/// here. Until it is, the name carries the caveat.
inline QDate market_date_et(const QDateTime& instant) {
    // Built once. Constructing a QTimeZone is an IANA lookup behind a mutex
    // plus a shared-data allocation; the lambdas this replaced hoisted it
    // outside their loops, and nearest_within_window() calls this once per
    // point per stored record. Same pattern as PortfolioPerfChart.
    static const QTimeZone kEt("America/New_York");
    // A missing tz database makes kEt invalid, and toTimeZone() would then
    // yield an invalid QDate that propagates silently: `d < today` compares two
    // null julian days and is always false, so every reported quarter files as
    // upcoming, and daysTo() on invalid dates is 0 — "reports today". Degrade
    // to UTC instead, which is at most five hours from ET and never invalid.
    // PreIpoService already logs this condition when it hits it.
    if (!kEt.isValid())
        return instant.toUTC().date();
    return instant.toTimeZone(kEt).date();
}

inline QDate market_date_et(qint64 unix_secs) {
    return market_date_et(QDateTime::fromSecsSinceEpoch(unix_secs));
}

/// Today, on the US market's calendar. Not the viewer's — a horizon or a
/// past/future split written from a local clock is a day out for anyone east or
/// west of the exchange, and worse still when only ONE side of a comparison
/// moves (see EarningsCalendarWidget, where it hid an upcoming print).
inline QDate market_today_et() {
    return market_date_et(QDateTime::currentDateTime());
}

} // namespace fincept::core::bartime
