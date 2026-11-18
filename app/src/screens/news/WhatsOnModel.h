#pragma once
// WhatsOnModel.h — the pure half of the News screen's WHAT'S ON drawer.
//
// Turns the app's existing free sources into one dated list and buckets it
// into EARLIER THIS WEEK / TODAY / THIS WEEK / THIS MONTH / UPCOMING:
//   * macro releases — ForexFactory's weekly calendar (MacroCalendar.h),
//     filtered to top-tier US releases by default; for releases already out,
//     the printed number from FRED (MacroReleases.h);
//   * FOMC decision days — the Fed's published meeting calendar;
//   * IPO pricings — the IPO Watch calendar (ipo_calendar.py deals);
//   * earnings of large caps and of the user's own names, ex-dividend dates
//     and insider cluster buys on the user's names (whats_on_calendar.py);
//   * Treasury auctions (TreasuryAuctions.h) and market-structure dates
//     computed locally (MarketCalendar.h).
// Every item carries a mechanical impact tag (impact_rules() states the
// table) and a "mine" flag for the user's watchlist/portfolio symbols.
// Header-only and Qt-Core-only; tests/screens/test_whats_on_model.cpp.

#include "services/calendar/MacroCalendar.h"
#include "services/calendar/MacroReleases.h"
#include "services/calendar/MarketCalendar.h"
#include "services/calendar/TreasuryAuctions.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <optional>

namespace finterm::whats_on {

namespace mc = finterm::macro_calendar;
namespace mr = finterm::macro_releases;
namespace mkt = finterm::market_calendar;
namespace ta = finterm::treasury_auctions;

enum class Impact { Low = 0, Med = 1, High = 2 };

inline QString impact_tag(Impact i) {
    return i == Impact::High ? QStringLiteral("HIGH") : i == Impact::Med ? QStringLiteral("MED") : QString();
}

inline QTimeZone et_zone() {
    static const QTimeZone tz("America/New_York");
    return tz;
}

struct Item {
    enum class Source { Macro, Fomc, Ipo, Earnings, Dividend, Insider, Auction, Market };
    Source source = Source::Macro;
    QDateTime when; ///< instant; for untimed items, local midnight of the day
                    ///< (earnings: the ET session's nominal hour, for ordering)
    bool timed = false;
    QString kind;   ///< FED / LABOR / INFLATION / GROWTH / MACRO / IPO / EARNINGS /
                    ///< EX-DIV / INSIDER / RATES / MARKET
    QString title;  ///< main text ("CPI m/m", "Acme Corp (ACME)")
    QString figures; ///< "F 0.3% · P 0.4%", "$15.00-17.00 · $200M" — may be empty
    QString detail; ///< multi-line detail shown on click
    QString key;    ///< IPO: ticker, else company (fallback IPO Watch selection key)
    QString deal_id; ///< IPO: the calendar's deal id — tickers repeat across re-filings
    Impact impact = Impact::Low;
    bool mine = false;   ///< touches a watchlist/portfolio symbol
    QString symbol;      ///< EARNINGS / EX-DIV / INSIDER: the ticker (opens Equity Research)
    QString session;     ///< EARNINGS: "BMO" / "AMC" / "" (not supplied)
    QString group;       ///< non-empty: items with the same group on the same day share one row
    bool released = false; ///< the number is out (macro actual, auction result, reported EPS)
    double surprise_pct = NAN; ///< EARNINGS: reported EPS vs consensus, % (NaN until reported)
    QDate day; ///< set for date-anchored items (earnings): the listed ET date, NOT
               ///< shifted by the viewer's zone ("Thu AMC" stays Thursday in Tokyo)
    QString name;      ///< EARNINGS / IPO / EX-DIV: short company name ("NVIDIA", "Anthropic")
    double size = NAN; ///< EARNINGS: market cap; IPO: deal size (top of range) — ordering only
    QString when_note; ///< untimed: what the day means ("expected to price") — after the date

    QDate local_date() const { return day.isValid() ? day : when.toLocalTime().date(); }
    /// Stable identity across re-renders (an expanded row stays expanded).
    QString row_key() const {
        return QString::number(static_cast<int>(source)) + QLatin1Char('|') + local_date().toString(Qt::ISODate) +
               QLatin1Char('|') + (group.isEmpty() ? title : group);
    }
};

// ── Impact: a fixed, mechanical table — not an opinion per event ────────────

/// The table, as the panel's tooltip states it.
inline QString impact_rules() {
    return QStringLiteral(
        "Impact is a fixed table, not a judgement per event:\n"
        "HIGH — FOMC decision, CPI, nonfarm payrolls, unemployment rate, GDP, PCE prices;\n"
        "        earnings of companies worth $500B+; IPOs raising $10B+\n"
        "MED — PPI, retail sales, jobless claims, ISM, JOLTS, hourly earnings, Fed Chair remarks,\n"
        "        FOMC minutes, other releases the calendar rates high; earnings of $50B+ companies\n"
        "        and of your stocks; IPOs raising $1B+; 10Y+ note/bond auctions; market holidays,\n"
        "        early closes, quad witching, quarter end; insider cluster buys in your stocks\n"
        "LOW — everything else (no tag): shown only under ALL\n"
        "IPO size = shares offered x top of the price range (Nasdaq calendar), before the\n"
        "        underwriters' over-allotment option");
}

constexpr double kNotableIpo = 1e9; ///< an IPO raising this much (top of range) is MED…
constexpr double kHighIpo = 10e9;   ///< …and HIGH from here

inline Impact ipo_impact(double deal_size) {
    if (!std::isfinite(deal_size))
        return Impact::Low;
    return deal_size >= kHighIpo ? Impact::High : deal_size >= kNotableIpo ? Impact::Med : Impact::Low;
}

// ── Plain-language formatting (every row reads without a legend) ────────────

/// "NVIDIA Corporation" → "NVIDIA", "The Home Depot, Inc." → "Home Depot",
/// "JPMorgan Chase & Co." → "JPMorgan Chase", "Anthropic, PBC" → "Anthropic".
/// Strips legal-form and holding-structure words from the END only, never to
/// nothing.
inline QString short_company(const QString& raw) {
    QString s = raw.simplified();
    // Nasdaq's security descriptions ("Cognex Corporation Common Stock",
    // "Artesian Resources Corporation Class A Common Stock") and its
    // "Home Depot, Inc. (The)" ordering.
    static const QRegularExpression security(
        QStringLiteral("\\s+(Class [A-Z]\\b|Common Stock|Ordinary Shares|American Depositary|Depositary Shares|"
                       "Sponsored ADR).*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression the_suffix(QStringLiteral("\\s*\\(The\\)\\s*$"),
                                               QRegularExpression::CaseInsensitiveOption);
    const QString before = s;
    s.remove(the_suffix);
    s.remove(security);
    if (s.isEmpty())
        s = before;
    if (s.startsWith(QLatin1String("The "), Qt::CaseInsensitive) && s.size() > 4)
        s = s.mid(4);
    static const QStringList tails = {
        "inc", "inc.", "incorporated", "corp", "corp.", "corporation", "co", "co.", "company", "ltd", "ltd.",
        "limited", "plc", "p.l.c.", "n.v.", "nv", "s.a.", "sa", "s.p.a.", "ag", "se", "pbc", "llc", "l.p.", "lp",
        "holdings", "holding", "group", "(adr)", "adr", "&", "and"};
    for (;;) {
        while (s.endsWith(QLatin1Char(',')) || s.endsWith(QLatin1Char(' ')))
            s.chop(1);
        const int sp = s.lastIndexOf(QLatin1Char(' '));
        if (sp <= 0)
            break;
        const QString last = s.mid(sp + 1).toLower();
        if (!tails.contains(last))
            break;
        s.truncate(sp);
    }
    return s.isEmpty() ? raw.trimmed() : s;
}

/// "Tue Nov 17"; the year is added when it is not `today`'s ("Tue Jan 5, 2027").
inline QString date_label(const QDate& d, const QDate& today) {
    if (!d.isValid())
        return QStringLiteral("—");
    const QLocale en(QLocale::English);
    return en.toString(d, d.year() == today.year() ? QStringLiteral("ddd MMM d") : QStringLiteral("ddd MMM d, yyyy"));
}

/// "Wed Dec 9, in 26 days" / "Mon Nov 16, tomorrow" / "Fri Nov 13, today".
inline QString days_away(const QDate& d, const QDate& today) {
    const qint64 n = today.daysTo(d);
    return date_label(d, today) + QStringLiteral(", ") +
           (n == 0   ? QStringLiteral("today")
            : n == 1 ? QStringLiteral("tomorrow")
            : n > 1  ? QStringLiteral("in %1 days").arg(n)
                     : QStringLiteral("%1 days ago").arg(-n));
}

/// Earnings session in words: BMO/AMC are Nasdaq's codes, not the reader's.
inline QString session_label(const QString& session) {
    if (session == QLatin1String("BMO"))
        return QStringLiteral("before open");
    if (session == QLatin1String("AMC"))
        return QStringLiteral("after close");
    return QStringLiteral("time TBA"); // to be announced
}

/// "8:30 ET", "14:00 ET" — the release clock the market quotes.
inline QString clock_et(const QDateTime& when) {
    return when.toTimeZone(et_zone()).toString(QStringLiteral("H:mm")) + QStringLiteral(" ET");
}

/// The figures of a macro release in words: "Actual 0.3% vs forecast 0.4% ·
/// prior 0.6%", "Actual pending · forecast …" (out, number not yet
/// available), "Forecast 0.3% · prior 0.4%" (not out yet).
inline QString figures_text(const QString& actual, const QString& forecast, const QString& prior,
                            bool pending = false) {
    QStringList parts;
    if (!actual.isEmpty())
        parts << (forecast.isEmpty() ? QStringLiteral("Actual %1").arg(actual)
                                     : QStringLiteral("Actual %1 vs forecast %2").arg(actual, forecast));
    else if (pending)
        parts << (forecast.isEmpty() ? QStringLiteral("Actual pending")
                                     : QStringLiteral("Actual pending · forecast %1").arg(forecast));
    else if (!forecast.isEmpty())
        parts << QStringLiteral("Forecast %1").arg(forecast);
    if (!prior.isEmpty())
        parts << (parts.isEmpty() ? QStringLiteral("Prior %1") : QStringLiteral("prior %1")).arg(prior);
    return parts.join(QStringLiteral(" · "));
}

/// The calendar's own names for a few headline releases, in the words the
/// news uses. Display only: lookups (FRED spec, FOMC de-dupe) keep the
/// calendar's title.
inline QString plain_macro_title(const QString& title) {
    static const QHash<QString, QString> map = {
        {QStringLiteral("Non-Farm Employment Change"), QStringLiteral("Nonfarm payrolls")},
        {QStringLiteral("Unemployment Claims"), QStringLiteral("Jobless claims")},
        {QStringLiteral("Prelim UoM Consumer Sentiment"), QStringLiteral("UMich consumer sentiment (prelim)")},
        {QStringLiteral("Revised UoM Consumer Sentiment"), QStringLiteral("UMich consumer sentiment (final)")},
        {QStringLiteral("Prelim UoM Inflation Expectations"), QStringLiteral("UMich inflation expectations (prelim)")},
        {QStringLiteral("Revised UoM Inflation Expectations"), QStringLiteral("UMich inflation expectations (final)")},
        {QStringLiteral("JOLTS Job Openings"), QStringLiteral("Job openings (JOLTS)")},
        {QStringLiteral("Federal Funds Rate"), QStringLiteral("Federal Funds Rate (FOMC decision)")},
    };
    return map.value(title, title);
}

/// "NASDAQ Global Select" → "Nasdaq"; NYSE names pass through.
inline QString exchange_short(const QString& exch) {
    return exch.contains(QLatin1String("NASDAQ"), Qt::CaseInsensitive) ? QStringLiteral("Nasdaq") : exch;
}

/// "$58B" / "$1.5B" / "$240M" — compact deal sizes.
inline QString usd_compact(double usd) {
    if (!std::isfinite(usd))
        return QStringLiteral("—");
    if (usd >= 1e9)
        return ta::money_b(usd); // "$58B" / "$1.2B" — the auctions' format
    if (usd >= 1e6)
        return QStringLiteral("$%1M").arg(QString::number(usd / 1e6, 'f', usd >= 1e7 ? 0 : 1));
    return QStringLiteral("$%1K").arg(QString::number(usd / 1e3, 'f', 0));
}

inline Impact macro_impact(const QString& title, int ff_importance) {
    const QString t = title.toLower();
    auto has = [&](const char* s) { return t.contains(QLatin1String(s)); };
    if (has("federal funds rate") || has("fomc statement") || has("fomc press conference") ||
        has("fomc rate decision") || (has("cpi") && !has("median") && !has("trimmed")) ||
        has("non-farm employment change") || has("nonfarm payrolls") || t == QLatin1String("unemployment rate") ||
        has("gdp") || has("pce price index"))
        return Impact::High;
    if (has("ppi") || has("retail sales") || has("unemployment claims") || has("ism ") || has("jolts") ||
        has("average hourly earnings") || has("fed chair") || has("fomc meeting minutes") || ff_importance >= 3)
        return Impact::Med;
    return Impact::Low;
}

constexpr double kMajorCap = 50e9; ///< "earnings that matter": $50B+ (roughly the 150 largest US listings)
constexpr double kHighCap = 500e9; ///< …and HIGH impact from $500B

inline Impact earnings_impact(double market_cap, bool mine) {
    if (std::isfinite(market_cap) && market_cap >= kHighCap)
        return Impact::High;
    if (mine || (std::isfinite(market_cap) && market_cap >= kMajorCap))
        return Impact::Med;
    return Impact::Low;
}

/// Ticker normalisation shared by every "is this one of mine" test:
/// BRK-B / BRK/B / brk.b → BRK.B.
inline QString norm_symbol(QString s) {
    s = s.trimmed().toUpper();
    s.replace(QLatin1Char('-'), QLatin1Char('.'));
    s.replace(QLatin1Char('/'), QLatin1Char('.'));
    return s;
}

inline QSet<QString> norm_symbols(const QStringList& syms) {
    QSet<QString> out;
    for (const auto& s : syms) {
        const QString n = norm_symbol(s);
        if (!n.isEmpty())
            out.insert(n);
    }
    return out;
}

/// Monday of `d`'s ISO week.
inline QDate week_start(const QDate& d) {
    return d.addDays(1 - d.dayOfWeek());
}

// ── Macro ───────────────────────────────────────────────────────────────────

inline QString macro_figures(const mc::MacroEvent& e) {
    return figures_text(e.actual, e.forecast, e.previous);
}

using FredSeries = QHash<QString, QVector<mr::Obs>>; ///< FRED id → observations

/// FRED series needed to print the actuals of releases already out (from
/// `from` up to `now`) — what the panel downloads.
inline QStringList fred_series_needed(const QVector<mc::MacroEvent>& events, const QDateTime& now,
                                      const QDate& from, bool top_tier_us_only) {
    QStringList out;
    for (const auto& e : events) {
        if (!e.when.isValid() || e.when > now || e.when.toLocalTime().date() < from)
            continue;
        if (e.country != QLatin1String("USD") && e.country != QLatin1String("US"))
            continue;
        if (top_tier_us_only && !mc::is_top_tier_us(e))
            continue;
        if (const auto s = mr::spec_for(e.title); s && !out.contains(s->fred_id))
            out << s->fred_id;
    }
    return out;
}

/// Macro events from `from` (default: today) onward; `top_tier_us_only`
/// applies the default importance filter (mc::is_top_tier_us). Releases at or
/// before `now` whose series is in `fred` get the printed actual — only when
/// FRED already holds the period this release reports (mr::printed); until
/// then the row says "awaiting FRED", never a stale number.
inline QVector<Item> items_from_macro(const QVector<mc::MacroEvent>& events, const QDateTime& now,
                                      bool top_tier_us_only, QDate from = {}, const FredSeries* fred = nullptr) {
    QVector<Item> out;
    const QDate today = now.toLocalTime().date();
    if (!from.isValid() || from > today)
        from = today;
    QVector<mc::MacroEvent> window;
    for (const auto& e : events)
        if (e.when.isValid() && e.when.toLocalTime().date() >= from)
            window.append(e);
    std::stable_sort(window.begin(), window.end(),
                     [](const mc::MacroEvent& a, const mc::MacroEvent& b) { return a.when < b.when; });
    for (const auto& e : window) {
        if (top_tier_us_only && !mc::is_top_tier_us(e))
            continue;
        if (e.importance == 0 && top_tier_us_only)
            continue; // holidays
        Item it;
        it.source = Item::Source::Macro;
        it.when = e.when;
        it.timed = e.timed;
        it.kind = mc::kind_of(e.title);
        if (it.kind.isEmpty())
            it.kind = QStringLiteral("MACRO");
        const bool us = e.country == QLatin1String("USD") || e.country == QLatin1String("US");
        it.title = e.country == QLatin1String("USD") ? plain_macro_title(e.title)
                                                     : e.country + QLatin1Char(' ') + e.title;
        it.impact = us ? macro_impact(e.title, e.importance) : (e.importance >= 3 ? Impact::Med : Impact::Low);
        it.figures = macro_figures(e);
        QString source_note = QStringLiteral("Source: ForexFactory weekly calendar (forecast = consensus)");
        const bool out_already = e.when <= now;
        if (out_already && us && e.actual.isEmpty() && fred) {
            if (const auto spec = mr::spec_for(e.title); spec && fred->contains(spec->fred_id)) {
                const QDate release_day = e.when.toTimeZone(et_zone()).date();
                const auto p = mr::printed(*spec, fred->value(spec->fred_id), release_day);
                it.released = !p.actual.isEmpty();
                // Until FRED posts the period: "Actual pending", never last month's number.
                it.figures = figures_text(p.actual, e.forecast, !p.prior.isEmpty() ? p.prior : e.previous,
                                          /*pending=*/true);
                source_note = p.actual.isEmpty()
                                  ? QStringLiteral("Actual: FRED %1 has not posted %2 yet. Forecast/previous: "
                                                   "ForexFactory.")
                                        .arg(spec->fred_id,
                                             mr::period_label(*spec, mr::expected_period(*spec, release_day)))
                                  : QStringLiteral("Actual and revised prior: FRED %1 (%2), computed with the "
                                                   "headline's transform — may differ from the published figure by "
                                                   "rounding. Forecast: ForexFactory consensus.")
                                        .arg(spec->fred_id, mr::period_label(*spec, p.period));
            }
        }
        const QString imp = e.importance >= 3 ? QStringLiteral("High") : e.importance == 2 ? QStringLiteral("Medium")
                                                                     : e.importance == 1  ? QStringLiteral("Low")
                                                                                          : QStringLiteral("Holiday");
        const QDateTime et = e.when.toTimeZone(et_zone());
        it.detail = QStringLiteral("%1 — %2\n%3 · calendar impact %4\nActual %5 · Forecast %6 · Previous %7\n%8")
                        .arg(e.country, e.title,
                             QLocale(QLocale::English)
                                 .toString(et, e.timed ? QStringLiteral("ddd d MMM yyyy HH:mm 'ET'")
                                                       : QStringLiteral("ddd d MMM yyyy")),
                             imp, e.actual.isEmpty() ? QStringLiteral("—") : e.actual,
                             e.forecast.isEmpty() ? QStringLiteral("—") : e.forecast,
                             e.previous.isEmpty() ? QStringLiteral("—") : e.previous, source_note);
        out.append(it);
    }
    return out;
}

/// The next `max_count` FOMC decision days from `today` on (the Fed page
/// lists a year and more ahead; the rest would bury UPCOMING). The statement
/// is released at 14:00 Eastern. A day the macro calendar already covers with
/// a rate-decision row ("Federal Funds Rate", "FOMC Statement") is not listed
/// twice.
inline QVector<Item> items_from_fomc(const QVector<QDate>& decisions, const QDate& today,
                                     const QVector<Item>& macro_items, int max_count = 2) {
    QVector<Item> out;
    int taken = 0;
    for (const QDate& d : decisions) {
        if (d < today)
            continue;
        if (taken++ >= max_count)
            break;
        const QDateTime when(d, QTime(14, 0), et_zone());
        const bool covered = std::any_of(macro_items.begin(), macro_items.end(), [&](const Item& m) {
            return m.local_date() == when.toLocalTime().date() &&
                   (m.title.contains(QLatin1String("Federal Funds Rate")) ||
                    m.title.contains(QLatin1String("FOMC Statement")));
        });
        if (covered)
            continue;
        Item it;
        it.source = Item::Source::Fomc;
        it.when = when;
        it.timed = true;
        it.kind = QStringLiteral("FED");
        it.title = QStringLiteral("FOMC rate decision");
        it.impact = Impact::High;
        it.detail = QStringLiteral("FOMC meeting ends %1; statement 14:00 ET (%2 local)\nSource: federalreserve.gov "
                                   "meeting calendar")
                        .arg(QLocale(QLocale::English).toString(d, QStringLiteral("ddd d MMM yyyy")),
                             when.toLocalTime().toString(QStringLiteral("HH:mm")));
        out.append(it);
    }
    return out;
}

/// What an IPO raises, from the ipo_calendar.py deal: shares offered x the
/// price range (lo/hi), or — when Nasdaq has no range/shares yet — its
/// registered "up to" amount (`proposed_amount`, which includes the
/// over-allotment) as `hi` alone. NaN when neither is known: an unknown size
/// is not a small one, it is unknown (and stays out of the top tier).
struct DealSize {
    double lo = NAN, hi = NAN;
    double price_lo = NAN, price_hi = NAN;
    double shares = NAN;
    bool registered = false; ///< hi is Nasdaq's "up to" dollar amount, not shares x price
};

inline DealSize ipo_deal_size(const QJsonObject& o) {
    DealSize z;
    const QStringList range =
        o.value(QLatin1String("price_range")).toString().remove(QLatin1Char('$')).split(QLatin1Char('-'));
    bool ok1 = false, ok2 = true;
    const double p1 = range.value(0).trimmed().toDouble(&ok1);
    const double p2 = range.size() > 1 ? range.value(1).trimmed().toDouble(&ok2) : p1;
    if (ok1 && ok2 && range.size() <= 2 && p1 > 0 && p2 >= p1) {
        z.price_lo = p1;
        z.price_hi = p2;
    }
    if (const QJsonValue sh = o.value(QLatin1String("shares")); sh.isDouble() && sh.toDouble() > 0)
        z.shares = sh.toDouble();
    if (std::isfinite(z.price_lo) && std::isfinite(z.shares)) {
        z.lo = z.shares * z.price_lo;
        z.hi = z.shares * z.price_hi;
    } else if (const QJsonValue pa = o.value(QLatin1String("proposed_amount")); pa.isDouble() && pa.toDouble() > 0) {
        z.hi = pa.toDouble();
        z.registered = true;
    }
    return z;
}

inline QString share_price(double p) {
    return QStringLiteral("$") + QString::number(p, 'f', p == std::floor(p) ? 0 : 2);
}

/// "$70–78 a share · raising $58–64B · Nasdaq".
inline QString ipo_figures(const DealSize& z, const QString& exchange) {
    QStringList fig;
    if (std::isfinite(z.price_lo))
        fig << (z.price_hi > z.price_lo
                    ? share_price(z.price_lo) + QStringLiteral("–") + share_price(z.price_hi).mid(1)
                    : share_price(z.price_lo)) +
                   QStringLiteral(" a share");
    if (std::isfinite(z.lo) && z.hi > z.lo) {
        const QString lo = usd_compact(z.lo), hi = usd_compact(z.hi);
        // "$58–64B" when both ends share a unit, else "$800M–1.2B".
        fig << QStringLiteral("raising ") + (lo.right(1) == hi.right(1) ? lo.chopped(1) : lo) +
                   QStringLiteral("–") + hi.mid(1);
    } else if (std::isfinite(z.hi)) {
        fig << (z.registered ? QStringLiteral("up to %1") : QStringLiteral("raising %1")).arg(usd_compact(z.hi));
    }
    if (!exchange.isEmpty())
        fig << exchange_short(exchange);
    return fig.join(QStringLiteral(" · "));
}

/// IPOs from the ipo_calendar.py "deals" array IPO Watch uses:
///   * every expected pricing (status "upcoming") dated today or later —
///     Nasdaq's expected date, set by the underwriters;
///   * a still-filed deal only when it is notable (>= kNotableIpo) AND IPO
///     Watch projects a dated window for it (imminent / estimate) — shown at
///     the projection's point with its window, and never above MED (a
///     statistical window is not a scheduled event). Long shots, dormant and
///     undated filings get no row: no date is invented.
/// Impact by size (ipo_impact): small deals are LOW and stay in the full view.
inline QVector<Item> items_from_ipo_deals(const QJsonArray& deals, const QDate& today) {
    QVector<Item> out;
    const QLocale en(QLocale::English);
    for (const auto& v : deals) {
        const QJsonObject o = v.toObject();
        const QString status = o.value(QLatin1String("status")).toString();
        const QJsonObject proj = o.value(QLatin1String("projection")).toObject();
        const QString pkind = proj.value(QLatin1String("kind")).toString();
        const bool upcoming = status == QLatin1String("upcoming");
        const bool projected = status == QLatin1String("filed") &&
                               (pkind == QLatin1String("imminent") || pkind == QLatin1String("estimate"));
        if (!upcoming && !projected)
            continue;
        const QDate d = QDate::fromString(upcoming ? o.value(QLatin1String("expected_date")).toString()
                                                   : proj.value(QLatin1String("date")).toString(),
                                          Qt::ISODate);
        if (!d.isValid() || d < today)
            continue;
        const QString company = o.value(QLatin1String("company")).toString().trimmed();
        const QString ticker = o.value(QLatin1String("ticker")).toString().trimmed();
        if (company.isEmpty())
            continue;
        const DealSize z = ipo_deal_size(o);
        Impact imp = ipo_impact(z.hi);
        if (projected) {
            if (imp == Impact::Low)
                continue; // the undated pipeline is IPO Watch's job; only notable filers here
            imp = std::min(imp, Impact::Med);
        }
        Item it;
        it.source = Item::Source::Ipo;
        it.when = QDateTime(d, QTime(0, 0)); // local day; pricing time is not published
        it.timed = false;
        it.kind = QStringLiteral("IPO");
        it.name = short_company(company);
        it.title = ticker.isEmpty() ? it.name : QStringLiteral("%1 (%2)").arg(it.name, ticker);
        it.size = z.hi;
        it.impact = imp;
        const QString exch = o.value(QLatin1String("exchange")).toString().trimmed();
        it.figures = ipo_figures(z, exch);
        it.key = ticker.isEmpty() ? company : ticker;
        it.deal_id = o.value(QLatin1String("deal_id")).toString();
        const QString size_line =
            std::isfinite(z.lo)
                ? QStringLiteral("%1M shares at $%2–%3 = %4–%5, before the over-allotment option")
                      .arg(QString::number(z.shares / 1e6, 'f', z.shares >= 1e7 ? 0 : 1))
                      .arg(z.price_lo, 0, 'f', 2)
                      .arg(z.price_hi, 0, 'f', 2)
                      .arg(usd_compact(z.lo), usd_compact(z.hi))
            : std::isfinite(z.hi) ? QStringLiteral("Offering of up to %1 (Nasdaq's dollar value of the shares "
                                                   "offered, incl. the over-allotment option); no share count yet")
                                        .arg(usd_compact(z.hi))
                                  : QStringLiteral("Size not published yet");
        if (upcoming) {
            it.when_note = QStringLiteral("expected to price");
            it.detail = QStringLiteral("%1 — expected to price %2 (Nasdaq's expected date, set by the underwriters; "
                                       "it can slip). Shares usually start trading the next business day.\n%3%4")
                            .arg(company, en.toString(d, QStringLiteral("dddd MMM d, yyyy")), size_line,
                                 exch.isEmpty() ? QString() : QStringLiteral(" · ") + exch);
        } else {
            const QDate lo = QDate::fromString(proj.value(QLatin1String("lo")).toString(), Qt::ISODate);
            const QDate hi = QDate::fromString(proj.value(QLatin1String("hi")).toString(), Qt::ISODate);
            it.when_note = lo.isValid() && hi.isValid() && hi > lo
                               ? QStringLiteral("projected, window %1 – %2")
                                     .arg(en.toString(lo, QStringLiteral("MMM d")),
                                          en.toString(hi, QStringLiteral("MMM d")))
                               : QStringLiteral("projected");
            it.detail = QStringLiteral("%1 — filed, no pricing date set. The IPO calendar's projection: %2\n%3")
                            .arg(company, proj.value(QLatin1String("basis")).toString(), size_line);
        }
        out.append(it);
    }
    return out;
}

// ── Company events (whats_on_calendar.py) ───────────────────────────────────

/// A JSON number, or NaN for null/absent — never 0 (a missing market cap is
/// unknown, not a micro cap).
inline double jnum(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1String(k));
    return v.isDouble() ? v.toDouble() : NAN;
}

inline QString money_short(double usd) {
    if (!std::isfinite(usd))
        return QStringLiteral("—");
    if (std::fabs(usd) >= 1e12)
        return QStringLiteral("$%1T").arg(QString::number(usd / 1e12, 'f', 2));
    if (std::fabs(usd) >= 1e9)
        return QStringLiteral("$%1B").arg(QString::number(usd / 1e9, 'f', 0));
    if (std::fabs(usd) >= 1e6)
        return QStringLiteral("$%1M").arg(QString::number(usd / 1e6, 'f', 1));
    return QStringLiteral("$%1").arg(QString::number(usd, 'f', 0));
}

inline QString eps(double v) {
    return std::isfinite(v) ? QString::number(v, 'f', 2) : QString();
}

/// The "earnings that matter" cut alone: market cap ≥ `major_cap` OR one of
/// `mine` (an unknown cap is not large). One rule for the arrival cut and the
/// items below.
inline bool earnings_row_matters(const QJsonObject& o, const QSet<QString>& mine, double major_cap = kMajorCap) {
    const double cap = jnum(o, "market_cap");
    return mine.contains(norm_symbol(o.value(QLatin1String("symbol")).toString())) ||
           (std::isfinite(cap) && cap >= major_cap);
}

/// The rows that can matter, from the feed's every-company answer — cut once
/// on arrival rather than on every repaint.
inline QJsonArray earnings_that_matter(const QJsonArray& rows, const QSet<QString>& mine,
                                       double major_cap = kMajorCap) {
    QJsonArray out;
    for (const auto& v : rows)
        if (earnings_row_matters(v.toObject(), mine, major_cap))
            out.append(v);
    return out;
}

/// Earnings that matter: market cap ≥ `major_cap` OR one of `mine`, dated
/// `from` onward. The ET session decides the order within a day (BMO 07:00,
/// unknown 12:00, AMC 16:05 — nominal, the row shows the session, not a
/// clock). A second share class of the same company on the same day
/// (PBR / PBR.A) is one report.
inline QVector<Item> items_from_earnings(const QJsonArray& rows, const QDate& from, const QSet<QString>& mine,
                                         double major_cap = kMajorCap) {
    QVector<Item> out;
    QSet<QString> seen; // name|date
    const QLocale en(QLocale::English);
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        const QString sym = norm_symbol(o.value(QLatin1String("symbol")).toString());
        const QDate d = QDate::fromString(o.value(QLatin1String("date")).toString(), Qt::ISODate);
        if (sym.isEmpty() || !d.isValid() || d < from)
            continue;
        if (!earnings_row_matters(o, mine, major_cap))
            continue;
        const double cap = jnum(o, "market_cap");
        const bool is_mine = mine.contains(sym);
        const QString name = o.value(QLatin1String("name")).toString().trimmed();
        const QString dedupe = name.toLower() + QLatin1Char('|') + d.toString(Qt::ISODate);
        if (!name.isEmpty() && seen.contains(dedupe) && !is_mine)
            continue;
        seen.insert(dedupe);
        const QString session = o.value(QLatin1String("session")).toString();
        Item it;
        it.source = Item::Source::Earnings;
        it.kind = QStringLiteral("EARNINGS");
        it.symbol = sym;
        it.name = short_company(name.isEmpty() ? sym : name);
        it.title = it.name;
        it.size = cap;
        it.session = session == QLatin1String("BMO") || session == QLatin1String("AMC") ? session : QString();
        const QTime nominal = it.session == QLatin1String("BMO")   ? QTime(7, 0)
                              : it.session == QLatin1String("AMC") ? QTime(16, 5)
                                                                   : QTime(12, 0);
        it.when = QDateTime(d, nominal, et_zone());
        it.day = d;
        it.timed = false;
        it.mine = is_mine;
        it.impact = earnings_impact(cap, is_mine);
        it.group = QStringLiteral("EARN:") + it.session;
        const double est = jnum(o, "eps_forecast"), act = jnum(o, "eps_actual"), sur = jnum(o, "surprise_pct");
        if (std::isfinite(act)) {
            it.released = true;
            it.surprise_pct = sur;
            it.figures = QStringLiteral("EPS %1").arg(eps(act));
            if (std::isfinite(est))
                it.figures += QStringLiteral(" vs %1 expected").arg(eps(est));
            if (std::isfinite(sur))
                it.figures += QStringLiteral(" (%1%2% surprise)").arg(sur >= 0 ? QStringLiteral("+") : QString(),
                                                             QString::number(sur, 'f', 1));
        } else if (std::isfinite(est)) {
            it.figures = QStringLiteral("EPS estimate %1").arg(eps(est));
        }
        const QString when_txt = it.session == QLatin1String("BMO")   ? QStringLiteral("before the open")
                                 : it.session == QLatin1String("AMC") ? QStringLiteral("after the close")
                                                                      : QStringLiteral("time not supplied");
        const double n_est = jnum(o, "estimates");
        const QString fq = o.value(QLatin1String("fiscal_quarter")).toString();
        it.detail = QStringLiteral("%1 (%2) — %3, %4\nMarket cap %5%6%7%8")
                        .arg(name.isEmpty() ? sym : name, sym, en.toString(d, QStringLiteral("ddd d MMM")), when_txt,
                             money_short(cap),
                             it.figures.isEmpty() ? QString() : QStringLiteral(" · ") + it.figures,
                             std::isfinite(n_est) ? QStringLiteral(" (%1 estimates)").arg(int(n_est)) : QString(),
                             fq.isEmpty() ? QString() : QStringLiteral(" · quarter ending ") + fq);
        out.append(it);
    }
    return out;
}

/// Ex-dividend dates of the user's names (the script sends only those).
inline QVector<Item> items_from_dividends(const QJsonArray& rows, const QDate& from) {
    QVector<Item> out;
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        const QString sym = norm_symbol(o.value(QLatin1String("symbol")).toString());
        const QDate d = QDate::fromString(o.value(QLatin1String("ex_date")).toString(), Qt::ISODate);
        if (sym.isEmpty() || !d.isValid() || d < from)
            continue;
        Item it;
        it.source = Item::Source::Dividend;
        it.kind = QStringLiteral("EX-DIV");
        it.symbol = sym;
        it.mine = true;
        it.when = QDateTime(d, QTime(0, 0));
        const QString company = o.value(QLatin1String("name")).toString().trimmed();
        it.name = company.isEmpty() ? sym : short_company(company);
        it.title = it.name == sym ? QStringLiteral("%1 goes ex-dividend").arg(sym)
                                  : QStringLiteral("%1 (%2) goes ex-dividend").arg(it.name, sym);
        const double amt = jnum(o, "amount");
        if (std::isfinite(amt))
            it.figures = QStringLiteral("$%1 a share")
                             .arg(amt < 0.1 ? QString::number(amt, 'g', 3) : QString::number(amt, 'f', 2));
        const QString pay = o.value(QLatin1String("pay_date")).toString();
        it.detail = QStringLiteral("%1 — buy before %2 to receive the dividend%3\nSource: Nasdaq dividend calendar")
                        .arg(o.value(QLatin1String("name")).toString(), d.toString(Qt::ISODate),
                             pay.isEmpty() ? QString() : QStringLiteral("; paid ") + pay);
        out.append(it);
    }
    return out;
}

/// Insider cluster buys (≥ 2 insiders, open market, no 10b5-1 plans or 10%
/// owners) in the user's names, dated by the last filing.
inline QVector<Item> items_from_insiders(const QJsonArray& rows, const QDate& from, const QString& last_scanned) {
    QVector<Item> out;
    for (const auto& v : rows) {
        const QJsonObject o = v.toObject();
        const QString sym = norm_symbol(o.value(QLatin1String("symbol")).toString());
        const QDate d = QDate::fromString(o.value(QLatin1String("last_filed")).toString(), Qt::ISODate);
        if (sym.isEmpty() || !d.isValid() || d < from)
            continue;
        const int n = o.value(QLatin1String("insiders")).toInt();
        Item it;
        it.source = Item::Source::Insider;
        it.kind = QStringLiteral("INSIDER");
        it.symbol = sym;
        it.mine = true;
        it.impact = Impact::Med;
        it.when = QDateTime(d, QTime(0, 0));
        it.released = true;
        it.title = QStringLiteral("%1 — %2 insiders bought").arg(sym).arg(n);
        it.figures = money_short(jnum(o, "value"));
        it.detail = QStringLiteral("%1: open-market purchases by %2 insiders, last filed %3 (trades to %4).\n"
                                   "Source: SEC Form 4, from the Ownership scan's local store%5")
                        .arg(o.value(QLatin1String("issuer")).toString())
                        .arg(n)
                        .arg(d.toString(Qt::ISODate), o.value(QLatin1String("last_trade")).toString(),
                             last_scanned.isEmpty() ? QString()
                                                    : QStringLiteral(" (last scanned %1)").arg(last_scanned));
        out.append(it);
    }
    return out;
}

// ── Treasury auctions + market structure ───────────────────────────────────

/// Bills ≥ 13 weeks share one row a day ("13W · 26W bill"); each note/bond
/// is its own row. 10-year-and-longer coupons are MED.
inline QVector<Item> items_from_auctions(const QVector<ta::Auction>& auctions, const QDate& from) {
    QVector<Item> out;
    const QLocale en(QLocale::English);
    for (const auto& a : auctions) {
        if (a.date < from)
            continue;
        Item it;
        it.source = Item::Source::Auction;
        it.kind = QStringLiteral("RATES");
        it.timed = a.close.isValid();
        it.when = it.timed ? a.close : QDateTime(a.date, QTime(0, 0));
        it.title = a.label + QStringLiteral(" auction");
        it.figures = ta::figures(a);
        it.released = a.has_result();
        it.impact = a.coupon && a.years >= 10 ? Impact::Med : Impact::Low;
        if (!a.coupon)
            it.group = QStringLiteral("BILLS");
        it.detail = QStringLiteral("%1 %2 — competitive close %3%4\n%5Source: TreasuryDirect")
                        .arg(a.term, a.type,
                             a.close.isValid() ? en.toString(a.close.toTimeZone(et_zone()),
                                                             QStringLiteral("ddd d MMM HH:mm 'ET'"))
                                               : en.toString(a.date, QStringLiteral("ddd d MMM")),
                             a.announced ? QString() : QStringLiteral(" (tentative — not yet announced)"),
                             it.figures.isEmpty() ? QString() : it.figures + QLatin1Char('\n'));
        out.append(it);
    }
    return out;
}

inline QVector<Item> items_from_market_dates(const QVector<mkt::MarketDate>& dates) {
    using K = mkt::MarketDate::Kind;
    QVector<Item> out;
    for (const auto& m : dates) {
        Item it;
        it.source = Item::Source::Market;
        it.kind = QStringLiteral("MARKET");
        it.title = m.title;
        it.detail = m.detail;
        if (m.kind == K::EarlyClose) {
            it.timed = true;
            it.when = QDateTime(m.date, QTime(13, 0), et_zone());
        } else {
            it.when = QDateTime(m.date, QTime(0, 0));
        }
        it.impact = m.kind == K::Opex || m.kind == K::MonthEnd ? Impact::Low : Impact::Med;
        out.append(it);
    }
    return out;
}

// ── Labels ──────────────────────────────────────────────────────────────────

/// The row's date line — always the calendar date, the countdown only as a
/// hint beside it:
///   timed:   "Thu Nov 19 · 8:30 ET", "Fri Nov 13 · 8:30 ET · in 2h 05m"
///            (within 24 h ahead), "… · 35m ago" (out within 3 h);
///   untimed: "Tue Nov 17 · after close" (earnings, session in words),
///            "Thu Nov 19 · expected to price" (IPO; `with_note`).
/// A timed item's day AND clock are US Eastern — the release clock the market
/// quotes ("Tue 14:00 ET" never sits beside the viewer's Wednesday); an
/// untimed one keeps its listed date (Item::local_date). The year is shown
/// when it is not the current one.
inline QString when_label(const Item& it, const QDateTime& now, bool with_note = true) {
    if (!it.when.isValid())
        return QStringLiteral("—");
    if (!it.timed) {
        QString s = date_label(it.local_date(), now.toLocalTime().date());
        if (it.source == Item::Source::Earnings)
            s += QStringLiteral(" · ") + session_label(it.session);
        if (with_note && !it.when_note.isEmpty())
            s += QStringLiteral(" · ") + it.when_note;
        return s;
    }
    QString s = date_label(it.when.toTimeZone(et_zone()).date(), now.toTimeZone(et_zone()).date()) +
                QStringLiteral(" · ") + clock_et(it.when);
    const qint64 secs = now.secsTo(it.when);
    auto span = [](qint64 v) {
        const qint64 h = v / 3600, m = (v % 3600) / 60;
        if (h == 0)
            return QStringLiteral("%1m").arg(std::max<qint64>(m, 1));
        return m == 0 ? QStringLiteral("%1h").arg(h) : QStringLiteral("%1h %2m").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    };
    if (secs >= 0 && secs < 24 * 3600)
        s += QStringLiteral(" · in ") + span(secs);
    else if (secs < 0 && -secs < 3 * 3600) // just out; older needs no hint
        s += QStringLiteral(" · ") + span(-secs) + QStringLiteral(" ago");
    return s;
}

/// Still ahead: a timed item after `now`; an untimed one on a later day, or
/// today — earnings until their nominal session hour (an AMC report until
/// 16:05 ET), any other all-day item (an IPO pricing, an ex-date) all day:
/// its `when` is the day's midnight, not an hour it happens at.
inline bool is_ahead(const Item& it, const QDateTime& now) {
    if (!it.when.isValid())
        return false;
    if (it.timed)
        return it.when > now;
    const QDate today = now.toLocalTime().date();
    if (it.local_date() != today)
        return it.local_date() > today;
    return it.source != Item::Source::Earnings || it.when > now;
}

/// What happens, in words: "NVIDIA earnings", "Anthropic IPO prices",
/// "CPI m/m", "Microsoft (MSFT) goes ex-dividend".
inline QString event_phrase(const Item& it) {
    switch (it.source) {
    case Item::Source::Earnings:
        return (it.name.isEmpty() ? it.symbol : it.name) + QStringLiteral(" earnings");
    case Item::Source::Ipo:
        return (it.name.isEmpty() ? it.title : it.name) + QStringLiteral(" IPO prices");
    default:
        return it.title;
    }
}

/// The NEXT line: "NVIDIA earnings — Thu Nov 19 · after close".
inline QString next_text(const Item& it, const QDateTime& now) {
    return event_phrase(it) + QStringLiteral(" — ") + when_label(it, now, /*with_note=*/false);
}

/// The next event worth a line of its own: market-wide, the first HIGH item
/// still ahead; in MY STOCKS mode (`mine_only`), the first event on the
/// user's names at any impact — the line follows the list it sits above.
/// Ties (one session, several reports) go to the larger company / deal.
inline std::optional<Item> next_up(const QVector<Item>& items, const QDateTime& now, bool mine_only = false) {
    std::optional<Item> best;
    for (const auto& it : items) {
        if (mine_only ? !it.mine : it.impact != Impact::High)
            continue;
        if (!is_ahead(it, now))
            continue;
        const bool bigger = std::isfinite(it.size) && (!best || !std::isfinite(best->size) || it.size > best->size);
        if (!best || it.when < best->when || (it.when == best->when && bigger))
            best = it;
    }
    return best;
}



// ── Sections ────────────────────────────────────────────────────────────────

enum class Section { Earlier, Today, ThisWeek, ThisMonth, Later, None };

/// EARLIER = Monday of this week up to yesterday (what came out: only rows
/// with a number, the user's names, and MED+); then mc::bucket_for.
inline Section section_for(const QDate& d, const QDate& today) {
    if (!d.isValid())
        return Section::None;
    if (d < today)
        return d >= week_start(today) ? Section::Earlier : Section::None;
    switch (mc::bucket_for(d, today)) {
    case mc::Bucket::Today:
        return Section::Today;
    case mc::Bucket::ThisWeek:
        return Section::ThisWeek;
    case mc::Bucket::ThisMonth:
        return Section::ThisMonth;
    case mc::Bucket::Upcoming:
        return Section::Later;
    case mc::Bucket::Past:
        break;
    }
    return Section::None;
}

inline QString section_title(Section s) {
    switch (s) {
    case Section::Earlier:
        return QStringLiteral("EARLIER THIS WEEK");
    case Section::Today:
        return QStringLiteral("TODAY");
    case Section::ThisWeek:
        return QStringLiteral("THIS WEEK");
    case Section::ThisMonth:
        return QStringLiteral("THIS MONTH");
    case Section::Later:
        return QStringLiteral("UPCOMING");
    case Section::None:
        break;
    }
    return {};
}

/// One displayed row: a single item, or every item of one group on one day
/// (the day's BMO earnings, the day's bill auctions).
struct Row {
    QVector<Item> items;
    const Item& head() const { return items.front(); }
    bool mine() const {
        return std::any_of(items.begin(), items.end(), [](const Item& i) { return i.mine; });
    }
    Impact impact() const {
        Impact m = Impact::Low;
        for (const auto& i : items)
            m = std::max(m, i.impact);
        return m;
    }
};

struct Filter {
    bool mine_only = false;           ///< MY STOCKS: only events on the user's symbols
    Impact min_impact = Impact::Low;  ///< the impact selector: HIGH / MED+ / ALL (Low)
};

/// Earnings in a row: your names first, then by impact, then by market cap
/// (unknown size last) — the names a row can show are the ones that matter.
inline bool earnings_before(const Item& a, const Item& b) {
    if (a.mine != b.mine)
        return a.mine;
    if (a.impact != b.impact)
        return a.impact > b.impact;
    const bool fa = std::isfinite(a.size), fb = std::isfinite(b.size);
    if (fa != fb)
        return fa;
    return fa && a.size > b.size;
}

/// How many names an earnings row shows before "+N more": `max_names`, but
/// never hides one of your names.
inline int earnings_shown(const Row& r, int max_names = 3) {
    const int mine = int(std::count_if(r.items.begin(), r.items.end(), [](const Item& i) { return i.mine; }));
    return std::min<int>(int(r.items.size()), std::max(max_names, mine));
}

/// Sort, filter, group and section. Within a day: plain all-day items first,
/// then by instant (earnings by session). Grouped items join the row of the
/// first item of their group that day; inside an earnings row see
/// earnings_before. `hidden` (optional) counts, per section, the items the
/// impact selector left out (shown as "+N more").
inline QMap<Section, QVector<Row>> sectionize(QVector<Item> items, const QDate& today, const Filter& f = {},
                                              QMap<Section, int>* hidden = nullptr) {
    auto plain_untimed = [](const Item& i) { return !i.timed && i.session.isEmpty() && i.group.isEmpty(); };
    std::stable_sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
        if (a.local_date() != b.local_date())
            return a.local_date() < b.local_date();
        if (plain_untimed(a) != plain_untimed(b))
            return plain_untimed(a);
        return a.when < b.when;
    });
    QMap<Section, QVector<Row>> out;
    QHash<QString, QPair<Section, int>> group_rows; // group|date → row
    for (auto& it : items) {
        if (f.mine_only && !it.mine)
            continue;
        const Section s = section_for(it.local_date(), today);
        if (s == Section::None)
            continue;
        if (s == Section::Earlier && !(it.released || it.mine || it.impact >= Impact::Med))
            continue;
        // The selector applies to every row, your names included: a focused
        // view that quietly kept every MED/LOW event on a long watchlist
        // would not be focused.
        if (it.impact < f.min_impact) {
            if (hidden)
                ++(*hidden)[s];
            continue;
        }
        if (!it.group.isEmpty()) {
            const QString gk = it.group + QLatin1Char('|') + it.local_date().toString(Qt::ISODate);
            if (const auto g = group_rows.constFind(gk); g != group_rows.constEnd()) {
                out[g->first][g->second].items.append(std::move(it));
                continue;
            }
            group_rows.insert(gk, {s, int(out[s].size())});
        }
        out[s].append(Row{{std::move(it)}});
    }
    for (auto& rows : out) {
        for (auto& r : rows) {
            if (r.items.size() > 1 && r.head().source == Item::Source::Earnings)
                std::stable_sort(r.items.begin(), r.items.end(), earnings_before);
        }
    }
    return out;
}

} // namespace finterm::whats_on
