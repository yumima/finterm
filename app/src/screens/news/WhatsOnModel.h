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

    QDate local_date() const { return when.toLocalTime().date(); }
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
        "        earnings of companies worth $500B+\n"
        "MED — PPI, retail sales, jobless claims, ISM, JOLTS, hourly earnings, Fed Chair remarks,\n"
        "        FOMC minutes, other releases the calendar rates high; earnings of $50B+ companies\n"
        "        and of your names; 10Y+ note/bond auctions; market holidays, early closes,\n"
        "        quad witching, quarter end; insider cluster buys in your names\n"
        "LOW — everything else (no tag)");
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
    QStringList parts;
    if (!e.actual.isEmpty())
        parts << QStringLiteral("A %1").arg(e.actual);
    if (!e.forecast.isEmpty())
        parts << QStringLiteral("F %1").arg(e.forecast);
    if (!e.previous.isEmpty())
        parts << QStringLiteral("P %1").arg(e.previous);
    return parts.join(QStringLiteral(" · "));
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
        it.title = e.country == QLatin1String("USD") ? e.title : e.country + QLatin1Char(' ') + e.title;
        it.impact = us ? macro_impact(e.title, e.importance) : (e.importance >= 3 ? Impact::Med : Impact::Low);
        it.figures = macro_figures(e);
        QString source_note = QStringLiteral("Source: ForexFactory weekly calendar (forecast = consensus)");
        const bool out_already = e.when <= now;
        if (out_already && us && e.actual.isEmpty() && fred) {
            if (const auto spec = mr::spec_for(e.title); spec && fred->contains(spec->fred_id)) {
                const QDate release_day = e.when.toTimeZone(et_zone()).date();
                const auto p = mr::printed(*spec, fred->value(spec->fred_id), release_day);
                QStringList parts;
                if (!p.actual.isEmpty()) {
                    parts << QStringLiteral("A %1").arg(p.actual);
                    it.released = true;
                } else {
                    parts << QStringLiteral("A awaiting FRED");
                }
                if (!e.forecast.isEmpty())
                    parts << QStringLiteral("F %1").arg(e.forecast);
                const QString prior = !p.prior.isEmpty() ? p.prior : e.previous;
                if (!prior.isEmpty())
                    parts << QStringLiteral("P %1").arg(prior);
                it.figures = parts.join(QStringLiteral(" · "));
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

/// Expected IPO pricings (status "upcoming") dated today or later, from the
/// ipo_calendar.py "deals" array IPO Watch uses.
inline QVector<Item> items_from_ipo_deals(const QJsonArray& deals, const QDate& today) {
    QVector<Item> out;
    for (const auto& v : deals) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("status")).toString() != QLatin1String("upcoming"))
            continue;
        const QDate d = QDate::fromString(o.value(QLatin1String("expected_date")).toString(), Qt::ISODate);
        if (!d.isValid() || d < today)
            continue;
        const QString company = o.value(QLatin1String("company")).toString().trimmed();
        const QString ticker = o.value(QLatin1String("ticker")).toString().trimmed();
        if (company.isEmpty())
            continue;
        Item it;
        it.source = Item::Source::Ipo;
        it.when = QDateTime(d, QTime(0, 0)); // local day; pricing time is not published
        it.timed = false;
        it.kind = QStringLiteral("IPO");
        it.title = ticker.isEmpty() ? company : QStringLiteral("%1 (%2)").arg(company, ticker);
        const QString range = o.value(QLatin1String("price_range")).toString().trimmed();
        const QString exch = o.value(QLatin1String("exchange")).toString().trimmed();
        QStringList fig;
        if (!range.isEmpty())
            fig << QStringLiteral("$") + range;
        if (!exch.isEmpty())
            fig << exch;
        it.figures = fig.join(QStringLiteral(" · "));
        it.key = ticker.isEmpty() ? company : ticker;
        it.deal_id = o.value(QLatin1String("deal_id")).toString();
        it.detail = QStringLiteral("Expected to price %1").arg(QLocale(QLocale::English).toString(d, "ddd d MMM yyyy"));
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
        const double cap = jnum(o, "market_cap");
        const bool is_mine = mine.contains(sym);
        if (!is_mine && !(std::isfinite(cap) && cap >= major_cap))
            continue;
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
        it.title = sym;
        it.session = session == QLatin1String("BMO") || session == QLatin1String("AMC") ? session : QString();
        const QTime nominal = it.session == QLatin1String("BMO")   ? QTime(7, 0)
                              : it.session == QLatin1String("AMC") ? QTime(16, 5)
                                                                   : QTime(12, 0);
        it.when = QDateTime(d, nominal, et_zone());
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
                it.figures += QStringLiteral(" vs %1 est").arg(eps(est));
            if (std::isfinite(sur))
                it.figures += QStringLiteral(" (%1%2%)").arg(sur >= 0 ? QStringLiteral("+") : QString(),
                                                             QString::number(sur, 'f', 1));
        } else if (std::isfinite(est)) {
            it.figures = QStringLiteral("EPS est %1").arg(eps(est));
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
        it.title = QStringLiteral("%1 goes ex-dividend").arg(sym);
        const double amt = jnum(o, "amount");
        if (std::isfinite(amt))
            it.figures = QStringLiteral("$%1/sh").arg(amt < 0.1 ? QString::number(amt, 'g', 3) : QString::number(amt, 'f', 2));
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

/// "in 2h 05m · 08:30 ET" (timed, within 24 h), "35m ago" / "2h ago" (timed,
/// out within the last 3 h), "Today 08:30 ET", "Thu 08:30 ET" (within a
/// week either side), "Nov 20 08:30 ET"; untimed:
/// "Today", "Thu", "Nov 20", plus the earnings session ("Thu AMC"). Clock
/// times are US Eastern — the release clock the market quotes.
inline QString when_label(const Item& it, const QDateTime& now) {
    if (!it.when.isValid())
        return QStringLiteral("—");
    const QDate today = now.toLocalTime().date();
    const QDate d = it.local_date();
    const QLocale en(QLocale::English);
    QString day;
    if (d == today)
        day = QStringLiteral("Today");
    else if (d == today.addDays(-1))
        day = QStringLiteral("Yest");
    else if (std::abs(today.daysTo(d)) < 7)
        day = en.toString(d, QStringLiteral("ddd"));
    else
        day = en.toString(d, QStringLiteral("MMM d"));
    if (!it.timed)
        return it.session.isEmpty() ? day : day + QLatin1Char(' ') + it.session;
    const QString clock = it.when.toTimeZone(et_zone()).toString(QStringLiteral("HH:mm")) + QStringLiteral(" ET");
    const qint64 secs = now.secsTo(it.when);
    auto span = [](qint64 s) {
        const qint64 h = s / 3600, m = (s % 3600) / 60;
        if (h == 0)
            return QStringLiteral("%1m").arg(std::max<qint64>(m, 1));
        return m == 0 ? QStringLiteral("%1h").arg(h) : QStringLiteral("%1h %2m").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    };
    if (secs >= 0 && secs < 24 * 3600)
        return QStringLiteral("in %1 · %2").arg(span(secs), clock);
    if (secs < 0 && d == today && -secs < 3 * 3600) // just out; older reads better as a clock
        return QStringLiteral("%1 ago").arg(span(-secs));
    return day + QLatin1Char(' ') + clock;
}

/// First HIGH item still ahead (timed: after now; untimed: from today on).
inline std::optional<Item> next_high(const QVector<Item>& items, const QDateTime& now) {
    const QDate today = now.toLocalTime().date();
    std::optional<Item> best;
    for (const auto& it : items) {
        if (it.impact != Impact::High)
            continue;
        const bool ahead = it.timed ? it.when > now : it.local_date() > today ||
                                                          (it.local_date() == today && it.when > now);
        if (!ahead)
            continue;
        if (!best || it.when < best->when)
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
    bool mine_only = false; ///< YOUR NAMES
};

/// Sort, filter, group and section. Within a day: plain all-day items first,
/// then by instant (earnings by session). Grouped items join the row of the
/// first item of their group that day; inside an earnings row the largest
/// impact comes first.
inline QMap<Section, QVector<Row>> sectionize(QVector<Item> items, const QDate& today, const Filter& f = {}) {
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
                std::stable_sort(r.items.begin(), r.items.end(),
                                 [](const Item& a, const Item& b) { return a.impact > b.impact; });
        }
    }
    return out;
}

/// Items grouped by horizon, each group chronological; past items dropped.
inline QMap<mc::Bucket, QVector<Item>> bucketize(QVector<Item> items, const QDate& today) {
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.local_date() != b.local_date())
            return a.local_date() < b.local_date();
        // Untimed (all-day) items first within a day, then by clock.
        if (a.timed != b.timed)
            return !a.timed;
        return a.when < b.when;
    });
    QMap<mc::Bucket, QVector<Item>> out;
    for (auto& it : items) {
        const auto b = mc::bucket_for(it.local_date(), today);
        if (b != mc::Bucket::Past)
            out[b].append(std::move(it));
    }
    return out;
}

} // namespace finterm::whats_on
