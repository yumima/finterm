// src/screens/equity_research/ReportModel.h
#pragma once
// The ER Report tab's numbers: series lifted out of the statements, the
// ratios and growth rates derived from them, and the findings (strengths and
// risks) read off the data by fixed, stated rules.
//
// Pure (QtCore only) so every figure the report prints is unit-tested. Absent
// is NaN throughout — a bank has no gross profit, which is not a gross profit
// of zero — and every derived figure is NaN unless all of its inputs exist.
//
// Nothing here is a forecast. The findings describe what the data already
// shows; the thresholds are named in each finding so a reader can disagree.

#include "services/equity/EquityResearchModels.h"

#include <QDate>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>

namespace finterm::screens::report {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline bool ok(double v) {
    return std::isfinite(v);
}

inline double jnum(const QJsonObject& o, const QString& key) {
    const QJsonValue v = o.value(key);
    return v.isDouble() ? v.toDouble() : kNaN; // null stays NaN, never 0
}

/// Figures for axis labels, tiles and prompts: 1.2T / 345.6B / 12.3M, sign kept.
inline QString compact(double v, int dp = 1) {
    if (!std::isfinite(v))
        return QStringLiteral("—");
    if (v == 0.0)
        return QStringLiteral("0");
    const double a = std::abs(v);
    const QString sign = v < 0 ? QStringLiteral("−") : QString();
    const QLocale loc(QLocale::English);
    if (a >= 1e12)
        return sign + loc.toString(a / 1e12, 'f', dp) + QLatin1Char('T');
    if (a >= 1e9)
        return sign + loc.toString(a / 1e9, 'f', dp) + QLatin1Char('B');
    if (a >= 1e6)
        return sign + loc.toString(a / 1e6, 'f', dp) + QLatin1Char('M');
    if (a >= 1e4)
        return sign + loc.toString(a / 1e3, 'f', dp) + QLatin1Char('K');
    return sign + loc.toString(a, 'f', a >= 100 ? 0 : a >= 10 ? 1 : 2);
}

/// a ÷ b as a percent; NaN unless both exist and b is positive.
inline double pct_of(double a, double b) {
    return ok(a) && ok(b) && b > 0 ? a / b * 100.0 : kNaN;
}

/// Change from `prev` to `cur` in percent. A base that is zero or negative
/// has no meaningful growth rate (a loss turning to a profit is not "+300%").
inline double growth_pct(double cur, double prev) {
    return ok(cur) && ok(prev) && prev > 0 ? (cur - prev) / prev * 100.0 : kNaN;
}

/// Compound annual growth over `years`; needs both ends positive.
inline double cagr_pct(double first, double last, double years) {
    return ok(first) && ok(last) && first > 0 && last > 0 && years > 0
               ? (std::pow(last / first, 1.0 / years) - 1.0) * 100.0
               : kNaN;
}

inline double median(QVector<double> v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !ok(x); }), v.end());
    if (v.isEmpty())
        return kNaN;
    std::sort(v.begin(), v.end());
    const int n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

inline QString p1(double v) { // "12.3%"
    return QLocale(QLocale::English).toString(v, 'f', 1) + QLatin1Char('%');
}
inline QString sp1(double v) { // "+12.3%"
    return (v > 0 ? QStringLiteral("+") : v < 0 ? QStringLiteral("−") : QString()) +
           QLocale(QLocale::English).toString(std::abs(v), 'f', 1) + QLatin1Char('%');
}
inline QString pts(double v) { // "+2.1 pts"
    return (v > 0 ? QStringLiteral("+") : v < 0 ? QStringLiteral("−") : QString()) +
           QLocale(QLocale::English).toString(std::abs(v), 'f', 1) + QStringLiteral(" pts");
}

// ── Extras (daemon `report_extras`) ─────────────────────────────────────────

struct Quarter {
    QString period; ///< fiscal quarter end, ISO
    double revenue = kNaN, gross_profit = kNaN, operating_income = kNaN, net_income = kNaN, eps = kNaN;
    double rd = kNaN, ocf = kNaN, capex = kNaN, fcf = kNaN, buybacks = kNaN, dividends = kNaN;

    QString label() const { // "Jul 26"
        const QDate d = QDate::fromString(period, Qt::ISODate);
        return d.isValid() ? QLocale::c().toString(d, QStringLiteral("MMM yy")) : period;
    }
};

struct AnalystAction {
    QDate date;
    QString firm, to_grade, from_grade, action; ///< action: up | down | main | init | reit
};

struct RecBucket {
    QString period; ///< "0m" now, "-1m" a month ago …
    int strong_buy = -1, buy = -1, hold = -1, sell = -1, strong_sell = -1; ///< -1 = not reported
    int total() const {
        return std::max(0, strong_buy) + std::max(0, buy) + std::max(0, hold) + std::max(0, sell) +
               std::max(0, strong_sell);
    }
};

struct Extras {
    QString symbol;
    QString financial_currency; ///< the currency the statements are in; may differ from the quote's
    qint64 as_of = 0;
    QVector<Quarter> quarters; ///< newest first
    QVector<AnalystAction> actions; ///< newest first
    QVector<RecBucket> rec_trend;
    QStringList problems; ///< parts that failed, in words
};

/// The quarter a year before `quarters[i]` (newest first), found by its
/// period end — never by position: a quarter the vendor omits would otherwise
/// turn "a year earlier" into fifteen months. -1 when it is not listed.
inline int year_ago_index(const QVector<Quarter>& quarters, int i) {
    if (i < 0 || i >= quarters.size())
        return -1;
    const QDate d = QDate::fromString(quarters[i].period, Qt::ISODate);
    if (!d.isValid())
        return -1;
    for (int j = i + 1; j < quarters.size(); ++j) {
        const QDate e = QDate::fromString(quarters[j].period, Qt::ISODate);
        // Fiscal quarters end on drifting days (52/53-week years): two weeks' grace.
        if (e.isValid() && std::abs(e.daysTo(d) - 365) <= 14)
            return j;
    }
    return -1;
}

/// Revenue of the latest quarter against the same quarter a year earlier.
inline double latest_quarter_yoy_pct(const QVector<Quarter>& quarters) {
    const int j = year_ago_index(quarters, 0);
    return j < 0 ? kNaN : growth_pct(quarters[0].revenue, quarters[j].revenue);
}

inline Extras parse_extras(const QJsonObject& o) {
    Extras x;
    x.symbol = o.value(QLatin1String("symbol")).toString();
    x.financial_currency = o.value(QLatin1String("financial_currency")).toString();
    x.as_of = static_cast<qint64>(o.value(QLatin1String("as_of")).toDouble());
    for (const auto& v : o.value(QLatin1String("quarters")).toArray()) {
        const QJsonObject r = v.toObject();
        Quarter q;
        q.period = r.value(QLatin1String("period")).toString();
        if (q.period.isEmpty())
            continue;
        q.revenue = jnum(r, QStringLiteral("revenue"));
        q.gross_profit = jnum(r, QStringLiteral("gross_profit"));
        q.operating_income = jnum(r, QStringLiteral("operating_income"));
        q.net_income = jnum(r, QStringLiteral("net_income"));
        q.eps = jnum(r, QStringLiteral("eps"));
        q.rd = jnum(r, QStringLiteral("rd"));
        q.ocf = jnum(r, QStringLiteral("ocf"));
        q.capex = jnum(r, QStringLiteral("capex"));
        q.fcf = jnum(r, QStringLiteral("fcf"));
        q.buybacks = jnum(r, QStringLiteral("buybacks"));
        q.dividends = jnum(r, QStringLiteral("dividends"));
        x.quarters.append(q);
    }
    for (const auto& v : o.value(QLatin1String("actions")).toArray()) {
        const QJsonObject r = v.toObject();
        AnalystAction a;
        a.date = QDate::fromString(r.value(QLatin1String("date")).toString(), Qt::ISODate);
        a.firm = r.value(QLatin1String("firm")).toString();
        a.to_grade = r.value(QLatin1String("to_grade")).toString();
        a.from_grade = r.value(QLatin1String("from_grade")).toString();
        a.action = r.value(QLatin1String("action")).toString();
        if (a.date.isValid() && !a.firm.isEmpty())
            x.actions.append(a);
    }
    for (const auto& v : o.value(QLatin1String("rec_trend")).toArray()) {
        const QJsonObject r = v.toObject();
        RecBucket b;
        b.period = r.value(QLatin1String("period")).toString();
        auto count = [&r](const char* k) {
            const QJsonValue j = r.value(QLatin1String(k));
            return j.isDouble() ? j.toInt() : -1;
        };
        b.strong_buy = count("strong_buy");
        b.buy = count("buy");
        b.hold = count("hold");
        b.sell = count("sell");
        b.strong_sell = count("strong_sell");
        if (!b.period.isEmpty() && b.total() > 0)
            x.rec_trend.append(b);
    }
    for (const char* part : {"quarters", "actions", "rec_trend"}) {
        const QString e = o.value(QLatin1String(part) + QStringLiteral("_error")).toString();
        if (!e.isEmpty())
            x.problems << QStringLiteral("%1: %2").arg(QLatin1String(part), e);
    }
    return x;
}

// ── Filings (report_filings.py) ─────────────────────────────────────────────

struct FilingSection {
    QString text;
    int length = 0; ///< size of the whole section in the filing
    bool truncated = false;
};

struct Filing {
    QString form, filing_date, period, accession;
    QString url; ///< the filing's index page on sec.gov
    FilingSection business, risk_factors, mdna;
    bool valid() const { return !form.isEmpty(); }
    QString cite() const { // "10-K filed 2026-02-25"
        return valid() ? QStringLiteral("%1 filed %2").arg(form, filing_date) : QString();
    }
};

/// One slice of the year's revenue, as the company's annual report tags it.
struct BreakdownRow {
    QString label;
    double value = kNaN;
    double prior = kNaN; ///< the same slice a year earlier; NaN when not reported then
    double growth_pct() const { return report::growth_pct(value, prior); }
};

/// Revenue cut one way — by segment, by product or by geography.
struct Breakdown {
    QString axis; ///< "segment" | "product" | "geography"
    QString period, prior_period;
    double coverage_pct = kNaN; ///< the rows' share of the year's revenue
    QVector<BreakdownRow> rows; ///< largest first
    QString title() const {
        return axis == QLatin1String("geography") ? QStringLiteral("Revenue by geography")
               : axis == QLatin1String("segment") ? QStringLiteral("Revenue by segment")
                                                  : QStringLiteral("Revenue by product");
    }
    /// The largest slice's share of the rows, percent.
    double top_share_pct() const {
        double total = 0;
        for (const auto& r : rows)
            if (ok(r.value))
                total += r.value;
        return rows.isEmpty() || !(total > 0) ? kNaN : rows.first().value / total * 100.0;
    }
};

/// A customer the filing says accounts for a disclosed share of revenue.
struct Customer {
    QString label; ///< as filed — usually anonymous ("Customer A")
    double pct = kNaN;
    QString scope; ///< the segment the share is of, when not the whole company
    QString period;
};

struct Filings {
    Filing annual, quarterly;
    QString annual_error, quarterly_error;
    QVector<Breakdown> breakdowns;
    QVector<Customer> customers;
    QString breakdown_source; ///< "10-K filed 2026-02-25"
    bool any() const { return annual.valid() || quarterly.valid(); }
    const Breakdown* breakdown(const QString& axis) const {
        for (const auto& b : breakdowns)
            if (b.axis == axis)
                return &b;
        return nullptr;
    }
};

inline Filings parse_filings(const QJsonObject& o) {
    auto read = [](const QJsonObject& f) {
        Filing out;
        out.form = f.value(QLatin1String("form")).toString();
        out.filing_date = f.value(QLatin1String("filing_date")).toString();
        out.period = f.value(QLatin1String("period")).toString();
        out.accession = f.value(QLatin1String("accession")).toString();
        out.url = f.value(QLatin1String("url")).toString();
        const QJsonObject secs = f.value(QLatin1String("sections")).toObject();
        auto sec = [&secs](const char* k) {
            const QJsonObject s = secs.value(QLatin1String(k)).toObject();
            return FilingSection{s.value(QLatin1String("text")).toString(), s.value(QLatin1String("length")).toInt(),
                                 s.value(QLatin1String("truncated")).toBool()};
        };
        out.business = sec("business");
        out.risk_factors = sec("risk_factors");
        out.mdna = sec("mdna");
        return out;
    };
    Filings out;
    if (o.value(QLatin1String("annual")).isObject())
        out.annual = read(o.value(QLatin1String("annual")).toObject());
    if (o.value(QLatin1String("quarterly")).isObject())
        out.quarterly = read(o.value(QLatin1String("quarterly")).toObject());
    out.annual_error = o.value(QLatin1String("annual_error")).toString();
    out.quarterly_error = o.value(QLatin1String("quarterly_error")).toString();
    out.breakdown_source = o.value(QLatin1String("breakdown_source")).toString();
    for (const auto& v : o.value(QLatin1String("revenue_breakdown")).toArray()) {
        const QJsonObject b = v.toObject();
        Breakdown bd;
        bd.axis = b.value(QLatin1String("axis")).toString();
        bd.period = b.value(QLatin1String("period")).toString();
        bd.prior_period = b.value(QLatin1String("prior_period")).toString();
        bd.coverage_pct = jnum(b, QStringLiteral("coverage_pct"));
        for (const auto& rv : b.value(QLatin1String("rows")).toArray()) {
            const QJsonObject r = rv.toObject();
            BreakdownRow row{r.value(QLatin1String("label")).toString(), jnum(r, QStringLiteral("value")),
                             jnum(r, QStringLiteral("prior"))};
            if (!row.label.isEmpty() && ok(row.value) && row.value > 0)
                bd.rows.append(row);
        }
        if (bd.rows.size() >= 2)
            out.breakdowns.append(bd);
    }
    for (const auto& v : o.value(QLatin1String("customers")).toArray()) {
        const QJsonObject c = v.toObject();
        Customer cu{c.value(QLatin1String("label")).toString(), jnum(c, QStringLiteral("pct")),
                    c.value(QLatin1String("scope")).toString(), c.value(QLatin1String("period")).toString()};
        if (!cu.label.isEmpty() && ok(cu.pct) && cu.pct > 0)
            out.customers.append(cu);
    }
    return out;
}

// ── Fiscal years, from the annual statements ────────────────────────────────

struct FiscalYear {
    QString period; ///< fiscal year end, ISO
    double revenue = kNaN, gross_profit = kNaN, operating_income = kNaN, net_income = kNaN, ebitda = kNaN;
    double eps = kNaN, rd = kNaN, sbc = kNaN;
    double ocf = kNaN, capex = kNaN, fcf = kNaN, buybacks = kNaN, dividends = kNaN, acquisitions = kNaN;
    double cash = kNaN, debt = kNaN, equity = kNaN, total_assets = kNaN, shares = kNaN;

    QString label() const { // "FY25"
        const QDate d = QDate::fromString(period.left(10), Qt::ISODate);
        return d.isValid() ? QStringLiteral("FY%1").arg(d.year() % 100, 2, 10, QLatin1Char('0')) : period.left(4);
    }
    double gross_margin() const { return pct_of(gross_profit, revenue); }
    double operating_margin() const { return pct_of(operating_income, revenue); }
    double net_margin() const { return pct_of(net_income, revenue); }
    double fcf_margin() const { return pct_of(fcf, revenue); }
};

/// First of `names` the statement reports for this period.
inline double line(const QJsonObject& stmt, std::initializer_list<const char*> names) {
    for (const char* n : names) {
        const double v = jnum(stmt, QLatin1String(n));
        if (ok(v))
            return v;
    }
    return kNaN;
}

/// Fiscal years oldest first, joined across the three statements by period.
/// A year needs revenue or net income to be listed (yfinance pads the oldest
/// column with an all-empty year).
inline QVector<FiscalYear> fiscal_years(const services::equity::FinancialsData& fd) {
    QVector<FiscalYear> out;
    auto stmt_for = [](const QVector<QPair<QString, QJsonObject>>& rows, const QString& period) {
        for (const auto& r : rows)
            if (r.first == period)
                return r.second;
        return QJsonObject{};
    };
    for (const auto& inc : fd.income_statement) {
        FiscalYear y;
        y.period = inc.first.left(10);
        const QJsonObject& i = inc.second;
        const QJsonObject b = stmt_for(fd.balance_sheet, inc.first);
        const QJsonObject c = stmt_for(fd.cash_flow, inc.first);
        y.revenue = line(i, {"Total Revenue", "Operating Revenue"});
        y.gross_profit = line(i, {"Gross Profit"});
        y.operating_income = line(i, {"Operating Income"});
        y.net_income = line(i, {"Net Income", "Net Income Common Stockholders"});
        y.ebitda = line(i, {"EBITDA", "Normalized EBITDA"});
        y.eps = line(i, {"Diluted EPS", "Basic EPS"});
        y.rd = line(i, {"Research And Development"});
        y.sbc = line(c, {"Stock Based Compensation"});
        y.ocf = line(c, {"Operating Cash Flow"});
        y.capex = line(c, {"Capital Expenditure"});
        y.fcf = line(c, {"Free Cash Flow"});
        y.buybacks = line(c, {"Repurchase Of Capital Stock"});
        y.dividends = line(c, {"Cash Dividends Paid", "Common Stock Dividend Paid"});
        y.acquisitions = line(c, {"Purchase Of Business", "Net Business Purchase And Sale"});
        y.cash = line(b, {"Cash Cash Equivalents And Short Term Investments", "Cash And Cash Equivalents"});
        y.debt = line(b, {"Total Debt"});
        y.equity = line(b, {"Stockholders Equity", "Common Stock Equity"});
        y.total_assets = line(b, {"Total Assets"});
        y.shares = line(b, {"Ordinary Shares Number", "Share Issued"});
        if (ok(y.revenue) || ok(y.net_income))
            out.append(y);
    }
    std::sort(out.begin(), out.end(), [](const FiscalYear& a, const FiscalYear& b) { return a.period < b.period; });
    return out;
}

/// Money a company put to work in a year, as positive amounts. Cash-flow
/// statements carry outflows as negatives.
inline double outflow(double v) {
    return ok(v) ? std::abs(v) : kNaN;
}

// ── Peers ───────────────────────────────────────────────────────────────────

struct PeerStat {
    QString metric;
    double company = kNaN;
    double peer_median = kNaN;
    int peers = 0; ///< peers the median is taken over
    /// Company minus median, as a percent of the median (multiples) —
    /// NaN unless both exist and the median is positive.
    double premium_pct() const {
        return ok(company) && ok(peer_median) && peer_median > 0 ? (company - peer_median) / peer_median * 100.0 : kNaN;
    }
};

inline PeerStat peer_stat(const QString& metric, double company, const QVector<double>& peer_values) {
    PeerStat s;
    s.metric = metric;
    s.company = company;
    s.peer_median = median(peer_values);
    for (double v : peer_values)
        if (ok(v))
            ++s.peers;
    return s;
}

// ── Earnings track record ───────────────────────────────────────────────────

struct BeatRecord {
    int quarters = 0; ///< reported quarters with a usable surprise
    int beats = 0;
    double median_surprise_pct = kNaN;
    double median_abs_reaction_pct = kNaN; ///< typical size of the move on the print
    int reactions = 0;
};

/// Over the last `window` REPORTED quarters. A surprise flagged as an
/// accounting artefact (GAAP actual against an adjusted estimate) is left out,
/// as the Earnings tab does.
inline BeatRecord beat_record(const services::equity::EarningsAnalysis& a, int window = 8) {
    BeatRecord r;
    QVector<double> surprises, moves;
    int seen = 0;
    for (const auto& p : a.history) { // newest first
        if (p.is_estimate || !p.eps_actual)
            continue;
        if (++seen > window)
            break;
        if (p.surprise_pct && !p.surprise_suspect) {
            ++r.quarters;
            if (*p.surprise_pct > 0)
                ++r.beats;
            surprises.append(*p.surprise_pct);
        }
        if (p.reaction_pct)
            moves.append(std::abs(*p.reaction_pct));
    }
    r.median_surprise_pct = median(surprises);
    r.median_abs_reaction_pct = median(moves);
    r.reactions = moves.size();
    return r;
}

/// Where the full-year consensus EPS sits against 90 days ago, percent.
inline double estimate_drift_pct(const services::equity::EarningsAnalysis& a, const QString& period) {
    for (const auto& t : a.trend)
        if (t.period == period && t.current && t.d90 && *t.d90 > 0)
            return (*t.current - *t.d90) / *t.d90 * 100.0;
    return kNaN;
}

// ── How the stock has behaved ───────────────────────────────────────────────

struct RiskProfile {
    int sessions = 0;             ///< daily returns the figures are taken over
    double return_pct = kNaN;     ///< first close to last
    double volatility_pct = kNaN; ///< standard deviation of daily returns, annualised
    double max_drawdown_pct = kNaN; ///< deepest fall from a prior high, negative
    double beta = kNaN;           ///< sensitivity of its daily returns to the benchmark's
    double correlation = kNaN;    ///< of its daily returns with the benchmark's
    int paired_sessions = 0;      ///< sessions both traded
};

/// Realised over the candles given (a year of daily bars). History, not a
/// forecast of risk. Needs sixty returns; beta and correlation need sixty
/// sessions on which both the stock and the benchmark traded.
inline RiskProfile risk_profile(const QVector<services::equity::Candle>& stock,
                                const QVector<services::equity::Candle>& bench) {
    RiskProfile r;
    QVector<QPair<QDate, double>> px;
    for (const auto& c : stock)
        if (c.close > 0)
            px.append({c.date(), c.close});
    if (px.size() < 61)
        return r;
    QVector<double> rets;
    double peak = px.first().second, worst = 0.0;
    for (int i = 1; i < px.size(); ++i) {
        rets.append(px[i].second / px[i - 1].second - 1.0);
        peak = std::max(peak, px[i].second);
        worst = std::min(worst, px[i].second / peak - 1.0);
    }
    r.sessions = rets.size();
    r.return_pct = (px.last().second / px.first().second - 1.0) * 100.0;
    r.max_drawdown_pct = worst * 100.0;
    double mean = 0;
    for (double x : rets)
        mean += x;
    mean /= rets.size();
    double var = 0;
    for (double x : rets)
        var += (x - mean) * (x - mean);
    var /= rets.size() - 1;
    r.volatility_pct = std::sqrt(var) * std::sqrt(252.0) * 100.0;

    // Returns paired by session date: a day only one of them traded is no pair.
    QHash<QDate, double> bclose;
    for (const auto& c : bench)
        if (c.close > 0)
            bclose.insert(c.date(), c.close);
    QVector<double> a, b;
    for (int i = 1; i < px.size(); ++i) {
        const double b1 = bclose.value(px[i].first, kNaN), b0 = bclose.value(px[i - 1].first, kNaN);
        if (ok(b1) && ok(b0)) {
            a.append(px[i].second / px[i - 1].second - 1.0);
            b.append(b1 / b0 - 1.0);
        }
    }
    r.paired_sessions = a.size();
    if (a.size() >= 60) {
        double ma = 0, mb = 0;
        for (int i = 0; i < a.size(); ++i) {
            ma += a[i];
            mb += b[i];
        }
        ma /= a.size();
        mb /= b.size();
        double cov = 0, va = 0, vb = 0;
        for (int i = 0; i < a.size(); ++i) {
            cov += (a[i] - ma) * (b[i] - mb);
            va += (a[i] - ma) * (a[i] - ma);
            vb += (b[i] - mb) * (b[i] - mb);
        }
        if (vb > 0 && va > 0) {
            r.beta = cov / vb;
            r.correlation = cov / std::sqrt(va * vb);
        }
    }
    return r;
}

/// Correlation of two instruments' daily returns over the sessions both
/// traded; NaN with fewer than sixty such sessions.
struct ReturnLink {
    QString label;
    double correlation = kNaN;
    int sessions = 0;
};

inline ReturnLink return_link(const QString& label, const QVector<services::equity::Candle>& a,
                              const QVector<services::equity::Candle>& b) {
    ReturnLink out;
    out.label = label;
    QHash<QDate, double> bclose;
    for (const auto& c : b)
        if (c.close > 0)
            bclose.insert(c.date(), c.close);
    QVector<double> ra, rb;
    for (int i = 1; i < a.size(); ++i) {
        const double b1 = bclose.value(a[i].date(), kNaN), b0 = bclose.value(a[i - 1].date(), kNaN);
        if (a[i].close > 0 && a[i - 1].close > 0 && ok(b1) && ok(b0)) {
            ra.append(a[i].close / a[i - 1].close - 1.0);
            rb.append(b1 / b0 - 1.0);
        }
    }
    out.sessions = ra.size();
    if (ra.size() < 60)
        return out;
    double ma = 0, mb = 0;
    for (int i = 0; i < ra.size(); ++i) {
        ma += ra[i];
        mb += rb[i];
    }
    ma /= ra.size();
    mb /= rb.size();
    double cov = 0, va = 0, vb = 0;
    for (int i = 0; i < ra.size(); ++i) {
        cov += (ra[i] - ma) * (rb[i] - mb);
        va += (ra[i] - ma) * (ra[i] - ma);
        vb += (rb[i] - mb) * (rb[i] - mb);
    }
    if (va > 0 && vb > 0)
        out.correlation = cov / std::sqrt(va * vb);
    return out;
}

// ── The latest quarter against the one before, and a year earlier ───────────

struct QuarterMetric {
    QString name;
    double latest = kNaN;
    double vs_prior = kNaN;    ///< change on the quarter before
    double vs_year_ago = kNaN; ///< change on the same quarter a year earlier
    bool in_points = false;    ///< a margin: changes are percentage points, not percent
    bool is_money = false;
};

struct QuarterReview {
    QString period, prior_period, year_ago_period;
    QVector<QuarterMetric> metrics;
    bool valid() const { return !metrics.isEmpty(); }
};

/// Sequential changes carry the company's seasonality (a March quarter is
/// "down" on December every year); the year-ago column is the one free of it.
/// Both are shown, labelled, and neither is computed across a missing quarter.
inline QuarterReview review_quarter(const QVector<Quarter>& q) {
    QuarterReview r;
    if (q.isEmpty())
        return r;
    const Quarter& now = q[0];
    r.period = now.period;
    // The quarter before by date as well: ~3 months earlier, not merely the next row.
    const Quarter* prev = nullptr;
    if (q.size() > 1) {
        const QDate d0 = QDate::fromString(now.period, Qt::ISODate), d1 = QDate::fromString(q[1].period, Qt::ISODate);
        if (d0.isValid() && d1.isValid() && std::abs(d1.daysTo(d0) - 91) <= 14)
            prev = &q[1];
    }
    const int yi = year_ago_index(q, 0);
    const Quarter* year = yi >= 0 ? &q[yi] : nullptr;
    if (prev)
        r.prior_period = prev->period;
    if (year)
        r.year_ago_period = year->period;
    auto money = [&](const QString& name, double Quarter::*f) {
        QuarterMetric m{name, now.*f, prev ? growth_pct(now.*f, prev->*f) : kNaN, year ? growth_pct(now.*f, year->*f) : kNaN,
                        false, true};
        if (ok(m.latest))
            r.metrics.append(m);
    };
    auto margin = [&](const QString& name, double Quarter::*num) {
        const double cur = pct_of(now.*num, now.revenue);
        auto delta = [&](const Quarter* o) {
            const double then = o ? pct_of(o->*num, o->revenue) : kNaN;
            return ok(cur) && ok(then) ? cur - then : kNaN;
        };
        QuarterMetric m{name, cur, delta(prev), delta(year), true, false};
        if (ok(m.latest))
            r.metrics.append(m);
    };
    money(QStringLiteral("Revenue"), &Quarter::revenue);
    margin(QStringLiteral("Gross margin"), &Quarter::gross_profit);
    margin(QStringLiteral("Operating margin"), &Quarter::operating_income);
    money(QStringLiteral("Net income"), &Quarter::net_income);
    {
        QuarterMetric m{QStringLiteral("Diluted EPS"), now.eps, prev ? growth_pct(now.eps, prev->eps) : kNaN,
                        year ? growth_pct(now.eps, year->eps) : kNaN, false, false};
        if (ok(m.latest))
            r.metrics.append(m);
    }
    money(QStringLiteral("Free cash flow"), &Quarter::fcf);
    return r;
}

// ── The multiple against its own past ───────────────────────────────────────

struct MultiplePoint {
    QString label; ///< "FY24", "Now"
    double pe = kNaN;
};

/// The price/earnings multiple at each fiscal year end — that day's close over
/// that year's diluted EPS — so today's can be read against the company's own
/// history rather than only against other companies. A year with a loss has
/// no multiple. `candles` must reach back to the years asked about; a year
/// end with no bar within ten days before it is left out.
inline QVector<MultiplePoint> pe_history(const QVector<FiscalYear>& years, const QVector<services::equity::Candle>& candles,
                                         double pe_now) {
    QVector<MultiplePoint> out;
    for (const auto& y : years) {
        const QDate end = QDate::fromString(y.period.left(10), Qt::ISODate);
        if (!end.isValid() || !ok(y.eps) || y.eps <= 0)
            continue;
        double close = kNaN;
        for (const auto& c : candles) { // oldest first
            const QDate d = c.date();
            if (d > end)
                break;
            if (d.daysTo(end) <= 10 && c.close > 0)
                close = c.close;
        }
        if (ok(close))
            out.append({y.label(), close / y.eps});
    }
    if (!out.isEmpty() && ok(pe_now) && pe_now > 0)
        out.append({QStringLiteral("Now"), pe_now});
    return out;
}

// ── What kind of company it is ──────────────────────────────────────────────

/// Some of the report's yardsticks mean nothing for some businesses: a bank's
/// "debt" is its raw material and it has no gross margin or free cash flow in
/// the industrial sense; a REIT pays out its cash and is judged on funds from
/// operations, which no source here carries. Those sections and rules are
/// left out for them, and the report says so, rather than drawn empty or —
/// worse — read as a weakness.
struct SectorProfile {
    bool balance_sheet_business = false; ///< banks, insurers, brokers: leverage and FCF rules do not apply
    bool reit = false;
    double high_roe_pct = 15.0; ///< what counts as a high return on equity here
    QString note;               ///< said in the report when something is left out
};

inline SectorProfile sector_profile(const QString& sector, const QString& industry) {
    SectorProfile p;
    const QString s = sector.toLower(), i = industry.toLower();
    if (s.contains(QLatin1String("financial")) &&
        (i.contains(QLatin1String("bank")) || i.contains(QLatin1String("insurance")) || i.contains(QLatin1String("capital markets")) ||
         i.contains(QLatin1String("credit")) || i.contains(QLatin1String("mortgage")) || i.contains(QLatin1String("asset management")))) {
        p.balance_sheet_business = true;
        p.high_roe_pct = 12.0;
        p.note = QStringLiteral("A financial company: borrowing is its business, so the debt, free-cash-flow and gross-margin "
                                "readings used for other companies are left out.");
    } else if (s.contains(QLatin1String("real estate")) && i.contains(QLatin1String("reit"))) {
        p.reit = true;
        p.balance_sheet_business = true;
        p.note = QStringLiteral("A REIT: it distributes most of its cash and is judged on funds from operations, which are "
                                "not in this data, so the free-cash-flow and leverage readings are left out.");
    }
    return p;
}

// ── A report, remembered ────────────────────────────────────────────────────

/// What a report said on the day it was built — enough to say, next time,
/// what has changed since.
struct Snapshot {
    QDate date;
    double price = kNaN, target_mean = kNaN, eps_fy0 = kNaN, eps_fy1 = kNaN, forward_pe = kNaN;
    QString latest_quarter;
    int analysts_buy = -1, analysts_total = -1;
    QStringList strengths, risks; ///< finding titles
    bool valid() const { return date.isValid(); }
};

inline QJsonObject to_json(const Snapshot& s) {
    auto n = [](double v) { return ok(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null); };
    return {{"date", s.date.toString(Qt::ISODate)},
            {"price", n(s.price)},
            {"target_mean", n(s.target_mean)},
            {"eps_fy0", n(s.eps_fy0)},
            {"eps_fy1", n(s.eps_fy1)},
            {"forward_pe", n(s.forward_pe)},
            {"latest_quarter", s.latest_quarter},
            {"analysts_buy", s.analysts_buy},
            {"analysts_total", s.analysts_total},
            {"strengths", QJsonArray::fromStringList(s.strengths)},
            {"risks", QJsonArray::fromStringList(s.risks)}};
}

inline Snapshot snapshot_from_json(const QJsonObject& o) {
    Snapshot s;
    s.date = QDate::fromString(o.value(QLatin1String("date")).toString(), Qt::ISODate);
    s.price = jnum(o, QStringLiteral("price"));
    s.target_mean = jnum(o, QStringLiteral("target_mean"));
    s.eps_fy0 = jnum(o, QStringLiteral("eps_fy0"));
    s.eps_fy1 = jnum(o, QStringLiteral("eps_fy1"));
    s.forward_pe = jnum(o, QStringLiteral("forward_pe"));
    s.latest_quarter = o.value(QLatin1String("latest_quarter")).toString();
    s.analysts_buy = o.value(QLatin1String("analysts_buy")).toInt(-1);
    s.analysts_total = o.value(QLatin1String("analysts_total")).toInt(-1);
    for (const auto& v : o.value(QLatin1String("strengths")).toArray())
        s.strengths << v.toString();
    for (const auto& v : o.value(QLatin1String("risks")).toArray())
        s.risks << v.toString();
    return s;
}

/// What is different between an earlier report and this one, in sentences.
/// Only what both reports knew is compared; nothing is said about a figure
/// either lacked.
inline QStringList changes_since(const Snapshot& then, const Snapshot& now) {
    QStringList out;
    if (!then.valid() || !now.valid())
        return out;
    const QLocale loc(QLocale::English);
    auto moved = [&](const QString& what, double a, double b, int dp, const QString& prefix) {
        const double g = growth_pct(b, a);
        if (ok(g) && std::abs(g) >= 0.5)
            out << QStringLiteral("%1 %2%3 → %2%4 (%5)").arg(what, prefix, loc.toString(a, 'f', dp), loc.toString(b, 'f', dp), sp1(g));
    };
    moved(QStringLiteral("Share price"), then.price, now.price, 2, QString());
    moved(QStringLiteral("Mean analyst target"), then.target_mean, now.target_mean, 2, QString());
    moved(QStringLiteral("Consensus EPS, current fiscal year"), then.eps_fy0, now.eps_fy0, 2, QString());
    moved(QStringLiteral("Consensus EPS, next fiscal year"), then.eps_fy1, now.eps_fy1, 2, QString());
    moved(QStringLiteral("Forward P/E"), then.forward_pe, now.forward_pe, 1, QString());
    if (!then.latest_quarter.isEmpty() && !now.latest_quarter.isEmpty() && then.latest_quarter != now.latest_quarter)
        out << QStringLiteral("A new quarter has been reported: the one ended %1 (it was %2).").arg(now.latest_quarter, then.latest_quarter);
    if (then.analysts_total > 0 && now.analysts_total > 0 && then.analysts_buy >= 0 && now.analysts_buy >= 0 &&
        then.analysts_buy != now.analysts_buy)
        out << QStringLiteral("Analysts rating it a buy: %1 of %2 → %3 of %4.")
                   .arg(then.analysts_buy).arg(then.analysts_total).arg(now.analysts_buy).arg(now.analysts_total);
    // One line per kind of change, however many findings moved: the brief stays brief.
    auto moved_findings = [&out](const QString& lead, const QStringList& from, const QStringList& to) {
        QStringList gone;
        for (const QString& t : from)
            if (!to.contains(t))
                gone << t;
        if (!gone.isEmpty())
            out << QStringLiteral("%1: %2.").arg(lead, gone.join(QStringLiteral("; ")));
    };
    moved_findings(QStringLiteral("New in its favour"), now.strengths, then.strengths);
    moved_findings(QStringLiteral("No longer in its favour"), then.strengths, now.strengths);
    moved_findings(QStringLiteral("New against it"), now.risks, then.risks);
    moved_findings(QStringLiteral("No longer against it"), then.risks, now.risks);
    return out;
}

// ── What the price implies ──────────────────────────────────────────────────

struct ImpliedPrice {
    QString basis;     ///< where the multiple comes from
    double multiple = kNaN;
    double price = kNaN; ///< multiple × the consensus EPS
    double vs_now_pct = kNaN;
};

/// Arithmetic, not a forecast: next fiscal year's consensus EPS at a given
/// earnings multiple. Needs positive EPS — a multiple of a loss is not a price.
inline ImpliedPrice implied_price(const QString& basis, double multiple, double eps, double price_now) {
    ImpliedPrice p;
    p.basis = basis;
    p.multiple = multiple;
    if (ok(multiple) && multiple > 0 && ok(eps) && eps > 0) {
        p.price = multiple * eps;
        if (ok(price_now) && price_now > 0)
            p.vs_now_pct = (p.price - price_now) / price_now * 100.0;
    }
    return p;
}

// ── Insider dealing (from the ownership register, when it is loaded) ────────

struct InsiderFlow {
    bool known = false;
    int window_months = 0;
    int buys = 0, sells = 0; ///< open-market transactions that were decisions (no 10b5-1 plan, no 10% holders)
    double buy_value = 0.0, sell_value = 0.0;
    /// False when any counted transaction was filed without a value: the
    /// totals above then understate, and nothing is concluded from them.
    bool values_complete = true;
};

// ── Findings ────────────────────────────────────────────────────────────────

/// A year-on-year change in share count beyond this is a split or a merger,
/// not buybacks or issuance.
inline constexpr double kMaxShareChangePct = 25.0;

enum class Tone { Strength, Risk, Note };

struct Finding {
    Tone tone = Tone::Note;
    QString area;   ///< GROWTH · PROFITABILITY · CASH · BALANCE SHEET · EARNINGS · VALUATION · SENTIMENT
    QString title;  ///< the reading
    QString detail; ///< the figures behind it, and the rule
};

struct Inputs {
    services::equity::StockInfo info;
    QVector<FiscalYear> years; ///< oldest first
    Extras extras;
    services::equity::EarningsAnalysis earnings;
    PeerStat peer_forward_pe; ///< metric premium vs peers
    PeerStat peer_op_margin;
    PeerStat peer_growth;
    InsiderFlow insiders;
    SectorProfile sector;
    Filings filings; ///< for the concentration findings
    RiskProfile risk;
    bool has_technicals = false;
    bool technicals_rated = false;
    services::equity::TechSignal technical_signal = services::equity::TechSignal::Neutral;
};

/// Strengths and risks read off the data. Each rule fires only when its
/// inputs exist; nothing is inferred from an absence.
inline QVector<Finding> derive_findings(const Inputs& in) {
    QVector<Finding> out;
    auto add = [&out](Tone t, const char* area, const QString& title, const QString& detail) {
        out.append({t, QLatin1String(area), title, detail});
    };
    const auto& y = in.years;
    const int n = y.size();

    // Growth: latest fiscal year, and the multi-year rate.
    if (n >= 2) {
        const double g = growth_pct(y[n - 1].revenue, y[n - 2].revenue);
        if (ok(g)) {
            const QString d = QStringLiteral("Revenue %1 in %2 against %3.").arg(sp1(g), y[n - 1].label(), y[n - 2].label());
            if (g >= 10.0)
                add(Tone::Strength, "GROWTH", QStringLiteral("Revenue growing at a double-digit rate"), d);
            else if (g < 0.0)
                add(Tone::Risk, "GROWTH", QStringLiteral("Revenue is shrinking"), d);
            else
                add(Tone::Note, "GROWTH", QStringLiteral("Revenue growth in single digits"), d);
        }
    }
    if (n >= 3) {
        const double c = cagr_pct(y[0].revenue, y[n - 1].revenue, n - 1);
        const double g = growth_pct(y[n - 1].revenue, y[n - 2].revenue);
        if (ok(c) && ok(g) && g < c - 5.0)
            add(Tone::Risk, "GROWTH", QStringLiteral("Growth is slowing"),
                QStringLiteral("Latest year %1 against %2 a year compounded over %3–%4.")
                    .arg(sp1(g), sp1(c), y[0].label(), y[n - 1].label()));
        else if (ok(c) && ok(g) && g > c + 5.0)
            add(Tone::Strength, "GROWTH", QStringLiteral("Growth is accelerating"),
                QStringLiteral("Latest year %1 against %2 a year compounded over %3–%4.")
                    .arg(sp1(g), sp1(c), y[0].label(), y[n - 1].label()));
    }

    // Profitability: the operating margin and its direction over two years.
    if (n >= 1 && ok(y[n - 1].operating_margin())) {
        const double m = y[n - 1].operating_margin();
        if (m < 0)
            add(Tone::Risk, "PROFITABILITY", QStringLiteral("Operating at a loss"),
                QStringLiteral("Operating margin %1 in %2.").arg(p1(m), y[n - 1].label()));
        if (n >= 3 && ok(y[n - 3].operating_margin())) {
            const double delta = m - y[n - 3].operating_margin();
            const QString d = QStringLiteral("Operating margin %1 in %2, %3 against %4.")
                                  .arg(p1(m), y[n - 1].label(), pts(delta), y[n - 3].label());
            if (delta >= 2.0)
                add(Tone::Strength, "PROFITABILITY", QStringLiteral("Margins are widening"), d);
            else if (delta <= -2.0)
                add(Tone::Risk, "PROFITABILITY", QStringLiteral("Margins are narrowing"), d);
        }
    }
    if (ok(in.info.roe)) {
        const double roe = in.info.roe * 100.0;
        if (roe >= in.sector.high_roe_pct)
            add(Tone::Strength, "PROFITABILITY", QStringLiteral("High return on equity"),
                QStringLiteral("Return on equity %1 (%2% or more counts as high for this kind of company).")
                    .arg(p1(roe))
                    .arg(in.sector.high_roe_pct, 0, 'f', 0));
        else if (roe < 0)
            add(Tone::Risk, "PROFITABILITY", QStringLiteral("Negative return on equity"),
                QStringLiteral("Return on equity %1.").arg(p1(roe)));
    }

    // Cash: free cash flow against revenue, and how much of profit is cash.
    // Not for a bank, insurer or REIT, where the figure does not mean that.
    if (!in.sector.balance_sheet_business && n >= 1 && ok(y[n - 1].fcf)) {
        const double m = y[n - 1].fcf_margin();
        if (y[n - 1].fcf < 0)
            add(Tone::Risk, "CASH", QStringLiteral("Burning cash"),
                QStringLiteral("Free cash flow was negative in %1.").arg(y[n - 1].label()));
        else if (ok(m) && m >= 15.0)
            add(Tone::Strength, "CASH", QStringLiteral("Strong free cash flow"),
                QStringLiteral("Free cash flow is %1 of revenue in %2 (15% or more counts as strong).")
                    .arg(p1(m), y[n - 1].label()));
        const double sbc = pct_of(y[n - 1].sbc, y[n - 1].revenue);
        if (ok(sbc) && sbc >= 10.0)
            add(Tone::Risk, "CASH", QStringLiteral("Heavy stock-based pay"),
                QStringLiteral("Stock-based compensation is %1 of revenue in %2 — free cash flow counts it as free.")
                    .arg(p1(sbc), y[n - 1].label()));
    }

    // Balance sheet: net cash, or debt against EBITDA. Borrowing is a
    // financial company's raw material, not its leverage.
    if (!in.sector.balance_sheet_business && n >= 1 && ok(y[n - 1].cash) && ok(y[n - 1].debt)) {
        const auto& last = y[n - 1];
        if (last.cash >= last.debt)
            add(Tone::Strength, "BALANCE SHEET", QStringLiteral("More cash than debt"),
                QStringLiteral("Cash and short-term investments exceed total debt at the end of %1.").arg(last.label()));
        else if (ok(last.ebitda) && last.ebitda > 0) {
            const double turns = (last.debt - last.cash) / last.ebitda;
            if (turns >= 3.0)
                add(Tone::Risk, "BALANCE SHEET", QStringLiteral("High leverage"),
                    QStringLiteral("Net debt is %1× EBITDA at the end of %2 (3× or more counts as high).")
                        .arg(QLocale(QLocale::English).toString(turns, 'f', 1), last.label()));
        } else if (ok(last.ebitda) && last.ebitda <= 0)
            add(Tone::Risk, "BALANCE SHEET", QStringLiteral("Net debt with no EBITDA to service it"),
                QStringLiteral("Total debt exceeds cash and EBITDA was not positive in %1.").arg(last.label()));
    }
    if (n >= 2 && ok(y[n - 1].shares) && ok(y[n - 2].shares) && y[n - 2].shares > 0) {
        const double d = growth_pct(y[n - 1].shares, y[n - 2].shares);
        // Year-end share counts are as filed, so a split between two year ends
        // looks like dilution of several hundred percent (and a reverse split
        // like a buyback). Nothing here knows the split history; a change too
        // large to be issuance or buybacks is left unread rather than misread.
        if (std::abs(d) > kMaxShareChangePct) {
        } else if (d <= -1.0)
            add(Tone::Strength, "BALANCE SHEET", QStringLiteral("Share count is falling"),
                QStringLiteral("Shares outstanding %1 over %2 — buybacks are outrunning issuance.").arg(sp1(d), y[n - 1].label()));
        else if (d >= 3.0)
            add(Tone::Risk, "BALANCE SHEET", QStringLiteral("Shareholders are being diluted"),
                QStringLiteral("Shares outstanding %1 over %2.").arg(sp1(d), y[n - 1].label()));
    }

    // Earnings: the record against consensus, and where consensus is heading.
    const BeatRecord br = beat_record(in.earnings);
    if (br.quarters >= 4) {
        const QString d = QStringLiteral("Beat consensus EPS in %1 of the last %2 reported quarters.").arg(br.beats).arg(br.quarters);
        if (br.beats * 4 >= br.quarters * 3)
            add(Tone::Strength, "EARNINGS", QStringLiteral("Consistently beats estimates"), d);
        else if (br.beats * 2 < br.quarters)
            add(Tone::Risk, "EARNINGS", QStringLiteral("Often misses estimates"), d);
    }
    const double drift = estimate_drift_pct(in.earnings, QStringLiteral("0y"));
    if (ok(drift)) {
        const QString d = QStringLiteral("Consensus EPS for the current fiscal year is %1 against 90 days ago.").arg(sp1(drift));
        if (drift >= 2.0)
            add(Tone::Strength, "EARNINGS", QStringLiteral("Estimates are being raised"), d);
        else if (drift <= -2.0)
            add(Tone::Risk, "EARNINGS", QStringLiteral("Estimates are being cut"), d);
    }

    // Valuation: against the peer basket.
    const double prem = in.peer_forward_pe.premium_pct();
    if (ok(prem) && in.peer_forward_pe.peers >= 3) {
        const QString d = QStringLiteral("Forward P/E %1 against a peer median of %2 (%3 peers).")
                              .arg(QLocale(QLocale::English).toString(in.peer_forward_pe.company, 'f', 1),
                                   QLocale(QLocale::English).toString(in.peer_forward_pe.peer_median, 'f', 1))
                              .arg(in.peer_forward_pe.peers);
        if (prem >= 30.0)
            add(Tone::Risk, "VALUATION", QStringLiteral("Priced well above peers"), d);
        else if (prem <= -20.0)
            add(Tone::Note, "VALUATION", QStringLiteral("Priced below peers"), d);
    }
    if (ok(in.info.target_mean) && ok(in.info.current_price) && in.info.current_price > 0 && in.info.analyst_count >= 3) {
        const double up = (in.info.target_mean - in.info.current_price) / in.info.current_price * 100.0;
        const QString d = QStringLiteral("Mean analyst target is %1 from the current price (%2 analysts).").arg(sp1(up)).arg(in.info.analyst_count);
        if (up <= -5.0)
            add(Tone::Risk, "VALUATION", QStringLiteral("Trading above the average analyst target"), d);
        else if (up >= 15.0)
            add(Tone::Note, "VALUATION", QStringLiteral("Analysts' targets sit well above the price"), d);
    }

    // Concentration, as the annual report discloses it.
    for (const auto& c : in.filings.customers)
        if (c.pct >= 10.0) {
            add(Tone::Risk, "CONCENTRATION", QStringLiteral("Depends on a few customers"),
                QStringLiteral("The annual report names a customer at %1 of %2 revenue (10% or more is disclosed as a concentration).")
                    .arg(p1(c.pct), c.scope.isEmpty() ? QStringLiteral("total") : c.scope));
            break; // the largest says it; the table lists the rest
        }
    for (const char* axis : {"segment", "product"})
        if (const Breakdown* b = in.filings.breakdown(QLatin1String(axis))) {
            const double share = b->top_share_pct();
            if (ok(share) && share >= 60.0)
                add(Tone::Note, "CONCENTRATION", QStringLiteral("One line of business dominates"),
                    QStringLiteral("%1 is %2 of revenue in the year to %3.").arg(b->rows.first().label, p1(share), b->period));
            break; // one cut is enough; segment is preferred over product
        }

    // How it has traded.
    if (in.risk.sessions >= 60 && ok(in.risk.max_drawdown_pct) && in.risk.max_drawdown_pct <= -30.0)
        add(Tone::Risk, "MARKET RISK", QStringLiteral("Deep drawdown within the year"),
            QStringLiteral("The shares fell %1 from a high at their worst over the last %2 sessions (30% or more counts as deep).")
                .arg(p1(std::abs(in.risk.max_drawdown_pct))).arg(in.risk.sessions));

    // Sentiment: short interest, insiders, the technical picture.
    if (ok(in.info.short_pct_of_float)) {
        const double s = in.info.short_pct_of_float * 100.0;
        if (s >= 10.0)
            add(Tone::Risk, "SENTIMENT", QStringLiteral("Heavily shorted"),
                QStringLiteral("%1 of the float is sold short (10% or more counts as heavy).").arg(p1(s)));
    }
    if (in.insiders.known && (in.insiders.buys > 0 || in.insiders.sells > 0)) {
        const QString d = QStringLiteral("%1 open-market purchases and %2 sales by insiders over %3 months, plan trades excluded.")
                              .arg(in.insiders.buys).arg(in.insiders.sells).arg(in.insiders.window_months);
        if (in.insiders.buys >= 2 && in.insiders.values_complete && in.insiders.buy_value > in.insiders.sell_value)
            add(Tone::Strength, "SENTIMENT", QStringLiteral("Insiders are buying"), d);
        else if (in.insiders.buys == 0 && in.insiders.sells >= 5)
            add(Tone::Note, "SENTIMENT", QStringLiteral("Insiders are selling, none buying"), d);
    }
    if (in.has_technicals && in.technicals_rated) {
        using services::equity::TechSignal;
        const TechSignal s = in.technical_signal;
        if (s == TechSignal::StrongBuy || s == TechSignal::Buy)
            add(Tone::Note, "SENTIMENT", QStringLiteral("Price trend is up"),
                QStringLiteral("The daily technical rating reads %1 — a description of the move so far, not a forecast.")
                    .arg(s == TechSignal::StrongBuy ? QStringLiteral("strong buy") : QStringLiteral("buy")));
        else if (s == TechSignal::StrongSell || s == TechSignal::Sell)
            add(Tone::Note, "SENTIMENT", QStringLiteral("Price trend is down"),
                QStringLiteral("The daily technical rating reads %1 — a description of the move so far, not a forecast.")
                    .arg(s == TechSignal::StrongSell ? QStringLiteral("strong sell") : QStringLiteral("sell")));
    }
    return out;
}

inline int count_tone(const QVector<Finding>& f, Tone t) {
    return static_cast<int>(std::count_if(f.begin(), f.end(), [t](const Finding& x) { return x.tone == t; }));
}

} // namespace finterm::screens::report
