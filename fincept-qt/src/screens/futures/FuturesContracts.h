#pragma once
#include <QDate>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::screens::futures {

/// Expiry / first-notice rule kinds. Each rule yields a deterministic next
/// expiry date given a reference date. We avoid network lookups for the
/// calendar — CME publishes these conventions and they don't change. Note:
/// the calendar is an *approximation* (US holidays aren't observed in the
/// business-day math; CME's actual last-trading-day may shift one biz day
/// around the major holidays). It's good enough for "is this rolling soon?"
/// awareness; verify with your broker before any final-week action.
///
/// Contracts whose rule is significantly more complex than fits a one-line
/// encoding (RB/HO/BZ each have month-shifted rules, several softs vary,
/// many financial contracts have product-specific notice quirks) are
/// intentionally left as `None` so the calendar shows "—" instead of a
/// confidently-wrong date.
enum class ExpiryRule {
    None,                                  // no rule mapped → render "—"
    QuarterlyThirdFriday,                  // ES/NQ/YM/RTY — Mar/Jun/Sep/Dec, 3rd Friday
    QuarterlyTwoBdaysBeforeThirdWed,       // FX (6E,6J,6B,6A,6C) — quarterly
    QuarterlyTreasuryNote7BdaysBeforeEom,  // ZN/ZB/ZF/UB — 7 bdays before last bday of Mar/Jun/Sep/Dec
    QuarterlyTreasuryLastBdayOfMonth,      // ZT — last bday of Mar/Jun/Sep/Dec
    MonthlyMetals3rdLastBday,              // Gold (GC) — 3rd-last bday of delivery month (active months)
    MonthlySilver3rdLastBday,              // Silver/Copper (SI/HG) — 3rd-last bday of delivery month (active months)
    MonthlyEnergy3BdaysBefore25th,         // CL — 3 bdays before 25th of month preceding delivery
    MonthlyNG3BdaysBeforeDelivery,         // NG — 3 bdays before first day of delivery month
    MonthlyGrainsBdayBeforeMid,            // ZC/ZW/ZS/ZL/ZM — biz day prior to 15th of delivery month (Z,H,K,N,U months)
    MonthlyLastFriday,                     // BTC, MET — last Friday of month
    MonthlyLastBday,                       // SR3, ZQ — approximate (last bday of month)
};

struct ContractDef {
    QString symbol;       // root, e.g. "ES"
    QString name;         // "E-mini S&P 500"
    QString asset_class;  // "INDEX" / "RATES" / "ENERGY" / ...
    double  tick = 0.0;
    double  multiplier = 0.0;
    /// Initial-margin estimate in USD (notional, recent CME baseline). These
    /// are point-in-time values that the exchange revises periodically; we
    /// surface them as a sizing aid, not a binding figure. A trader should
    /// confirm with their broker before sizing positions.
    double  initial_margin_usd = 0.0;
    ExpiryRule expiry_rule = ExpiryRule::None;
};

/// Compute the next expiry on or after `from` given a contract's rule.
/// Returns an invalid QDate if rule is None.
QDate next_expiry(const ContractDef& c, const QDate& from);

/// yfinance continuous-front-month symbol — for use when calling the yfinance
/// daemon (PythonWorker batch_quotes / historical_period) directly.
inline QString yf_symbol_for(const QString& root) {
    return root + "=F";
}

inline const QVector<ContractDef>& all_contracts() {
    using R = ExpiryRule;
    // Initial-margin column is a snapshot of CME baseline as of late 2025;
    // values shift periodically and brokers add their own buffer. Treat
    // these as sizing references, not binding.
    static const QVector<ContractDef> kContracts = {
        // Index
        {"ES",  "E-mini S&P 500",       "INDEX",  0.25,      50,         15'000, R::QuarterlyThirdFriday},
        {"NQ",  "E-mini Nasdaq 100",    "INDEX",  0.25,      20,         22'000, R::QuarterlyThirdFriday},
        {"YM",  "E-mini Dow",           "INDEX",  1.0,        5,          9'500, R::QuarterlyThirdFriday},
        {"RTY", "E-mini Russell 2000",  "INDEX",  0.10,      50,          8'500, R::QuarterlyThirdFriday},
        // Rates
        {"ZT",  "2-Year T-Note",        "RATES",  0.0078125, 2000,        1'400, R::QuarterlyTreasuryLastBdayOfMonth},
        {"ZF",  "5-Year T-Note",        "RATES",  0.0078125, 1000,        1'900, R::QuarterlyTreasuryLastBdayOfMonth},
        {"ZN",  "10-Year T-Note",       "RATES",  0.015625,  1000,        2'400, R::QuarterlyTreasuryNote7BdaysBeforeEom},
        {"ZB",  "30-Year T-Bond",       "RATES",  0.03125,   1000,        4'100, R::QuarterlyTreasuryNote7BdaysBeforeEom},
        {"UB",  "Ultra T-Bond",         "RATES",  0.03125,   1000,        5'500, R::QuarterlyTreasuryNote7BdaysBeforeEom},
        {"ZQ",  "30-Day Fed Funds",     "RATES",  0.0025,    4167,        1'200, R::MonthlyLastBday},
        {"SR3", "3-Month SOFR",         "RATES",  0.0025,    2500,        1'100, R::MonthlyLastBday},
        // Energy — only CL/NG have one-line rules. RB/HO use the month-before-delivery
        // LTD which we don't currently track; BZ depends on ICE's settlement schedule.
        // Leave the rest as None until properly modelled.
        {"CL",  "WTI Crude Oil",        "ENERGY", 0.01,      1000,        6'500, R::MonthlyEnergy3BdaysBefore25th},
        {"BZ",  "Brent Crude",          "ENERGY", 0.01,      1000,        6'500, R::None},
        {"NG",  "Henry Hub Nat Gas",    "ENERGY", 0.001,     10000,       4'500, R::MonthlyNG3BdaysBeforeDelivery},
        {"RB",  "RBOB Gasoline",        "ENERGY", 0.0001,    42000,       7'000, R::None},
        {"HO",  "Heating Oil",          "ENERGY", 0.0001,    42000,       7'500, R::None},
        // Metals (Gold/Plat/Palladium active = G/J/M/Q/Z; Silver/HG active = H/K/N/U/Z).
        // We gate by active months in the rule arm itself; both groups use
        // "3rd-last business day of the delivery month".
        {"GC",  "Gold",                 "METALS", 0.10,      100,        12'500, R::MonthlyMetals3rdLastBday},
        {"SI",  "Silver",               "METALS", 0.005,     5000,       18'000, R::MonthlySilver3rdLastBday},
        {"HG",  "Copper",               "METALS", 0.0005,    25000,       7'000, R::MonthlySilver3rdLastBday},
        {"PL",  "Platinum",             "METALS", 0.10,      50,          3'700, R::MonthlyMetals3rdLastBday},
        {"PA",  "Palladium",            "METALS", 0.05,      100,         9'500, R::MonthlyMetals3rdLastBday},
        // CBOT grains — LTD is biz day prior to 15th of delivery month;
        // delivery months are (H,K,N,U,Z) for grains, (F,H,K,N,Q,U,X) for soy.
        // We approximate with the grain set (good enough for ZC/ZW; ZS shows
        // a slightly conservative date in Jan/Q/X months).
        {"ZC",  "Corn",                 "AGS",    0.25,      50,          1'500, R::MonthlyGrainsBdayBeforeMid},
        {"ZW",  "Wheat",                "AGS",    0.25,      50,          2'200, R::MonthlyGrainsBdayBeforeMid},
        {"ZS",  "Soybeans",             "AGS",    0.25,      50,          3'200, R::MonthlyGrainsBdayBeforeMid},
        {"ZL",  "Soybean Oil",          "AGS",    0.0001,    60000,       2'000, R::MonthlyGrainsBdayBeforeMid},
        {"ZM",  "Soybean Meal",         "AGS",    0.10,      100,         2'600, R::MonthlyGrainsBdayBeforeMid},
        {"KC",  "Coffee",               "AGS",    0.05,      37500,       9'500, R::None},
        {"SB",  "Sugar #11",            "AGS",    0.01,      112000,      1'400, R::None},
        {"CC",  "Cocoa",                "AGS",    1.0,       10,          3'600, R::None},
        {"CT",  "Cotton",               "AGS",    0.01,      50000,       3'200, R::None},
        // FX
        {"6E",  "Euro FX",              "FX",     0.00005,   125000,      2'800, R::QuarterlyTwoBdaysBeforeThirdWed},
        {"6J",  "Japanese Yen",         "FX",     0.0000005, 12500000,    3'400, R::QuarterlyTwoBdaysBeforeThirdWed},
        {"6B",  "British Pound",        "FX",     0.0001,    62500,       2'400, R::QuarterlyTwoBdaysBeforeThirdWed},
        {"6A",  "Australian Dollar",    "FX",     0.0001,    100000,      1'800, R::QuarterlyTwoBdaysBeforeThirdWed},
        {"6C",  "Canadian Dollar",      "FX",     0.00005,   100000,      1'600, R::QuarterlyTwoBdaysBeforeThirdWed},
        // Crypto (CME)
        {"BTC", "Bitcoin Futures",      "CRYPTO", 5.0,       5,         115'000, R::MonthlyLastFriday},
        {"MET", "Micro Ether Futures",  "CRYPTO", 0.25,      0.1,         1'100, R::MonthlyLastFriday},
    };
    return kContracts;
}

// ─── Expiry calculator ──────────────────────────────────────────────────────

namespace detail {

inline QDate nth_weekday(int year, int month, int weekday /*1=Mon..7=Sun*/, int n) {
    QDate first(year, month, 1);
    int offset = (weekday - first.dayOfWeek() + 7) % 7;
    return first.addDays(offset + (n - 1) * 7);
}

inline QDate last_business_day(int year, int month) {
    QDate d(year, month, 1);
    d = d.addMonths(1).addDays(-1);
    while (d.dayOfWeek() > 5) d = d.addDays(-1);
    return d;
}

inline QDate add_business_days(QDate d, int n) {
    while (n > 0) {
        d = d.addDays(1);
        if (d.dayOfWeek() <= 5) --n;
    }
    while (n < 0) {
        d = d.addDays(-1);
        if (d.dayOfWeek() <= 5) ++n;
    }
    return d;
}

} // namespace detail

inline QDate next_expiry(const ContractDef& c, const QDate& from) {
    using namespace detail;
    if (c.expiry_rule == ExpiryRule::None) return {};

    // Active delivery months per rule. Tested against CME product specs;
    // we still skip holidays in the business-day math (good-enough for
    // "rolling soon?" awareness, not for an order desk).
    auto in_active = [](int month, const std::initializer_list<int>& months) {
        for (int m : months) if (month == m) return true;
        return false;
    };

    QDate ref = from.isValid() ? from : QDate::currentDate();
    for (int delta = 0; delta < 18; ++delta) {
        QDate probe = ref.addMonths(delta);
        const int y = probe.year();
        const int m = probe.month();

        QDate cand;
        switch (c.expiry_rule) {
            case ExpiryRule::QuarterlyThirdFriday: {
                if (!in_active(m, {3, 6, 9, 12})) continue;
                cand = nth_weekday(y, m, 5 /*Fri*/, 3);
                break;
            }
            case ExpiryRule::QuarterlyTwoBdaysBeforeThirdWed: {
                if (!in_active(m, {3, 6, 9, 12})) continue;
                cand = add_business_days(nth_weekday(y, m, 3 /*Wed*/, 3), -2);
                break;
            }
            case ExpiryRule::QuarterlyTreasuryNote7BdaysBeforeEom: {
                if (!in_active(m, {3, 6, 9, 12})) continue;
                cand = add_business_days(last_business_day(y, m), -7);
                break;
            }
            case ExpiryRule::QuarterlyTreasuryLastBdayOfMonth: {
                if (!in_active(m, {3, 6, 9, 12})) continue;
                cand = last_business_day(y, m);
                break;
            }
            case ExpiryRule::MonthlyMetals3rdLastBday: {
                // Gold/Plat/Palladium active months: G,J,M,Q,V,Z = Feb,Apr,Jun,Aug,Oct,Dec
                if (!in_active(m, {2, 4, 6, 8, 10, 12})) continue;
                // 3rd-last business day = 2 bdays back from last bday.
                cand = add_business_days(last_business_day(y, m), -2);
                break;
            }
            case ExpiryRule::MonthlySilver3rdLastBday: {
                // Silver active: H,K,N,U,Z = Mar,May,Jul,Sep,Dec
                // Copper (HG) active: H,K,N,U,Z = same
                if (!in_active(m, {3, 5, 7, 9, 12})) continue;
                cand = add_business_days(last_business_day(y, m), -2);
                break;
            }
            case ExpiryRule::MonthlyEnergy3BdaysBefore25th: {
                // CL: last-trading-day is 3 bdays before the 25th of the
                // month *preceding* delivery. We model the contract delivering
                // in month `m+1` as expiring in month `m`. Probe each month.
                QDate d25(y, m, 25);
                cand = add_business_days(d25, -3);
                break;
            }
            case ExpiryRule::MonthlyNG3BdaysBeforeDelivery: {
                QDate d1(y, m, 1);
                cand = add_business_days(d1, -3);
                break;
            }
            case ExpiryRule::MonthlyGrainsBdayBeforeMid: {
                // CBOT grains active months (H,K,N,U,Z + F for soy complex).
                // Use the union {1,3,5,7,9,12} as an upper-bound set — slightly
                // permissive but the surfaced date is still close to the LTD.
                if (!in_active(m, {1, 3, 5, 7, 9, 12})) continue;
                // Business day prior to the 15th.
                QDate d15(y, m, 15);
                cand = add_business_days(d15, -1);
                break;
            }
            case ExpiryRule::MonthlyLastFriday: {
                QDate eom = QDate(y, m, 1).addMonths(1).addDays(-1);
                while (eom.dayOfWeek() != 5) eom = eom.addDays(-1);
                cand = eom;
                break;
            }
            case ExpiryRule::MonthlyLastBday: {
                cand = last_business_day(y, m);
                break;
            }
            case ExpiryRule::None: return {};
        }
        if (cand.isValid() && cand >= ref) return cand;
    }
    return {};
}

/// Asset class tabs in display order. CHINA is sourced from akshare and
/// resolved at runtime, not from the static catalog.
inline const QStringList& asset_classes() {
    static const QStringList kClasses = {
        "INDEX", "RATES", "ENERGY", "METALS", "AGS", "FX", "CRYPTO", "CHINA"
    };
    return kClasses;
}

inline QStringList symbols_for_class(const QString& asset_class) {
    QStringList out;
    for (const auto& c : all_contracts())
        if (c.asset_class == asset_class)
            out.push_back(c.symbol);
    return out;
}

inline const ContractDef* find_contract(const QString& symbol) {
    for (const auto& c : all_contracts())
        if (c.symbol == symbol)
            return &c;
    return nullptr;
}

} // namespace fincept::screens::futures
