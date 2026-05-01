#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::screens::futures {

struct ContractDef {
    QString symbol;       // root, e.g. "ES"
    QString name;         // "E-mini S&P 500"
    QString asset_class;  // "INDEX" / "RATES" / "ENERGY" / ...
    double  tick = 0.0;
    double  multiplier = 0.0;
};

/// yfinance continuous-front-month symbol — for use when calling the yfinance
/// daemon (PythonWorker batch_quotes / historical_period) directly.
inline QString yf_symbol_for(const QString& root) {
    return root + "=F";
}

inline const QVector<ContractDef>& all_contracts() {
    static const QVector<ContractDef> kContracts = {
        // Index
        {"ES",  "E-mini S&P 500",       "INDEX",  0.25,      50},
        {"NQ",  "E-mini Nasdaq 100",    "INDEX",  0.25,      20},
        {"YM",  "E-mini Dow",           "INDEX",  1.0,        5},
        {"RTY", "E-mini Russell 2000",  "INDEX",  0.10,      50},
        // Rates
        {"ZT",  "2-Year T-Note",        "RATES",  0.0078125, 2000},
        {"ZF",  "5-Year T-Note",        "RATES",  0.0078125, 1000},
        {"ZN",  "10-Year T-Note",       "RATES",  0.015625,  1000},
        {"ZB",  "30-Year T-Bond",       "RATES",  0.03125,   1000},
        {"UB",  "Ultra T-Bond",         "RATES",  0.03125,   1000},
        {"ZQ",  "30-Day Fed Funds",     "RATES",  0.0025,    4167},
        {"SR3", "3-Month SOFR",         "RATES",  0.0025,    2500},
        // Energy
        {"CL",  "WTI Crude Oil",        "ENERGY", 0.01,      1000},
        {"BZ",  "Brent Crude",          "ENERGY", 0.01,      1000},
        {"NG",  "Henry Hub Nat Gas",    "ENERGY", 0.001,     10000},
        {"RB",  "RBOB Gasoline",        "ENERGY", 0.0001,    42000},
        {"HO",  "Heating Oil",          "ENERGY", 0.0001,    42000},
        // Metals
        {"GC",  "Gold",                 "METALS", 0.10,      100},
        {"SI",  "Silver",               "METALS", 0.005,     5000},
        {"HG",  "Copper",               "METALS", 0.0005,    25000},
        {"PL",  "Platinum",             "METALS", 0.10,      50},
        {"PA",  "Palladium",            "METALS", 0.05,      100},
        // Agriculture / Softs
        {"ZC",  "Corn",                 "AGS",    0.25,      50},
        {"ZW",  "Wheat",                "AGS",    0.25,      50},
        {"ZS",  "Soybeans",             "AGS",    0.25,      50},
        {"ZL",  "Soybean Oil",          "AGS",    0.0001,    60000},
        {"ZM",  "Soybean Meal",         "AGS",    0.10,      100},
        {"KC",  "Coffee",               "AGS",    0.05,      37500},
        {"SB",  "Sugar #11",            "AGS",    0.01,      112000},
        {"CC",  "Cocoa",                "AGS",    1.0,       10},
        {"CT",  "Cotton",               "AGS",    0.01,      50000},
        // FX
        {"6E",  "Euro FX",              "FX",     0.00005,   125000},
        {"6J",  "Japanese Yen",         "FX",     0.0000005, 12500000},
        {"6B",  "British Pound",        "FX",     0.0001,    62500},
        {"6A",  "Australian Dollar",    "FX",     0.0001,    100000},
        {"6C",  "Canadian Dollar",      "FX",     0.00005,   100000},
        // Crypto (CME)
        {"BTC", "Bitcoin Futures",      "CRYPTO", 5.0,       5},
        {"MET", "Micro Ether Futures",  "CRYPTO", 0.25,      0.1},
    };
    return kContracts;
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
