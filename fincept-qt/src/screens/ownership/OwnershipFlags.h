#pragma once
// The interpretive layer of Ownership: the header figures and the three flags.
//
// A table of holders answers "who owns this". The question a reader actually
// has is "so what does that mean for how it trades", and this is the whole of
// the answer the evidence supports — three findings, each with a documented
// direction, each stated with the number that produced it, the rule applied,
// the paper the rule comes from, and the date the number was true.
//
//   Insider cluster buy   — several insiders buying on the open market within
//                           a month. Lakonishok & Lee: +7% a year in small
//                           caps, t = 3.1. The one narrow alpha in this data.
//   Short-constrained     — days to cover and short interest as a share of
//                           float both elevated. Hong et al.: days to cover is
//                           the short-side measure that survived post-2000.
//   Concentrated register — the ten largest holders own a large share of the
//                           company. Ben-David et al.: a volatility read with a
//                           conditional drawdown, never a return forecast.
//
// Everything that used to be a "read" and is not one of these — institutional
// ownership as a level, index-flow weight, a short-interest jump, the mere
// presence of opportunistic filers — is shown as a number in the header and
// interpreted by nobody. The evidence file in plans/research says why.
//
// Rules, because interpretation is where a research tool starts lying:
//  1. Every flag states the number that produced it.
//  2. Every flag names its own threshold, so it can be argued with.
//  3. A flag whose inputs are missing is NOT emitted. Nothing is inferred from
//     absence.
//
// Header-only and Qt-Core only, so the whole derivation is testable without
// standing up a widget or touching the network.

#include "screens/ownership/OwnershipTypes.h"

#include <QDate>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace fincept::ownership {

// ── Thresholds ──────────────────────────────────────────────────────────────
// Named here rather than buried in the branches so they can be argued with.
inline constexpr int    kClusterWindowDays   = 30;    // Lakonishok & Lee's month
inline constexpr int    kClusterMinInsiders  = 2;     // OpenInsider's default group floor
inline constexpr double kDaysToCoverElevated = 5.0;   // a week of volume to exit
inline constexpr double kShortFloatElevated  = 0.10;  // 10% of float
inline constexpr double kSirioTopDecile      = 0.90;  // Drechsler & Drechsler's decile 10
inline constexpr double kTop10OfSharesOut    = 0.40;  // ten holders own 40% of the company
inline constexpr int    kHeaderWindowMonths  = 6;     // Bloomberg OWN's insider window

/// Open-market purchases by two or more distinct insiders inside a window.
///
/// Computed here rather than trusted from the parser so the exclusions that
/// the evidence turns on — 10% owners, 10b5-1 plan trades — are applied in
/// one place and tested. Joint filers on one filing share a name and count
/// once; two trades by the same person are one buyer.
inline QVector<BuyCluster> find_cluster_buys(const QVector<InsiderTransaction>& tx,
                                             int window_days = kClusterWindowDays,
                                             int min_insiders = kClusterMinInsiders) {
    QVector<const InsiderTransaction*> buys;
    for (const auto& t : tx)
        if (t.scorable_buy() && t.date.isValid())
            buys.push_back(&t);
    std::sort(buys.begin(), buys.end(),
              [](const InsiderTransaction* a, const InsiderTransaction* b) {
                  return a->date < b->date;
              });

    QVector<BuyCluster> out;
    int i = 0;
    while (i < buys.size()) {
        // Grow a window from this buy; the cluster is the widest run in which
        // every consecutive pair is within the window.
        int j = i;
        while (j + 1 < buys.size() && buys[j]->date.daysTo(buys[j + 1]->date) <= window_days)
            ++j;
        QSet<QString> names;
        double total = 0.0;
        for (int k = i; k <= j; ++k) {
            names.insert(buys[k]->insider);
            total += buys[k]->value.value_or(0.0);
        }
        if (names.size() >= min_insiders) {
            BuyCluster c;
            c.start = buys[i]->date;
            c.end = buys[j]->date;
            c.insiders = QStringList(names.begin(), names.end());
            c.insiders.sort();
            c.total_value = total;
            out.push_back(c);
        }
        i = j + 1;
    }
    // Newest first, so the header line names the current one.
    std::reverse(out.begin(), out.end());
    return out;
}

/// The figures the header shows. Each is present only when its inputs are.
struct HeaderStats {
    /// Institutional shares from the filings over the vendor's share count.
    std::optional<double> inst_pct_out;
    std::optional<double> inst_pct_float;
    std::optional<double> vendor_inst_pct;   ///< the vendor's own figure, for comparison
    std::optional<double> insiders_pct;
    std::optional<double> top10_share;       ///< of institutional value
    std::optional<double> top10_pct_out;     ///< of shares outstanding
    std::optional<double> broad_share;

    std::optional<double> si_pct_float;
    std::optional<double> days_to_cover;
    std::optional<double> sirio;             ///< short interest ÷ 13F shares
    std::optional<double> si_change_pct;
    QDate si_settlement;
    QDate si_published_after;

    int insider_buys = 0;    ///< scorable open-market buys in the window
    int insider_sells = 0;   ///< open-market sells, shown never scored
    double insider_buy_value = 0.0;
    double insider_sell_value = 0.0;
    int activist_filings = 0;
    QDate latest_activist;
};

inline HeaderStats derive_header(const OwnershipSnapshot& s,
                                 const QDate& today = QDate::currentDate()) {
    HeaderStats h;
    const auto& v = s.vendor;
    const auto& sum = s.summary;

    if (s.holders_ok && sum.total_shares > 0.0) {
        if (v.shares_outstanding && *v.shares_outstanding > 0.0)
            h.inst_pct_out = sum.total_shares / *v.shares_outstanding;
        if (v.float_shares && *v.float_shares > 0.0)
            h.inst_pct_float = sum.total_shares / *v.float_shares;
    }
    // The vendor's figure is institutions ÷ FLOAT in practice, and on a
    // closely held name it exceeds 1.0. Shown for comparison; an incoherent
    // one is not shown at all.
    if (v.held_pct_institutions && *v.held_pct_institutions >= 0.0 &&
        *v.held_pct_institutions <= 1.0)
        h.vendor_inst_pct = v.held_pct_institutions;
    if (v.held_pct_insiders && *v.held_pct_insiders >= 0.0 && *v.held_pct_insiders <= 1.0)
        h.insiders_pct = v.held_pct_insiders;

    if (s.holders_ok) {
        h.top10_share = sum.top10_share;
        h.broad_share = sum.broad_share;
        if (sum.top10_share && h.inst_pct_out)
            h.top10_pct_out = *sum.top10_share * *h.inst_pct_out;
    }

    if (const auto* r = s.short_history.latest()) {
        h.si_settlement = r->settlement;
        h.si_published_after = r->published_after;
        h.days_to_cover = r->days_to_cover;
        h.si_change_pct = r->change_pct;
        if (r->shares_short && v.float_shares && *v.float_shares > 0.0)
            h.si_pct_float = *r->shares_short / *v.float_shares;
        if (r->shares_short && s.holders_ok && sum.total_shares > 0.0)
            h.sirio = *r->shares_short / sum.total_shares;
    } else if (v.shares_short || v.short_pct_float) {
        // Vendor fallback: one reading, no history, its own date.
        h.si_settlement = v.short_as_of;
        h.si_pct_float = v.short_pct_float;
        h.days_to_cover = v.short_ratio;
        if (v.shares_short && s.holders_ok && sum.total_shares > 0.0)
            h.sirio = *v.shares_short / sum.total_shares;
    }

    const QDate since = today.addMonths(-kHeaderWindowMonths);
    for (const auto& t : s.transactions) {
        if (!t.open_market || t.derivative || !t.date.isValid() || t.date < since)
            continue;
        if (t.acquired) {
            if (!t.scorable_buy())
                continue;   // a 10% owner or a plan buy is not counted as a decision
            ++h.insider_buys;
            h.insider_buy_value += t.value.value_or(0.0);
        } else {
            ++h.insider_sells;
            h.insider_sell_value += t.value.value_or(0.0);
        }
    }
    for (const auto& st : s.stakes) {
        if (!st.activist)
            continue;
        ++h.activist_filings;
        if (!h.latest_activist.isValid() || st.filed_date > h.latest_activist)
            h.latest_activist = st.filed_date;
    }
    return h;
}

enum class FlagKind { InsiderClusterBuy, ShortConstrained, Concentrated };

struct Flag {
    FlagKind kind = FlagKind::Concentrated;
    QString  headline;   ///< three or four words
    QString  detail;     ///< one sentence carrying the driving number
    QString  basis;      ///< the rule, its threshold, its source
    QDate    as_of;      ///< the date the driving number was true
};

namespace detail {
inline QString pct(double fraction, int dp = 0) {
    return QString::number(fraction * 100.0, 'f', dp) + QStringLiteral("%");
}
inline QString compact(double v) {
    const double a = std::fabs(v);
    if (a >= 1e9) return QString::number(v / 1e9, 'f', 1) + QStringLiteral("B");
    if (a >= 1e6) return QString::number(v / 1e6, 'f', 1) + QStringLiteral("M");
    if (a >= 1e3) return QString::number(v / 1e3, 'f', 0) + QStringLiteral("K");
    return QString::number(v, 'f', 0);
}
} // namespace detail

/// The three flags, in the order they matter: the one with alpha behind it,
/// then the two risk reads.
inline QVector<Flag> derive_flags(const OwnershipSnapshot& s,
                                  const QDate& today = QDate::currentDate()) {
    using detail::compact;
    using detail::pct;
    QVector<Flag> out;
    const HeaderStats h = derive_header(s, today);

    // ── Insider cluster buy ─────────────────────────────────────────────────
    const auto clusters = find_cluster_buys(s.transactions);
    if (!clusters.isEmpty()) {
        const auto& c = clusters.first();
        Flag f;
        f.kind = FlagKind::InsiderClusterBuy;
        f.headline = QStringLiteral("Insider cluster buy");
        f.detail = QStringLiteral("%1 insiders bought on the open market between %2 and %3%4.")
                       .arg(c.insiders.size())
                       .arg(c.start.toString(QStringLiteral("d MMM yyyy")),
                            c.end.toString(QStringLiteral("d MMM yyyy")),
                            c.total_value > 0.0
                                ? QStringLiteral(", $") + compact(c.total_value) + QStringLiteral(" in total")
                                : QString());
        f.basis = QStringLiteral("%1 or more distinct insiders with code-P purchases inside %2 "
                                 "days; 10% owners and 10b5-1 plan trades excluded. Lakonishok "
                                 "& Lee 2001: +4.8%/yr, +7.3%/yr in small caps (t 3.1). The "
                                 "return is front-loaded — most of it inside a month.")
                      .arg(kClusterMinInsiders)
                      .arg(kClusterWindowDays);
        f.as_of = c.end;
        out.push_back(f);
    }

    // ── Short-constrained ───────────────────────────────────────────────────
    {
        const bool by_levels = h.days_to_cover && h.si_pct_float &&
                               *h.days_to_cover >= kDaysToCoverElevated &&
                               *h.si_pct_float >= kShortFloatElevated;
        const bool by_decile = s.sirio_percentile && *s.sirio_percentile >= kSirioTopDecile &&
                               h.sirio;
        if (by_levels || by_decile) {
            Flag f;
            f.kind = FlagKind::ShortConstrained;
            f.headline = QStringLiteral("Short-constrained");
            QStringList bits;
            if (h.days_to_cover)
                bits << QStringLiteral("%1 days to cover").arg(QString::number(*h.days_to_cover, 'f', 1));
            if (h.si_pct_float)
                bits << QStringLiteral("%1 of float short").arg(pct(*h.si_pct_float, 1));
            if (by_decile)
                bits << QStringLiteral("short interest ÷ 13F shares in the top decile "
                                       "(%1 of %2 stocks)")
                            .arg(pct(*s.sirio_percentile), QString::number(s.sirio_universe));
            f.detail = bits.join(QStringLiteral(" · ")) +
                       QStringLiteral(". Shorts cannot exit quickly, and the borrow is scarce; "
                                      "the position is fragile in both directions.");
            f.basis = QStringLiteral("Days to cover ≥ %1 with ≥ %2 of float short (Hong et al. "
                                     "2015: 1.19%/mo, t 6.7, a small-cap effect), or short "
                                     "interest ÷ institutional shares in the top decile "
                                     "(Drechsler & Drechsler: 1.51%/mo four-factor alpha, "
                                     "t 9.0). Squeeze cost at high utilisation eats two-thirds "
                                     "of it (Schultz 2024) — a risk statement, not a trade.")
                          .arg(QString::number(kDaysToCoverElevated, 'f', 0),
                               pct(kShortFloatElevated));
            f.as_of = h.si_settlement;
            out.push_back(f);
        }
    }

    // ── Concentrated register ───────────────────────────────────────────────
    if (h.top10_pct_out && *h.top10_pct_out >= kTop10OfSharesOut) {
        Flag f;
        f.kind = FlagKind::Concentrated;
        f.headline = QStringLiteral("Concentrated register");
        f.detail = QStringLiteral("The ten largest 13F holders own %1 of the company (%2 of "
                                  "institutional value, across %3 filers).")
                       .arg(pct(*h.top10_pct_out), pct(*h.top10_share),
                            QString::number(s.summary.holder_count));
        f.basis = QStringLiteral("Top-10 holders ≥ %1 of shares outstanding. Ben-David et al. "
                                 "2021: concentrated ownership raises volatility and returns "
                                 "fall in the worst 5%% of quarters, ≈ zero otherwise. A "
                                 "volatility read, not a forecast — and the count of owners "
                                 "cuts the other way (Greenwood & Thesmar).")
                      .arg(pct(kTop10OfSharesOut));
        f.as_of = s.summary.quarter;
        out.push_back(f);
    }
    return out;
}

/// Forward returns from a trade date, from a [["YYYY-MM-DD", close], ...]
/// series. A window that has not elapsed yet stays absent — "not yet" is not
/// a return.
struct ForwardReturns {
    std::optional<double> r1w, r1m, r3m;
};

inline ForwardReturns forward_returns(const QJsonArray& closes, const QDate& trade,
                                      const QDate& today = QDate::currentDate()) {
    ForwardReturns r;
    if (!trade.isValid() || closes.isEmpty())
        return r;
    const QVector<QDate> marks{trade, trade.addDays(7), trade.addMonths(1), trade.addMonths(3)};
    const auto px = closes_on_or_before(closes, marks);
    const auto& base = px[0];
    if (!base || *base <= 0.0)
        return r;
    auto at = [&](int i) -> std::optional<double> {
        if (marks[i] > today || !px[i] || *px[i] <= 0.0)
            return std::nullopt;
        return *px[i] / *base - 1.0;
    };
    r.r1w = at(1);
    r.r1m = at(2);
    r.r3m = at(3);
    return r;
}

} // namespace fincept::ownership
