// src/screens/pre_ipo/PrivateMarketFormat.h
#pragma once
// Text for the Private Market screen and its dashboard widgets: money and
// percent strings that say "—" for an absent number (never "$0"), and the
// headline valuation a row leads with — with what kind of number it is.

#include "screens/pre_ipo/PrivateMarketMath.h"

#include <QString>

#include <cmath>

namespace finterm::pre_ipo::pmfmt {



inline bool finite(double x) {
    return std::isfinite(x);
}

inline QString money_usd(double usd) {
    if (!pm::usable(usd))
        return QStringLiteral("—");
    if (usd >= 1e12)
        return QString("$%1T").arg(usd / 1e12, 0, 'f', 2);
    if (usd >= 1e9)
        return QString("$%1B").arg(usd / 1e9, 0, 'f', usd >= 1e11 ? 0 : 1);
    if (usd >= 1e6)
        return QString("$%1M").arg(usd / 1e6, 0, 'f', usd >= 1e8 ? 0 : 1);
    return QString("$%1K").arg(usd / 1e3, 0, 'f', 0);
}

inline QString pps_text(double pps) {
    return pm::usable(pps) ? QString("$%1").arg(pps, 0, 'f', 2) : QStringLiteral("—");
}

inline QString pct_text(double p, int prec = 1) {
    if (!finite(p))
        return QStringLiteral("—");
    return QString("%1%2%").arg(p >= 0 ? "+" : "").arg(p, 0, 'f', prec);
}

inline QString pct_html(double p, int prec = 1) {
    if (!finite(p))
        return QStringLiteral("<span class='muted'>—</span>");
    return QString("<span class='%1'>%2</span>").arg(p >= 0 ? "pos" : "neg", pct_text(p, prec));
}

inline QString round_label(const pre_ipo::FinancingRound& r) {
    QString s = r.series.isEmpty() ? QStringLiteral("Round") : r.series;
    if (r.month.isValid())
        s += QStringLiteral(" · ") + r.month.toString("MMM yyyy");
    return s;
}

/// Latest N-PORT consensus, when it sits on the marketplace's share basis.
inline double same_basis_mark(const pre_ipo::PrivateCompany& c) {
    const double m = c.analytics.consensus_mark_pps;
    if (!pm::usable(m))
        return pm::kNaN;
    const double ref = c.market.has_price() ? c.market.price : pm::kNaN;
    if (pm::usable(ref) && (m / ref > 3.0 || m / ref < 1.0 / 3.0))
        return pm::kNaN;
    return m;
}

/// The valuation a row leads with, and what it is: implied by the model
/// price, else the newest stated post-money, else the list row's figure,
/// else the curated seed.
inline double headline_valuation(const pre_ipo::PrivateCompany& c, QString* what) {
    const double implied = pm::implied_valuation(c.market, c.financing);
    if (pm::usable(implied)) {
        if (what)
            *what = QStringLiteral("implied");
        return implied;
    }
    if (const auto* r = pm::last_valued_round(c.financing)) {
        if (what)
            *what = QStringLiteral("last round");
        return r->post_money_usd;
    }
    if (pm::usable(c.market.list_last_round_valuation)) {
        if (what)
            *what = QStringLiteral("last round");
        return c.market.list_last_round_valuation;
    }
    if (c.last_valuation_usd > 0) {
        if (what)
            *what = QStringLiteral("reported");
        return c.last_valuation_usd;
    }
    return pm::kNaN;
}

inline QString strip_digits_cik(const QString& cik) {
    QString s = cik.trimmed();
    while (s.startsWith('0'))
        s.remove(0, 1);
    return s;
}

inline QString name_key(const QString& name) {
    return pm::slugify(pm::strip_legal_form(name));
}


} // namespace finterm::pre_ipo::pmfmt
