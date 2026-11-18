// src/screens/dashboard/widgets/ExtendedHoursMath.h
#pragma once
#include "services/calendar/MarketCalendar.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QLatin1String>
#include <QRegularExpression>
#include <QString>
#include <QTimeZone>
#include <QtGlobal>


#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

/// The rules behind every extended-hours (EXT%) surface, kept free of widget
/// types so they can be tested on real payloads. This arithmetic has put a
/// sign inversion, a stale reference and — when one surface showed the
/// overnight print and another the after-hours one, both unlabelled — two
/// "contradictory" numbers for one stock on screen.
///
/// The model: per symbol, the latest reading of EACH extended session —
/// pre-market, after-hours, overnight — each with the close it is measured
/// against. The portfolio surfaces show the latest of them and name its
/// session; the ER Overview panel shows each. Both read the same readings, so
/// they cannot disagree about a session.
namespace finterm::screens::widgets::exthours {

// ── Sessions and their names: the ONE label set ─────────────────────────────

enum class ExtSide { Pre = 0, Post = 1, Overnight = 2 };
inline constexpr int kSideCount = 3;
inline constexpr std::array<ExtSide, kSideCount> kSides{ExtSide::Pre, ExtSide::Post, ExtSide::Overnight};

/// The machine contract (daemon JSON, snapshot, a futures-table code).
inline QString ext_side_key(ExtSide s) {
    return s == ExtSide::Pre ? QStringLiteral("PRE") : s == ExtSide::Post ? QStringLiteral("POST")
                                                                          : QStringLiteral("OVERNIGHT");
}
inline std::optional<ExtSide> ext_side_from_key(const QString& k) {
    if (k == QLatin1String("PRE"))
        return ExtSide::Pre;
    if (k == QLatin1String("POST"))
        return ExtSide::Post;
    if (k == QLatin1String("OVERNIGHT"))
        return ExtSide::Overnight;
    return std::nullopt;
}
/// Short tag for a tight cell (heatmap tile, table cell).
inline QString ext_side_tag(ExtSide s) {
    return s == ExtSide::Pre ? QStringLiteral("PRE") : s == ExtSide::Post ? QStringLiteral("AH")
                                                                          : QStringLiteral("ON");
}
/// Full name for tooltips, panel titles and status lines.
inline QString ext_side_name(ExtSide s) {
    return s == ExtSide::Pre ? QStringLiteral("Pre-market") : s == ExtSide::Post ? QStringLiteral("After-hours")
                                                                                 : QStringLiteral("Overnight");
}
/// The session's ET window (after-hours runs 13:00–17:00 on an early close).
inline QString ext_side_window(ExtSide s) {
    return s == ExtSide::Pre ? QStringLiteral("04:00–09:30") : s == ExtSide::Post ? QStringLiteral("16:00–20:00")
                                                                                       : QStringLiteral("20:00–04:00");
}
/// What "EXT" means wherever a surface shows it.
inline QString ext_definition() {
    return QStringLiteral("Extended hours: pre-market 04:00–09:30, after-hours 16:00–20:00, "
                          "overnight 20:00–04:00 ET, vs the last regular close.");
}
/// Column / mode name.
inline QString ext_column_title() { return QStringLiteral("EXT%"); }

/// Whether a pre-market or after-hours print can land at `at`: a trading day,
/// 04:00–09:30 ET, or the bell to the end of after-hours — 16:00–20:00, and
/// 13:00–17:00 on an early close (the day after Thanksgiving, Christmas Eve,
/// 3 July). The same calendar as extended_rules.py (bell / post_window), from
/// services/calendar/MarketCalendar.h. Overnight prints come through the
/// stream, so the overnight session is not a fetch window.
inline bool fetch_window_open_at(const QDateTime& at) {
    const QDateTime et = at.toTimeZone(QTimeZone("America/New_York"));
    const QDate d = et.date();
    if (!finterm::market_calendar::is_trading_day(d))
        return false;
    const bool early = finterm::market_calendar::is_early_close(d);
    const QTime t = et.time();
    const QTime bell = early ? QTime(13, 0) : QTime(16, 0);
    const QTime post_end = early ? QTime(17, 0) : QTime(20, 0);
    return (t >= QTime(4, 0) && t < QTime(9, 30)) || (t >= bell && t < post_end);
}

/// Only plain US tickers trade these sessions: no exchange suffix, index,
/// future, FX pair or crypto. Same rule as extended_rules.has_us_extended_session
/// (scripts/extended_rules.py) — change them together.
inline bool has_us_extended_session(const QString& symbol) {
    const QString s = symbol.trimmed().toUpper();
    if (s.isEmpty() || s.startsWith('^') || s.contains('='))
        return false;
    if (s.endsWith(QLatin1String("-USD")) || s.endsWith(QLatin1String("-USDT")))
        return false;
    static const QRegularExpression re(QStringLiteral("^[A-Z]{1,5}(?:-[A-Z]{1,2}|\\.[ABC])?$"));
    return re.match(s).hasMatch();
}

// ── One session's reading ───────────────────────────────────────────────────

inline std::optional<double> json_num(const QJsonObject& o, const char* key) {
    const auto v = o.value(QLatin1String(key));
    if (!v.isDouble())
        return std::nullopt;
    return v.toDouble();
}

/// One session's latest print. One number is stored per fact; the move is
/// derived (`pct()`), never carried beside the prices it came from.
struct Reading {
    double price = 0;      ///< the print
    double regular = 0;    ///< the close it is measured against
    qint64 print_ts = 0;   ///< EVENT-STAMP: when it printed (0 = unknown)
    qint64 regular_ts = 0; ///< EVENT-STAMP: when that close was struck (0 = unknown)
    QDate day;             ///< ET date of the print
    QDate regular_day;     ///< ET date of the close (invalid = unknown: a print-built reading)
    qint64 updated_at = 0; ///< when this value last arrived
    double pct() const { return regular > 0 ? (price - regular) / regular * 100.0 : 0.0; }
    bool operator==(const Reading&) const = default;
};

/// One symbol: each session's reading, and the last completed close.
struct SymbolExt {
    std::array<std::optional<Reading>, kSideCount> sides;
    double regular = 0;    ///< the most recent completed close (from the fetch)
    qint64 regular_ts = 0; ///< EVENT-STAMP: when it was struck
    bool declined = false; ///< that close is stale: no readings, and prints build none
    const std::optional<Reading>& at(ExtSide s) const { return sides[static_cast<int>(s)]; }
    std::optional<Reading>& at(ExtSide s) { return sides[static_cast<int>(s)]; }
    bool operator==(const SymbolExt&) const = default;
};

/// A reading is a move since the LAST close only if it printed after it.
/// This morning's pre-market, after today's bell, is a true reading of the
/// pre-market session — but against yesterday's close, and no longer the
/// latest move; pairing it with today's close inverted a day's move once
/// (AMZN, -7.82% on a +3.9% day). Without a known close nothing is judged.
inline bool is_current(const SymbolExt& x, const Reading& r) {
    return x.regular_ts <= 0 || r.print_ts <= 0 || r.print_ts > x.regular_ts;
}

/// What a portfolio EXT surface shows: the latest current reading, named.
struct HeldQuote {
    double pct = 0;
    double price = 0;     ///< the extended print
    double regular = 0;   ///< the close it is measured against
    ExtSide side = ExtSide::Post;
    qint64 print_ts = 0;  ///< EVENT-STAMP: when the print happened (0 = unknown)
    qint64 updated_at = 0; ///< when this value last arrived
};

/// The cell text every portfolio EXT surface shows: "+1.30% ON".
inline QString ext_cell_text(const HeldQuote& q, int decimals) {
    return QString("%1%2% %3").arg(q.pct >= 0 ? "+" : "").arg(q.pct, 0, 'f', decimals).arg(ext_side_tag(q.side));
}

/// The tooltip line naming the print: "Overnight $157.48 vs the $155.46 close".
inline QString ext_quote_line(const HeldQuote& q) {
    return QString("%1 $%2 vs the $%3 close (%4%5%)")
        .arg(ext_side_name(q.side))
        .arg(q.price, 0, 'f', 2)
        .arg(q.regular, 0, 'f', 2)
        .arg(q.pct >= 0 ? "+" : "")
        .arg(q.pct, 0, 'f', 2);
}

inline std::optional<HeldQuote> latest(const SymbolExt& x) {
    std::optional<HeldQuote> out;
    if (x.declined)
        return out;
    for (const ExtSide s : kSides) {
        const auto& r = x.at(s);
        if (!r || !(r->regular > 0) || !is_current(x, *r))
            continue;
        const qint64 t = r->print_ts > 0 ? r->print_ts : r->updated_at;
        if (out && t < (out->print_ts > 0 ? out->print_ts : out->updated_at))
            continue;
        out = HeldQuote{r->pct(), r->price, r->regular, s, r->print_ts, r->updated_at};
    }
    return out;
}

/// Same close, within the stream's float32 encoding (see tick_pairs_with).
inline bool same_close(double a, double b) {
    return a > 0 && b > 0 && std::abs(a - b) <= 1e-5 * b;
}

/// Put `in` in a session's slot. The NEWER print wins, whatever its close: a
/// fetch is served from a quote that can trail the push stream by seconds,
/// and a print against a different close is a later session (the bell rang,
/// the stream rolled). An older one — a fetch behind the tape, a print a
/// reconnect replays — changes nothing. Returns whether the slot changed.
inline bool merge_reading(std::optional<Reading>& slot, const Reading& in) {
    if (!(in.price > 0) || !(in.regular > 0))
        return false;
    if (!slot) {
        slot = in;
        return true;
    }
    if (in.print_ts > 0 && slot->print_ts > in.print_ts)
        return false;
    // The same print again (each poll repeats it): nothing shown changes.
    if (slot->price == in.price && slot->regular == in.regular && slot->print_ts == in.print_ts &&
        slot->regular_ts == in.regular_ts && slot->day == in.day && slot->regular_day == in.regular_day)
        return false;
    Reading next = in;
    // A print-built reading does not know its close's day or time; the slot
    // does when it is the same close.
    if (same_close(slot->regular, in.regular)) {
        if (!next.regular_day.isValid())
            next.regular_day = slot->regular_day;
        if (next.regular_ts <= 0)
            next.regular_ts = slot->regular_ts;
    }
    slot = next;
    return true;
}

// ── The daemon's rows ───────────────────────────────────────────────────────

/// The daemon declined this symbol ON PURPOSE (its reference close is from an
/// earlier day): no number, and a live print must not build one back either.
/// Read from the explicit flag only — a row with no prints is "none yet", and
/// reading that as a refusal blanked heatmap tiles for good (2026-11-17).
inline bool row_declined(const QJsonObject& row) { return row.value(QLatin1String("declined")).toBool(); }

/// The source refused or dropped this symbol (rate limit, timeout): not an
/// answer. Keep what is shown and ask again.
inline bool row_unavailable(const QJsonObject& row) { return row.value(QLatin1String("unavailable")).toBool(); }

/// One session object of a row ({price, ts, day, ref, ref_ts, ref_date}).
inline std::optional<Reading> reading_from_json(const QJsonValue& v, qint64 now_secs) {
    if (!v.isObject())
        return std::nullopt;
    const QJsonObject o = v.toObject();
    const auto price = json_num(o, "price");
    const auto ref = json_num(o, "ref");
    if (!price || !ref || !(*price > 0) || !(*ref > 0))
        return std::nullopt;
    Reading r;
    r.price = *price;
    r.regular = *ref;
    r.print_ts = static_cast<qint64>(json_num(o, "ts").value_or(0));
    r.regular_ts = static_cast<qint64>(json_num(o, "ref_ts").value_or(0));
    // An extended print has to come after the close it is measured against.
    if (r.print_ts > 0 && r.regular_ts > 0 && r.print_ts <= r.regular_ts)
        return std::nullopt;
    r.day = QDate::fromString(o.value(QLatin1String("day")).toString(), Qt::ISODate);
    r.regular_day = QDate::fromString(o.value(QLatin1String("ref_date")).toString(), Qt::ISODate);
    r.updated_at = static_cast<qint64>(json_num(o, "updated_at").value_or(static_cast<double>(now_secs)));
    return r;
}

/// The same schema back (the service's snapshot), so one reader parses both.
inline QJsonObject reading_to_json(const Reading& r) {
    return QJsonObject{{"price", r.price},
                       {"ts", r.print_ts},
                       {"day", r.day.toString(Qt::ISODate)},
                       {"ref", r.regular},
                       {"ref_ts", r.regular_ts},
                       {"ref_date", r.regular_day.toString(Qt::ISODate)},
                       {"updated_at", r.updated_at}};
}

/// What one `extended_hours` row did.
enum class RowEffect { Updated, Kept, Declined, Unavailable };

/// Fold one daemon row into a symbol's readings — the single rule behind every
/// EXT surface, so none can disagree with another about the same symbol.
///
///   unavailable → not an answer: whatever is held stays (ask again later).
///   declined    → the reference close is stale: no readings, and a live
///                 print must not build one back.
///   otherwise   → the last close moves to the row's; each session the row
///                 carries is merged (merge_reading); a session it does not
///                 carry keeps its last reading — it is a real past print, and
///                 is_current() keeps it off the portfolio once a newer close
///                 is struck.
inline RowEffect apply_row(QHash<QString, SymbolExt>& all, const QJsonObject& row, qint64 now_secs) {
    const QString sym = row.value(QLatin1String("symbol")).toString();
    if (sym.isEmpty() || row_unavailable(row))
        return RowEffect::Unavailable;
    SymbolExt& x = all[sym];
    const SymbolExt before = x;
    const auto reg = json_num(row, "regular");
    const auto reg_ts = json_num(row, "regular_ts");
    if (reg && *reg > 0) {
        x.regular = *reg;
        x.regular_ts = reg_ts ? static_cast<qint64>(*reg_ts) : 0;
    }
    if (row_declined(row)) {
        x.declined = true;
        x.sides = {};
        return x == before ? RowEffect::Kept : RowEffect::Declined;
    }
    x.declined = false;
    if (const auto r = reading_from_json(row.value(QLatin1String("pre")), now_secs))
        merge_reading(x.at(ExtSide::Pre), *r);
    if (const auto r = reading_from_json(row.value(QLatin1String("post")), now_secs))
        merge_reading(x.at(ExtSide::Post), *r);
    return x == before ? RowEffect::Kept : RowEffect::Updated;
}

// ── The push stream's prints ────────────────────────────────────────────────

/// The session a stream print belongs to: market_hours 0 pre-market (proto3
/// omits 0), 2 after-hours, 4 overnight (20:00–04:00 ET). 1 is the regular
/// session — not an extended print.
inline std::optional<ExtSide> side_of_market_hours(int market_hours) {
    switch (market_hours) {
        case 0:
            return ExtSide::Pre;
        case 2:
            return ExtSide::Post;
        case 4:
            return ExtSide::Overnight;
        default:
            return std::nullopt;
    }
}

/// Lay one stream print on a symbol's readings. Returns whether anything
/// shown changed.
///
/// The print's change is Yahoo's own, against the close Yahoo pairs that
/// session with (pre-market: the previous close; after-hours and overnight:
/// the day's), so (price, price − change) is a self-consistent reading by
/// construction (services::tick_pairs_with is the same identity). A symbol
/// the fetch declined gets none.
///
/// Only a plain US stock's print may start a reading or move one to another
/// close: its market_hours are the US equity sessions. A future, FX pair,
/// coin or foreign listing is measured against its settle by the fetch, and
/// its stream change is against something else (a 24-hour change, Yahoo's own
/// previous close) — its print only moves a reading against the SAME close.
inline bool apply_print(QHash<QString, SymbolExt>& all, const QString& sym, int market_hours, double price,
                        double change, qint64 time_ms, qint64 now_secs) {
    const auto side = side_of_market_hours(market_hours);
    if (!side || !(price > 0) || !std::isfinite(price) || !std::isfinite(change))
        return false;
    const double close = price - change;
    if (!(close > 0))
        return false;
    const auto it = all.constFind(sym);
    if (it != all.constEnd() && it->declined)
        return false;
    if (!has_us_extended_session(sym)) {
        const auto* slot = it != all.constEnd() ? &it->at(*side) : nullptr;
        if (!slot || !*slot || !same_close((*slot)->regular, close))
            return false;
    }
    Reading r;
    r.price = price;
    r.regular = close;
    r.print_ts = time_ms > 0 ? time_ms / 1000 : now_secs;
    r.day = QDateTime::fromSecsSinceEpoch(r.print_ts, QTimeZone("America/New_York")).date(); // EVENT-STAMP: a print
    r.updated_at = now_secs;
    SymbolExt& x = all[sym];
    auto& slot = x.at(*side);
    // The same price against the same close is not news: no repaint for it.
    if (slot && same_close(slot->regular, close) && slot->price == price) {
        slot->print_ts = std::max(slot->print_ts, r.print_ts);
        return false;
    }
    return merge_reading(slot, r);
}

} // namespace finterm::screens::widgets::exthours
