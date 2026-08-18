#pragma once
// Turning an ownership register into a read-through.
//
// A table of holders answers "who owns this". It does not answer the question
// anyone actually has, which is "so what does that mean for the stock, and
// what does it mean for how it trades". This module derives that second answer.
//
// THREE RULES, because interpretation is where a research tool starts lying:
//
//  1. Every read states the number that produced it. "Tight float" on its own
//     is an opinion; "insiders and institutions hold 84% of shares out" is a
//     fact the user can check against the table above it.
//  2. Every read names its own threshold in `basis`. A user who disagrees with
//     "above 5 days to cover is elevated" can see that is the rule and discount
//     it. A number with a hidden cutoff is unfalsifiable.
//  3. A read whose inputs are missing is NOT emitted. Nothing is inferred from
//     absence — no default float, no assumed short interest. The screen shows
//     fewer reads rather than invented ones.
//
// Header-only and Qt-Core only, so the whole derivation is testable without
// standing up a widget or touching the network.

#include "screens/ownership/OwnershipTypes.h"

#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <functional>

namespace fincept::ownership {

/// How much attention a read deserves. Semantic, and deliberately separate
/// from any accent colour: Elevated means "this changes how the stock trades",
/// not "this is bad".
enum class Weight { Context, Notable, Elevated };

/// What the read is about, so the screen can group them the way a user asks
/// the question: about this stock, or about how it trades.
enum class Lens {
    Stock,   ///< what it means for this security
    Flows,   ///< what it means for how the security trades and with what
};

struct Read {
    Lens    lens = Lens::Stock;
    Weight  weight = Weight::Context;
    QString headline;  ///< four or five words
    QString detail;    ///< one sentence, carrying the driving number
    QString basis;     ///< the rule and threshold applied

    /// The number the read turns on, and the level it crossed, so a compact
    /// view can show HOW FAR past the rule this is rather than only that it
    /// is. Severity alone cannot: a register concentrated at 51% and one at
    /// 92% are both "Elevated" and look identical without these.
    ///
    /// Absent on reads with no threshold to be past — a count of activist
    /// filings is a fact, not a level — and those render the figure alone.
    std::optional<double> value;
    std::optional<double> threshold;
    /// The figure exactly as the sentence states it, so the compact view and
    /// the prose can never disagree about the same number. Carried as text
    /// because the unit differs per read: a share, a count, a number of days.
    QString value_text;
};

// ── Thresholds ──────────────────────────────────────────────────────────────
// Conventional desk levels, named here rather than buried in the branches so
// they can be argued with. None of them are the author's invention.
inline constexpr double kDaysToCoverElevated = 5.0;   // a week of volume to exit
inline constexpr double kDaysToCoverHigh     = 10.0;
inline constexpr double kShortFloatElevated  = 0.10;  // 10% of float
inline constexpr double kShortFloatHigh      = 0.20;
inline constexpr double kInstitutionalHeavy  = 0.70;  // 70% of shares out
inline constexpr double kCloselyHeld         = 0.85;  // insiders + institutions
inline constexpr double kTop5Concentrated    = 0.50;  // of reported 13F value
inline constexpr double kIndexComplexHeavy   = 0.40;  // of reported 13F value
inline constexpr double kShortInterestJump   = 0.15;  // 15% month over month

/// A book this wide is not expressing a view on each name.
///
/// This replaces guessing from firm NAMES. With the full 13F universe indexed,
/// breadth is measurable: BlackRock files 49,751 positions and Morgan Stanley
/// 45,667, and nobody holds an opinion on forty thousand companies. A book past
/// this many names is running index, model or advisory mandates, and its
/// presence on a register says nothing about the company.
///
/// Measured rather than matched, so it catches the index arm of a house nobody
/// thought to put on a list, and does not mislabel a concentrated manager who
/// happens to share a word with one.
inline constexpr int kBroadBookPositions = 1000;

inline bool is_broad_book(int position_count) {
    return position_count >= kBroadBookPositions;
}

/// Name-based fallback, used ONLY when no 13F index has been built and position
/// counts are therefore unavailable. Kept narrow and visible because it is a
/// heuristic: it cannot see an index arm under an unfamiliar name, and it is
/// deliberately not called "passive" — these houses run active mandates too and
/// a 13F does not separate them.
inline QStringList index_complexes() {
    return {QStringLiteral("blackrock"), QStringLiteral("vanguard"),
            QStringLiteral("state street"), QStringLiteral("geode"),
            QStringLiteral("ssga"),         QStringLiteral("street global")};
}

inline bool is_index_complex(const QString& holder) {
    const QString h = holder.toLower();
    const QStringList names = index_complexes();
    return std::any_of(names.cbegin(), names.cend(),
                       [&h](const QString& n) { return h.contains(n); });
}

namespace detail {

inline QString pct(double fraction, int dp = 1) {
    return QString::number(fraction * 100.0, 'f', dp) + QStringLiteral("%");
}

inline QString compact(double v) {
    const double a = std::fabs(v);
    if (a >= 1e12) return QString::number(v / 1e12, 'f', 2) + QStringLiteral("T");
    if (a >= 1e9)  return QString::number(v / 1e9,  'f', 2) + QStringLiteral("B");
    if (a >= 1e6)  return QString::number(v / 1e6,  'f', 1) + QStringLiteral("M");
    if (a >= 1e3)  return QString::number(v / 1e3,  'f', 1) + QStringLiteral("K");
    return QString::number(v, 'f', 0);
}

} // namespace detail

/// Derive the read-throughs for one snapshot.
///
/// Order is by weight then by lens, so the thing that changes how the stock
/// trades sits above the thing that merely describes it.
inline QVector<Read> derive_reads(const OwnershipSnapshot& s) {
    using detail::compact;
    using detail::pct;
    QVector<Read> out;

    // ── Float and who is sitting on it ──────────────────────────────────────
    const auto& si = s.shorts;
    if (si.held_pct_institutions && si.held_pct_insiders) {
        const double closely = *si.held_pct_institutions + *si.held_pct_insiders;
        // Yahoo's heldPercentInstitutions is institutions/FLOAT in practice, not
        // institutions/shares-outstanding, so on a closely-held small cap it
        // exceeds 1.0 on its own. Summing it with the insider figure then yields
        // "118% of shares outstanding", which is impossible — and printing an
        // impossible number as an Elevated finding destroys trust in every
        // other read on the screen. When the inputs contradict each other, say
        // nothing.
        const bool coherent = *si.held_pct_institutions <= 1.0 &&
                              *si.held_pct_insiders <= 1.0 && closely <= 1.0;
        if (coherent && closely >= kCloselyHeld) {
            out.push_back({Lens::Stock, Weight::Elevated,
                QStringLiteral("Thin tradeable float"),
                QStringLiteral("Insiders and institutions together hold %1 of shares outstanding "
                               "(%2 institutional, %3 insider), so a small share of the company "
                               "is actually available to trade.")
                    .arg(pct(closely), pct(*si.held_pct_institutions), pct(*si.held_pct_insiders)),
                QStringLiteral("Flagged above %1 combined. A thin float means each dollar of "
                               "buying or selling moves the price further.").arg(pct(kCloselyHeld, 0)), closely, kCloselyHeld, pct(closely, 0)});
        }
    }
    if (si.held_pct_institutions && *si.held_pct_institutions <= 1.0 &&
        *si.held_pct_institutions >= kInstitutionalHeavy) {
        out.push_back({Lens::Flows, Weight::Notable,
            QStringLiteral("Institutionally owned"),
            QStringLiteral("Institutions hold %1 of shares outstanding. Price is set by a small "
                           "number of professional desks rather than by retail flow, and the "
                           "stock will react to quarter-end rebalancing.")
                .arg(pct(*si.held_pct_institutions)),
            QStringLiteral("Flagged above %1 of shares outstanding.").arg(pct(kInstitutionalHeavy, 0)),
            *si.held_pct_institutions, kInstitutionalHeavy,
            pct(*si.held_pct_institutions, 0)});
    }

    // ── Concentration on the register ───────────────────────────────────────
    // Measured from the index when one exists: thousands of filers with exact
    // values, so "the five largest hold X%" is a measurement. The vendor
    // fallback is a truncated top-N and can only ever give a floor, which its
    // basis line admits.
    {
        QVector<double> values;
        double total = 0.0;
        bool complete = false;
        if (!s.smart_money.isEmpty()) {
            for (const auto& p : s.smart_money) {
                if (p.is_derivative || !p.value) continue;
                values.push_back(*p.value);
                total += *p.value;
            }
            complete = true;
        } else {
            for (const auto& h : s.holders) {
                if (!h.value) continue;
                values.push_back(*h.value);
                total += *h.value;
            }
        }
        std::sort(values.begin(), values.end(), std::greater<double>());
        const int counted = values.size();
        double top5 = 0.0;
        for (int i = 0; i < counted && i < 5; ++i) top5 += values[i];

        // A "top five of five" ratio is 100% by construction and measures
        // nothing; require enough rows for the comparison to carry information.
        if (total > 0.0 && counted >= 8) {
            const double share = top5 / total;
            if (share >= kTop5Concentrated) {
                out.push_back({Lens::Stock, Weight::Elevated,
                    QStringLiteral("Concentrated register"),
                    QStringLiteral("The five largest of %1 holders account for %2 of the "
                                   "institutional value on file. Concentrated ownership is a "
                                   "documented predictor of volatility: correlated selling by a "
                                   "few holders is what makes a position unwind violently.")
                        .arg(counted).arg(pct(share)),
                    complete
                        ? QStringLiteral("Flagged above %1, measured across every 13F filer in "
                                         "the indexed quarter.").arg(pct(kTop5Concentrated, 0))
                        : QStringLiteral("Flagged above %1 of the value across the holders the "
                                         "data provider reports — a top-N list, not the whole "
                                         "register, so this is a floor on concentration rather "
                                         "than a measure of it. Build the 13F index for the "
                                         "measured version.").arg(pct(kTop5Concentrated, 0)),
                    share, kTop5Concentrated, pct(share, 0)});
            }
        }
    }

    // ── Institutional ownership, computed rather than quoted ────────────────
    // Two sources answer this and they disagree, so the screen computes it from
    // the filings and shows the vendor's number beside it instead of silently
    // picking one. The count of filings behind the number is what makes it
    // auditable — a vendor aggregate cannot be checked against anything.
    if (s.index_shares_held > 0.0 && si.shares_outstanding && *si.shares_outstanding > 0.0) {
        const double computed = s.index_shares_held / *si.shares_outstanding;
        QString detail =
            QStringLiteral("%1 of shares outstanding, summed from %2 individual 13F filings "
                           "reporting %3 shares against %4 outstanding.")
                .arg(pct(computed))
                .arg(s.holder_universe)
                .arg(compact(s.index_shares_held), compact(*si.shares_outstanding));
        if (si.held_pct_institutions) {
            const double gap = std::fabs(computed - *si.held_pct_institutions);
            detail += gap >= 0.05
                          ? QStringLiteral(" The data vendor reports %1 — a %2 gap, which is "
                                           "what a quarter of drift between the two sources "
                                           "looks like.")
                                .arg(pct(*si.held_pct_institutions), pct(gap))
                          : QStringLiteral(" The data vendor reports %1, which agrees.")
                                .arg(pct(*si.held_pct_institutions));
        }
        QString basis =
            QStringLiteral("Computed from the filings, not quoted from a vendor: every 13F "
                           "position in the indexed quarter divided by shares outstanding. "
                           "It is a floor, because filers below the 13F threshold and "
                           "non-13F institutions do not appear.");
        // Freshness is the ceiling on this whole screen and belongs on the
        // number, not in a footnote. SEC publishes the bulk data sets only
        // after the filing window closes, so the complete source can trail the
        // vendor's by a full quarter.
        if (s.index_quarter.isValid()) {
            basis += QStringLiteral(" As of %1")
                         .arg(s.index_quarter.toString(QStringLiteral("MMM yyyy")));
            if (s.vendor_quarter.isValid() && s.vendor_quarter > s.index_quarter) {
                basis += QStringLiteral(", and the vendor already has %1 — the SEC bulk data "
                                        "sets publish after the filing window closes, so the "
                                        "complete source trails the shallow one by a quarter.")
                             .arg(s.vendor_quarter.toString(QStringLiteral("MMM yyyy")));
            } else {
                basis += QStringLiteral(".");
            }
        }
        out.push_back({Lens::Stock, Weight::Context,
                       QStringLiteral("Institutional ownership"), detail, basis,
                       computed, std::nullopt, pct(computed, 0)});
    }

    // ── Index-money weight on the register ──────────────────────────────────
    // Computed independently of the provider's holder table: with a 13F index
    // built, this is answerable from the filings alone, and gating it on a
    // separate feed meant it silently never fired.
    {
        double idx = -1.0;
        bool measured = false;
        if (!s.smart_money.isEmpty()) {
            double broad = 0.0, all = 0.0;
            for (const auto& p : s.smart_money) {
                if (p.is_derivative || !p.value)
                    continue;
                all += *p.value;
                if (is_broad_book(p.position_count))
                    broad += *p.value;
            }
            if (all > 0.0) {
                idx = broad / all;
                measured = true;
            }
        }
        if (!measured && !s.holders.isEmpty()) {
            double total = 0.0, index_value = 0.0;
            for (const auto& h : s.holders) {
                if (!h.value) continue;
                total += *h.value;
                if (is_index_complex(h.holder)) index_value += *h.value;
            }
            if (total > 0.0)
                idx = index_value / total;
        }

        if (idx >= kIndexComplexHeavy) {
            out.push_back({Lens::Flows, Weight::Notable,
                QStringLiteral("Trades on index flow"),
                QStringLiteral("%1 of institutional value sits in books that are not expressing "
                               "a view on this company. A large part of the register moves with "
                               "index and ETF flows, so the stock is less responsive to "
                               "company-specific news than its fundamentals alone suggest.")
                    .arg(pct(idx)),
                measured
                    ? QStringLiteral("Flagged above %1 of institutional value held in books of "
                                     "%2 or more positions. Breadth is measured from the filings, "
                                     "not matched against a list of firm names — a book of that "
                                     "width is running index or model mandates whatever it is "
                                     "called. Read it as index-money weight, not as a passive "
                                     "percentage.")
                          .arg(pct(kIndexComplexHeavy, 0)).arg(kBroadBookPositions)
                    : QStringLiteral("Flagged above %1 of reported value held by BlackRock, "
                                     "Vanguard, State Street or Geode. This is the fallback used "
                                     "when no 13F index has been built: it matches firm NAMES, so "
                                     "it cannot see an index arm under a name it does not know, "
                                     "and those houses run active mandates too. Build the index "
                                     "for the measured version.").arg(pct(kIndexComplexHeavy, 0)),
                idx, kIndexComplexHeavy, pct(idx, 0)});
        }
    }

    // ── The short side ──────────────────────────────────────────────────────
    if (si.short_ratio) {
        const double d = *si.short_ratio;
        if (d >= kDaysToCoverElevated) {
            const bool high = d >= kDaysToCoverHigh;
            out.push_back({Lens::Stock, high ? Weight::Elevated : Weight::Notable,
                QStringLiteral("Crowded short"),
                QStringLiteral("It would take %1 days of average volume for short sellers to "
                               "close out. Shorts cannot exit quickly, so good news forces "
                               "buying into a book that is already short.")
                    .arg(QString::number(d, 'f', 1)),
                QStringLiteral("Flagged above %1 days to cover, %2 above %3.")
                    .arg(QString::number(kDaysToCoverElevated, 'f', 0),
                         QStringLiteral("elevated"),
                         QString::number(kDaysToCoverHigh, 'f', 0)),
                d, kDaysToCoverElevated, QString::number(d, 'f', 1) + QStringLiteral("d")});
        }
    }
    if (si.pct_float && *si.pct_float >= kShortFloatElevated) {
        const bool high = *si.pct_float >= kShortFloatHigh;
        out.push_back({Lens::Stock, high ? Weight::Elevated : Weight::Notable,
            QStringLiteral("Heavily shorted"),
            QStringLiteral("%1 of the float is sold short. That is a standing bid under the "
                           "stock on any positive surprise, and a standing supply of sellers "
                           "on any disappointment.").arg(pct(*si.pct_float)),
            QStringLiteral("Flagged above %1 of float, %2 above %3.")
                .arg(pct(kShortFloatElevated, 0), QStringLiteral("elevated"),
                     pct(kShortFloatHigh, 0)),
            *si.pct_float, kShortFloatElevated, pct(*si.pct_float, 0)});
    }
    if (si.shares_short && si.shares_short_prior && *si.shares_short_prior > 0.0) {
        const double chg = (*si.shares_short - *si.shares_short_prior) / *si.shares_short_prior;
        if (std::fabs(chg) >= kShortInterestJump) {
            const bool up = chg > 0.0;
            out.push_back({Lens::Flows, Weight::Notable,
                up ? QStringLiteral("Shorts building") : QStringLiteral("Shorts covering"),
                QStringLiteral("Short interest %1 %2 since the prior settlement, from %3 to %4 "
                               "shares. Someone is changing their mind in size.")
                    .arg(up ? QStringLiteral("rose") : QStringLiteral("fell"),
                         pct(std::fabs(chg), 0), compact(*si.shares_short_prior),
                         compact(*si.shares_short)),
                QStringLiteral("Flagged at a %1 move between the two reported settlement dates.")
                    .arg(pct(kShortInterestJump, 0)),
                std::fabs(chg), kShortInterestJump,
                (up ? QStringLiteral("+") : QStringLiteral("-")) + pct(std::fabs(chg), 0)});
        }
    }

    // ── The insiders ────────────────────────────────────────────────────────
    if (!s.clusters.isEmpty()) {
        const auto& c = s.clusters.first();
        out.push_back({Lens::Stock, Weight::Elevated,
            QStringLiteral("Insider cluster buy"),
            QStringLiteral("%1 insiders bought on the open market within a month around %2%3. "
                           "Several people with the same private information acting the same "
                           "way is the insider signal with the strongest evidence behind it.")
                .arg(c.insiders.size())
                .arg(c.start.toString(QStringLiteral("d MMM yyyy")),
                     c.total_value > 0.0 ? QStringLiteral(", totalling $") + compact(c.total_value)
                                         : QString()),
            QStringLiteral("Two or more distinct insiders with open-market purchases (code P) "
                           "inside 30 days. Grants and option exercises are excluded — those "
                           "vest on a calendar, they are not decisions."),
            static_cast<double>(c.insiders.size()), std::nullopt,
            QStringLiteral("%1 buyers").arg(c.insiders.size())});
    }
    int opportunistic_buyers = 0;
    for (const auto& p : s.insiders)
        if (p.pattern == Pattern::Opportunistic) ++opportunistic_buyers;
    if (opportunistic_buyers > 0) {
        out.push_back({Lens::Stock, Weight::Context,
            QStringLiteral("Opportunistic filers present"),
            QStringLiteral("%1 of %2 insiders trade on no annual schedule. The predictive content "
                           "in insider trading concentrates in these filers rather than in the "
                           "ones who sell the same month every year.")
                .arg(opportunistic_buyers).arg(s.insiders.size()),
            QStringLiteral("Routine means a trade in the same calendar month in three or more "
                           "years of that insider's own filing history (Cohen, Malloy and "
                           "Pomorski). Insiders without enough history are left unclassified."),
            static_cast<double>(opportunistic_buyers), std::nullopt,
            QStringLiteral("%1 of %2").arg(opportunistic_buyers).arg(s.insiders.size())});
    }

    // ── Activists ───────────────────────────────────────────────────────────
    int activist_filings = 0;
    QDate latest_activist;
    for (const auto& st : s.stakes) {
        if (!st.activist) continue;
        ++activist_filings;
        if (!latest_activist.isValid() || st.filed_date > latest_activist)
            latest_activist = st.filed_date;
    }
    if (activist_filings > 0) {
        out.push_back({Lens::Stock, Weight::Elevated,
            QStringLiteral("Activist on the register"),
            QStringLiteral("%1 Schedule 13D filing%2 in the window, most recently %3. A 13D is a "
                           "holder above 5%4 declaring an intent to influence the company — a "
                           "dated catalyst, not a snapshot.")
                .arg(activist_filings)
                .arg(activist_filings == 1 ? QString() : QStringLiteral("s"),
                     latest_activist.toString(QStringLiteral("d MMM yyyy")),
                     QStringLiteral("%")),
            QStringLiteral("13D means activist intent and is due within five business days; 13G "
                           "is the passive equivalent and is not counted here."),
            static_cast<double>(activist_filings), std::nullopt,
            QStringLiteral("%1 filing%2").arg(activist_filings)
                .arg(activist_filings == 1 ? QString() : QStringLiteral("s"))});
    }

    std::stable_sort(out.begin(), out.end(), [](const Read& a, const Read& b) {
        return static_cast<int>(a.weight) > static_cast<int>(b.weight);
    });
    return out;
}

/// Reads for one lens, preserving derive_reads' ordering.
inline QVector<Read> reads_for(const QVector<Read>& all, Lens lens) {
    QVector<Read> out;
    for (const auto& r : all)
        if (r.lens == lens) out.push_back(r);
    return out;
}

} // namespace fincept::ownership
