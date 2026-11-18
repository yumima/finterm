#pragma once
// WorldMarketsModel.h — what the Markets screen's WORLD mode shows and the
// small arithmetic behind it: the instrument catalogue grouped by section,
// yield / basis-point / spread maths, the heat value of a row, and whether a
// row is live, stale or missing. Pure (Qt Core only) — unit-tested in
// tests/screens/test_world_markets.cpp.
//
// Sources (all free, no key): Yahoo Finance through the app's yfinance daemon
// for every symbol below; the US Treasury's daily par yield curve
// (home.treasury.gov, end of day) for the 2-year and the 2s10s spread, which
// Yahoo has no cash series for. No India sources (user policy).

#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <limits>

namespace finterm::screens::world {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

enum class Kind {
    Index,     ///< cash equity index
    Future,    ///< equity-index future (continuous front month)
    Yield,     ///< a yield in percent; changes are shown in basis points
    Fx,        ///< currency pair or the dollar index
    Commodity, ///< commodity future
    Crypto,
    Vol,       ///< volatility index
    Treasury,  ///< from the Treasury par curve, not a Yahoo symbol (no chart)
};

struct Instrument {
    QString symbol;   ///< the Yahoo symbol, or a "UST:" key for Treasury rows
    QString label;    ///< what the row says
    Kind kind = Kind::Index;
    QString exchange; ///< MarketSessions id for the session column; empty = none shown
    int decimals = 2;
};

struct Section {
    QString title;
    QVector<Instrument> rows;
    bool show_session = false; ///< SESSION column
    bool show_fut = false;     ///< FUT% column (cash indices with a future)
    QString note;              ///< header tooltip: sources, limits
};

/// Treasury par-curve rows (not Yahoo symbols).
inline const QString kUst2y = QStringLiteral("UST:2Y");
inline const QString kUst2s10s = QStringLiteral("UST:2s10s");

inline bool is_treasury_key(const QString& s) {
    return s.startsWith(QLatin1String("UST:"));
}

/// The board, in reading order. Columns are laid out by the view.
inline const QVector<Section>& world_sections() {
    static const QVector<Section> v = {
        {QStringLiteral("AMERICAS"),
         {
             {"^GSPC", "S&P 500", Kind::Index, "NYC"},
             {"^NDX", "NASDAQ 100", Kind::Index, "NYC"},
             {"^DJI", "DOW JONES", Kind::Index, "NYC"},
             {"^RUT", "RUSSELL 2000", Kind::Index, "NYC"},
             {"^GSPTSE", "S&P/TSX", Kind::Index, "TOR"},
             {"^BVSP", "BOVESPA", Kind::Index, "SAO"},
         },
         true, true,
         QStringLiteral("FUT% is each index's future (pinned contract vs its own previous close), shown while "
                        "the cash market is not in its regular session — a cash index has no extended-hours "
                        "prints.")},
        {QStringLiteral("EUROPE"),
         {
             {"^STOXX50E", "EURO STOXX 50", Kind::Index, "FRA"},
             {"^GDAXI", "DAX", Kind::Index, "FRA"},
             {"^FTSE", "FTSE 100", Kind::Index, "LON"},
             {"^FCHI", "CAC 40", Kind::Index, "PAR"},
             {"^IBEX", "IBEX 35", Kind::Index, "MAD"},
             {"FTSEMIB.MI", "FTSE MIB", Kind::Index, "MIL"},
             {"^SSMI", "SMI", Kind::Index, "ZRH"},
             {"^AEX", "AEX", Kind::Index, "AMS"},
         },
         true, false,
         QStringLiteral("The feed carries no Eurex / ICE index futures, so European indices have no FUT%.")},
        {QStringLiteral("ASIA-PACIFIC"),
         {
             {"^N225", "NIKKEI 225", Kind::Index, "TYO"},
             {"^HSI", "HANG SENG", Kind::Index, "HKG"},
             {"000001.SS", "SHANGHAI COMP", Kind::Index, "SHA"},
             {"000300.SS", "CSI 300", Kind::Index, "SHA"},
             {"^KS11", "KOSPI", Kind::Index, "SEO"},
             {"^AXJO", "ASX 200", Kind::Index, "SYD"},
             {"^TWII", "TAIEX", Kind::Index, "TPE"},
         },
         true, true,
         QStringLiteral("FUT% only for the Nikkei (CME NIY, in yen); the feed has no HKEX / SGX / CFFEX "
                        "index futures.")},
        {QStringLiteral("US FUTURES"),
         {
             {"ES=F", "E-MINI S&P 500", Kind::Future, "CME"},
             {"NQ=F", "E-MINI NASDAQ 100", Kind::Future, "CME"},
             {"YM=F", "E-MINI DOW", Kind::Future, "CME"},
             {"RTY=F", "E-MINI RUSSELL", Kind::Future, "CME"},
         },
         true, false, QStringLiteral("Continuous front-month contracts; change vs the previous settle.")},
        {QStringLiteral("US RATES"),
         {
             {kUst2y, "US 2Y", Kind::Treasury, QString(), 3},
             {"^FVX", "US 5Y", Kind::Yield, QString(), 3},
             {"^TNX", "US 10Y", Kind::Yield, QString(), 3},
             {"^TYX", "US 30Y", Kind::Yield, QString(), 3},
             {kUst2s10s, "2s10s", Kind::Treasury, QString(), 1},
         },
         false, false,
         QStringLiteral("5Y/10Y/30Y: Cboe yield indices via Yahoo, live. 2Y and 2s10s: US Treasury daily par "
                        "yield curve, end of day (Yahoo has no cash 2-year). Bund, Gilt and JGB 10-year "
                        "yields have no free live source on the feed, so they are not shown.")},
        {QStringLiteral("FX"),
         {
             {"DX-Y.NYB", "DOLLAR INDEX", Kind::Fx, "FX", 3},
             {"EURUSD=X", "EUR/USD", Kind::Fx, "FX", 4},
             {"USDJPY=X", "USD/JPY", Kind::Fx, "FX", 2},
             {"GBPUSD=X", "GBP/USD", Kind::Fx, "FX", 4},
             {"CNH=X", "USD/CNH", Kind::Fx, "FX", 4},
             {"AUDUSD=X", "AUD/USD", Kind::Fx, "FX", 4},
         },
         false, false, QString()},
        {QStringLiteral("COMMODITIES"),
         {
             {"CL=F", "WTI CRUDE", Kind::Commodity, "CME"},
             {"BZ=F", "BRENT", Kind::Commodity, "CME"},
             {"GC=F", "GOLD", Kind::Commodity, "CME", 1},
             {"SI=F", "SILVER", Kind::Commodity, "CME", 3},
             {"HG=F", "COPPER", Kind::Commodity, "CME", 4},
             {"NG=F", "NATURAL GAS", Kind::Commodity, "CME", 3},
         },
         false, false, QStringLiteral("NYMEX / COMEX front-month futures; change vs the previous settle.")},
        {QStringLiteral("CRYPTO"),
         {
             {"BTC-USD", "BITCOIN", Kind::Crypto, "24/7"},
             {"ETH-USD", "ETHER", Kind::Crypto, "24/7"},
         },
         false, false, QStringLiteral("Change since the feed's 00:00 UTC daily close (a 24/7 market has no session close).")},
        {QStringLiteral("VOLATILITY"),
         {
             {"^VIX", "VIX", Kind::Vol, QString()},
             {"^VIX9D", "VIX 9-DAY", Kind::Vol, QString()},
             {"^VIX3M", "VIX 3-MONTH", Kind::Vol, QString()},
         },
         false, false, QString()},
    };
    return v;
}

/// Every Yahoo symbol on the board (Treasury keys excluded), in board order.
inline QStringList world_quote_symbols() {
    QStringList out;
    for (const auto& s : world_sections())
        for (const auto& r : s.rows)
            if (!is_treasury_key(r.symbol) && !out.contains(r.symbol))
                out.append(r.symbol);
    return out;
}

// ── Yields and basis points ─────────────────────────────────────────────────

/// A Cboe yield index (^TNX & co.) as a percent yield. Cboe defines these as
/// 10 x the yield (^TNX 41.23 = 4.123%); Yahoo serves them already divided
/// (5.154 = 5.154%). No real US yield is above 25%, so a larger number is the
/// x10 convention. NaN / non-positive stays NaN.
inline double yield_percent(double quoted) {
    if (!std::isfinite(quoted) || !(quoted > 0))
        return kNaN;
    return quoted > 25.0 ? quoted / 10.0 : quoted;
}

/// A yield change in percentage points → basis points, on the same scale
/// yield_percent() used for the level (`quoted` is the level as quoted).
inline double change_bp(double quoted, double change_points) {
    if (!std::isfinite(change_points) || !std::isfinite(yield_percent(quoted)))
        return kNaN;
    const double scale = quoted > 25.0 ? 10.0 : 100.0; // x10 index: 1.0 point = 10bp
    return change_points * scale;
}

/// Spread long − short in basis points; NaN if either leg is absent.
inline double spread_bp(double long_pct, double short_pct) {
    if (!std::isfinite(long_pct) || !std::isfinite(short_pct))
        return kNaN;
    return (long_pct - short_pct) * 100.0;
}

// ── Formatting (unicode minus, explicit sign: colour never carries meaning alone)

inline QString minus() {
    return QString(QChar(0x2212));
}

/// "+0.53%", "−1.20%", "0.00%" when it rounds to zero; "—" when unknown.
inline QString signed_pct(double v, int dp = 2) {
    if (!std::isfinite(v))
        return QString(QChar(0x2014));
    const QString body = QString::number(std::abs(v), 'f', dp);
    if (body.toDouble() == 0.0)
        return body + QLatin1Char('%');
    return (v > 0 ? QStringLiteral("+") : minus()) + body + QLatin1Char('%');
}

/// "+3.1bp", "−12bp" (whole bp from 10 up), "0.0bp"; "—" when unknown.
inline QString signed_bp(double bp) {
    if (!std::isfinite(bp))
        return QString(QChar(0x2014));
    const int dp = std::abs(bp) >= 9.95 ? 0 : 1;
    const QString body = QString::number(std::abs(bp), 'f', dp);
    if (body.toDouble() == 0.0)
        return body + QStringLiteral("bp");
    return (bp > 0 ? QStringLiteral("+") : minus()) + body + QStringLiteral("bp");
}

/// A signed number at `dp` places ("+12.40", "−0.0031"); "—" when unknown.
inline QString signed_num(double v, int dp) {
    if (!std::isfinite(v))
        return QString(QChar(0x2014));
    const QString body = QString::number(std::abs(v), 'f', dp);
    if (body.toDouble() == 0.0)
        return body;
    return (v > 0 ? QStringLiteral("+") : minus()) + body;
}

/// Grouped price ("43,675.12"); "—" when absent (NaN or not positive).
inline QString price_text(double v, int dp) {
    if (!std::isfinite(v) || !(v > 0))
        return QString(QChar(0x2014));
    QString s = QString::number(v, 'f', dp);
    const int dot = s.indexOf(QLatin1Char('.'));
    int i = (dot < 0 ? s.size() : dot) - 3;
    while (i > 0) {
        s.insert(i, QLatin1Char(','));
        i -= 3;
    }
    return s;
}

/// The number a row's heat cell is tinted by, on the %-move scale the
/// heatmap palette expects: a percent change, or for a yield its bp change / 10
/// (10bp tints like a 1% move). NaN = untinted.
inline double heat_value(Kind kind, double pct, double bp) {
    if (kind == Kind::Yield || kind == Kind::Treasury)
        return std::isfinite(bp) ? bp / 10.0 : kNaN;
    return pct;
}

// ── Row status ─────────────────────────────────────────────────────────────

enum class RowStatus {
    Missing, ///< no usable quote — every cell is "—"
    Stale,   ///< a quote, but not a fresh one (an earlier session's, or its refresh failed)
    Live,
};

/// `has_quote`: any quote delivered; `price`: its last; `fresh`: polled or
/// ticked in this process; `failed`: the last refresh for it errored.
inline RowStatus row_status(bool has_quote, double price, bool fresh, bool failed) {
    if (!has_quote || !std::isfinite(price) || !(price > 0))
        return RowStatus::Missing;
    if (!fresh || failed)
        return RowStatus::Stale;
    return RowStatus::Live;
}

// ── VIX term structure ──────────────────────────────────────────────────────

struct TermStructure {
    double ratio = kNaN; ///< VIX / VIX3M
    QString label;       ///< "contango" / "inverted" / "flat"; empty when unknown
};

/// Spot VIX against 3-month VIX: below 1 (front cheaper) is the usual
/// contango; above 1 is an inverted curve. Within 1% of parity reads flat.
inline TermStructure vix_term(double vix, double vix3m) {
    TermStructure t;
    if (!std::isfinite(vix) || !(vix > 0) || !std::isfinite(vix3m) || !(vix3m > 0))
        return t;
    t.ratio = vix / vix3m;
    if (t.ratio < 0.99)
        t.label = QStringLiteral("contango");
    else if (t.ratio > 1.01)
        t.label = QStringLiteral("inverted");
    else
        t.label = QStringLiteral("flat");
    return t;
}

} // namespace finterm::screens::world
