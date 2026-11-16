// src/services/futures/FuturesCatalog.h
#pragma once
// The futures the Futures screen lists, with the contract terms needed to
// turn a price into money.
//
// `point_value` is US dollars per 1.00 of the price AS YAHOO QUOTES IT — corn
// is quoted in cents a bushel, so one cent on 5,000 bushels is $50; soybean
// oil in cents a pound on 60,000 lb is $600. `tick` is the minimum
// fluctuation in the same quoted units. Both are exchange contract terms
// (CME Group / ICE US specifications); every tick was checked against live
// prints on 2026-11-16 (each price a whole number of ticks).
//
// Deliberately absent: margin. Exchanges revise it often and the app has no
// feed for it; a hardcoded figure would be a stale number shown as current.
//
// `cftc` is the CFTC contract market code of the Commitments of Traders
// report for the market (each verified against the published market name);
// empty where the micro contract has no report worth pairing with it.
//
// These roots are futures-screen symbols only. Many are also stock tickers
// (CL Colgate, ES Eversource, HE Hawaiian Electric) — never classify a bare
// symbol as a future by looking it up here.

#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace finterm::services::futures {

struct ContractSpec {
    QString root;
    QString name;
    QString group;
    double tick = 0.0;
    double point_value = 0.0;
    QString unit;  ///< how the price is quoted
    QString size;  ///< what one contract is
    QString cftc;
    /// Price is 100 minus an interest rate (Fed Funds, SOFR).
    bool rate_quoted = false;
};

inline const QVector<ContractSpec>& all_contracts() {
    static const QVector<ContractSpec> k = {
        // Equity index
        {"ES", "E-mini S&P 500", "INDEX", 0.25, 50, "index points", "$50 × index", "13874A"},
        {"NQ", "E-mini Nasdaq-100", "INDEX", 0.25, 20, "index points", "$20 × index", "209742"},
        {"YM", "E-mini Dow", "INDEX", 1.0, 5, "index points", "$5 × index", "124603"},
        {"RTY", "E-mini Russell 2000", "INDEX", 0.10, 50, "index points", "$50 × index", "239742"},
        {"MES", "Micro E-mini S&P 500", "INDEX", 0.25, 5, "index points", "$5 × index", ""},
        {"MNQ", "Micro E-mini Nasdaq-100", "INDEX", 0.25, 2, "index points", "$2 × index", ""},
        {"NKD", "Nikkei 225 (USD)", "INDEX", 5.0, 5, "index points", "$5 × index", ""},
        // Interest rates
        {"ZT", "2-Year T-Note", "RATES", 0.00390625, 2000, "points of par", "$200,000 face", "042601"},
        {"ZF", "5-Year T-Note", "RATES", 0.0078125, 1000, "points of par", "$100,000 face", "044601"},
        {"ZN", "10-Year T-Note", "RATES", 0.015625, 1000, "points of par", "$100,000 face", "043602"},
        {"ZB", "U.S. Treasury Bond", "RATES", 0.03125, 1000, "points of par", "$100,000 face", "020601"},
        {"UB", "Ultra T-Bond", "RATES", 0.03125, 1000, "points of par", "$100,000 face", "020604"},
        {"ZQ", "30-Day Fed Funds", "RATES", 0.0025, 4167, "100 − rate", "$5,000,000 × 30 days", "045601", true},
        {"SR3", "3-Month SOFR", "RATES", 0.0025, 2500, "100 − rate", "$1,000,000 × 3 months", "134741", true},
        // Energy
        {"CL", "WTI Crude Oil", "ENERGY", 0.01, 1000, "$/barrel", "1,000 barrels", "067651"},
        {"BZ", "Brent Crude (last day)", "ENERGY", 0.01, 1000, "$/barrel", "1,000 barrels", "06765T"},
        {"NG", "Henry Hub Natural Gas", "ENERGY", 0.001, 10000, "$/MMBtu", "10,000 MMBtu", "023651"},
        {"RB", "RBOB Gasoline", "ENERGY", 0.0001, 42000, "$/gallon", "42,000 gallons", "111659"},
        {"HO", "NY Harbor ULSD", "ENERGY", 0.0001, 42000, "$/gallon", "42,000 gallons", "022651"},
        {"MCL", "Micro WTI Crude Oil", "ENERGY", 0.01, 100, "$/barrel", "100 barrels", ""},
        // Metals
        {"GC", "Gold", "METALS", 0.10, 100, "$/troy oz", "100 troy oz", "088691"},
        {"SI", "Silver", "METALS", 0.005, 5000, "$/troy oz", "5,000 troy oz", "084691"},
        {"HG", "Copper", "METALS", 0.0005, 25000, "$/lb", "25,000 lb", "085692"},
        {"PL", "Platinum", "METALS", 0.10, 50, "$/troy oz", "50 troy oz", "076651"},
        {"PA", "Palladium", "METALS", 0.50, 100, "$/troy oz", "100 troy oz", "075651"},
        {"MGC", "Micro Gold", "METALS", 0.10, 10, "$/troy oz", "10 troy oz", ""},
        // Grains & oilseeds
        {"ZC", "Corn", "GRAINS", 0.25, 50, "¢/bushel", "5,000 bushels", "002602"},
        {"ZW", "Chicago SRW Wheat", "GRAINS", 0.25, 50, "¢/bushel", "5,000 bushels", "001602"},
        {"KE", "KC HRW Wheat", "GRAINS", 0.25, 50, "¢/bushel", "5,000 bushels", "001612"},
        {"ZS", "Soybeans", "GRAINS", 0.25, 50, "¢/bushel", "5,000 bushels", "005602"},
        {"ZL", "Soybean Oil", "GRAINS", 0.01, 600, "¢/lb", "60,000 lb", "007601"},
        {"ZM", "Soybean Meal", "GRAINS", 0.10, 100, "$/short ton", "100 short tons", "026603"},
        {"ZO", "Oats", "GRAINS", 0.25, 50, "¢/bushel", "5,000 bushels", "004603"},
        {"ZR", "Rough Rice", "GRAINS", 0.005, 2000, "$/cwt", "2,000 cwt", "039601"},
        // Softs
        {"KC", "Coffee C", "SOFTS", 0.05, 375, "¢/lb", "37,500 lb", "083731"},
        {"SB", "Sugar No. 11", "SOFTS", 0.01, 1120, "¢/lb", "112,000 lb", "080732"},
        {"CC", "Cocoa", "SOFTS", 1.0, 10, "$/tonne", "10 tonnes", "073732"},
        {"CT", "Cotton No. 2", "SOFTS", 0.01, 500, "¢/lb", "50,000 lb", "033661"},
        {"OJ", "Orange Juice", "SOFTS", 0.05, 150, "¢/lb", "15,000 lb", "040701"},
        {"LBR", "Lumber", "SOFTS", 0.50, 27.5, "$/thousand board ft", "27,500 board ft", "058644"},
        // Livestock
        {"LE", "Live Cattle", "LIVESTOCK", 0.025, 400, "¢/lb", "40,000 lb", "057642"},
        {"HE", "Lean Hogs", "LIVESTOCK", 0.025, 400, "¢/lb", "40,000 lb", "054642"},
        {"GF", "Feeder Cattle", "LIVESTOCK", 0.025, 500, "¢/lb", "50,000 lb", "061641"},
        // Currencies
        {"6E", "Euro FX", "FX", 0.00005, 125000, "USD per EUR", "€125,000", "099741"},
        {"6J", "Japanese Yen", "FX", 0.0000005, 12500000, "USD per JPY", "¥12,500,000", "097741"},
        {"6B", "British Pound", "FX", 0.0001, 62500, "USD per GBP", "£62,500", "096742"},
        {"6A", "Australian Dollar", "FX", 0.00005, 100000, "USD per AUD", "A$100,000", "232741"},
        {"6C", "Canadian Dollar", "FX", 0.00005, 100000, "USD per CAD", "C$100,000", "090741"},
        {"6S", "Swiss Franc", "FX", 0.00005, 125000, "USD per CHF", "CHF 125,000", "092741"},
        {"6M", "Mexican Peso", "FX", 0.00001, 500000, "USD per MXN", "MXN 500,000", "095741"},
        {"6N", "New Zealand Dollar", "FX", 0.00005, 100000, "USD per NZD", "NZ$100,000", "112741"},
        // Crypto
        {"BTC", "Bitcoin", "CRYPTO", 5.0, 5, "$/bitcoin", "5 bitcoin", "133741"},
        {"MBT", "Micro Bitcoin", "CRYPTO", 5.0, 0.1, "$/bitcoin", "0.1 bitcoin", ""},
        {"ETH", "Ether", "CRYPTO", 0.50, 50, "$/ether", "50 ether", "146021"},
        {"MET", "Micro Ether", "CRYPTO", 0.50, 0.1, "$/ether", "0.1 ether", ""},
    };
    return k;
}

/// The first page: one screen of the contracts that set the tone for each
/// asset class, so the whole tape is read before any one market is opened.
inline const QString& majors_group() {
    static const QString k = QStringLiteral("MAJORS");
    return k;
}

inline const QStringList& major_roots() {
    static const QStringList k = {"ES", "NQ", "RTY", "ZN", "ZB", "SR3", "CL", "NG", "GC", "SI",
                                  "HG", "ZC", "ZS", "KC", "LE", "6E", "6J", "BTC"};
    return k;
}

inline const QStringList& groups() {
    static const QStringList k = {majors_group(), "INDEX", "RATES", "ENERGY", "METALS", "GRAINS",
                                  "SOFTS",        "LIVESTOCK", "FX", "CRYPTO"};
    return k;
}

inline const ContractSpec* find_contract(const QString& root) {
    for (const auto& c : all_contracts())
        if (c.root == root)
            return &c;
    return nullptr;
}

/// The contracts a group's board lists. MAJORS is a view across the classes,
/// in the order major_roots() gives; every other group is a contract's own.
inline QVector<const ContractSpec*> contracts_in(const QString& group) {
    QVector<const ContractSpec*> out;
    if (group == majors_group()) {
        for (const QString& root : major_roots())
            if (const auto* c = find_contract(root))
                out.append(c);
        return out;
    }
    for (const auto& c : all_contracts())
        if (c.group == group)
            out.append(&c);
    return out;
}

/// The continuous front-month symbol the quote feed streams.
inline QString continuous_symbol(const QString& root) {
    return root + QStringLiteral("=F");
}

/// Decimals that show a price to its tick. Binary-fraction ticks (Treasuries'
/// 64ths and 128ths) stop at five — what the feed itself carries.
inline int price_decimals(double tick) {
    if (!(tick > 0))
        return 2;
    for (int d = 0; d <= 7; ++d) {
        const double scaled = tick * std::pow(10.0, d);
        if (std::abs(scaled - std::round(scaled)) < 1e-9)
            return tick < 1e-5 ? d : std::min(d, 5);
    }
    return 5;
}

} // namespace finterm::services::futures
