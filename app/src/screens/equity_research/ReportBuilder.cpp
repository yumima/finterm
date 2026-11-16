// src/screens/equity_research/ReportBuilder.cpp
#include "screens/equity_research/ReportBuilder.h"

#include "core/util/BarTime.h"
#include "ui/formatting/NumberFormat.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QPrinter>
#include <QTextDocument>
#include <QUrl>

#include <cmath>

namespace finterm::screens::report {

namespace eq = services::equity;
namespace fmt = ui::formatting;

namespace {

const QLocale& en() {
    static const QLocale l(QLocale::English);
    return l;
}

QString num(double v, int dp) {
    return ok(v) ? en().toString(v, 'f', dp) : fmt::placeholder();
}

QString pct(double v, int dp = 1) { // v already a percent
    return ok(v) ? en().toString(v, 'f', dp) + QLatin1Char('%') : fmt::placeholder();
}

QString spct(double v, int dp = 1) {
    if (!ok(v))
        return fmt::placeholder();
    return (v > 0 ? QStringLiteral("+") : v < 0 ? QStringLiteral("−") : QString()) + en().toString(std::abs(v), 'f', dp) +
           QLatin1Char('%');
}

/// A Yahoo fraction (0.153) as a percent value (15.3); NaN stays NaN.
double as_pct(double fraction) {
    return ok(fraction) ? fraction * 100.0 : kNaN;
}

double opt(const std::optional<double>& v) {
    return v ? *v : kNaN;
}

/// The symbol a currency is written with, or its code and a space.
QString ccy_prefix(const QString& code) {
    return code.isEmpty() ? QString() : fmt::detail::currency_symbol(code);
}

struct Html {
    const Palette& pal;
    int width;
    qreal dpr;
    const ImageSink& sink;
    QString out;
    int px;

    QString c(const QColor& col) const { return col.name(); }

    void raw(const QString& s) { out += s; }

    void section(const QString& number, const QString& title, const QString& sub = {}) {
        out += QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0' style='margin-top:26px;'><tr>"
                              "<td style='border-bottom:2px solid %1;padding-bottom:4px;'>"
                              "<span style='color:%1;font-size:%2px;font-weight:700;'>%3&nbsp;&nbsp;</span>"
                              "<span style='color:%4;font-size:%5px;font-weight:700;'>%6</span>"
                              "<span style='color:%7;font-size:%2px;'>&nbsp;&nbsp;%8</span></td></tr></table>")
                   .arg(c(pal.accent))
                   .arg(px)
                   .arg(number, c(pal.text))
                   .arg(px + 5)
                   .arg(title.toHtmlEscaped(), c(pal.text_dim), sub.toHtmlEscaped());
    }

    void sub(const QString& title) {
        out += QStringLiteral("<p style='margin:14px 0 4px 0;color:%1;font-size:%2px;font-weight:700;'>%3</p>")
                   .arg(c(pal.accent))
                   .arg(px)
                   .arg(title.toHtmlEscaped());
    }

    void para(const QString& text, bool dim = false) {
        if (text.trimmed().isEmpty())
            return;
        out += QStringLiteral("<p style='margin:4px 0 8px 0;color:%1;font-size:%2px;line-height:140%;'>%3</p>")
                   .arg(c(dim ? pal.text_dim : pal.text))
                   .arg(px + (dim ? 0 : 1))
                   .arg(text.toHtmlEscaped());
    }

    void note(const QString& text) {
        if (text.trimmed().isEmpty())
            return;
        out += QStringLiteral("<p style='margin:2px 0 8px 0;color:%1;font-size:%2px;'>%3</p>")
                   .arg(c(pal.text_dim))
                   .arg(px - 1)
                   .arg(text.toHtmlEscaped());
    }

    QString bullets_html(const QStringList& items, const QColor& mark, const QString& glyph = QStringLiteral("■")) const {
        QString s = QStringLiteral("<table cellspacing='0' cellpadding='3'>");
        for (const QString& it : items)
            s += QStringLiteral("<tr><td valign='top' style='color:%1;font-size:%2px;'>%3</td>"
                                "<td style='color:%4;font-size:%5px;line-height:135%;'>%6</td></tr>")
                     .arg(c(mark))
                     .arg(px - 2)
                     .arg(glyph, c(pal.text))
                     .arg(px + 1)
                     .arg(it.toHtmlEscaped());
        return s + QStringLiteral("</table>");
    }

    void bullets(const QStringList& items, const QColor& mark) {
        if (!items.isEmpty())
            out += bullets_html(items, mark);
    }

    /// One chart, full width or as a cell of a row.
    QString img(const QString& id, const QImage& image) const {
        const QSize logical = image.size() / image.devicePixelRatio();
        return QStringLiteral("<img src='%1' width='%2' height='%3'>").arg(sink(id, image)).arg(logical.width()).arg(logical.height());
    }

    void row(const QStringList& cells) {
        out += QStringLiteral("<table cellspacing='0' cellpadding='0' style='margin-top:8px;'><tr>");
        for (int i = 0; i < cells.size(); ++i)
            out += QStringLiteral("<td valign='top' style='padding-right:%1px;'>%2</td>").arg(i + 1 < cells.size() ? 10 : 0).arg(cells[i]);
        out += QStringLiteral("</tr></table>");
    }

    int half() const { return (width - 10) / 2; }

    /// A table of facts: header row, then rows of cells (first column left, the rest right).
    void table(const QStringList& head, const QVector<QStringList>& rows, const QVector<QVector<QColor>>& colors = {},
               int text_columns = 1) {
        if (rows.isEmpty())
            return;
        out += QStringLiteral("<table cellspacing='0' cellpadding='5' style='margin-top:6px;'><tr>");
        for (int i = 0; i < head.size(); ++i)
            out += QStringLiteral("<td align='%1' style='color:%2;font-size:%3px;font-weight:700;border-bottom:1px solid %4;'>%5</td>")
                       .arg(i < text_columns ? QStringLiteral("left") : QStringLiteral("right"), c(pal.text_dim))
                       .arg(px - 1)
                       .arg(c(pal.border), head[i].toHtmlEscaped());
        out += QStringLiteral("</tr>");
        for (int r = 0; r < rows.size(); ++r) {
            out += QStringLiteral("<tr>");
            for (int i = 0; i < rows[r].size(); ++i) {
                const QColor col = r < colors.size() && i < colors[r].size() && colors[r][i].isValid() ? colors[r][i] : pal.text;
                out += QStringLiteral("<td align='%1' style='color:%2;font-size:%3px;padding-right:14px;'>%4</td>")
                           .arg(i < text_columns ? QStringLiteral("left") : QStringLiteral("right"), c(col))
                           .arg(px)
                           .arg(rows[r][i].toHtmlEscaped());
            }
            out += QStringLiteral("</tr>");
        }
        out += QStringLiteral("</table>");
    }
};

QColor tone(const Palette& pal, double v) {
    return !ok(v) || v == 0 ? pal.text : v > 0 ? pal.positive : pal.negative;
}

/// Company row of the peer basket, or null.
const eq::PeerData* own_row(const ReportDoc& d) {
    for (const auto& p : d.peers)
        if (p.symbol.compare(d.symbol, Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

PeerStat stat_of(const ReportDoc& d, const QString& metric, double eq::PeerData::*field, double scale = 1.0) {
    QVector<double> others;
    double own = kNaN;
    for (const auto& p : d.peers) {
        const double v = ok(p.*field) ? p.*field * scale : kNaN;
        if (p.symbol.compare(d.symbol, Qt::CaseInsensitive) == 0)
            own = v;
        else
            others.append(v);
    }
    return peer_stat(metric, own, others);
}

QString statement_ccy(const ReportDoc& d) {
    return d.extras.financial_currency.isEmpty() ? d.info.currency : d.extras.financial_currency;
}

/// The statements are in another currency than the share price (an ADR).
bool mixed_currency(const ReportDoc& d) {
    return !d.extras.financial_currency.isEmpty() && !d.info.currency.isEmpty() &&
           d.extras.financial_currency.compare(d.info.currency, Qt::CaseInsensitive) != 0;
}

/// What statement money is written with. When the company reports in another
/// currency than its shares trade in, the code itself — "$" for both a
/// Canadian and a US dollar would hide the difference.
QString statement_prefix(const ReportDoc& d) {
    return mixed_currency(d) ? d.extras.financial_currency + QLatin1Char(' ') : ccy_prefix(statement_ccy(d));
}

const eq::EarningsEstimateRow* estimate(const ReportDoc& d, const QString& period) {
    for (const auto& e : d.earnings.estimates)
        if (e.period == period)
            return &e;
    return nullptr;
}

} // namespace

Inputs make_inputs(const ReportDoc& d) {
    Inputs in;
    in.info = d.info;
    if (d.has_financials)
        in.years = fiscal_years(d.financials);
    in.extras = d.extras;
    if (d.has_earnings)
        in.earnings = d.earnings;
    // A basket of index funds is not a peer group; default_peers() falls back to one.
    if (d.peers.size() >= 4) {
        in.peer_forward_pe = stat_of(d, QStringLiteral("Forward P/E"), &eq::PeerData::forward_pe);
        in.peer_op_margin = stat_of(d, QStringLiteral("Operating margin"), &eq::PeerData::operating_margin, 100.0);
        in.peer_growth = stat_of(d, QStringLiteral("Revenue growth"), &eq::PeerData::revenue_growth, 100.0);
    }
    in.insiders = d.insiders;
    in.filings = d.filings;
    in.risk = risk_profile(d.candles, d.bench);
    in.has_technicals = d.has_technicals;
    in.technicals_rated = d.has_technicals && d.technicals.rated;
    in.technical_signal = d.technicals.overall_signal;
    return in;
}

QVector<Headline> headlines(const ReportDoc& d) {
    QVector<Headline> out;
    for (const auto& n : d.news)
        if (!n.title.trimmed().isEmpty())
            out.append({n.title, n.publisher, n.published_date});
    return relevant_headlines(out, d.symbol, d.info.company_name);
}

QString figures_block(const ReportDoc& d) {
    QStringList L;
    const QString cp = ccy_prefix(d.info.currency);
    const QString sp = statement_prefix(d);
    auto add = [&L](const QString& k, const QString& v) {
        if (!v.isEmpty() && v != fmt::placeholder())
            L << QStringLiteral("%1: %2").arg(k, v);
    };
    add(QStringLiteral("Company"), d.info.company_name);
    add(QStringLiteral("Sector / industry"), QStringList{d.info.sector, d.info.industry}.join(QStringLiteral(" / ")));
    if (ok(d.info.current_price))
        add(QStringLiteral("Share price"), cp + num(d.info.current_price, 2));
    if (ok(d.info.market_cap))
        add(QStringLiteral("Market capitalisation"), cp + compact(d.info.market_cap));
    if (ok(d.info.week52_low) && ok(d.info.week52_high))
        add(QStringLiteral("52-week range"), QStringLiteral("%1%2 to %1%3").arg(cp, num(d.info.week52_low, 2), num(d.info.week52_high, 2)));

    const auto years = d.has_financials ? fiscal_years(d.financials) : QVector<FiscalYear>{};
    for (int i = 0; i < years.size(); ++i) {
        const auto& y = years[i];
        QStringList parts;
        if (ok(y.revenue))
            parts << QStringLiteral("revenue %1%2").arg(sp, compact(y.revenue));
        if (i > 0 && ok(growth_pct(y.revenue, years[i - 1].revenue)))
            parts << QStringLiteral("revenue growth %1").arg(spct(growth_pct(y.revenue, years[i - 1].revenue)));
        if (ok(y.operating_margin()))
            parts << QStringLiteral("operating margin %1").arg(pct(y.operating_margin()));
        if (ok(y.net_income))
            parts << QStringLiteral("net income %1%2").arg(sp, compact(y.net_income));
        if (ok(y.fcf))
            parts << QStringLiteral("free cash flow %1%2").arg(sp, compact(y.fcf));
        if (!parts.isEmpty())
            L << QStringLiteral("%1 (year ended %2): %3").arg(y.label(), y.period, parts.join(QStringLiteral(", ")));
    }
    for (int i = 0; i < d.extras.quarters.size() && i < 5; ++i) {
        const auto& q = d.extras.quarters[i];
        QStringList parts;
        if (ok(q.revenue))
            parts << QStringLiteral("revenue %1%2").arg(sp, compact(q.revenue));
        if (ok(pct_of(q.operating_income, q.revenue)))
            parts << QStringLiteral("operating margin %1").arg(pct(pct_of(q.operating_income, q.revenue)));
        if (ok(q.eps))
            parts << QStringLiteral("diluted EPS %1%2").arg(sp, num(q.eps, 2));
        if (!parts.isEmpty())
            L << QStringLiteral("Quarter ended %1: %2").arg(q.period, parts.join(QStringLiteral(", ")));
    }
    if (const double g = latest_quarter_yoy_pct(d.extras.quarters); ok(g))
        L << QStringLiteral("Latest quarter revenue against the same quarter a year earlier: %1").arg(spct(g));
    if (!years.isEmpty()) {
        const auto& y = years.last();
        if (ok(y.cash))
            add(QStringLiteral("Cash and short-term investments (%1)").arg(y.label()), sp + compact(y.cash));
        if (ok(y.debt))
            add(QStringLiteral("Total debt (%1)").arg(y.label()), sp + compact(y.debt));
    }
    if (ok(d.info.pe_ratio))
        add(QStringLiteral("Trailing P/E"), num(d.info.pe_ratio, 1));
    if (ok(d.info.forward_pe))
        add(QStringLiteral("Forward P/E"), num(d.info.forward_pe, 1));
    if (ok(d.info.ev_to_ebitda))
        add(QStringLiteral("EV / EBITDA"), num(d.info.ev_to_ebitda, 1));
    if (ok(d.info.roe))
        add(QStringLiteral("Return on equity"), pct(as_pct(d.info.roe)));
    if (ok(d.info.dividend_yield) && d.info.dividend_yield > 0)
        add(QStringLiteral("Dividend yield"), pct(as_pct(d.info.dividend_yield), 2));
    if (ok(d.info.target_mean) && d.info.analyst_count > 0)
        L << QStringLiteral("Analyst price targets (%1 analysts): mean %2%3, low %2%4, high %2%5")
                 .arg(d.info.analyst_count)
                 .arg(cp, num(d.info.target_mean, 2), num(d.info.target_low, 2), num(d.info.target_high, 2));
    if (!d.info.recommendation_key.isEmpty())
        add(QStringLiteral("Analyst consensus"), d.info.recommendation_key);
    if (d.has_earnings) {
        const BeatRecord br = beat_record(d.earnings);
        if (br.quarters > 0)
            L << QStringLiteral("Beat consensus EPS in %1 of the last %2 reported quarters").arg(br.beats).arg(br.quarters);
        for (const char* per : {"0q", "0y", "+1y"}) {
            if (const auto* e = estimate(d, QLatin1String(per))) {
                QStringList parts;
                if (e->eps_avg)
                    parts << QStringLiteral("EPS %1").arg(num(*e->eps_avg, 2));
                if (e->eps_growth)
                    parts << QStringLiteral("EPS growth %1").arg(spct(*e->eps_growth * 100.0));
                if (e->rev_avg)
                    parts << QStringLiteral("revenue %1").arg(compact(*e->rev_avg));
                if (e->rev_growth)
                    parts << QStringLiteral("revenue growth %1").arg(spct(*e->rev_growth * 100.0));
                if (!parts.isEmpty())
                    L << QStringLiteral("Consensus for %1: %2").arg(e->label.isEmpty() ? e->period : e->label, parts.join(QStringLiteral(", ")));
            }
        }
        if (d.earnings.next.timestamp)
            L << QStringLiteral("Next earnings report: %1%2")
                     .arg(core::bartime::market_date_et(*d.earnings.next.timestamp).toString(Qt::ISODate),
                          d.earnings.next.is_estimated ? QStringLiteral(" (estimated date)") : QString());
    }
    if (ok(d.info.short_pct_of_float))
        add(QStringLiteral("Short interest"), pct(as_pct(d.info.short_pct_of_float)) + QStringLiteral(" of float"));
    const QString cuts = breakdown_block(d.filings);
    if (!cuts.isEmpty())
        L << cuts;
    const RiskProfile risk = risk_profile(d.candles, d.bench);
    if (risk.sessions >= 60) {
        L << QStringLiteral("Share price over the last %1 sessions: %2, annualised volatility %3, deepest fall from a high %4")
                 .arg(risk.sessions)
                 .arg(spct(risk.return_pct), pct(risk.volatility_pct), pct(std::abs(risk.max_drawdown_pct)));
        if (ok(risk.beta))
            L << QStringLiteral("Beta to %1 over the same period: %2").arg(d.bench_symbol, num(risk.beta, 2));
    }
    return L.join(QLatin1Char('\n'));
}

// ════════════════════════════════════════════════════════════════════════════

QString build_report_html(const ReportDoc& d, const Palette& pal, int width, qreal dpr, const ImageSink& sink) {
    Html h{pal, width, dpr, sink, {}, pal.font_px};
    const int px = pal.font_px;
    const QString cp = ccy_prefix(d.info.currency);
    const QString sccy = statement_ccy(d);
    const QString sp = statement_prefix(d);
    const auto years = d.has_financials ? fiscal_years(d.financials) : QVector<FiscalYear>{};
    const Inputs inputs = make_inputs(d);
    const QVector<Finding> findings = derive_findings(inputs);
    const Format money = [sp](double v) { return sp + compact(v); };
    const Format money0 = [](double v) { return compact(v, 0); };
    const Format percent = [](double v) { return en().toString(v, 'f', std::abs(v) < 10 ? 1 : 0) + QLatin1Char('%'); };
    const Format plain1 = [](double v) { return en().toString(v, 'f', 1); };
    const Format plain2 = [](double v) { return en().toString(v, 'f', 2); };
    const QSize half(h.half(), 250);
    const QSize full(width, 270);
    const QString name = d.info.company_name.isEmpty() ? d.symbol : d.info.company_name;

    h.raw(QStringLiteral("<html><body style='background:%1;color:%2;font-family:\"%3\";font-size:%4px;'>"
                         "<table width='%5' align='center' cellspacing='0' cellpadding='0'><tr><td>")
              .arg(h.c(pal.bg), h.c(pal.text), pal.font_family)
              .arg(px)
              .arg(width));

    // ── Cover ──────────────────────────────────────────────────────────────
    h.raw(QStringLiteral("<p style='margin:0;color:%1;font-size:%2px;font-weight:700;'>INVESTOR REPORT · %3</p>"
                         "<p style='margin:4px 0 0 0;color:%4;font-size:%5px;font-weight:700;'>%6 "
                         "<span style='color:%7;font-size:%8px;'>%9</span></p>")
              .arg(h.c(pal.accent))
              .arg(px)
              .arg(d.generated.toString(QStringLiteral("d MMMM yyyy")).toUpper(), h.c(pal.text))
              .arg(px + 14)
              .arg(name.toHtmlEscaped(), h.c(pal.text_dim))
              .arg(px + 4)
              .arg(d.symbol.toHtmlEscaped()));
    QStringList idline;
    for (const QString& s : {d.info.exchange, d.info.sector, d.info.industry, d.info.country})
        if (!s.isEmpty())
            idline << s;
    if (d.info.employees > 0)
        idline << QStringLiteral("%1 employees").arg(en().toString(d.info.employees));
    h.note(idline.join(QStringLiteral("  ·  ")));

    // KPI tiles.
    struct Tile {
        QString label, value, sub;
        QColor sub_color;
    };
    QVector<Tile> tiles;
    if (ok(d.info.current_price)) {
        QString sub;
        if (ok(d.info.week52_low) && ok(d.info.week52_high) && d.info.week52_high > d.info.week52_low)
            sub = QStringLiteral("%1 of its 52-week range")
                      .arg(pct((d.info.current_price - d.info.week52_low) / (d.info.week52_high - d.info.week52_low) * 100.0, 0));
        tiles.append({QStringLiteral("SHARE PRICE"), cp + num(d.info.current_price, 2), sub, pal.text_dim});
    }
    if (ok(d.info.market_cap))
        tiles.append({QStringLiteral("MARKET CAP"), cp + compact(d.info.market_cap),
                      ok(d.info.enterprise_value) ? QStringLiteral("enterprise value %1%2").arg(cp, compact(d.info.enterprise_value)) : QString(),
                      pal.text_dim});
    if (!years.isEmpty() && ok(years.last().revenue)) {
        const double g = years.size() >= 2 ? growth_pct(years.last().revenue, years[years.size() - 2].revenue) : kNaN;
        tiles.append({QStringLiteral("REVENUE %1").arg(years.last().label()), sp + compact(years.last().revenue),
                      ok(g) ? spct(g) + QStringLiteral(" on the year before") : QString(), tone(pal, g)});
    }
    if (!years.isEmpty() && ok(years.last().operating_margin())) {
        const double delta = years.size() >= 2 ? years.last().operating_margin() - years[years.size() - 2].operating_margin() : kNaN;
        tiles.append({QStringLiteral("OPERATING MARGIN"), pct(years.last().operating_margin()),
                      ok(delta) ? pts(delta) + QStringLiteral(" on the year before") : QString(), tone(pal, delta)});
    }
    if (!years.isEmpty() && ok(years.last().fcf))
        tiles.append({QStringLiteral("FREE CASH FLOW %1").arg(years.last().label()), sp + compact(years.last().fcf),
                      ok(years.last().fcf_margin()) ? pct(years.last().fcf_margin()) + QStringLiteral(" of revenue") : QString(),
                      pal.text_dim});
    if (ok(d.info.forward_pe)) {
        const double prem = inputs.peer_forward_pe.premium_pct();
        tiles.append({QStringLiteral("FORWARD P/E"), num(d.info.forward_pe, 1),
                      ok(prem) ? spct(prem, 0) + QStringLiteral(" vs peer median") : QString(), pal.text_dim});
    } else if (ok(d.info.pe_ratio)) {
        tiles.append({QStringLiteral("TRAILING P/E"), num(d.info.pe_ratio, 1), QString(), pal.text_dim});
    }
    if (!tiles.isEmpty()) {
        h.raw(QStringLiteral("<table width='100%' cellspacing='6' cellpadding='10' style='margin-top:10px;'><tr>"));
        for (const auto& t : tiles)
            h.raw(QStringLiteral("<td bgcolor='%1' width='%2'><span style='color:%3;font-size:%4px;font-weight:700;'>%5</span><br>"
                                 "<span style='color:%6;font-size:%7px;font-weight:700;'>%8</span><br>"
                                 "<span style='color:%9;font-size:%4px;'>%10</span></td>")
                      .arg(h.c(pal.panel))
                      .arg((width - 6 * (tiles.size() + 1)) / tiles.size() - 20) // less spacing and padding
                      .arg(h.c(pal.text_dim))
                      .arg(px - 1)
                      .arg(t.label.toHtmlEscaped(), h.c(pal.text))
                      .arg(px + 8)
                      .arg(t.value.toHtmlEscaped(), h.c(t.sub_color), t.sub.isEmpty() ? QStringLiteral("&nbsp;") : t.sub.toHtmlEscaped()));
        h.raw(QStringLiteral("</tr></table>"));
    }
    if (!d.info.quote_type.isEmpty() && d.info.quote_type.compare(QLatin1String("EQUITY"), Qt::CaseInsensitive) != 0)
        h.note(QStringLiteral("%1 is listed as %2, not an operating company: it files no annual report and has no earnings or "
                              "statements of its own, so most of this report's sections have nothing to show.")
                   .arg(d.symbol, d.info.quote_type.toLower()));
    if (mixed_currency(d))
        h.note(QStringLiteral("The shares are quoted in %1; the company reports in %2. Statement figures below are in %2 and are "
                              "not converted.")
                   .arg(d.info.currency, d.extras.financial_currency));

    // ── 1. At a glance ─────────────────────────────────────────────────────
    h.section(QStringLiteral("1"), QStringLiteral("AT A GLANCE"));
    if (d.summary.valid)
        h.para(d.summary.summary);
    else
        h.note(d.narrative_note);

    QStringList strengths, risks, notes;
    for (const auto& f : findings) {
        const QString line = QStringLiteral("%1 — %2").arg(f.title, f.detail);
        (f.tone == Tone::Strength ? strengths : f.tone == Tone::Risk ? risks : notes) << line;
    }
    if (!findings.isEmpty()) {
        auto column = [&](const QString& title, const QStringList& items, const QColor& col, const QString& glyph) {
            return QStringLiteral("<p style='margin:6px 0 2px 0;color:%1;font-size:%2px;font-weight:700;'>%3 (%4)</p>%5")
                .arg(h.c(col))
                .arg(px)
                .arg(title)
                .arg(items.size())
                .arg(items.isEmpty() ? QStringLiteral("<p style='color:%1;font-size:%2px;'>None of the rules fired.</p>").arg(h.c(pal.text_dim)).arg(px)
                                     : h.bullets_html(items, col, glyph));
        };
        h.raw(QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0'><tr><td width='50%' valign='top' style='padding-right:14px;'>") +
              column(QStringLiteral("WHAT THE NUMBERS SHOW IN ITS FAVOUR"), strengths, pal.positive, QStringLiteral("▲")) +
              QStringLiteral("</td><td width='50%' valign='top'>") +
              column(QStringLiteral("WHAT THE NUMBERS SHOW AGAINST IT"), risks, pal.negative, QStringLiteral("▼")) +
              QStringLiteral("</td></tr></table>"));
        if (!notes.isEmpty()) {
            h.sub(QStringLiteral("ALSO WORTH KNOWING"));
            h.bullets(notes, pal.text_dim);
        }
        h.note(QStringLiteral("Read off the data by fixed rules, each stated beside its figure. They describe what has "
                              "happened; none is a forecast."));
    }

    // Price against the benchmark, both rebased to 100 a year ago.
    if (d.candles.size() >= 20) {
        QHash<QDate, double> bench;
        for (const auto& b : d.bench)
            bench.insert(b.date(), b.close);
        ChartSpec spec;
        Series own{d.symbol, {}, 0}, idx{d.bench_symbol, {}, 5};
        double own0 = kNaN, idx0 = kNaN;
        QString last_month;
        for (const auto& k : d.candles) {
            const double b = bench.value(k.date(), kNaN);
            if (!ok(own0) && k.close > 0 && (d.bench.isEmpty() || ok(b))) {
                own0 = k.close;
                idx0 = b;
            }
            if (!ok(own0))
                continue;
            const QString month = k.date().toString(QStringLiteral("MMM yy"));
            spec.categories << (month == last_month ? QString() : month); // one label a month
            last_month = month;
            own.values.append(k.close / own0 * 100.0);
            idx.values.append(ok(b) && ok(idx0) && idx0 > 0 ? b / idx0 * 100.0 : kNaN);
        }
        spec.lines.append(own);
        const bool has_bench = std::any_of(idx.values.begin(), idx.values.end(), [](double v) { return ok(v); });
        if (has_bench)
            spec.lines.append(idx);
        spec.title = has_bench ? QStringLiteral("One year: %1 against %2").arg(d.symbol, d.bench_symbol)
                               : QStringLiteral("One year: %1").arg(d.symbol);
        const double perf = own.values.isEmpty() ? kNaN : own.values.last() - 100.0;
        const double bperf = has_bench ? idx.values.last() - 100.0 : kNaN;
        spec.note = QStringLiteral("Both start at 100. %1 %2%3 · daily closes")
                        .arg(d.symbol, spct(perf), ok(bperf) ? QStringLiteral(", %1 %2").arg(d.bench_symbol, spct(bperf)) : QString());
        spec.fmt = [](double v) { return en().toString(v, 'f', 0); };
        spec.free_baseline = true;
        spec.rule = 100.0;
        h.row({h.img(QStringLiteral("price"), render_chart(spec, pal, full, dpr))});
    }

    // ── 2. The business ────────────────────────────────────────────────────
    h.section(QStringLiteral("2"), QStringLiteral("THE BUSINESS"),
              d.filings.annual.valid() ? QStringLiteral("from the %1").arg(d.filings.annual.cite()) : QString());
    if (d.business.valid) {
        h.para(d.business.business_model);
        if (!d.business.segments.isEmpty()) {
            h.sub(QStringLiteral("SEGMENTS AND WHAT DRIVES THEM"));
            h.bullets(d.business.segments, pal.accent);
        }
        if (!d.business.customers.isEmpty()) {
            h.sub(QStringLiteral("CUSTOMERS"));
            h.para(d.business.customers);
        }
        if (!d.business.competition.isEmpty()) {
            h.sub(QStringLiteral("COMPETITION"));
            h.para(d.business.competition);
        }
    } else if (!d.info.description.isEmpty()) {
        h.para(d.info.description);
        h.note(QStringLiteral("Company profile from Yahoo Finance. ") + d.narrative_note);
    } else {
        h.note(d.narrative_note);
    }

    if (!d.filings.breakdowns.isEmpty()) {
        // Segment before product (one of them), then geography: two charts.
        QVector<const Breakdown*> show;
        if (const auto* b = d.filings.breakdown(QStringLiteral("segment")))
            show.append(b);
        else if (const auto* b2 = d.filings.breakdown(QStringLiteral("product")))
            show.append(b2);
        if (show.size() == 1 && show.first()->axis == QLatin1String("segment") && show.first()->rows.size() <= 3)
            if (const auto* b3 = d.filings.breakdown(QStringLiteral("product")))
                show.append(b3); // three segments say little; the product lines say more
        if (const auto* g = d.filings.breakdown(QStringLiteral("geography")); g && show.size() < 2)
            show.append(g);
        QStringList cells;
        for (const Breakdown* b : show) {
            HBarSpec hb;
            hb.title = b->title();
            hb.note = QStringLiteral("Year ended %1 · change on the year before in brackets").arg(b->period);
            hb.fmt = money;
            hb.highlight = 0;
            for (const auto& r : b->rows) {
                hb.labels << (ok(r.growth_pct()) ? QStringLiteral("%1 (%2)").arg(r.label, spct(r.growth_pct(), 0)) : r.label);
                hb.values.append(r.value);
            }
            cells << h.img(QStringLiteral("cut_") + b->axis,
                           render_hbars(hb, pal, QSize(show.size() == 1 ? width : h.half(), 62 + 26 * b->rows.size()), dpr));
        }
        h.row(cells);
        h.note(QStringLiteral("As tagged in the %1; each cut adds up to the year's revenue.").arg(d.filings.breakdown_source));
    }
    if (!d.filings.customers.isEmpty()) {
        h.sub(QStringLiteral("CUSTOMERS THE ANNUAL REPORT SINGLES OUT"));
        QVector<QStringList> rows;
        for (const auto& c : d.filings.customers)
            rows.append({c.label, pct(c.pct, 0), c.scope.isEmpty() ? QStringLiteral("total revenue") : c.scope + QStringLiteral(" revenue")});
        h.table({QStringLiteral("CUSTOMER, AS FILED"), QStringLiteral("SHARE"), QStringLiteral("OF")}, rows);
        h.note(QStringLiteral("A company must disclose any customer at 10% or more of revenue; it need not name it."));
    }

    // ── 3. What is new ─────────────────────────────────────────────────────
    h.section(QStringLiteral("3"), QStringLiteral("WHAT IS NEW"),
              d.filings.quarterly.valid() ? QStringLiteral("from the %1 and recent headlines").arg(d.filings.quarterly.cite())
                                          : QStringLiteral("recent headlines"));
    if (d.business.valid) {
        if (!d.business.recent_developments.isEmpty()) {
            h.sub(QStringLiteral("THIS QUARTER"));
            h.bullets(d.business.recent_developments, pal.accent);
        }
        if (!d.business.new_technology.isEmpty()) {
            h.sub(QStringLiteral("NEW PRODUCTS AND TECHNOLOGY"));
            h.bullets(d.business.new_technology, pal.accent);
        }
        if (!d.business.new_business.isEmpty()) {
            h.sub(QStringLiteral("NEW BUSINESS: MARKETS, PARTNERS, ACQUISITIONS"));
            h.bullets(d.business.new_business, pal.accent);
        }
    }
    if (!headlines(d).isEmpty()) {
        h.sub(QStringLiteral("HEADLINES"));
        QString list = QStringLiteral("<table cellspacing='0' cellpadding='3'>");
        // The same stories the model was given: those about this company,
        // when they can be told apart (relevant_headlines).
        const QVector<Headline> kept = headlines(d);
        QVector<eq::NewsArticle> listed;
        for (const auto& a : d.news)
            if (std::any_of(kept.begin(), kept.end(), [&a](const Headline& k) { return k.title == a.title; }))
                listed.append(a);
        for (int i = 0; i < listed.size() && i < 8; ++i) {
            const auto& a = listed[i];
            list += QStringLiteral("<tr><td valign='top' style='color:%1;font-size:%2px;' width='78'>%3</td>"
                                   "<td style='font-size:%4px;'><a href='%5' style='color:%6;text-decoration:none;'>%7</a> "
                                   "<span style='color:%1;font-size:%2px;'>%8</span></td></tr>")
                        .arg(h.c(pal.text_dim))
                        .arg(px - 1)
                        .arg(a.published_date.left(10).toHtmlEscaped())
                        .arg(px + 1)
                        .arg(a.url.toHtmlEscaped(), h.c(pal.text), a.title.toHtmlEscaped(), a.publisher.toHtmlEscaped());
        }
        h.raw(list + QStringLiteral("</table>"));
    } else if (!d.business.valid) {
        h.note(QStringLiteral("No recent headlines for this company."));
    }

    // ── 4. Financial performance ───────────────────────────────────────────
    h.section(QStringLiteral("4"), QStringLiteral("FINANCIAL PERFORMANCE"),
              sccy.isEmpty() ? QString() : QStringLiteral("figures in %1").arg(sccy));
    if (years.isEmpty()) {
        h.note(QStringLiteral("No annual statements are available for this company."));
    } else {
        QStringList labels;
        for (const auto& y : years)
            labels << y.label();
        ChartSpec rev;
        rev.title = QStringLiteral("Revenue and net income by fiscal year");
        rev.categories = labels;
        Series r{QStringLiteral("Revenue"), {}, 0}, ni{QStringLiteral("Net income"), {}, 1};
        for (const auto& y : years) {
            r.values.append(y.revenue);
            ni.values.append(y.net_income);
        }
        rev.bars = {r, ni};
        rev.fmt = money0;
        rev.bar_labels = true;
        if (years.size() >= 2) {
            const double c = cagr_pct(years.first().revenue, years.last().revenue, years.size() - 1);
            rev.note = ok(c) ? QStringLiteral("Revenue compounded %1 a year, %2–%3").arg(spct(c), years.first().label(), years.last().label())
                             : QString();
        }
        ChartSpec mar;
        mar.title = QStringLiteral("Margins by fiscal year");
        mar.note = QStringLiteral("Percent of revenue");
        mar.categories = labels;
        Series gm{QStringLiteral("Gross"), {}, 2}, om{QStringLiteral("Operating"), {}, 0}, nm{QStringLiteral("Net"), {}, 1};
        for (const auto& y : years) {
            gm.values.append(y.gross_margin());
            om.values.append(y.operating_margin());
            nm.values.append(y.net_margin());
        }
        for (const auto& s : {gm, om, nm})
            if (std::any_of(s.values.begin(), s.values.end(), [](double v) { return ok(v); }))
                mar.lines.append(s);
        mar.fmt = percent;
        mar.free_baseline = true;
        h.row({h.img(QStringLiteral("rev"), render_chart(rev, pal, half, dpr)), h.img(QStringLiteral("margins"), render_chart(mar, pal, half, dpr))});
    }
    if (!d.extras.quarters.isEmpty()) {
        QVector<Quarter> qs = d.extras.quarters; // newest first → oldest first
        std::reverse(qs.begin(), qs.end());
        QStringList labels;
        for (const auto& q : qs)
            labels << q.label();
        ChartSpec qr;
        qr.title = QStringLiteral("Quarterly revenue and operating margin");
        qr.categories = labels;
        Series r{QStringLiteral("Revenue"), {}, 0}, m{QStringLiteral("Operating margin"), {}, 1};
        for (const auto& q : qs) {
            r.values.append(q.revenue);
            m.values.append(pct_of(q.operating_income, q.revenue));
        }
        qr.bars = {r};
        if (std::any_of(m.values.begin(), m.values.end(), [](double v) { return ok(v); })) {
            qr.lines = {m};
            qr.lines_on_right = true;
        }
        qr.fmt = money0;
        qr.fmt_right = percent;
        qr.bar_labels = true;
        if (const double g = latest_quarter_yoy_pct(d.extras.quarters); ok(g))
            qr.note = QStringLiteral("Latest quarter %1 on the same quarter a year earlier").arg(spct(g));
        ChartSpec qe;
        qe.title = QStringLiteral("Quarterly diluted EPS");
        qe.note = QStringLiteral("As reported (GAAP)");
        qe.categories = labels;
        Series e{QStringLiteral("EPS"), {}, 0};
        for (const auto& q : qs)
            e.values.append(q.eps);
        qe.bars = {e};
        qe.fmt = plain2;
        qe.bar_labels = true;
        h.row({h.img(QStringLiteral("qrev"), render_chart(qr, pal, half, dpr)), h.img(QStringLiteral("qeps"), render_chart(qe, pal, half, dpr))});
    } else if (d.has_extras) {
        h.note(QStringLiteral("No quarterly statements are available for this company."));
    }

    // ── 5. Cash and investment ─────────────────────────────────────────────
    h.section(QStringLiteral("5"), QStringLiteral("CASH AND WHERE IT GOES"),
              sccy.isEmpty() ? QString() : QStringLiteral("figures in %1").arg(sccy));
    if (d.business.valid && !d.business.investment.isEmpty())
        h.para(d.business.investment);
    if (!years.isEmpty()) {
        QStringList labels;
        for (const auto& y : years)
            labels << y.label();
        ChartSpec cf;
        cf.title = QStringLiteral("Cash generated");
        cf.categories = labels;
        Series ocf{QStringLiteral("Operating cash flow"), {}, 0}, fcf{QStringLiteral("Free cash flow"), {}, 1};
        for (const auto& y : years) {
            ocf.values.append(y.ocf);
            fcf.values.append(y.fcf);
        }
        cf.bars = {ocf, fcf};
        cf.fmt = money0;
        cf.bar_labels = true;
        cf.note = QStringLiteral("Free cash flow = operating cash flow less capital expenditure");

        ChartSpec use;
        use.title = QStringLiteral("Where the money went");
        use.note = QStringLiteral("R&D is an expense; the rest are cash outflows");
        use.categories = labels;
        use.stacked = true;
        Series rd{QStringLiteral("R&D"), {}, 0}, capex{QStringLiteral("Capex"), {}, 1}, acq{QStringLiteral("Acquisitions"), {}, 4},
            bb{QStringLiteral("Buybacks"), {}, 2}, div{QStringLiteral("Dividends"), {}, 3};
        for (const auto& y : years) {
            rd.values.append(outflow(y.rd));
            capex.values.append(outflow(y.capex));
            // Purchases of businesses are outflows; a net inflow (a disposal) is not an investment.
            acq.values.append(ok(y.acquisitions) && y.acquisitions < 0 ? -y.acquisitions : kNaN);
            bb.values.append(outflow(y.buybacks));
            div.values.append(outflow(y.dividends));
        }
        for (const auto& s : {rd, capex, acq, bb, div})
            if (std::any_of(s.values.begin(), s.values.end(), [](double v) { return ok(v) && v > 0; }))
                use.bars.append(s);
        use.fmt = money0;
        h.row({h.img(QStringLiteral("cash"), render_chart(cf, pal, half, dpr)), h.img(QStringLiteral("uses"), render_chart(use, pal, half, dpr))});

        ChartSpec bs;
        bs.title = QStringLiteral("Cash against debt at each year end");
        bs.categories = labels;
        Series cash{QStringLiteral("Cash and short-term investments"), {}, 3}, debt{QStringLiteral("Total debt"), {}, 5};
        for (const auto& y : years) {
            cash.values.append(y.cash);
            debt.values.append(y.debt);
        }
        bs.bars = {cash, debt};
        bs.fmt = money0;
        bs.bar_labels = true;

        ChartSpec inv;
        inv.title = QStringLiteral("Investment intensity");
        inv.note = QStringLiteral("Percent of revenue");
        inv.categories = labels;
        Series rdp{QStringLiteral("R&D"), {}, 0}, cxp{QStringLiteral("Capex"), {}, 1}, sbc{QStringLiteral("Stock-based pay"), {}, 4};
        for (const auto& y : years) {
            rdp.values.append(pct_of(outflow(y.rd), y.revenue));
            cxp.values.append(pct_of(outflow(y.capex), y.revenue));
            sbc.values.append(pct_of(y.sbc, y.revenue));
        }
        for (const auto& s : {rdp, cxp, sbc})
            if (std::any_of(s.values.begin(), s.values.end(), [](double v) { return ok(v); }))
                inv.lines.append(s);
        inv.fmt = percent;
        h.row({h.img(QStringLiteral("balance"), render_chart(bs, pal, half, dpr)), h.img(QStringLiteral("intensity"), render_chart(inv, pal, half, dpr))});
    }

    // ── 6. Earnings ────────────────────────────────────────────────────────
    h.section(QStringLiteral("6"), QStringLiteral("EARNINGS AGAINST EXPECTATIONS"));
    if (!d.has_earnings || !d.earnings.has_content()) {
        h.note(QStringLiteral("No earnings history or estimates are published for this company."));
    } else {
        QVector<eq::EarningsPoint> reported;
        for (const auto& p : d.earnings.history) // newest first
            if (!p.is_estimate && p.eps_actual && reported.size() < 8)
                reported.append(p);
        const BeatRecord br = beat_record(d.earnings);
        QStringList facts;
        if (br.quarters > 0)
            facts << QStringLiteral("Beat consensus in %1 of the last %2 reported quarters").arg(br.beats).arg(br.quarters);
        if (ok(br.median_surprise_pct))
            facts << QStringLiteral("median surprise %1").arg(spct(br.median_surprise_pct));
        if (ok(br.median_abs_reaction_pct))
            facts << QStringLiteral("the stock typically moved %1 either way on the print (median of %2)")
                         .arg(pct(br.median_abs_reaction_pct))
                         .arg(br.reactions);
        if (!facts.isEmpty())
            h.para(facts.join(QStringLiteral("; ")) + QLatin1Char('.'));

        QStringList cells;
        if (!reported.isEmpty()) {
            QVector<eq::EarningsPoint> asc = reported;
            std::reverse(asc.begin(), asc.end());
            ChartSpec ep;
            ep.title = QStringLiteral("EPS: reported against consensus");
            ep.note = QStringLiteral("Reported is GAAP; consensus is the street's adjusted figure");
            Series est{QStringLiteral("Consensus"), {}, 5}, act{QStringLiteral("Reported"), {}, 0};
            for (const auto& p : asc) {
                ep.categories << core::bartime::market_date_et(p.timestamp).toString(QStringLiteral("MMM yy"));
                est.values.append(opt(p.eps_estimate));
                act.values.append(opt(p.eps_actual));
            }
            ep.bars = {est, act};
            ep.fmt = plain2;
            cells << h.img(QStringLiteral("eps"), render_chart(ep, pal, half, dpr));

            ChartSpec rx;
            rx.title = QStringLiteral("How the stock moved on each report");
            rx.note = QStringLiteral("Close before the report to the close after, percent");
            rx.categories = ep.categories;
            Series mv{QStringLiteral("Move"), {}, 1};
            for (const auto& p : asc)
                mv.values.append(opt(p.reaction_pct));
            rx.bars = {mv};
            rx.fmt = percent;
            rx.bar_labels = true;
            cells << h.img(QStringLiteral("reaction"), render_chart(rx, pal, half, dpr));
            h.row(cells);
        }

        // Where consensus has moved.
        ChartSpec rev;
        rev.title = QStringLiteral("Where the EPS consensus has moved");
        rev.note = QStringLiteral("Mean estimate, 90 days ago to now");
        rev.categories = QStringList{QStringLiteral("90d ago"), QStringLiteral("60d"), QStringLiteral("30d"), QStringLiteral("7d"), QStringLiteral("Now")};
        int color = 0;
        for (const auto& t : d.earnings.trend) {
            if (t.period != QLatin1String("0y") && t.period != QLatin1String("+1y"))
                continue;
            Series s{t.label.isEmpty() ? t.period : t.label, {opt(t.d90), opt(t.d60), opt(t.d30), opt(t.d7), opt(t.current)}, color++};
            if (std::any_of(s.values.begin(), s.values.end(), [](double v) { return ok(v); }))
                rev.lines.append(s);
        }
        rev.fmt = plain2;
        rev.free_baseline = true;

        QVector<QStringList> next_rows;
        const auto& nx = d.earnings.next;
        if (nx.timestamp)
            next_rows.append({QStringLiteral("Next report"),
                              core::bartime::market_date_et(*nx.timestamp).toString(QStringLiteral("d MMM yyyy")) +
                                  (nx.is_estimated ? QStringLiteral(" (estimated)") : QString())});
        if (nx.eps_avg)
            next_rows.append({QStringLiteral("Consensus EPS"),
                              num(*nx.eps_avg, 2) + (nx.eps_low && nx.eps_high ? QStringLiteral("  (%1 – %2)").arg(num(*nx.eps_low, 2), num(*nx.eps_high, 2))
                                                                             : QString())});
        if (nx.year_ago_eps)
            next_rows.append({QStringLiteral("Same quarter last year"), num(*nx.year_ago_eps, 2)});
        if (nx.rev_avg)
            next_rows.append({QStringLiteral("Consensus revenue"), compact(*nx.rev_avg)});
        if (nx.analysts)
            next_rows.append({QStringLiteral("Analysts"), num(*nx.analysts, 0)});
        if (nx.implied && nx.implied->total_move_pct)
            next_rows.append({QStringLiteral("Move the options price in"),
                              QStringLiteral("±%1 by %2").arg(pct(*nx.implied->total_move_pct), nx.implied->expiry)});
        Html side{pal, h.half(), dpr, sink, {}, px};
        side.sub(QStringLiteral("THE COMING REPORT"));
        if (next_rows.isEmpty())
            side.note(QStringLiteral("No date or consensus is published yet."));
        else
            side.table({QStringLiteral("ITEM"), QStringLiteral("VALUE")}, next_rows);
        if (!rev.lines.isEmpty())
            h.row({h.img(QStringLiteral("revisions"), render_chart(rev, pal, half, dpr)), side.out});
        else
            h.raw(side.out);
    }

    // ── 7. Valuation and peers ─────────────────────────────────────────────
    h.section(QStringLiteral("7"), QStringLiteral("VALUATION AND PEERS"));
    {
        QVector<QStringList> rows;
        auto add = [&rows](const QString& k, const QString& v) {
            if (v != fmt::placeholder())
                rows.append({k, v});
        };
        add(QStringLiteral("Trailing P/E"), num(d.info.pe_ratio, 1));
        add(QStringLiteral("Forward P/E"), num(d.info.forward_pe, 1));
        add(QStringLiteral("PEG"), num(d.info.peg_ratio, 2));
        add(QStringLiteral("Price / book"), num(d.info.price_to_book, 1));
        add(QStringLiteral("EV / revenue"), num(d.info.ev_to_revenue, 1));
        add(QStringLiteral("EV / EBITDA"), num(d.info.ev_to_ebitda, 1));
        add(QStringLiteral("Dividend yield"), ok(d.info.dividend_yield) && d.info.dividend_yield > 0 ? pct(as_pct(d.info.dividend_yield), 2) : fmt::placeholder());
        add(QStringLiteral("Beta"), num(d.info.beta, 2));

        // What the price implies — arithmetic on the consensus, not a forecast.
        QVector<QStringList> implied;
        const auto* fy1 = estimate(d, QStringLiteral("+1y"));
        const double eps = fy1 ? opt(fy1->eps_avg) : kNaN;
        if (ok(eps) && eps > 0 && !mixed_currency(d)) {
            for (const auto& ip : {implied_price(QStringLiteral("its own trailing P/E"), d.info.pe_ratio, eps, d.info.current_price),
                                   implied_price(QStringLiteral("its own forward P/E"), d.info.forward_pe, eps, d.info.current_price),
                                   implied_price(QStringLiteral("the peer median forward P/E"), inputs.peer_forward_pe.peer_median, eps, d.info.current_price)})
                if (ok(ip.price))
                    implied.append({QStringLiteral("At ") + ip.basis, num(ip.multiple, 1) + QStringLiteral("×"), cp + num(ip.price, 2), spct(ip.vs_now_pct)});
        }
        Html left{pal, h.half(), dpr, sink, {}, px}, right{pal, h.half(), dpr, sink, {}, px};
        left.sub(QStringLiteral("MULTIPLES"));
        if (rows.isEmpty())
            left.note(QStringLiteral("No valuation multiples are published."));
        else
            left.table({QStringLiteral("MEASURE"), QStringLiteral("VALUE")}, rows);
        right.sub(QStringLiteral("WHAT A MULTIPLE WOULD PUT ON NEXT YEAR'S EARNINGS"));
        if (implied.isEmpty()) {
            right.note(mixed_currency(d) ? QStringLiteral("Not shown: earnings and the share price are in different currencies.")
                                         : QStringLiteral("Needs a positive consensus EPS for next fiscal year."));
        } else {
            right.table({QStringLiteral("MULTIPLE"), QStringLiteral("×"), QStringLiteral("PRICE"), QStringLiteral("VS NOW")}, implied);
            right.note(QStringLiteral("Consensus EPS for %1 (%2) times each multiple. Arithmetic, not a forecast: it says what "
                                      "the price assumes, not where it will go.")
                           .arg(fy1->label.isEmpty() ? QStringLiteral("next fiscal year") : fy1->label, num(eps, 2)));
        }
        h.row({left.out, right.out});
    }
    if (d.peers.size() >= 4 && own_row(d)) {
        struct Metric {
            QString title;
            double eq::PeerData::*field;
            double scale;
            Format f;
            QString id;
        };
        const Metric metrics[] = {
            {QStringLiteral("Forward P/E"), &eq::PeerData::forward_pe, 1.0, plain1, QStringLiteral("peer_pe")},
            {QStringLiteral("Revenue growth"), &eq::PeerData::revenue_growth, 100.0, percent, QStringLiteral("peer_growth")},
            {QStringLiteral("Operating margin"), &eq::PeerData::operating_margin, 100.0, percent, QStringLiteral("peer_margin")},
            {QStringLiteral("Return on equity"), &eq::PeerData::roe, 100.0, percent, QStringLiteral("peer_roe")},
        };
        QStringList cells;
        const bool four_across = width >= 960;
        const QSize quarter(four_across ? (width - 30) / 4 : h.half(), 60 + 26 * d.peers.size());
        for (const auto& m : metrics) {
            HBarSpec hb;
            hb.title = m.title;
            hb.fmt = m.f;
            QVector<double> others;
            for (int i = 0; i < d.peers.size(); ++i) {
                const auto& p = d.peers[i];
                const double v = ok(p.*(m.field)) ? p.*(m.field) * m.scale : kNaN;
                hb.labels << p.symbol;
                hb.values.append(v);
                if (p.symbol.compare(d.symbol, Qt::CaseInsensitive) == 0)
                    hb.highlight = i;
                else
                    others.append(v);
            }
            hb.reference = median(others);
            hb.reference_label = QStringLiteral("peer median");
            cells << h.img(m.id, render_hbars(hb, pal, quarter, dpr));
        }
        if (four_across) {
            h.row(cells);
        } else {
            h.row(cells.mid(0, 2));
            h.row(cells.mid(2));
        }
        h.note((d.peers_basis.isEmpty() ? QStringLiteral("Peers are a hand-kept basket for this ticker, not a classification.")
                                        : d.peers_basis) +
               QStringLiteral(" Ratios from Yahoo Finance."));
    } else {
        h.note(QStringLiteral("No peer group could be formed for this ticker, so nothing is compared."));
    }

    // ── 8. Analysts and ownership ──────────────────────────────────────────
    h.section(QStringLiteral("8"), QStringLiteral("ANALYSTS AND OWNERSHIP"));
    {
        QStringList cells;
        if (!d.extras.rec_trend.isEmpty()) {
            StackSpec st;
            st.title = QStringLiteral("Analyst ratings");
            st.note = QStringLiteral("Number of analysts at each rating");
            st.part_names = {QStringLiteral("Strong buy"), QStringLiteral("Buy"), QStringLiteral("Hold"), QStringLiteral("Sell"), QStringLiteral("Strong sell")};
            QColor pos2 = pal.positive, neg2 = pal.negative;
            pos2.setAlphaF(0.6);
            neg2.setAlphaF(0.6);
            st.part_colors = {pal.positive, pos2, pal.text_dim, neg2, pal.negative};
            for (const auto& b : d.extras.rec_trend) {
                st.row_labels << (b.period == QLatin1String("0m") ? QStringLiteral("Now")
                                                                  : QStringLiteral("%1 mo ago").arg(b.period.mid(1).chopped(1)));
                auto v = [](int n) { return n >= 0 ? double(n) : kNaN; };
                st.rows.append({v(b.strong_buy), v(b.buy), v(b.hold), v(b.sell), v(b.strong_sell)});
            }
            cells << h.img(QStringLiteral("ratings"), render_stack(st, pal, QSize(h.half(), 190), dpr));
        }
        if (ok(d.info.target_low) && ok(d.info.target_high) && d.info.target_high > d.info.target_low) {
            RangeSpec rs;
            rs.title = QStringLiteral("Analyst price targets");
            rs.note = d.info.analyst_count > 0 ? QStringLiteral("%1 analysts · twelve-month targets").arg(d.info.analyst_count) : QString();
            rs.low = d.info.target_low;
            rs.high = d.info.target_high;
            rs.mean = d.info.target_mean;
            rs.current = d.info.current_price;
            rs.mean_label = QStringLiteral("mean");
            rs.current_label = QStringLiteral("price now");
            rs.fmt = [cp](double v) { return cp + en().toString(v, 'f', v >= 100 ? 0 : 2); };
            cells << h.img(QStringLiteral("targets"), render_range(rs, pal, QSize(h.half(), 190), dpr));
        }
        if (!cells.isEmpty())
            h.row(cells);
        else
            h.note(QStringLiteral("No analyst ratings or targets are published for this company."));

        QVector<QStringList> rows;
        QVector<QVector<QColor>> colors;
        for (int i = 0; i < d.extras.actions.size() && rows.size() < 8; ++i) {
            const auto& a = d.extras.actions[i];
            if (a.action == QLatin1String("main") || a.action == QLatin1String("reit"))
                continue; // a reiterated rating is not a change
            const bool up = a.action == QLatin1String("up"), down = a.action == QLatin1String("down");
            rows.append({a.date.toString(QStringLiteral("d MMM yyyy")), a.firm,
                         up ? QStringLiteral("Upgrade") : down ? QStringLiteral("Downgrade") : QStringLiteral("New coverage"),
                         a.from_grade.isEmpty() ? a.to_grade : QStringLiteral("%1 → %2").arg(a.from_grade, a.to_grade)});
            colors.append({QColor(), QColor(), up ? pal.positive : down ? pal.negative : pal.text, QColor()});
        }
        if (!rows.isEmpty()) {
            h.sub(QStringLiteral("RATING CHANGES, LAST SIX MONTHS"));
            h.table({QStringLiteral("DATE"), QStringLiteral("FIRM"), QStringLiteral("ACTION"), QStringLiteral("RATING")}, rows, colors, 4);
        }

        QVector<QStringList> own;
        if (ok(d.info.held_institutions_pct))
            own.append({QStringLiteral("Held by institutions"), pct(as_pct(d.info.held_institutions_pct))});
        if (ok(d.info.held_insiders_pct))
            own.append({QStringLiteral("Held by insiders"), pct(as_pct(d.info.held_insiders_pct), 2)});
        if (ok(d.info.short_pct_of_float))
            own.append({QStringLiteral("Sold short, of float"), pct(as_pct(d.info.short_pct_of_float))});
        if (ok(d.info.short_ratio))
            own.append({QStringLiteral("Days of volume to cover the short"), num(d.info.short_ratio, 1)});
        if (d.insiders.known)
            own.append({QStringLiteral("Insider open-market trades, %1 months").arg(d.insiders.window_months),
                        QStringLiteral("bought %1× ($%2) · sold %3× ($%4)")
                            .arg(d.insiders.buys)
                            .arg(compact(d.insiders.buy_value))
                            .arg(d.insiders.sells)
                            .arg(compact(d.insiders.sell_value))});
        if (!own.isEmpty()) {
            h.sub(QStringLiteral("WHO OWNS IT"));
            h.table({QStringLiteral("MEASURE"), QStringLiteral("VALUE")}, own);
        }
    }

    // ── 9. Advantages and risks, in the company's own words ────────────────
    h.section(QStringLiteral("9"), QStringLiteral("ADVANTAGES AND RISKS"),
              d.filings.annual.valid() ? QStringLiteral("as the %1 describes them").arg(d.filings.annual.cite()) : QString());
    if (d.business.valid && (!d.business.advantages.isEmpty() || !d.business.risks.isEmpty())) {
        h.raw(QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0'><tr><td width='50%' valign='top' style='padding-right:14px;'>"
                             "<p style='margin:6px 0 2px 0;color:%1;font-size:%2px;font-weight:700;'>ADVANTAGES</p>%3</td>"
                             "<td width='50%' valign='top'><p style='margin:6px 0 2px 0;color:%4;font-size:%2px;font-weight:700;'>RISKS</p>%5</td></tr></table>")
                  .arg(h.c(pal.positive))
                  .arg(px)
                  .arg(h.bullets_html(d.business.advantages, pal.positive, QStringLiteral("▲")), h.c(pal.negative),
                       h.bullets_html(d.business.risks, pal.negative, QStringLiteral("▼"))));
    } else if (!d.filings.annual.risk_factors.text.isEmpty()) {
        h.sub(QStringLiteral("RISK FACTORS — FROM THE FILING"));
        h.para(d.filings.annual.risk_factors.text.left(2400) + (d.filings.annual.risk_factors.text.size() > 2400 ? QStringLiteral(" …") : QString()), true);
    } else {
        h.note(d.filings.annual_error.isEmpty() ? d.narrative_note
                                                : QStringLiteral("No annual report could be read (%1).").arg(d.filings.annual_error));
    }

    if (inputs.risk.sessions >= 60) {
        const RiskProfile& rk = inputs.risk;
        QVector<QStringList> rows;
        rows.append({QStringLiteral("Return over the period"), spct(rk.return_pct)});
        rows.append({QStringLiteral("Volatility, annualised"), pct(rk.volatility_pct)});
        rows.append({QStringLiteral("Deepest fall from a high"), pct(rk.max_drawdown_pct)});
        if (ok(rk.beta))
            rows.append({QStringLiteral("Beta to %1").arg(d.bench_symbol), num(rk.beta, 2)});
        if (ok(rk.correlation))
            rows.append({QStringLiteral("Correlation with %1").arg(d.bench_symbol), num(rk.correlation, 2)});
        h.sub(QStringLiteral("HOW THE SHARES HAVE TRADED"));
        h.table({QStringLiteral("OVER THE LAST %1 SESSIONS").arg(rk.sessions), QStringLiteral("VALUE")}, rows);
        h.note(QStringLiteral("Computed from daily closes. What the shares did, not what they will do."));
    }

    // ── 10. Outlook ────────────────────────────────────────────────────────
    h.section(QStringLiteral("10"), QStringLiteral("OUTLOOK"));
    if (d.has_earnings) {
        QVector<QStringList> rows;
        for (const char* per : {"0q", "+1q", "0y", "+1y"}) {
            if (const auto* e = estimate(d, QLatin1String(per))) {
                if (!e->eps_avg && !e->rev_avg)
                    continue;
                rows.append({e->label.isEmpty() ? e->period : e->label, e->eps_avg ? num(*e->eps_avg, 2) : fmt::placeholder(),
                             e->eps_growth ? spct(*e->eps_growth * 100.0) : fmt::placeholder(),
                             e->rev_avg ? compact(*e->rev_avg) : fmt::placeholder(),
                             e->rev_growth ? spct(*e->rev_growth * 100.0) : fmt::placeholder()});
            }
        }
        if (!rows.isEmpty()) {
            h.sub(QStringLiteral("WHAT ANALYSTS EXPECT"));
            h.table({QStringLiteral("PERIOD"), QStringLiteral("EPS"), QStringLiteral("EPS GROWTH"), QStringLiteral("REVENUE"), QStringLiteral("REVENUE GROWTH")}, rows);
            h.note(QStringLiteral("Analyst consensus from Yahoo Finance — the street's expectation, not this report's."));
        }
    }
    if (d.summary.valid) {
        h.raw(QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0'><tr><td width='50%' valign='top' style='padding-right:14px;'>"
                             "<p style='margin:10px 0 2px 0;color:%1;font-size:%2px;font-weight:700;'>THE CASE FOR</p>%3</td>"
                             "<td width='50%' valign='top'><p style='margin:10px 0 2px 0;color:%4;font-size:%2px;font-weight:700;'>THE CASE AGAINST</p>%5</td></tr></table>")
                  .arg(h.c(pal.positive))
                  .arg(px)
                  .arg(h.bullets_html(d.summary.bull_case, pal.positive, QStringLiteral("▲")), h.c(pal.negative),
                       h.bullets_html(d.summary.bear_case, pal.negative, QStringLiteral("▼"))));
        if (!d.summary.watch.isEmpty()) {
            h.sub(QStringLiteral("WHAT TO WATCH IN THE NEXT REPORT"));
            h.bullets(d.summary.watch, pal.accent);
        }
        if (!d.summary.direction.isEmpty()) {
            const QColor col = d.summary.direction == QLatin1String("up") ? pal.positive
                               : d.summary.direction == QLatin1String("down") ? pal.negative
                                                                               : pal.text;
            h.raw(QStringLiteral("<table cellspacing='0' cellpadding='10' style='margin-top:12px;'><tr><td bgcolor='%1'>"
                                 "<span style='color:%2;font-size:%3px;font-weight:700;'>THE MODEL'S READING, NEXT THREE MONTHS</span><br>"
                                 "<span style='color:%4;font-size:%5px;font-weight:700;'>%6</span>"
                                 "<span style='color:%7;font-size:%8px;'>&nbsp;&nbsp;%9</span></td></tr></table>")
                      .arg(h.c(pal.panel), h.c(pal.text_dim))
                      .arg(px - 1)
                      .arg(h.c(col))
                      .arg(px + 8)
                      .arg(d.summary.direction.toUpper(), h.c(pal.text))
                      .arg(px + 1)
                      .arg(d.summary.rationale.toHtmlEscaped()));
            h.note(QStringLiteral("A language model's reading of the figures above — a direction, deliberately without a target "
                                  "or a probability. It is an opinion to weigh, not a measured forecast."));
        }
    } else {
        h.note(d.narrative_note);
    }

    // ── Sources ────────────────────────────────────────────────────────────
    h.section(QStringLiteral("—"), QStringLiteral("SOURCES"));
    {
        QVector<QStringList> rows;
        QVector<QVector<QColor>> colors;
        for (const auto& s : d.sources) {
            const QString state = s.state == Source::State::Ready     ? QStringLiteral("loaded")
                                  : s.state == Source::State::Failed  ? QStringLiteral("unavailable")
                                  : s.state == Source::State::None    ? QStringLiteral("none")
                                                                      : QStringLiteral("loading…");
            rows.append({s.name, s.origin, state, s.detail});
            colors.append({QColor(), pal.text_dim, s.state == Source::State::Failed ? pal.negative : pal.text_dim, pal.text_dim});
        }
        h.table({QStringLiteral("WHAT"), QStringLiteral("FROM"), QStringLiteral("STATE"), QStringLiteral("DETAIL")}, rows, colors, 4);
        QStringList links;
        for (const Filing* f : {&d.filings.annual, &d.filings.quarterly})
            if (f->valid() && f->url.startsWith(QLatin1String("https://www.sec.gov/")))
                links << QStringLiteral("<a href='%1' style='color:%2;'>%3</a>").arg(f->url.toHtmlEscaped(), h.c(pal.accent), f->cite().toHtmlEscaped());
        if (!links.isEmpty())
            h.raw(QStringLiteral("<p style='margin:6px 0;color:%1;font-size:%2px;'>The filings, on sec.gov: %3</p>")
                      .arg(h.c(pal.text_dim))
                      .arg(px - 1)
                      .arg(links.join(QStringLiteral(" · "))));
        if (!d.narrative_note.isEmpty() && (d.summary.valid || d.business.valid))
            h.note(d.narrative_note);
        h.note(QStringLiteral("Every figure is the vendor's or is computed from the vendor's by the arithmetic named beside it. "
                              "Prose marked as a model's is written from the filings and figures shown here, with any number "
                              "it could not find in them removed. This report is information, not investment advice."));
    }

    h.raw(QStringLiteral("</td></tr></table></body></html>"));
    // Widths in pixels, not percent: a paginated QTextDocument (the PDF)
    // resolves a percentage against less than the page and squeezes the
    // two-column blocks into the left half.
    h.out.replace(QStringLiteral("width='100%'"), QStringLiteral("width='%1'").arg(width));
    h.out.replace(QStringLiteral("width='50%'"), QStringLiteral("width='%1'").arg((width - 14) / 2));
    return h.out;
}

// ── export ─────────────────────────────────────────────────────────────────

namespace {

// A4 less 15 mm margins is 180 mm. At 110 dpi that is 779 px — room for two
// charts side by side — and the 13 px body text prints at 8.5 pt.
constexpr int kPdfDpi = 110;
constexpr int kPdfWidth = 778;
constexpr int kHtmlWidth = 1040;

/// A document whose images come from memory.
class ImageDocument : public QTextDocument {
  public:
    QHash<QString, QImage> images;

  protected:
    QVariant loadResource(int type, const QUrl& name) override {
        if (type == QTextDocument::ImageResource && name.scheme() == QLatin1String("report"))
            return images.value(name.path().mid(1));
        return QTextDocument::loadResource(type, name);
    }
};

} // namespace

int write_report_pdf(const ReportDoc& d, const QString& path) {
    QPrinter printer(QPrinter::ScreenResolution);
    // The report's sizes are in pixels. At the printer's native resolution a
    // 12 px font is a hundredth of an inch tall; at a screen-like resolution a
    // pixel is the pixel the page was designed in. The charts are rendered at
    // three times that, so they stay sharp in print.
    printer.setResolution(kPdfDpi);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);

    ImageDocument document;
    const QString html = build_report_html(d, Palette::paper(), kPdfWidth, 3.0, [&document](const QString& id, const QImage& img) {
        document.images.insert(id, img);
        return QStringLiteral("report:/") + id;
    });
    document.setDocumentMargin(0);
    document.setPageSize(printer.pageRect(QPrinter::DevicePixel).size());
    document.setHtml(html);
    const int pages = document.pageCount();
    document.print(&printer);
    return QFileInfo(path).size() > 0 ? pages : 0;
}

bool write_report_html(const ReportDoc& d, const QString& path) {
    QString html = build_report_html(d, Palette::paper(), kHtmlWidth, 2.0, [](const QString&, const QImage& img) {
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    });
    // A browser needs to be told the encoding, and centres the page itself.
    html.replace(QStringLiteral("<html><body style='"),
                 QStringLiteral("<!doctype html><html><head><meta charset='utf-8'><title>%1 — investor report</title></head>"
                                "<body style='margin:28px;")
                     .arg(d.symbol.toHtmlEscaped()));
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(html.toUtf8()) > 0;
}

} // namespace finterm::screens::report
