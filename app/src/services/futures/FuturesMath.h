// src/services/futures/FuturesMath.h
#pragma once
// Forward curve, positioning and trade-ticket arithmetic for the Futures
// screen. Pure (QtCore only) so every number on the screen is unit-tested.
//
// Absent is NaN throughout: a contract with no reported open interest is not
// a contract with zero open interest, and nothing here turns one into the
// other.

#include "services/futures/FuturesCatalog.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QVector>

#include <cmath>
#include <limits>

namespace finterm::services::futures {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ── Curve ───────────────────────────────────────────────────────────────────

struct CurveContract {
    QString symbol; ///< "CLF27.NYM"
    int year = 0;
    int month = 0;
    double price = kNaN;
    double prev_close = kNaN;
    double change = kNaN;
    double pct = kNaN;
    double volume = kNaN;
    double oi = kNaN;
    double bid = kNaN;
    double ask = kNaN;
    QDate expiry; ///< last trading day, as the vendor reports it
    qint64 ts = 0; ///< time of the last print
    bool stale = false; ///< has not printed for a day and a half

    QString code() const { return symbol.section(QLatin1Char('.'), 0, 0); }
    QString month_label() const {
        return month >= 1 && month <= 12
                   ? QStringLiteral("%1 %2").arg(QLocale::c().monthName(month, QLocale::ShortFormat))
                         .arg(year % 100, 2, 10, QLatin1Char('0'))
                   : QString();
    }
};

struct Curve {
    QString root;
    QString front; ///< the month the continuous series follows
    QString currency;
    qint64 as_of = 0;
    QVector<CurveContract> contracts; ///< nearest first

    int index_of(const QString& symbol) const {
        for (int i = 0; i < contracts.size(); ++i)
            if (contracts[i].symbol == symbol)
                return i;
        return -1;
    }
};

/// Where the same months settled some sessions ago.
struct PastCurve {
    QString label; ///< "1W", "1M"
    int sessions_back = 0;
    QDate date;
    QHash<QString, double> closes; ///< by contract symbol; absent = did not trade
};

inline double json_num(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : kNaN; // null stays NaN, never 0
}

inline Curve parse_curve(const QJsonObject& o) {
    Curve c;
    c.root = o.value(QLatin1String("root")).toString();
    c.front = o.value(QLatin1String("front")).toString();
    c.currency = o.value(QLatin1String("currency")).toString();
    c.as_of = static_cast<qint64>(o.value(QLatin1String("as_of")).toDouble());
    for (const auto& v : o.value(QLatin1String("contracts")).toArray()) {
        const QJsonObject r = v.toObject();
        CurveContract k;
        k.symbol = r.value(QLatin1String("symbol")).toString();
        const QString ym = r.value(QLatin1String("month")).toString(); // "2026-12"
        k.year = ym.left(4).toInt();
        k.month = ym.mid(5, 2).toInt();
        k.price = json_num(r, "price");
        if (k.symbol.isEmpty() || k.month < 1 || k.month > 12 || !(k.price > 0))
            continue;
        k.prev_close = json_num(r, "prev_close");
        k.change = json_num(r, "change");
        k.pct = json_num(r, "pct");
        k.volume = json_num(r, "volume");
        k.oi = json_num(r, "oi");
        k.bid = json_num(r, "bid");
        k.ask = json_num(r, "ask");
        k.expiry = QDate::fromString(r.value(QLatin1String("expiry")).toString(), Qt::ISODate);
        k.ts = static_cast<qint64>(json_num(r, "ts") > 0 ? json_num(r, "ts") : 0);
        k.stale = r.value(QLatin1String("stale")).toBool();
        c.contracts.append(k);
    }
    return c;
}

inline QVector<PastCurve> parse_curve_history(const QJsonObject& o) {
    QVector<PastCurve> out;
    for (const auto& v : o.value(QLatin1String("curves")).toArray()) {
        const QJsonObject r = v.toObject();
        PastCurve p;
        p.label = r.value(QLatin1String("label")).toString();
        p.sessions_back = r.value(QLatin1String("sessions_back")).toInt();
        p.date = QDate::fromString(r.value(QLatin1String("date")).toString(), Qt::ISODate);
        const QJsonObject closes = r.value(QLatin1String("closes")).toObject();
        for (auto it = closes.begin(); it != closes.end(); ++it)
            if (it.value().isDouble() && it.value().toDouble() > 0)
                p.closes.insert(it.key(), it.value().toDouble());
        if (!p.label.isEmpty() && p.date.isValid() && !p.closes.isEmpty())
            out.append(p);
    }
    return out;
}

/// The rate a 100-minus-rate contract implies, in percent.
inline double implied_rate(double price) {
    return std::isfinite(price) && price > 0 ? 100.0 - price : kNaN;
}

enum class Shape { Unknown, Contango, Backwardation, Flat };

inline QString shape_label(Shape s) {
    switch (s) {
        case Shape::Contango: return QStringLiteral("CONTANGO");
        case Shape::Backwardation: return QStringLiteral("BACKWARDATION");
        case Shape::Flat: return QStringLiteral("FLAT");
        case Shape::Unknown: break;
    }
    return QString();
}

/// One month measured against the anchor (front) month.
struct Carry {
    double spread = kNaN;         ///< this month − anchor, in price
    double spread_pct = kNaN;     ///< as a percent of the anchor
    double annualized_pct = kNaN; ///< spread_pct scaled to a year by the days between the two expiries
};

/// `annualized_pct` needs both expiry dates; without them it stays NaN rather
/// than assume thirty-day months.
inline Carry carry_between(const CurveContract& anchor, const CurveContract& other) {
    Carry c;
    if (!(anchor.price > 0) || !(other.price > 0))
        return c;
    c.spread = other.price - anchor.price;
    c.spread_pct = c.spread / anchor.price * 100.0;
    if (anchor.expiry.isValid() && other.expiry.isValid()) {
        const qint64 days = anchor.expiry.daysTo(other.expiry);
        if (days != 0)
            c.annualized_pct = c.spread_pct * 365.0 / static_cast<double>(days);
    }
    return c;
}

struct CurveSummary {
    int anchor = -1; ///< index of the front month in Curve::contracts
    int next = -1;   ///< the next month out that has printed recently
    int far = -1;    ///< the month nearest one year past the anchor
    Carry next_carry;
    Carry far_carry;
    Shape shape = Shape::Unknown;
    /// What holding the anchor and rolling into `next` earns a long, percent
    /// a year: positive in backwardation, negative in contango.
    double roll_yield_pct = kNaN;
};

/// A slope smaller than this (percent of the front price) reads as flat.
inline constexpr double kFlatSlopePct = 0.05;

inline CurveSummary summarize(const Curve& curve) {
    CurveSummary s;
    const auto& k = curve.contracts;
    if (k.isEmpty())
        return s;
    s.anchor = curve.index_of(curve.front);
    if (s.anchor < 0)
        s.anchor = 0;
    for (int i = s.anchor + 1; i < k.size(); ++i)
        if (!k[i].stale) {
            s.next = i;
            break;
        }
    if (s.next < 0)
        return s;
    s.next_carry = carry_between(k[s.anchor], k[s.next]);
    if (std::isfinite(s.next_carry.annualized_pct))
        s.roll_yield_pct = -s.next_carry.annualized_pct;

    // The shape is read a year out where the curve reaches that far, else at
    // its last fresh month — one calendar spread is too noisy to name a curve.
    const int target = k[s.anchor].year * 12 + k[s.anchor].month + 12;
    int best_gap = std::numeric_limits<int>::max();
    for (int i = s.anchor + 1; i < k.size(); ++i) {
        if (k[i].stale)
            continue;
        const int gap = std::abs(k[i].year * 12 + k[i].month - target);
        if (gap < best_gap) {
            best_gap = gap;
            s.far = i;
        }
    }
    if (s.far >= 0) {
        s.far_carry = carry_between(k[s.anchor], k[s.far]);
        const double p = s.far_carry.spread_pct;
        if (std::isfinite(p))
            s.shape = std::abs(p) < kFlatSlopePct ? Shape::Flat : p > 0 ? Shape::Contango : Shape::Backwardation;
    }
    return s;
}

// ── Positioning (CFTC Commitments of Traders) ───────────────────────────────

struct CotRow {
    QDate date; ///< the Tuesday the positions were held
    double oi = kNaN;
    double nc_long = kNaN, nc_short = kNaN; ///< non-commercial: large speculators
    double c_long = kNaN, c_short = kNaN;   ///< commercial: hedgers
    double nr_long = kNaN, nr_short = kNaN; ///< non-reportable: small traders

    double nc_net() const { return nc_long - nc_short; }
    double c_net() const { return c_long - c_short; }
    double nr_net() const { return nr_long - nr_short; }
};

struct CotReport {
    QString code;
    QString market;
    QVector<CotRow> rows; ///< oldest first
};

inline CotReport parse_cot(const QJsonObject& o) {
    CotReport rep;
    rep.code = o.value(QLatin1String("code")).toString();
    rep.market = o.value(QLatin1String("market")).toString();
    for (const auto& v : o.value(QLatin1String("rows")).toArray()) {
        const QJsonObject r = v.toObject();
        CotRow row;
        row.date = QDate::fromString(r.value(QLatin1String("date")).toString(), Qt::ISODate);
        if (!row.date.isValid())
            continue;
        row.oi = json_num(r, "oi");
        row.nc_long = json_num(r, "nc_long");
        row.nc_short = json_num(r, "nc_short");
        row.c_long = json_num(r, "c_long");
        row.c_short = json_num(r, "c_short");
        row.nr_long = json_num(r, "nr_long");
        row.nr_short = json_num(r, "nr_short");
        rep.rows.append(row);
    }
    return rep;
}

struct CotSummary {
    QDate date;
    int weeks = 0; ///< reports the range below is taken over
    double nc_net = kNaN, nc_net_change = kNaN;
    double c_net = kNaN, c_net_change = kNaN;
    double nr_net = kNaN;
    double oi = kNaN, oi_change = kNaN;
    double nc_net_pct_oi = kNaN;
    /// Where the latest speculator net sits in its own range over `weeks`:
    /// 0 = the most net short of the window, 100 = the most net long.
    double nc_range_pct = kNaN;
};

inline CotSummary summarize_cot(const CotReport& rep) {
    CotSummary s;
    if (rep.rows.isEmpty())
        return s;
    const CotRow& last = rep.rows.last();
    s.date = last.date;
    s.nc_net = last.nc_net();
    s.c_net = last.c_net();
    s.nr_net = last.nr_net();
    s.oi = last.oi;
    if (std::isfinite(s.nc_net) && s.oi > 0)
        s.nc_net_pct_oi = s.nc_net / s.oi * 100.0;
    if (rep.rows.size() >= 2) {
        const CotRow& prev = rep.rows[rep.rows.size() - 2];
        s.nc_net_change = s.nc_net - prev.nc_net();
        s.c_net_change = s.c_net - prev.c_net();
        s.oi_change = s.oi - prev.oi;
    }
    double lo = kNaN, hi = kNaN;
    for (const auto& r : rep.rows) {
        const double n = r.nc_net();
        if (!std::isfinite(n))
            continue;
        ++s.weeks;
        lo = std::isfinite(lo) ? std::min(lo, n) : n;
        hi = std::isfinite(hi) ? std::max(hi, n) : n;
    }
    // A range needs history: a percentile of three reports says nothing.
    if (s.weeks >= 26 && std::isfinite(s.nc_net) && hi > lo)
        s.nc_range_pct = (s.nc_net - lo) / (hi - lo) * 100.0;
    return s;
}

// ── Trade ticket ────────────────────────────────────────────────────────────

struct Ticket {
    bool is_long = true;
    double contracts = 0;
    double entry = kNaN;
    double stop = kNaN;   ///< NaN = none
    double target = kNaN; ///< NaN = none
};

struct TicketResult {
    double notional = kNaN;   ///< dollars controlled
    double tick_value = kNaN; ///< dollars per tick, whole position
    double risk_ticks = kNaN, risk_usd = kNaN;
    double reward_ticks = kNaN, reward_usd = kNaN;
    double reward_to_risk = kNaN;
    QString problem; ///< why the ticket cannot be placed; empty when it can
};

inline TicketResult evaluate(const Ticket& t, const ContractSpec& spec) {
    TicketResult r;
    if (!(spec.tick > 0) || !(spec.point_value > 0)) {
        r.problem = QStringLiteral("no contract terms for this root");
        return r;
    }
    if (!(t.contracts > 0) || std::floor(t.contracts) != t.contracts) {
        r.problem = QStringLiteral("enter a whole number of contracts");
        return r;
    }
    if (!(t.entry > 0)) {
        r.problem = QStringLiteral("no price to trade at yet");
        return r;
    }
    r.notional = t.entry * spec.point_value * t.contracts;
    r.tick_value = spec.tick * spec.point_value * t.contracts;
    const double dir = t.is_long ? 1.0 : -1.0;
    if (std::isfinite(t.stop)) {
        const double dist = (t.entry - t.stop) * dir; // positive when the stop is on the losing side
        if (!(dist > 0)) {
            r.problem = t.is_long ? QStringLiteral("a long's stop belongs below the entry")
                                  : QStringLiteral("a short's stop belongs above the entry");
            return r;
        }
        r.risk_ticks = dist / spec.tick;
        r.risk_usd = dist * spec.point_value * t.contracts;
    }
    if (std::isfinite(t.target)) {
        const double dist = (t.target - t.entry) * dir;
        if (!(dist > 0)) {
            r.problem = t.is_long ? QStringLiteral("a long's target belongs above the entry")
                                  : QStringLiteral("a short's target belongs below the entry");
            return r;
        }
        r.reward_ticks = dist / spec.tick;
        r.reward_usd = dist * spec.point_value * t.contracts;
    }
    if (r.risk_usd > 0 && std::isfinite(r.reward_usd))
        r.reward_to_risk = r.reward_usd / r.risk_usd;
    return r;
}

/// Open profit of a position in dollars. `quantity_units` is contracts times
/// the point value — the form the paper engine stores a futures position in.
inline double contracts_from_units(double quantity_units, const ContractSpec& spec) {
    return spec.point_value > 0 ? quantity_units / spec.point_value : kNaN;
}

} // namespace finterm::services::futures
