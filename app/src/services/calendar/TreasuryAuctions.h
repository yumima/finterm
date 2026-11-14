#pragma once
// TreasuryAuctions.h — US Treasury auctions from TreasuryDirect's public
// securities API (no key):
//   https://www.treasurydirect.gov/TA_WS/securities/upcoming?format=json
//   https://www.treasurydirect.gov/TA_WS/securities/auctioned?format=json&days=N
// "upcoming" holds announced auctions (with offering size and close time) and
// the tentative schedule (no size, no time); "auctioned" holds results.
//
// Kept: bills of 13 weeks and longer, every note and bond (incl. TIPS, FRNs).
// Cash-management bills and 4/6/8-week bills are routine funding and dropped.
// Header-only, Qt-Core-only, pure; tests/services/test_macro_releases.cpp.

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimeZone>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace finterm::treasury_auctions {

struct Auction {
    QString cusip;
    QString type;   ///< Bill / Note / Bond (TIPS and FRN are Notes/Bonds with a flag)
    QString term;   ///< "13-Week", "10-Year" (the ORIGINAL term for a reopening)
    QString label;  ///< "13W bill", "10Y note (reopening)", "5Y TIPS"
    QDate date;     ///< competitive close date
    QDateTime close; ///< ET instant of the competitive close; invalid when not announced
    bool announced = false; ///< offering size published (false: tentative schedule)
    bool coupon = false;    ///< note or bond
    int years = 0;          ///< original term in whole years (coupons), 0 for bills
    double offering = NAN;  ///< $
    // Results (auctioned only) — NaN when absent.
    double high_yield = NAN; ///< coupons: high yield %; bills: high discount rate %
    double investment_rate = NAN; ///< bills: bond-equivalent rate %
    double bid_to_cover = NAN;
    bool has_result() const { return std::isfinite(high_yield) || std::isfinite(bid_to_cover); }
};

inline double num(const QJsonObject& o, const char* k) {
    const QJsonValue v = o.value(QLatin1String(k));
    if (v.isDouble())
        return v.toDouble();
    bool ok = false;
    const double d = v.toString().trimmed().toDouble(&ok);
    return ok ? d : NAN;
}

/// "13-Week" → 13 weeks; "10-Year" → 10 years; "9-Year 10-Month" → 9.
inline int leading_int(const QString& s) {
    int n = 0, i = 0;
    while (i < s.size() && s[i].isDigit())
        n = n * 10 + s[i++].digitValue();
    return n;
}

/// One security; `ok` false when it is out of scope (short bill, CMB) or
/// unreadable.
inline Auction parse_one(const QJsonObject& o, bool* ok) {
    Auction a;
    *ok = false;
    a.cusip = o.value(QLatin1String("cusip")).toString();
    a.type = o.value(QLatin1String("securityType")).toString();
    const bool reopening = o.value(QLatin1String("reopening")).toString() == QLatin1String("Yes");
    const QString orig = o.value(QLatin1String("originalSecurityTerm")).toString();
    a.coupon = a.type == QLatin1String("Note") || a.type == QLatin1String("Bond");
    // A coupon reopening is named by its original term ("9-Year 8-Month" is
    // the 10-year reopening); a bill is what it is now (a 13-week bill can be
    // a reopened 52-week CUSIP).
    a.term = a.coupon && reopening && !orig.isEmpty() ? orig : o.value(QLatin1String("securityTerm")).toString();
    a.date = QDateTime::fromString(o.value(QLatin1String("competitiveAuctionCloseDate")).toString(), Qt::ISODate).date();
    if (!a.date.isValid())
        a.date = QDateTime::fromString(o.value(QLatin1String("auctionDate")).toString(), Qt::ISODate).date();
    if (!a.date.isValid() || a.term.isEmpty())
        return a;
    if (o.value(QLatin1String("cashManagementBill")).toString() == QLatin1String("Yes"))
        return a;
    if (a.type == QLatin1String("Bill")) {
        if (!a.term.contains(QLatin1String("Week")) || leading_int(a.term) < 13)
            return a;
    } else if (!a.coupon) {
        return a;
    }
    a.years = a.coupon ? leading_int(a.term) : 0;
    const bool tips = o.value(QLatin1String("inflationIndexSecurity")).toString() == QLatin1String("Yes") ||
                      o.value(QLatin1String("type")).toString() == QLatin1String("TIPS");
    const bool frn = o.value(QLatin1String("floatingRate")).toString() == QLatin1String("Yes");
    const int n = leading_int(a.term);
    const QString unit = a.coupon ? QStringLiteral("Y") : QStringLiteral("W");
    const QString what = tips ? QStringLiteral("TIPS")
                         : frn ? QStringLiteral("FRN")
                               : a.type.toLower();
    a.label = QStringLiteral("%1%2 %3").arg(n).arg(unit, what);
    if (reopening && a.coupon)
        a.label += QStringLiteral(" (reopening)");
    a.offering = num(o, "offeringAmount");
    a.announced = std::isfinite(a.offering) && a.offering > 0;
    const QString t = o.value(QLatin1String("competitiveAuctionCloseTime")).toString().trimmed();
    const QTime clock = QLocale(QLocale::English).toTime(t, QStringLiteral("hh:mm AP"));
    if (clock.isValid())
        a.close = QDateTime(a.date, clock, QTimeZone("America/New_York"));
    a.high_yield = a.coupon ? num(o, "highYield") : num(o, "highDiscountRate");
    if (!a.coupon && !std::isfinite(a.high_yield))
        a.high_yield = num(o, "highRate");
    a.investment_rate = num(o, "highInvestmentRate");
    if (!std::isfinite(a.investment_rate))
        a.investment_rate = num(o, "investmentRate");
    a.bid_to_cover = num(o, "bidToCoverRatio");
    *ok = true;
    return a;
}

/// Upcoming + auctioned merged by CUSIP and close date (a result replaces the
/// announcement of the same auction), chronological.
inline QVector<Auction> parse(const QJsonArray& upcoming, const QJsonArray& auctioned) {
    QVector<Auction> out;
    auto add = [&](const QJsonArray& arr) {
        for (const auto& v : arr) {
            bool ok = false;
            Auction a = parse_one(v.toObject(), &ok);
            if (!ok)
                continue;
            auto same = std::find_if(out.begin(), out.end(), [&](const Auction& b) {
                return b.cusip == a.cusip && b.date == a.date && !a.cusip.isEmpty();
            });
            if (same != out.end())
                *same = a;
            else
                out.append(a);
        }
    };
    add(upcoming);
    add(auctioned);
    std::stable_sort(out.begin(), out.end(), [](const Auction& a, const Auction& b) { return a.date < b.date; });
    return out;
}

inline QString money_b(double usd) {
    if (!std::isfinite(usd) || usd <= 0)
        return {};
    return QStringLiteral("$%1B").arg(QString::number(usd / 1e9, 'f', usd >= 1e10 ? 0 : 1));
}

/// "Yield 5.441% · bid-to-cover 2.36" (coupon result: the high yield; bills: "Discount rate …"), "$42B" (announced), "" (tentative).
inline QString figures(const Auction& a) {
    QStringList parts;
    if (std::isfinite(a.high_yield))
        parts << QStringLiteral("%1 %2%").arg(a.coupon ? QStringLiteral("Yield") : QStringLiteral("Discount rate"),
                                               QString::number(a.high_yield, 'f', 3));
    if (std::isfinite(a.bid_to_cover))
        parts << QStringLiteral("bid-to-cover %1").arg(QString::number(a.bid_to_cover, 'f', 2));
    if (parts.isEmpty() && a.announced)
        parts << money_b(a.offering);
    return parts.join(QStringLiteral(" · "));
}

} // namespace finterm::treasury_auctions
