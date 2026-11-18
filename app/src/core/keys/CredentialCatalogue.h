// GENERATED FILE — do not edit by hand.
//
// Regenerate:  python3 scripts/gen_credential_catalogue.py
// Verified by: tests/scripts/test_credential_catalogue.py (ctest)
//
// Every credential the app can use, derived from the scripts that read
// them. This is the single source of truth for three things that used to
// be three separate hand-maintained lists, all disagreeing:
//   • which keys SettingsScreen offers the user,
//   • which keys PythonRunner injects from SecureStorage,
//   • which keys survive the subprocess credential strip.
//
// That last one made the drift a real bug rather than untidiness: a
// credential-shaped variable missing from the allow-list is DELETED from
// the child environment, so a key the user had correctly exported was
// reported by the panel as "not configured".

// Generated, and --check compares it byte-for-byte: keep the formatter off.
// clang-format off

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace finterm::keys {

struct CredentialDef {
    const char* env_name;  ///< e.g. "FRED_API_KEY"
    const char* label;     ///< human name shown in Settings
    const char* group;     ///< Settings › API Keys section
    bool secret;           ///< false: an id/email/preference, shown in clear
};

/// Section order for Settings › API Keys.
inline const QStringList& groups() {
    static const QStringList kGroups = {"Your identity", "Market data", "AI agent tools", "Economics & government"};
    return kGroups;
}

// 33 credentials.
inline const QVector<CredentialDef>& catalogue() {
    static const QVector<CredentialDef> kAll = {
        {"ALPACA_API_KEY_ID", "Alpaca market data — key ID", "Market data", true},
        {"ALPACA_API_SECRET_KEY", "Alpaca market data — secret key", "Market data", true},
        {"AUS_DATA_API_KEY", "Aus Data", "Economics & government", true},
        {"BEA_API_KEY", "BEA (Bureau of Economic Analysis)", "Economics & government", true},
        {"BLS_API_KEY", "BLS (Bureau of Labor Statistics)", "Economics & government", true},
        {"CANADA_GOV_API_KEY", "Canada Gov", "Economics & government", true},
        {"CENSUS_API_KEY", "US Census Bureau", "Economics & government", true},
        {"CFTC_APP_TOKEN", "Cftc", "Economics & government", true},
        {"CONGRESS_GOV_API_KEY", "Congress.gov (Gov Data)", "Economics & government", true},
        {"DATABENTO_API_KEY", "Databento", "Market data", true},
        {"DATAGOVUK_API_KEY", "Datagovuk", "Economics & government", true},
        {"DATA_GOV_HK_LANGUAGE", "Data Gov Hk Language", "Economics & government", false},
        {"EDGAR_IDENTITY", "Edgar Identity", "Economics & government", true},
        {"EIA_API_KEY", "EIA (Energy Information Administration)", "Economics & government", true},
        {"ENTREPRISE_API_KEY", "Entreprise", "Economics & government", true},
        {"EUROSTAT_API_KEY", "Eurostat", "Economics & government", true},
        {"FINANCIAL_DATASETS_API_KEY", "Financial Datasets (financialdatasets.ai)", "Market data", true},
        {"FINNHUB_API_KEY", "Finnhub", "Market data", true},
        {"FINTERM_CONTACT_EMAIL", "Contact email (declared to SEC, FINRA, OpenStreetMap)", "Your identity", false},
        {"FMP_API_KEY", "Financial Modeling Prep", "Market data", true},
        {"FRED_API_KEY", "FRED (Federal Reserve)", "Economics & government", true},
        {"GITHUB_ACCESS_TOKEN", "GitHub (agents' GitHub tools)", "AI agent tools", true},
        {"IMF_API_KEY", "Imf", "Economics & government", true},
        {"OPENBB_PAT", "OpenBB personal access token (agents' OpenBB tools)", "AI agent tools", true},
        {"OPENFIGI_API_KEY", "OpenFIGI (13F symbol mapping — optional)", "Market data", true},
        {"POLYGON_API_KEY", "Polygon.io / Massive", "Market data", true},
        {"SWISS_GOV_API_KEY", "Swiss Gov", "Economics & government", true},
        {"TIINGO_API_KEY", "Tiingo", "Market data", true},
        {"TRADING_ECONOMICS_API_KEY", "Trading Economics", "Economics & government", true},
        {"TREASURY_API_KEY", "Treasury", "Economics & government", true},
        {"UN_COMTRADE_API_KEY", "Un Comtrade", "Economics & government", true},
        {"WORLDBANK_API_KEY", "Worldbank", "Economics & government", true},
        {"WTO_API_KEY", "Wto", "Economics & government", true},
    };
    return kAll;
}

/// Env-var names only — the allow-list for credential injection and for
/// stripping unmanaged secrets out of a child process environment.
inline const QStringList& env_names() {
    static const QStringList kNames = [] {
        QStringList out;
        out.reserve(catalogue().size());
        for (const auto& c : catalogue())
            out << QString::fromLatin1(c.env_name);
        return out;
    }();
    return kNames;
}

} // namespace finterm::keys

// clang-format on
