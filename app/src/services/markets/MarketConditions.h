#pragma once
// MarketConditions.h — the arithmetic behind the market-condition readings
// (Markets ▸ OVERVIEW, the dashboard's condition widgets and the chart
// dialog's condition strip). Pure: Qt Core only, unit-tested in
// tests/services/test_market_conditions.cpp.
//
// One function per reading, so a symbol's numbers are the same wherever they
// are drawn. Every reading is computed from DAILY bars (bars::parse output,
// oldest first); a reading the history cannot support is NaN — shown as "—",
// never a guessed or zero value.
//
//   SMA 20/50/200     simple means of the last N closes
//   vs 200D           last close against its 200-day mean, percent
//   1Y / 5Y PCTL      percent rank of the last close among the window's
//                     closes (ties count half). NaN unless the history
//                     actually spans the window (5Y needs ≥ 4.75 years).
//   RSI 14            Wilder's relative strength index
//   ADX 14, ±DI       Wilder's directional movement — trend STRENGTH
//   regime            ADX ≥ 25 → TREND (↑ if +DI > −DI, ↓ otherwise);
//                     ADX < 20 → RANGE (mean-reverting conditions);
//                     between → MIXED
//   1M                return over the last 21 bars, percent
//
// Correlation: Pearson over the last N daily changes on the dates BOTH
// series have (a coin trades weekends, an index does not). Prices use log
// returns; a YIELD series uses first differences (a yield is already a rate).
//
// FRED keys ("FRED:<A>[/<B>][*k][@yoy]") and their transforms live here too.

#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finterm::services::conditions {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct DatedValue {
    QDate date;
    double value = kNaN;
};

enum class Regime { Unknown, TrendUp, TrendDown, Range, Mixed };

inline QString regime_label(Regime r) {
    switch (r) {
        case Regime::TrendUp:
            return QStringLiteral("TREND ↑");
        case Regime::TrendDown:
            return QStringLiteral("TREND ↓");
        case Regime::Range:
            return QStringLiteral("RANGE");
        case Regime::Mixed:
            return QStringLiteral("MIXED");
        case Regime::Unknown:
            break;
    }
    return QString(QChar(0x2014));
}

inline QString regime_key(Regime r) {
    switch (r) {
        case Regime::TrendUp:
            return QStringLiteral("up");
        case Regime::TrendDown:
            return QStringLiteral("down");
        case Regime::Range:
            return QStringLiteral("range");
        case Regime::Mixed:
            return QStringLiteral("mixed");
        case Regime::Unknown:
            break;
    }
    return {};
}

inline Regime regime_from_key(const QString& k) {
    if (k == QLatin1String("up"))
        return Regime::TrendUp;
    if (k == QLatin1String("down"))
        return Regime::TrendDown;
    if (k == QLatin1String("range"))
        return Regime::Range;
    if (k == QLatin1String("mixed"))
        return Regime::Mixed;
    return Regime::Unknown;
}

/// One bar as the readings need it (daily; `date` is the session).
struct Bar {
    QDate date;
    double high = kNaN;
    double low = kNaN;
    double close = kNaN;
};

struct Stats {
    QDate as_of; ///< the last bar's session
    double last = kNaN;
    double sma20 = kNaN, sma50 = kNaN, sma200 = kNaN;
    double vs_sma200_pct = kNaN;
    double pctile_1y = kNaN, pctile_5y = kNaN;
    double rsi14 = kNaN;
    double adx14 = kNaN, plus_di = kNaN, minus_di = kNaN;
    double ret_1m_pct = kNaN;
    Regime regime = Regime::Unknown;
    /// The last kKeepCloses closes, oldest first — what correlation and the
    /// live reading (live_reading) read.
    QVector<DatedValue> closes;

    bool valid() const { return as_of.isValid() && std::isfinite(last); }
};

inline constexpr int kKeepCloses = 400; // > 1 calendar year even for 24/7 coins
inline constexpr double kTrendAdx = 25.0;
inline constexpr double kRangeAdx = 20.0;

/// Mean of the last `n` closes; NaN with fewer than n bars.
inline double sma(const QVector<Bar>& b, int n) {
    if (n <= 0 || b.size() < n)
        return kNaN;
    double s = 0;
    for (int i = b.size() - n; i < b.size(); ++i)
        s += b[i].close;
    return s / n;
}

/// Percent rank (0–100) of `v` among `xs`; ties count half. NaN when empty.
inline double percent_rank(const QVector<double>& xs, double v) {
    if (xs.isEmpty() || !std::isfinite(v))
        return kNaN;
    int below = 0, equal = 0;
    for (double x : xs) {
        if (x < v)
            ++below;
        else if (x == v)
            ++equal;
    }
    return (below + 0.5 * equal) / xs.size() * 100.0;
}

/// Percent rank of the last close among the closes of the trailing `years`.
/// NaN unless the history reaches back to (as_of − years), within `slack_days`.
inline double window_percentile(const QVector<Bar>& b, double years, int slack_days) {
    if (b.isEmpty())
        return kNaN;
    const QDate end = b.last().date;
    const QDate start = end.addDays(-static_cast<qint64>(std::llround(years * 365.25)));
    if (b.first().date > start.addDays(slack_days))
        return kNaN; // history does not span the window
    QVector<double> xs;
    for (const auto& x : b)
        if (x.date > start)
            xs.append(x.close);
    return percent_rank(xs, b.last().close);
}

/// Wilder's RSI over `n`; NaN with fewer than n+1 bars.
inline double rsi(const QVector<Bar>& b, int n = 14) {
    if (b.size() < n + 1)
        return kNaN;
    double gain = 0, loss = 0;
    for (int i = 1; i <= n; ++i) {
        const double d = b[i].close - b[i - 1].close;
        (d > 0 ? gain : loss) += std::abs(d);
    }
    gain /= n;
    loss /= n;
    for (int i = n + 1; i < b.size(); ++i) {
        const double d = b[i].close - b[i - 1].close;
        gain = (gain * (n - 1) + (d > 0 ? d : 0)) / n;
        loss = (loss * (n - 1) + (d < 0 ? -d : 0)) / n;
    }
    if (loss == 0)
        return gain == 0 ? 50.0 : 100.0;
    return 100.0 - 100.0 / (1.0 + gain / loss);
}

struct Adx {
    double adx = kNaN, plus_di = kNaN, minus_di = kNaN;
};

/// Wilder's ADX / ±DI over `n`; needs 2n+1 bars. Bars without a usable
/// high/low (a level series like a yield index often has them) still work —
/// the high and low are then the close.
inline Adx adx(const QVector<Bar>& b, int n = 14) {
    Adx out;
    if (b.size() < 2 * n + 1)
        return out;
    auto hi = [&](int i) { return std::isfinite(b[i].high) ? std::max(b[i].high, b[i].close) : b[i].close; };
    auto lo = [&](int i) { return std::isfinite(b[i].low) ? std::min(b[i].low, b[i].close) : b[i].close; };
    double tr_s = 0, pdm_s = 0, mdm_s = 0;
    double dx_sum = 0, adx_v = kNaN;
    int dx_count = 0;
    for (int i = 1; i < b.size(); ++i) {
        const double up = hi(i) - hi(i - 1);
        const double down = lo(i - 1) - lo(i);
        const double pdm = (up > down && up > 0) ? up : 0;
        const double mdm = (down > up && down > 0) ? down : 0;
        const double tr = std::max({hi(i) - lo(i), std::abs(hi(i) - b[i - 1].close), std::abs(lo(i) - b[i - 1].close)});
        if (i <= n) {
            tr_s += tr;
            pdm_s += pdm;
            mdm_s += mdm;
            if (i < n)
                continue;
        } else {
            tr_s = tr_s - tr_s / n + tr;
            pdm_s = pdm_s - pdm_s / n + pdm;
            mdm_s = mdm_s - mdm_s / n + mdm;
        }
        if (tr_s <= 0)
            continue;
        const double pdi = 100.0 * pdm_s / tr_s;
        const double mdi = 100.0 * mdm_s / tr_s;
        const double dx = (pdi + mdi) > 0 ? 100.0 * std::abs(pdi - mdi) / (pdi + mdi) : 0.0;
        out.plus_di = pdi;
        out.minus_di = mdi;
        if (!std::isfinite(adx_v)) {
            dx_sum += dx;
            if (++dx_count == n)
                adx_v = dx_sum / n;
        } else {
            adx_v = (adx_v * (n - 1) + dx) / n;
        }
    }
    out.adx = adx_v;
    if (!std::isfinite(adx_v)) {
        out.plus_di = out.minus_di = kNaN;
    }
    return out;
}

/// A break in a daily series: the first close-to-close move of more than
/// `factor`× either way (positive closes only). A vendor splicing an unrelated
/// series onto a symbol (Yahoo's QO=F history is not gold) shows up as one;
/// readings over such a history would be fiction. Invalid date = no break.
struct Break {
    QDate date;
    double ratio = kNaN;
    bool found() const { return date.isValid(); }
};

inline Break find_break(const QVector<Bar>& b, double factor = 3.0) {
    Break out;
    for (int i = 1; i < b.size(); ++i) {
        const double a = b[i - 1].close, c = b[i].close;
        if (!(a > 0) || !(c > 0))
            continue;
        const double r = c / a;
        if (r > factor || r < 1.0 / factor)
            out = {b[i].date, r}; // the newest break is the one that matters
    }
    return out;
}

inline Regime classify(const Adx& a) {
    if (!std::isfinite(a.adx))
        return Regime::Unknown;
    if (a.adx >= kTrendAdx)
        return a.plus_di >= a.minus_di ? Regime::TrendUp : Regime::TrendDown;
    if (a.adx < kRangeAdx)
        return Regime::Range;
    return Regime::Mixed;
}

/// Every reading from daily bars (oldest first). Bars without a finite close
/// are skipped. Empty input → an invalid Stats.
inline Stats compute(const QVector<Bar>& raw) {
    QVector<Bar> b;
    b.reserve(raw.size());
    for (const auto& x : raw)
        if (x.date.isValid() && std::isfinite(x.close) && (b.isEmpty() || x.date > b.last().date))
            b.append(x);
    Stats s;
    if (b.isEmpty())
        return s;
    s.as_of = b.last().date;
    s.last = b.last().close;
    s.sma20 = sma(b, 20);
    s.sma50 = sma(b, 50);
    s.sma200 = sma(b, 200);
    if (std::isfinite(s.sma200) && s.sma200 != 0)
        s.vs_sma200_pct = (s.last / s.sma200 - 1.0) * 100.0;
    s.pctile_1y = window_percentile(b, 1.0, 10);
    s.pctile_5y = window_percentile(b, 5.0, 92); // ≥ 4.75 years of history
    s.rsi14 = rsi(b, 14);
    const Adx a = adx(b, 14);
    s.adx14 = a.adx;
    s.plus_di = a.plus_di;
    s.minus_di = a.minus_di;
    s.regime = classify(a);
    if (b.size() > 21 && b[b.size() - 22].close != 0)
        s.ret_1m_pct = (s.last / b[b.size() - 22].close - 1.0) * 100.0;
    for (int i = std::max(0, static_cast<int>(b.size()) - kKeepCloses); i < b.size(); ++i)
        s.closes.append({b[i].date, b[i].close});
    return s;
}

// ── Live reading ────────────────────────────────────────────────────────────
//
// vs 200D and the 1-year percentile at the CURRENT price, the way a desk reads
// them: the live quote against the completed sessions' closes. The daily bars
// often lack today's (Yahoo's in-progress bar is empty), and on a big day a
// reading frozen at yesterday's close contradicts the price beside it.
//
//   last bar dated today or later (the bars include the live session — Asia,
//   crypto's UTC day) → that bar's close is replaced by the live price;
//   otherwise, if the live price differs from the last close → it is today's
//   session, appended; equal (a weekend, a holiday) → nothing to add.
// RSI / ADX / regime stay on completed bars (they are bar-shaped readings).

struct LiveReading {
    double last = kNaN;
    double vs_sma200_pct = kNaN;
    double pctile_1y = kNaN;
    bool live = false; ///< the live price is in it
};

inline LiveReading live_reading(QVector<DatedValue> closes, double live_price, const QDate& today) {
    LiveReading r;
    if (closes.isEmpty())
        return r;
    if (std::isfinite(live_price) && live_price > 0 && today.isValid()) {
        if (closes.last().date >= today) {
            closes.last().value = live_price;
            r.live = true;
        } else if (std::abs(closes.last().value - live_price) >
                   1e-6 * std::max(std::abs(live_price), std::abs(closes.last().value))) {
            // Equal within float noise (a weekend: the quote IS the last
            // close; history closes carry float32 noise) → nothing to add.
            closes.append({today, live_price});
            r.live = true;
        }
    }
    const double last = closes.last().value;
    r.last = last;
    if (closes.size() >= 200) {
        double sum = 0;
        for (int i = closes.size() - 200; i < closes.size(); ++i)
            sum += closes[i].value;
        const double m = sum / 200;
        if (m != 0)
            r.vs_sma200_pct = (last / m - 1.0) * 100.0;
    }
    const QDate end = closes.last().date;
    const QDate start = end.addDays(-365);
    if (closes.first().date <= start.addDays(10)) {
        QVector<double> w;
        for (const auto& c : closes)
            if (c.date > start)
                w.append(c.value);
        r.pctile_1y = percent_rank(w, last);
    }
    return r;
}

// ── Correlation ─────────────────────────────────────────────────────────────

/// Yields move in rate points, not percent: their daily change is a difference.
inline bool is_yield_symbol(const QString& sym) {
    static const QStringList k = {"^IRX", "^FVX", "^TNX", "^TYX"};
    return k.contains(sym);
}

/// Daily changes of `xs` keyed by the date of the later point. Log return for
/// prices (non-positive points dropped), difference for yields.
inline QHash<QDate, double> daily_changes(const QVector<DatedValue>& xs, bool difference) {
    QHash<QDate, double> out;
    for (int i = 1; i < xs.size(); ++i) {
        const double a = xs[i - 1].value, b = xs[i].value;
        if (!std::isfinite(a) || !std::isfinite(b))
            continue;
        if (difference)
            out.insert(xs[i].date, b - a);
        else if (a > 0 && b > 0)
            out.insert(xs[i].date, std::log(b / a));
    }
    return out;
}

inline double pearson(const QVector<double>& x, const QVector<double>& y) {
    const int n = std::min(x.size(), y.size());
    if (n < 3)
        return kNaN;
    double mx = 0, my = 0;
    for (int i = 0; i < n; ++i) {
        mx += x[i];
        my += y[i];
    }
    mx /= n;
    my /= n;
    double sxy = 0, sxx = 0, syy = 0;
    for (int i = 0; i < n; ++i) {
        sxy += (x[i] - mx) * (y[i] - my);
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
    }
    if (sxx <= 0 || syy <= 0)
        return kNaN;
    return sxy / std::sqrt(sxx * syy);
}

/// The paired changes over the dates BOTH series have, oldest first: the
/// levels are matched by date first, then each change runs from one common
/// date to the next. (Changes taken on each series' own calendar would pair a
/// coin's Sunday→Monday move with an index's Friday→Monday one.)
inline void aligned_changes(const QVector<DatedValue>& a, bool a_diff, const QVector<DatedValue>& b, bool b_diff,
                            QVector<QDate>& dates, QVector<double>& xa, QVector<double>& xb) {
    QHash<QDate, double> bv;
    for (const auto& x : b)
        if (std::isfinite(x.value))
            bv.insert(x.date, x.value);
    QVector<DatedValue> ca, cb; // the common dates' levels, in a's order
    for (const auto& x : a)
        if (std::isfinite(x.value) && bv.contains(x.date)) {
            ca.append(x);
            cb.append({x.date, bv.value(x.date)});
        }
    dates.clear();
    xa.clear();
    xb.clear();
    for (int i = 1; i < ca.size(); ++i) {
        const double a0 = ca[i - 1].value, a1 = ca[i].value, b0 = cb[i - 1].value, b1 = cb[i].value;
        double da, db;
        if (a_diff)
            da = a1 - a0;
        else if (a0 > 0 && a1 > 0)
            da = std::log(a1 / a0);
        else
            continue;
        if (b_diff)
            db = b1 - b0;
        else if (b0 > 0 && b1 > 0)
            db = std::log(b1 / b0);
        else
            continue;
        dates.append(ca[i].date);
        xa.append(da);
        xb.append(db);
    }
}

/// Correlation of the last `window` paired daily changes. NaN with fewer than
/// 80% of the window in common (two series that barely overlap say nothing).
inline double correlation(const QVector<DatedValue>& a, bool a_diff, const QVector<DatedValue>& b, bool b_diff,
                          int window) {
    QVector<QDate> d;
    QVector<double> xa, xb;
    aligned_changes(a, a_diff, b, b_diff, d, xa, xb);
    if (window < 3 || xa.size() < static_cast<int>(std::ceil(window * 0.8)))
        return kNaN;
    const int from = std::max(0, static_cast<int>(xa.size()) - window);
    return pearson(xa.mid(from), xb.mid(from));
}

/// Rolling `window` correlation, one point per paired date from the window's end.
inline QVector<DatedValue> rolling_correlation(const QVector<DatedValue>& a, bool a_diff,
                                               const QVector<DatedValue>& b, bool b_diff, int window) {
    QVector<DatedValue> out;
    QVector<QDate> d;
    QVector<double> xa, xb;
    aligned_changes(a, a_diff, b, b_diff, d, xa, xb);
    for (int end = window; end <= xa.size(); ++end) {
        const double r = pearson(xa.mid(end - window, window), xb.mid(end - window, window));
        if (std::isfinite(r))
            out.append({d[end - 1], r});
    }
    return out;
}

// ── FRED keys ───────────────────────────────────────────────────────────────
//
// "FRED:DFF"                    the series as published
// "FRED:CPIAUCSL@yoy"           percent change from the same date a year before
// "FRED:NCBEILQ027S/GDP*0.1"    ratio of two series on the dates both have,
//                               times k (here $M / $B × 100 → percent)

struct FredKey {
    QString a, b;     ///< series ids; b empty = no ratio
    double scale = 1; ///< multiplier applied to a (or a/b)
    bool yoy = false;
    bool valid() const { return !a.isEmpty(); }
    QStringList series() const { return b.isEmpty() ? QStringList{a} : QStringList{a, b}; }
};

inline bool is_fred_key(const QString& s) {
    return s.startsWith(QLatin1String("FRED:"), Qt::CaseInsensitive);
}

inline bool valid_fred_id(const QString& id) {
    if (id.size() < 2 || id.size() > 30)
        return false;
    for (const QChar c : id)
        if (!(c.isUpper() || c.isDigit() || c == QLatin1Char('_')))
            return false;
    return true;
}

inline FredKey parse_fred_key(const QString& key) {
    FredKey k;
    if (!is_fred_key(key))
        return k;
    QString body = key.mid(5).trimmed().toUpper();
    if (body.endsWith(QLatin1String("@YOY"))) {
        k.yoy = true;
        body.chop(4);
    }
    const int star = body.indexOf(QLatin1Char('*'));
    if (star >= 0) {
        bool ok = false;
        const double s = body.mid(star + 1).toDouble(&ok);
        if (!ok || !std::isfinite(s) || s == 0)
            return {};
        k.scale = s;
        body = body.left(star);
    }
    const int slash = body.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        k.a = body.left(slash);
        k.b = body.mid(slash + 1);
        if (!valid_fred_id(k.b))
            return {};
    } else {
        k.a = body;
    }
    if (!valid_fred_id(k.a))
        return {};
    return k;
}

/// The value series a key describes, from its source series (oldest first).
inline QVector<DatedValue> fred_values(const FredKey& k, const QVector<DatedValue>& a, const QVector<DatedValue>& b) {
    QVector<DatedValue> base;
    if (k.b.isEmpty()) {
        for (const auto& x : a)
            if (std::isfinite(x.value))
                base.append({x.date, x.value * k.scale});
    } else {
        QHash<QDate, double> bv;
        for (const auto& x : b)
            if (std::isfinite(x.value) && x.value != 0)
                bv.insert(x.date, x.value);
        for (const auto& x : a)
            if (std::isfinite(x.value) && bv.contains(x.date))
                base.append({x.date, x.value / bv.value(x.date) * k.scale});
    }
    if (!k.yoy)
        return base;
    QHash<QDate, double> by_date;
    for (const auto& x : base)
        by_date.insert(x.date, x.value);
    QVector<DatedValue> out;
    for (const auto& x : base) {
        const QDate prior = x.date.addYears(-1);
        const auto it = by_date.constFind(prior);
        if (it != by_date.constEnd() && it.value() != 0)
            out.append({x.date, (x.value / it.value() - 1.0) * 100.0});
    }
    return out;
}

/// The latest reading of a level series and how it compares.
struct LevelStats {
    QDate as_of, prev_date;
    double last = kNaN, prev = kNaN;
    double pctile_10y = kNaN; ///< NaN unless the history spans ~10 years
    double change() const { return std::isfinite(last) && std::isfinite(prev) ? last - prev : kNaN; }
    bool valid() const { return as_of.isValid() && std::isfinite(last); }
};

inline LevelStats level_stats(const QVector<DatedValue>& xs) {
    LevelStats s;
    if (xs.isEmpty())
        return s;
    s.as_of = xs.last().date;
    s.last = xs.last().value;
    if (xs.size() > 1) {
        s.prev_date = xs[xs.size() - 2].date;
        s.prev = xs[xs.size() - 2].value;
    }
    const QDate start = s.as_of.addYears(-10);
    if (xs.first().date <= start.addDays(120)) {
        QVector<double> w;
        for (const auto& x : xs)
            if (x.date > start)
                w.append(x.value);
        s.pctile_10y = percent_rank(w, s.last);
    }
    return s;
}

} // namespace finterm::services::conditions
