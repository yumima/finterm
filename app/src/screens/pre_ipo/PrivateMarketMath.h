// src/screens/pre_ipo/PrivateMarketMath.h
#pragma once
// Pure arithmetic and matching for the Private Market screen — no Qt widgets,
// no services, so it is unit-tested directly (tests/screens/test_private_market_math.cpp).
//
// Valuation discipline. A marketplace price is per share; a valuation needs a
// share count, which no free source publishes. The one honest count available
// is the one a round implies: post-money ÷ price per share, from the SAME
// round, from the SAME source as the price. So:
//   implied valuation = model price × (post-money / pps of the newest round
//                       that states both)
// and it is labelled with the round it rests on. Mixing sources is wrong in a
// way that is easy to miss: NPM and Clarity carry Anthropic on different share
// bases ($89 vs $589 per share, 2026-11-17), so one's price over the other's
// round reads as a 6x discount.

#include "screens/pre_ipo/PreIpoTypes.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>

namespace finterm::pre_ipo::pm {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline bool usable(double x) {
    return std::isfinite(x) && x > 0;
}

/// Newest round that states a price per share (rounds are newest first).
inline const FinancingRound* last_priced_round(const QVector<FinancingRound>& rounds) {
    for (const auto& r : rounds)
        if (usable(r.pps))
            return &r;
    return nullptr;
}

/// Newest round that states a post-money valuation.
inline const FinancingRound* last_valued_round(const QVector<FinancingRound>& rounds) {
    for (const auto& r : rounds)
        if (usable(r.post_money_usd))
            return &r;
    return nullptr;
}

/// Newest round stating BOTH post-money and price per share — the round whose
/// share count the implied valuation borrows.
inline const FinancingRound* share_basis_round(const QVector<FinancingRound>& rounds) {
    for (const auto& r : rounds)
        if (usable(r.pps) && usable(r.post_money_usd))
            return &r;
    return nullptr;
}

/// Shares implied by share_basis_round, NaN when no round states both.
inline double implied_shares(const QVector<FinancingRound>& rounds) {
    const FinancingRound* r = share_basis_round(rounds);
    return r ? r->post_money_usd / r->pps : kNaN;
}

/// Model price × implied shares; NaN when either is missing.
inline double implied_valuation(const MarketQuote& q, const QVector<FinancingRound>& rounds) {
    const double sh = implied_shares(rounds);
    return q.has_price() && usable(sh) ? q.price * sh : kNaN;
}

/// (price / ref − 1) × 100; NaN unless both are positive.
inline double premium_pct(double price, double ref) {
    return usable(price) && usable(ref) ? (price / ref - 1.0) * 100.0 : kNaN;
}

// ── Matching a company to the marketplace's slug ───────────────────────────

/// Lowercase, non-alphanumerics → '-', trimmed.
inline QString slugify(const QString& s) {
    static const QRegularExpression non_alnum(QStringLiteral("[^a-z0-9]+"));
    QString out = s.toLower();
    out.replace(non_alnum, QStringLiteral("-"));
    while (out.endsWith('-'))
        out.chop(1);
    while (out.startsWith('-'))
        out.remove(0, 1);
    return out;
}

/// The name without trailing legal-form words ("Stripe, Inc." → "Stripe",
/// "Anthropic PBC" → "Anthropic"). Only legal forms: "Technologies" or
/// "Labs" are part of names ("Ripple Labs") and stay.
inline QString strip_legal_form(const QString& name) {
    static const QSet<QString> legal{QStringLiteral("inc"),     QStringLiteral("incorporated"), QStringLiteral("corp"),
                                     QStringLiteral("corporation"), QStringLiteral("co"),       QStringLiteral("company"),
                                     QStringLiteral("llc"),     QStringLiteral("ltd"),          QStringLiteral("limited"),
                                     QStringLiteral("plc"),     QStringLiteral("pbc"),          QStringLiteral("lp"),
                                     QStringLiteral("sa"),      QStringLiteral("ag"),           QStringLiteral("gmbh"),
                                     QStringLiteral("bv"),      QStringLiteral("nv")};
    static const QRegularExpression sep(QStringLiteral("[^A-Za-z0-9&]+"));
    QStringList words = name.split(sep, Qt::SkipEmptyParts);
    while (words.size() > 1 && legal.contains(words.last().toLower()))
        words.removeLast();
    return words.join(' ');
}

/// The marketplace slug for a company, or empty. Tried in order: a slug it
/// already carries, its id, then each of its names (display name first, then
/// aliases) as given and without the legal form. Exact slug equality only — a
/// near miss binding Stripe's price to some other "Stripe" filer is worse than
/// no price.
inline QString match_slug(const QString& current, const QString& id, const QString& name, const QStringList& aliases,
                          const QSet<QString>& universe) {
    if (universe.isEmpty())
        return {};
    auto hit = [&](const QString& s) { return !s.isEmpty() && universe.contains(s); };
    if (hit(current))
        return current;
    if (hit(id))
        return id;
    QStringList names{name};
    names += aliases;
    for (const auto& n : names) {
        const QString a = slugify(n);
        if (hit(a))
            return a;
        const QString b = slugify(strip_legal_form(n));
        if (hit(b))
            return b;
    }
    return {};
}

/// "ripple-labs" → "Ripple Labs": a display name for a slug whose page has
/// not been read yet (the sitemap carries slugs only).
inline QString display_from_slug(const QString& slug) {
    QStringList parts = slug.split('-', Qt::SkipEmptyParts);
    for (auto& p : parts)
        if (!p.isEmpty())
            p[0] = p[0].toUpper();
    return parts.join(' ');
}

} // namespace finterm::pre_ipo::pm
