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

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::keys {

struct CredentialDef {
    const char* env_name;  ///< e.g. "FRED_API_KEY"
    const char* label;     ///< human name shown in Settings
    const char* group;     ///< Settings › API Keys section
    bool secret;           ///< false: an id/email/preference, shown in clear
};

/// Section order for Settings › API Keys.
inline const QStringList& groups() {
    static const QStringList kGroups = {"Your identity", "Market data", "Crypto", "Energy, climate & geospatial", "Research & datasets", "Economics & government"};
    return kGroups;
}

// 146 credentials.
inline const QVector<CredentialDef>& catalogue() {
    static const QVector<CredentialDef> kAll = {
        {"ACLED_API_KEY", "Acled", "Economics & government", true},
        {"ACLED_EMAIL", "Acled", "Your identity", false},
        {"ADB_API_KEY", "Adb", "Economics & government", true},
        {"AFDB_API_KEY", "Afdb", "Economics & government", true},
        {"AFRICA_API_KEY", "Africa", "Economics & government", true},
        {"AISSTREAM_API_KEY", "Aisstream", "Energy, climate & geospatial", true},
        {"ALPHA_SPREAD_API_KEY", "Alpha Spread", "Market data", true},
        {"ALPHA_VANTAGE_API_KEY", "Alpha Vantage", "Market data", true},
        {"ARXIV_API_KEY", "Arxiv", "Research & datasets", true},
        {"AUS_DATA_API_KEY", "Aus Data", "Economics & government", true},
        {"BALTIC_API_KEY", "Baltic", "Energy, climate & geospatial", true},
        {"BEA_API_KEY", "BEA (Bureau of Economic Analysis)", "Economics & government", true},
        {"BIS_API_KEY", "Bis", "Economics & government", true},
        {"BLS_API_KEY", "BLS (Bureau of Labor Statistics)", "Economics & government", true},
        {"CANADA_GOV_API_KEY", "Canada Gov", "Economics & government", true},
        {"CBOE_API_KEY", "Cboe", "Market data", true},
        {"CENSUS_API_KEY", "US Census Bureau", "Economics & government", true},
        {"CFTC_APP_TOKEN", "Cftc", "Economics & government", true},
        {"CLIMATE_WATCH_API_KEY", "Climate Watch", "Energy, climate & geospatial", true},
        {"CMC_API_KEY", "Cmc", "Crypto", true},
        {"CME_API_KEY", "Cme", "Market data", true},
        {"COINCAP_API_KEY", "Coincap", "Crypto", true},
        {"COINGECKO_API_KEY", "Coingecko", "Crypto", true},
        {"COINGLASS_API_KEY", "Coinglass", "Crypto", true},
        {"COMTRADE_API_KEY", "Comtrade", "Economics & government", true},
        {"CONGRESS_GOV_API_KEY", "Congress.gov (Gov Data)", "Economics & government", true},
        {"COPERNICUS_API_KEY", "Copernicus", "Energy, climate & geospatial", true},
        {"CROSSREF_API_KEY", "Crossref", "Research & datasets", true},
        {"CROSSREF_EMAIL", "Crossref", "Your identity", false},
        {"CRYPTOCOMPARE_API_KEY", "Cryptocompare", "Crypto", true},
        {"DATABENTO_API_KEY", "Databento", "Market data", true},
        {"DATAGOVSG_API_KEY", "Datagovsg", "Economics & government", true},
        {"DATAGOVUK_API_KEY", "Datagovuk", "Economics & government", true},
        {"DATA_GOV_HK_LANGUAGE", "Data Gov Hk Language", "Economics & government", false},
        {"DATA_WORLD_TOKEN", "Data World", "Research & datasets", true},
        {"EBRD_API_KEY", "Ebrd", "Economics & government", true},
        {"EIA_API_KEY", "EIA (Energy Information Administration)", "Economics & government", true},
        {"EMBER_API_KEY", "Ember", "Energy, climate & geospatial", true},
        {"ENTREPRISE_API_KEY", "Entreprise", "Economics & government", true},
        {"ENTSOE_API_KEY", "Entsoe", "Energy, climate & geospatial", true},
        {"ENTSOG_API_KEY", "Entsog", "Energy, climate & geospatial", true},
        {"EODHD_API_KEY", "Eodhd", "Market data", true},
        {"ESTAT_APP_ID", "Estat", "Economics & government", false},
        {"EUROSTAT_API_KEY", "Eurostat", "Economics & government", true},
        {"FAO_API_KEY", "Fao", "Economics & government", true},
        {"FDIC_API_KEY", "Fdic", "Economics & government", true},
        {"FINANCIAL_DATASETS_API_KEY", "Financial Datasets (financialdatasets.ai)", "Market data", true},
        {"FINCEPT_CONTACT_EMAIL", "Contact email (declared to SEC, FINRA, OpenStreetMap)", "Your identity", false},
        {"FINNHUB_API_KEY", "Finnhub", "Market data", true},
        {"FMP_API_KEY", "Fmp", "Market data", true},
        {"FRED_API_KEY", "FRED (Federal Reserve)", "Economics & government", true},
        {"GFD_API_KEY", "Gfd", "Market data", true},
        {"GFW_API_KEY", "Gfw", "Energy, climate & geospatial", true},
        {"GHS_API_KEY", "Ghs", "Economics & government", true},
        {"GITHUB_TOKEN", "Github", "Research & datasets", true},
        {"GLASSNODE_API_KEY", "Glassnode", "Crypto", true},
        {"GLEIF_API_KEY", "Gleif", "Market data", true},
        {"GLOBAL_SOLAR_ATLAS_API_KEY", "Global Solar Atlas", "Energy, climate & geospatial", true},
        {"GLOBAL_WIND_ATLAS_API_KEY", "Global Wind Atlas", "Energy, climate & geospatial", true},
        {"GOVDATA_API_KEY", "Govdata", "Economics & government", true},
        {"GOVINFO_API_KEY", "Govinfo", "Economics & government", true},
        {"HARVARD_DATAVERSE_TOKEN", "Harvard Dataverse", "Research & datasets", true},
        {"HUD_API_KEY", "Hud", "Economics & government", true},
        {"IADB_API_KEY", "Iadb", "Economics & government", true},
        {"ICE_API_KEY", "Ice", "Market data", true},
        {"IEA_API_KEY", "Iea", "Energy, climate & geospatial", true},
        {"IEX_CLOUD_API_KEY", "Iex Cloud", "Market data", true},
        {"ILOSTAT_API_KEY", "Ilostat", "Economics & government", true},
        {"IMF_API_KEY", "Imf", "Economics & government", true},
        {"INTRINIO_API_KEY", "Intrinio", "Market data", true},
        {"INVESTING_API_KEY", "Investing", "Market data", true},
        {"IRENA_API_KEY", "Irena", "Energy, climate & geospatial", true},
        {"IRS_API_KEY", "Irs", "Economics & government", true},
        {"ISDB_API_KEY", "Isdb", "Economics & government", true},
        {"KAGGLE_KEY", "Kaggle", "Research & datasets", true},
        {"KAGGLE_USERNAME", "Kaggle Username", "Your identity", false},
        {"LME_API_KEY", "Lme", "Market data", true},
        {"MACROTRENDS_API_KEY", "Macrotrends", "Market data", true},
        {"MADDISON_API_KEY", "Maddison", "Economics & government", true},
        {"MARINETRAFFIC_API_KEY", "Marinetraffic", "Energy, climate & geospatial", true},
        {"MARKETSTACK_API_KEY", "Marketstack", "Market data", true},
        {"MESSARI_API_KEY", "Messari", "Crypto", true},
        {"METALS_DEV_API_KEY", "Metals Dev", "Market data", true},
        {"N2YO_API_KEY", "N2yo", "Energy, climate & geospatial", true},
        {"NASDAQ_API_KEY", "Nasdaq", "Market data", true},
        {"NASDAQ_DATA_LINK_API_KEY", "Nasdaq Data Link (Quandl)", "Market data", true},
        {"NBER_API_KEY", "Nber", "Research & datasets", true},
        {"NOAA_CDO_TOKEN", "Noaa Cdo", "Energy, climate & geospatial", true},
        {"NUMBEO_API_KEY", "Numbeo", "Economics & government", true},
        {"OBS_API_KEY", "Obs", "Economics & government", true},
        {"OC_API_KEY", "Oc", "Market data", true},
        {"OECD_API_KEY", "Oecd", "Economics & government", true},
        {"OPEC_API_KEY", "Opec", "Energy, climate & geospatial", true},
        {"OPENAQ_API_KEY", "Openaq", "Energy, climate & geospatial", true},
        {"OPENBB_API_KEY", "Openbb", "Market data", true},
        {"OPENEXCHANGERATES_API_KEY", "Openexchangerates", "Market data", true},
        {"OPENFIGI_API_KEY", "OpenFIGI (13F symbol mapping — optional)", "Market data", true},
        {"OPENSECRETS_API_KEY", "Opensecrets", "Economics & government", true},
        {"OPEN_OWNERSHIP_API_KEY", "Open Ownership", "Economics & government", true},
        {"OPSD_API_KEY", "Opsd", "Energy, climate & geospatial", true},
        {"PLATTS_API_KEY", "Platts", "Market data", true},
        {"POLYGON_API_KEY", "Polygon.io", "Market data", true},
        {"PORTCHAIN_API_KEY", "Portchain", "Energy, climate & geospatial", true},
        {"PRS_API_KEY", "Prs", "Economics & government", true},
        {"PUBLIC_APIS_FINANCE_KEY", "Public Apis Finance", "Market data", true},
        {"PWT_API_KEY", "Pwt", "Economics & government", true},
        {"QUIVER_API_KEY", "Quiver", "Market data", true},
        {"RELIEFWEB_API_KEY", "Reliefweb", "Economics & government", true},
        {"SEMANTIC_SCHOLAR_API_KEY", "Semantic Scholar", "Research & datasets", true},
        {"SENTINELHUB_CLIENT_ID", "Sentinelhub Client Id", "Energy, climate & geospatial", false},
        {"SENTINELHUB_CLIENT_SECRET", "Sentinelhub Client", "Energy, climate & geospatial", true},
        {"SIMFIN_API_KEY", "Simfin", "Market data", true},
        {"SSRN_API_KEY", "Ssrn", "Research & datasets", true},
        {"STOOQ_API_KEY", "Stooq", "Market data", true},
        {"SWISS_GOV_API_KEY", "Swiss Gov", "Economics & government", true},
        {"TIINGO_API_KEY", "Tiingo", "Market data", true},
        {"TRADINGVIEW_API_KEY", "Tradingview", "Market data", true},
        {"TRADING_ECONOMICS_API_KEY", "Trading Economics", "Economics & government", true},
        {"TRANSPARENCY_API_KEY", "Transparency", "Economics & government", true},
        {"TREASURY_API_KEY", "Treasury", "Economics & government", true},
        {"TWELVE_DATA_API_KEY", "Twelve Data", "Market data", true},
        {"TWFY_API_KEY", "Twfy", "Economics & government", true},
        {"UNCTAD_API_KEY", "Unctad", "Economics & government", true},
        {"UNDP_API_KEY", "Undp", "Economics & government", true},
        {"UNEP_API_KEY", "Unep", "Energy, climate & geospatial", true},
        {"UNFPA_API_KEY", "Unfpa", "Economics & government", true},
        {"UNHCR_API_KEY", "Unhcr", "Economics & government", true},
        {"UNICEF_API_KEY", "Unicef", "Economics & government", true},
        {"UN_COMTRADE_API_KEY", "Un Comtrade", "Economics & government", true},
        {"UN_STATS_API_KEY", "Un Stats", "Economics & government", true},
        {"USDA_API_KEY", "Usda", "Economics & government", true},
        {"USDA_ERS_API_KEY", "Usda Ers", "Economics & government", true},
        {"USDA_FAS_API_KEY", "Usda Fas", "Economics & government", true},
        {"USDA_NASS_API_KEY", "Usda Nass", "Economics & government", true},
        {"WAQI_TOKEN", "Waqi", "Energy, climate & geospatial", true},
        {"WEFORUM_API_KEY", "Weforum", "Research & datasets", true},
        {"WHO_API_KEY", "Who", "Economics & government", true},
        {"WHO_IMMUNIZATION_API_KEY", "Who Immunization", "Economics & government", true},
        {"WID_API_KEY", "Wid", "Economics & government", true},
        {"WIPO_API_KEY", "Wipo", "Research & datasets", true},
        {"WORLDBANK_API_KEY", "Worldbank", "Economics & government", true},
        {"WORLDPOP_API_KEY", "Worldpop", "Energy, climate & geospatial", true},
        {"WORLD_BANK_API_KEY", "World Bank", "Economics & government", true},
        {"WTO_API_KEY", "Wto", "Economics & government", true},
        {"YH_FINANCE_API_KEY", "Yh Finance", "Market data", true},
        {"ZENODO_ACCESS_TOKEN", "Zenodo", "Research & datasets", true},
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

} // namespace fincept::keys
