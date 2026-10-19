#pragma once
// The SCREENER tab's data: the JSON `screener_data.py scan` prints, parsed
// into plain structs, and the one place that says how each metric reads.
// Header-only and Qt-Core-only so the parse and the formatting rules are
// unit-tested without a widget (tests/screens/test_screener_model.cpp).
//
// The vendor-null rule holds here too: a metric the scan could not compute
// arrives as JSON null, and QJsonValue::toDouble() reads null as 0.0 — a
// confident zero earnings yield, a 0% ROE. num() returns nullopt instead, and
// every formatter turns nullopt into the placeholder.

#include "ui/formatting/NumberFormat.h"
#include "ui/theme/Theme.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <optional>

namespace fincept::screens::screener {

namespace fmt = fincept::ui::formatting;

inline std::optional<double> num(const QJsonObject& o, const QString& key) {
    const QJsonValue v = o.value(key);
    if (!v.isDouble())
        return std::nullopt; // null, missing, or not a number — absent, never 0
    const double d = v.toDouble();
    return std::isfinite(d) ? std::optional<double>(d) : std::nullopt;
}

/// What the backtest measured for a strategy (see
/// plans/research/screener_research_2026-10.md). `tested == false` means no
/// backtest could be run; only `note` is meaningful then.
struct Evidence {
    bool tested = false;
    std::optional<double> excess;   // per year vs the equal-weighted index, fraction
    std::optional<double> t;        // t-stat of the monthly excess
    std::optional<double> max_dd;   // fraction, negative
    QVector<std::optional<double>> subperiods;
    QString note;
    // trade-simulated strategies only (the Luk pullback)
    std::optional<int> trades;
    std::optional<double> win_rate, avg_R;
    QVector<double> curve; // growth of $1, monthly; NaN where absent
};

struct Strategy {
    QString id, name, family, source, summary, rank_by;
    QStringList rules, columns, symbols;
    Evidence evidence;
};

struct ScanResult {
    QString error;
    QString asof; // exchange date of the last bar
    bool intraday = false;  // today's bar is still moving — not a close
    QString prices_fetched; // "YYYY-MM-DD HH:MM ET" of the oldest price fetch used
    int stale_prices = 0;   // names served from an older cache (their download failed)
    QString plan_asof;      // the completed session the pullback plans are drawn from
    int live_applied = 0;        // names whose today's bar is TradingView's quote
    int live_on_last_close = 0;  // names without a usable quote: shown at their last real close
    std::optional<int> live_delay_min; // worst delay among the quotes used; nullopt = unknown
    QString live_price_time;     // "HH:MM ET" the quoted prices are from (send time minus delay)
    QString basis;
    int universe_n = 0;
    int priced = 0;
    std::optional<double> min_mcap;
    bool has_market = false;
    bool above50 = false;
    bool above200 = false;
    QStringList subperiods;
    QStringList curve_months;           // the evidence curves' calendar
    QVector<double> curve_ew, curve_spy; // the index lines they are drawn against
    bool meta_only = false;             // `meta`: strategies + evidence, no lists yet
    QVector<Strategy> strategies;
    QHash<QString, QJsonObject> rows;

    const Strategy* strategy(const QString& id) const {
        for (const auto& s : strategies)
            if (s.id == id)
                return &s;
        return nullptr;
    }
};

inline QVector<double> number_list(const QJsonValue& v) {
    QVector<double> out;
    for (const auto& e : v.toArray())
        out << (e.isDouble() && std::isfinite(e.toDouble()) ? e.toDouble() : std::nan(""));
    return out;
}

inline QStringList string_list(const QJsonValue& v) {
    QStringList out;
    for (const auto& e : v.toArray())
        if (e.isString())
            out << e.toString();
    return out;
}

inline ScanResult parse_scan(const QJsonObject& root) {
    ScanResult r;
    r.error = root.value(QStringLiteral("error")).toString();
    if (!r.error.isEmpty())
        return r;
    r.asof = root.value(QStringLiteral("asof")).toString();
    r.intraday = root.value(QStringLiteral("intraday")).toBool();
    r.prices_fetched = root.value(QStringLiteral("prices_fetched")).toString();
    r.stale_prices = root.value(QStringLiteral("stale_prices")).toInt();
    r.plan_asof = root.value(QStringLiteral("plan_asof")).toString();
    const QJsonObject lb = root.value(QStringLiteral("live_bar")).toObject();
    r.live_applied = lb.value(QStringLiteral("applied")).toInt();
    r.live_on_last_close = lb.value(QStringLiteral("on_last_close")).toInt();
    if (const auto d = num(lb, QStringLiteral("delayed_min")))
        r.live_delay_min = int(*d);
    r.live_price_time = lb.value(QStringLiteral("price_time")).toString();
    r.basis = root.value(QStringLiteral("evidence_basis")).toString();
    const QJsonObject u = root.value(QStringLiteral("universe")).toObject();
    r.universe_n = u.value(QStringLiteral("n")).toInt();
    r.priced = u.value(QStringLiteral("priced")).toInt();
    r.min_mcap = num(u, QStringLiteral("min_mcap"));
    const QJsonObject m = root.value(QStringLiteral("market")).toObject();
    r.has_market = m.value(QStringLiteral("above200")).isBool();
    r.above50 = m.value(QStringLiteral("above50")).toBool();
    r.above200 = m.value(QStringLiteral("above200")).toBool();
    r.subperiods = string_list(root.value(QStringLiteral("subperiods")));
    r.curve_months = string_list(root.value(QStringLiteral("curve_months")));
    r.curve_ew = number_list(root.value(QStringLiteral("curve_ew")));
    r.curve_spy = number_list(root.value(QStringLiteral("curve_spy")));
    r.meta_only = !root.contains(QStringLiteral("rows"));
    for (const auto& sv : root.value(QStringLiteral("strategies")).toArray()) {
        const QJsonObject so = sv.toObject();
        Strategy s;
        s.id = so.value(QStringLiteral("id")).toString();
        if (s.id.isEmpty())
            continue;
        s.name = so.value(QStringLiteral("name")).toString();
        s.family = so.value(QStringLiteral("family")).toString();
        s.source = so.value(QStringLiteral("source")).toString();
        s.summary = so.value(QStringLiteral("summary")).toString();
        s.rank_by = so.value(QStringLiteral("rank_by")).toString();
        s.rules = string_list(so.value(QStringLiteral("rules")));
        s.columns = string_list(so.value(QStringLiteral("columns")));
        s.symbols = string_list(so.value(QStringLiteral("symbols")));
        const QJsonObject eo = so.value(QStringLiteral("evidence")).toObject();
        s.evidence.tested = eo.value(QStringLiteral("tested")).toBool();
        s.evidence.excess = num(eo, QStringLiteral("excess"));
        s.evidence.t = num(eo, QStringLiteral("t"));
        s.evidence.max_dd = num(eo, QStringLiteral("max_dd"));
        for (const auto& x : eo.value(QStringLiteral("subperiods")).toArray()) {
            const double d = x.toDouble(std::nan(""));
            s.evidence.subperiods << (x.isDouble() && std::isfinite(d) ? std::optional<double>(d)
                                                                       : std::nullopt);
        }
        s.evidence.note = eo.value(QStringLiteral("note")).toString();
        if (const auto n = num(eo, QStringLiteral("trades")))
            s.evidence.trades = int(*n);
        s.evidence.win_rate = num(eo, QStringLiteral("win_rate"));
        s.evidence.avg_R = num(eo, QStringLiteral("avg_R"));
        s.evidence.curve = number_list(eo.value(QStringLiteral("curve")));
        r.strategies << s;
    }
    const QJsonObject rows = root.value(QStringLiteral("rows")).toObject();
    for (auto it = rows.begin(); it != rows.end(); ++it)
        r.rows.insert(it.key(), it.value().toObject());
    return r;
}

// ── How each metric reads ────────────────────────────────────────────────────

enum class Kind {
    Pct,        // fraction -> "12.3%"
    PctSigned,  // fraction -> "+12.3%", green/red
    Ratio,      // plain multiple -> "14.2"
    Int,        // "87"
    Percentile, // 0..1 -> "93"
    Money,      // "$123.45"
    Compact,    // "$4.2B"
};

struct ColumnDef {
    const char* key;
    const char* header;
    Kind kind;
    const char* tip;
};

inline const QVector<ColumnDef>& column_defs() {
    static const QVector<ColumnDef> defs = {
        {"price", "Price", Kind::Money, "Last close"},
        {"chg_1d", "1D", Kind::PctSigned, "Change on the last session"},
        {"mcap", "Mkt cap", Kind::Compact, "Market capitalisation"},
        {"rs", "RS", Kind::Int,
         "Relative-strength rating 1-99: percentile of 0.4·3M + 0.2·6M + 0.2·9M + 0.2·12M return "
         "against this universe (IBD-style)"},
        {"off_high", "Off high", Kind::PctSigned, "Distance below the 52-week closing high"},
        {"above_low", "Above low", Kind::Pct, "Distance above the 52-week closing low"},
        {"r1m", "1M", Kind::PctSigned, "Return, 21 sessions"},
        {"r3m", "3M", Kind::PctSigned, "Return, 63 sessions"},
        {"r6m", "6M", Kind::PctSigned, "Return, 126 sessions"},
        {"r12m", "12M", Kind::PctSigned, "Return, 252 sessions"},
        {"mom12_1", "12-1", Kind::PctSigned, "Return from 12 months ago to 1 month ago"},
        {"adr20", "ADR%", Kind::Pct, "Average daily range (high/low − 1), 20 sessions"},
        {"vol252", "Vol 1Y", Kind::Pct, "Annualised volatility of daily returns, past year"},
        {"dollar_vol50", "$Vol 50D", Kind::Compact, "Average daily dollar volume, 50 sessions"},
        {"eps_growth_q", "EPS Q y/y", Kind::PctSigned, "Latest-quarter diluted EPS vs a year earlier"},
        {"eps_growth_fy", "EPS FY y/y", Kind::PctSigned, "Fiscal-year diluted EPS growth"},
        {"rev_growth", "Rev y/y", Kind::PctSigned, "Revenue growth, trailing twelve months"},
        {"roe", "ROE", Kind::Pct, "Return on equity"},
        {"roic", "ROIC", Kind::Pct, "Return on invested capital (Magic Formula's return on capital)"},
        {"gross_margin", "Gross mgn", Kind::Pct, "Gross margin, trailing twelve months"},
        {"gpa", "GP/Assets", Kind::Pct, "Gross profit ÷ total assets (Novy-Marx)"},
        {"earnings_yield", "EBIT/EV", Kind::Pct, "Earnings yield: EBIT ÷ enterprise value"},
        {"pe", "P/E", Kind::Ratio, "Price ÷ earnings, trailing"},
        {"pb", "P/B", Kind::Ratio, "Price ÷ book value"},
        {"ps", "P/S", Kind::Ratio, "Price ÷ sales"},
        {"ev_ebitda", "EV/EBITDA", Kind::Ratio, "Enterprise value ÷ EBITDA"},
        {"piotroski", "F-score", Kind::Int, "Piotroski F-score 0-9, trailing twelve months"},
        {"div_yield", "Div yld", Kind::Pct, "Dividend yield"},
        {"shareholder_yield", "Sh. yield", Kind::Pct, "Dividend yield + buyback yield"},
        {"entry", "Entry", Kind::Money,
         "Buy stop at the high of the plan's session — the last COMPLETED one (his trigger is intraday)"},
        {"stop", "Stop", Kind::Money, "Below the 3-session pullback low, 1-3% under the entry"},
        {"risk_pct", "Risk", Kind::Pct, "Entry-to-stop distance: 1R"},
        {"target3", "3R", Kind::Money, "First partial: a third at entry + 3R"},
        {"target5", "5R", Kind::Money, "Second partial: a third at entry + 5R"},
        {"group_rs", "Group RS", Kind::Int,
         "Industry strength 1-99: the industry's median RS, ranked across industries (3+ names)"},
        {"group_leaders", "Grp lead", Kind::Int, "Names in the same industry with RS >= 80 (a 'hot theme')"},
        {"screens", "Screens", Kind::Int, "How many of the screens pick this stock"},
        {"value_score", "Value pctl", Kind::Percentile,
         "Average percentile of E/P, B/P, S/P, CFO/P, EBIT/EV, shareholder yield (100 = cheapest)"},
    };
    return defs;
}

inline const ColumnDef* column_def(const QString& key) {
    for (const auto& d : column_defs())
        if (key == QLatin1String(d.key))
            return &d;
    return nullptr;
}

inline QString format_value(Kind k, const std::optional<double>& v) {
    if (!v)
        return fmt::placeholder();
    switch (k) {
        case Kind::Pct:
            return fmt::format_percent(*v * 100.0, 1);
        case Kind::PctSigned:
            return fmt::format_percent(*v * 100.0, 1, true);
        case Kind::Ratio:
            return QString::number(*v, 'f', 1);
        case Kind::Int:
            return QString::number(qRound64(*v));
        case Kind::Percentile:
            return QString::number(qRound64(*v * 100.0));
        case Kind::Money:
            return fmt::format_money(*v);
        case Kind::Compact:
            return fmt::format_money(*v, QStringLiteral("USD"), true);
    }
    return fmt::placeholder();
}

/// Only signed quantities are coloured: a 30% ROE is not "good news green".
inline QString colour_for(Kind k, const std::optional<double>& v) {
    if (!v || k != Kind::PctSigned)
        return {};
    if (*v > 0)
        return ui::colors::GREEN();
    if (*v < 0)
        return ui::colors::RED();
    return {};
}

/// Shares for a trade risking `risk_frac` of `account` between entry and stop.
/// nullopt when the plan is incomplete or the stop is not below the entry.
inline std::optional<double> position_shares(double account, double risk_frac,
                                             const std::optional<double>& entry,
                                             const std::optional<double>& stop) {
    if (!entry || !stop || !(account > 0) || !(risk_frac > 0) || !(*entry > *stop))
        return std::nullopt;
    return std::floor(account * risk_frac / (*entry - *stop));
}

/// Names picked by several screens, most-picked first. RS Leaders is a subset
/// of the Trend Template by construction, so a name in both counts once.
struct OverviewRow {
    QString symbol;
    QStringList by; // strategy names
};

inline QVector<OverviewRow> overview(const ScanResult& r, int min_screens = 2) {
    QHash<QString, QStringList> by;
    for (const auto& s : r.strategies) {
        if (s.id == QLatin1String("rs_leaders"))
            continue; // nested in trend_template
        for (const auto& sym : s.symbols)
            by[sym] << s.name;
    }
    QVector<OverviewRow> out;
    for (auto it = by.begin(); it != by.end(); ++it)
        if (it.value().size() >= min_screens)
            out.push_back({it.key(), it.value()});
    std::sort(out.begin(), out.end(), [&](const OverviewRow& a, const OverviewRow& b) {
        if (a.by.size() != b.by.size())
            return a.by.size() > b.by.size();
        const double ra = num(r.rows.value(a.symbol), QStringLiteral("rs")).value_or(-1);
        const double rb = num(r.rows.value(b.symbol), QStringLiteral("rs")).value_or(-1);
        if (ra != rb)
            return ra > rb;
        return a.symbol < b.symbol;
    });
    return out;
}

} // namespace fincept::screens::screener
