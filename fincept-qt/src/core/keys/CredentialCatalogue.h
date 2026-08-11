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
};

// 155 credentials.
inline const QVector<CredentialDef>& catalogue() {
    static const QVector<CredentialDef> kAll = {
        {"ACLED_API_KEY", "Acled"},
        {"ACLED_EMAIL", "Acled"},
        {"ADB_API_KEY", "Adb"},
        {"AFDB_API_KEY", "Afdb"},
        {"AFRICA_API_KEY", "Africa"},
        {"AISSTREAM_API_KEY", "Aisstream"},
        {"ALPHA_SPREAD_API_KEY", "Alpha Spread"},
        {"ALPHA_VANTAGE_API_KEY", "Alpha Vantage"},
        {"ARXIV_API_KEY", "Arxiv"},
        {"AUS_DATA_API_KEY", "Aus Data"},
        {"BALTIC_API_KEY", "Baltic"},
        {"BEA_API_KEY", "BEA (Bureau of Economic Analysis)"},
        {"BINANCE_API_KEY", "Binance"},
        {"BINANCE_SECRET_KEY", "Binance"},
        {"BIS_API_KEY", "Bis"},
        {"BLS_API_KEY", "BLS (Bureau of Labor Statistics)"},
        {"CANADA_GOV_API_KEY", "Canada Gov"},
        {"CBOE_API_KEY", "Cboe"},
        {"CENSUS_API_KEY", "US Census Bureau"},
        {"CFTC_APP_TOKEN", "Cftc"},
        {"CLIMATE_WATCH_API_KEY", "Climate Watch"},
        {"CMC_API_KEY", "Cmc"},
        {"CME_API_KEY", "Cme"},
        {"COINCAP_API_KEY", "Coincap"},
        {"COINGECKO_API_KEY", "Coingecko"},
        {"COINGLASS_API_KEY", "Coinglass"},
        {"COMTRADE_API_KEY", "Comtrade"},
        {"CONGRESS_GOV_API_KEY", "Congress.gov (Power Trader)"},
        {"COPERNICUS_API_KEY", "Copernicus"},
        {"CROSSREF_API_KEY", "Crossref"},
        {"CROSSREF_EMAIL", "Crossref"},
        {"CRYPTOCOMPARE_API_KEY", "Cryptocompare"},
        {"DATABENTO_API_KEY", "Databento"},
        {"DATAGOVSG_API_KEY", "Datagovsg"},
        {"DATAGOVUK_API_KEY", "Datagovuk"},
        {"DATA_GOV_HK_LANGUAGE", "Data Gov Hk Language"},
        {"DATA_WORLD_TOKEN", "Data World"},
        {"EBRD_API_KEY", "Ebrd"},
        {"EIA_API_KEY", "EIA (Energy Information Administration)"},
        {"EMBER_API_KEY", "Ember"},
        {"ENTREPRISE_API_KEY", "Entreprise"},
        {"ENTSOE_API_KEY", "Entsoe"},
        {"ENTSOG_API_KEY", "Entsog"},
        {"EODHD_API_KEY", "Eodhd"},
        {"ESTAT_APP_ID", "Estat"},
        {"EUROSTAT_API_KEY", "Eurostat"},
        {"FAO_API_KEY", "Fao"},
        {"FDIC_API_KEY", "Fdic"},
        {"FINNHUB_API_KEY", "Finnhub"},
        {"FMP_API_KEY", "Fmp"},
        {"FRED_API_KEY", "FRED (Federal Reserve)"},
        {"GFD_API_KEY", "Gfd"},
        {"GFW_API_KEY", "Gfw"},
        {"GHS_API_KEY", "Ghs"},
        {"GITHUB_TOKEN", "Github"},
        {"GLASSNODE_API_KEY", "Glassnode"},
        {"GLEIF_API_KEY", "Gleif"},
        {"GLOBAL_SOLAR_ATLAS_API_KEY", "Global Solar Atlas"},
        {"GLOBAL_WIND_ATLAS_API_KEY", "Global Wind Atlas"},
        {"GOVDATA_API_KEY", "Govdata"},
        {"GOVINFO_API_KEY", "Govinfo"},
        {"HARVARD_DATAVERSE_TOKEN", "Harvard Dataverse"},
        {"HUD_API_KEY", "Hud"},
        {"IADB_API_KEY", "Iadb"},
        {"ICE_API_KEY", "Ice"},
        {"IEA_API_KEY", "Iea"},
        {"IEX_CLOUD_API_KEY", "Iex Cloud"},
        {"IEX_CLOUD_TOKEN", "Iex Cloud"},
        {"ILOSTAT_API_KEY", "Ilostat"},
        {"IMF_API_KEY", "Imf"},
        {"INTRINIO_API_KEY", "Intrinio"},
        {"INVESTING_API_KEY", "Investing"},
        {"IRENA_API_KEY", "Irena"},
        {"IRS_API_KEY", "Irs"},
        {"ISDB_API_KEY", "Isdb"},
        {"KAGGLE_KEY", "Kaggle"},
        {"KAGGLE_USERNAME", "Kaggle Username"},
        {"KRAKEN_API_KEY", "Kraken"},
        {"KRAKEN_SECRET_KEY", "Kraken"},
        {"LME_API_KEY", "Lme"},
        {"LOGURU_AUTOINIT", "Loguru Autoinit"},
        {"LOGURU_LEVEL", "Loguru Level"},
        {"MACROTRENDS_API_KEY", "Macrotrends"},
        {"MADDISON_API_KEY", "Maddison"},
        {"MARINETRAFFIC_API_KEY", "Marinetraffic"},
        {"MARKETSTACK_API_KEY", "Marketstack"},
        {"MESSARI_API_KEY", "Messari"},
        {"METALS_DEV_API_KEY", "Metals Dev"},
        {"N2YO_API_KEY", "N2yo"},
        {"NASDAQ_API_KEY", "Nasdaq"},
        {"NASDAQ_DATA_LINK_API_KEY", "Nasdaq Data Link (Quandl)"},
        {"NBER_API_KEY", "Nber"},
        {"NOAA_CDO_TOKEN", "Noaa Cdo"},
        {"NUMBEO_API_KEY", "Numbeo"},
        {"OBS_API_KEY", "Obs"},
        {"OC_API_KEY", "Oc"},
        {"OECD_API_KEY", "Oecd"},
        {"OPEC_API_KEY", "Opec"},
        {"OPENAQ_API_KEY", "Openaq"},
        {"OPENBB_API_KEY", "Openbb"},
        {"OPENEXCHANGERATES_API_KEY", "Openexchangerates"},
        {"OPENFIGI_API_KEY", "Openfigi"},
        {"OPENSECRETS_API_KEY", "Opensecrets"},
        {"OPEN_OWNERSHIP_API_KEY", "Open Ownership"},
        {"OPSD_API_KEY", "Opsd"},
        {"PLATTS_API_KEY", "Platts"},
        {"POLYGON_API_KEY", "Polygon.io"},
        {"POLYMARKET_API_KEY", "Polymarket"},
        {"POLYMARKET_PASSPHRASE", "Polymarket Passphrase"},
        {"POLYMARKET_SECRET", "Polymarket"},
        {"POLYMARKET_WALLET", "Polymarket Wallet"},
        {"PORTCHAIN_API_KEY", "Portchain"},
        {"PRS_API_KEY", "Prs"},
        {"PUBLIC_APIS_FINANCE_KEY", "Public Apis Finance"},
        {"PWT_API_KEY", "Pwt"},
        {"QUIVER_API_KEY", "Quiver"},
        {"RELIEFWEB_API_KEY", "Reliefweb"},
        {"SEMANTIC_SCHOLAR_API_KEY", "Semantic Scholar"},
        {"SENTINELHUB_CLIENT_ID", "Sentinelhub Client Id"},
        {"SENTINELHUB_CLIENT_SECRET", "Sentinelhub Client"},
        {"SIMFIN_API_KEY", "Simfin"},
        {"SSRN_API_KEY", "Ssrn"},
        {"STOOQ_API_KEY", "Stooq"},
        {"SWISS_GOV_API_KEY", "Swiss Gov"},
        {"TIINGO_API_KEY", "Tiingo"},
        {"TRADINGVIEW_API_KEY", "Tradingview"},
        {"TRADING_ECONOMICS_API_KEY", "Trading Economics"},
        {"TRANSPARENCY_API_KEY", "Transparency"},
        {"TREASURY_API_KEY", "Treasury"},
        {"TWELVE_DATA_API_KEY", "Twelve Data"},
        {"TWFY_API_KEY", "Twfy"},
        {"UNCTAD_API_KEY", "Unctad"},
        {"UNDP_API_KEY", "Undp"},
        {"UNEP_API_KEY", "Unep"},
        {"UNFPA_API_KEY", "Unfpa"},
        {"UNHCR_API_KEY", "Unhcr"},
        {"UNICEF_API_KEY", "Unicef"},
        {"UN_COMTRADE_API_KEY", "Un Comtrade"},
        {"UN_STATS_API_KEY", "Un Stats"},
        {"USDA_API_KEY", "Usda"},
        {"USDA_ERS_API_KEY", "Usda Ers"},
        {"USDA_FAS_API_KEY", "Usda Fas"},
        {"USDA_NASS_API_KEY", "Usda Nass"},
        {"WAQI_TOKEN", "Waqi"},
        {"WEFORUM_API_KEY", "Weforum"},
        {"WHO_API_KEY", "Who"},
        {"WHO_IMMUNIZATION_API_KEY", "Who Immunization"},
        {"WID_API_KEY", "Wid"},
        {"WIPO_API_KEY", "Wipo"},
        {"WORLDBANK_API_KEY", "Worldbank"},
        {"WORLDPOP_API_KEY", "Worldpop"},
        {"WORLD_BANK_API_KEY", "World Bank"},
        {"WTO_API_KEY", "Wto"},
        {"YH_FINANCE_API_KEY", "Yh Finance"},
        {"ZENODO_ACCESS_TOKEN", "Zenodo"},
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
