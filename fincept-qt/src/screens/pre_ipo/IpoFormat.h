// src/screens/pre_ipo/IpoFormat.h
#pragma once

#include <QString>
#include <QStringList>

/// Pure parsing/formatting for the IPO Watch screen.
///
/// Extracted from IpoWatchView so it can be tested without linking a QWidget.
/// Both functions here have shipped a wrong-but-plausible number:
///
///   • parse_price_mid returned the LOW end of a range while reporting
///     success whenever the high end failed to parse ("$8.00-$10.00 per ADS"),
///     understating deal size by the full width of the range and biasing the
///     pop % that divides by it.
///   • a share count was formatted with the CURRENCY formatter, so 12,000,000
///     unlocking shares rendered as "$12M" under a header reading SHARES.
///
/// Neither failed loudly; both produced a figure that looked right. That is
/// the argument for keeping them pure and pinned by tests.
namespace fincept::pre_ipo::fmt {

/// Compact money ("$1.25B", "$350M", "$12K"). Em-dash for non-positive.
inline QString money(double dollars) {
    if (dollars <= 0)   return QStringLiteral("—");
    if (dollars >= 1e9) return QStringLiteral("$%1B").arg(dollars / 1e9, 0, 'f', 2);
    if (dollars >= 1e6) return QStringLiteral("$%1M").arg(dollars / 1e6, 0, 'f', 0);
    return QStringLiteral("$%1K").arg(dollars / 1e3, 0, 'f', 0);
}

/// Compact bare magnitude for quantities that are NOT money — share counts,
/// volumes. Deliberately adjacent to money() because confusing the two is the
/// specific mistake this exists to prevent.
inline QString count(double n) {
    if (n <= 0)   return QStringLiteral("—");
    if (n >= 1e9) return QStringLiteral("%1B").arg(n / 1e9, 0, 'f', 2);
    if (n >= 1e6) return QStringLiteral("%1M").arg(n / 1e6, 0, 'f', 1);
    if (n >= 1e3) return QStringLiteral("%1K").arg(n / 1e3, 0, 'f', 0);
    return QString::number(n, 'f', 0);
}

/// Leading numeric run of `s`, so unit suffixes ("10.00 per ADS", "17.00*")
/// don't reject an otherwise-parseable number. `ok` is false when there is no
/// leading number at all.
inline double leading_number(const QString& s, bool* ok) {
    const QString t = s.trimmed();
    int end = 0;
    while (end < t.size() && (t.at(end).isDigit() || t.at(end) == '.' ||
                              ((end == 0) && (t.at(end) == '+' || t.at(end) == '-'))))
        ++end;
    if (end == 0) { *ok = false; return 0; }
    return t.left(end).toDouble(ok);
}

/// Midpoint of a filed price range ("$15.00-$17.00" → 16.00).
///
/// Returns 0 with *ok == false whenever the range cannot be resolved to a
/// midpoint with confidence. Callers already render an em-dash for that, which
/// is the honest outcome — the alternative, silently returning one end, is a
/// deal size that is wrong by 10–20% and looks exactly like a right one.
inline double price_mid(const QString& price_range, bool* ok_out = nullptr) {
    if (ok_out) *ok_out = false;
    if (price_range.isEmpty()) return 0;
    QString pr = price_range;
    pr.remove('$');
    const QStringList parts = pr.split('-', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return 0;

    bool ok_lo = false;
    const double lo = leading_number(parts.at(0), &ok_lo);
    if (!ok_lo) return 0;

    if (parts.size() == 1) {
        // A single part is one PRICE only if no second number hides behind a
        // separator we don't split on — Nasdaq also emits en dashes and the
        // word "to" ("$15.00 – $17.00", "$15.00 to $17.00").
        const QString only = parts.at(0).trimmed();
        int num_end = 0;
        while (num_end < only.size() &&
               (only.at(num_end).isDigit() || only.at(num_end) == '.' ||
                only.at(num_end) == '+' || only.at(num_end) == '-'))
            ++num_end;
        for (int i = num_end; i < only.size(); ++i)
            if (only.at(i).isDigit())
                return 0;   // an unpaired second number — unknown, not lo
        if (ok_out) *ok_out = true;
        return lo;
    }

    bool ok_hi = false;
    const double hi = leading_number(parts.at(1), &ok_hi);
    if (!ok_hi) return 0;   // a range we cannot resolve is NOT its low end
    if (ok_out) *ok_out = true;
    return (lo + hi) / 2.0;
}

} // namespace fincept::pre_ipo::fmt
