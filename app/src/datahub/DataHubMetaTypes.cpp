#include "datahub/DataHubMetaTypes.h"

#include "core/logging/Logger.h"

namespace finterm::datahub {

void register_metatypes() {
    qRegisterMetaType<finterm::services::QuoteData>("finterm::services::QuoteData");
    qRegisterMetaType<finterm::services::MarketDataService::TopMovers>("finterm::services::MarketDataService::TopMovers");
    qRegisterMetaType<finterm::services::HistoryPoint>("finterm::services::HistoryPoint");
    qRegisterMetaType<finterm::services::InfoData>("finterm::services::InfoData");
    qRegisterMetaType<finterm::services::NewsArticle>("finterm::services::NewsArticle");
    qRegisterMetaType<finterm::services::EconomicsResult>("finterm::services::EconomicsResult");
    qRegisterMetaType<QVector<finterm::services::HistoryPoint>>("QVector<finterm::services::HistoryPoint>");
    qRegisterMetaType<QVector<finterm::services::NewsArticle>>("QVector<finterm::services::NewsArticle>");
    qRegisterMetaType<QVector<double>>("QVector<double>");
    qRegisterMetaType<finterm::trading::TickerData>("finterm::trading::TickerData");
    qRegisterMetaType<finterm::trading::OrderBookData>("finterm::trading::OrderBookData");
    qRegisterMetaType<finterm::trading::Candle>("finterm::trading::Candle");
    qRegisterMetaType<finterm::trading::TradeData>("finterm::trading::TradeData");
    qRegisterMetaType<finterm::datahub::SubscriptionState>("finterm::datahub::SubscriptionState");
    qRegisterMetaType<finterm::services::polymarket::OrderBook>("finterm::services::polymarket::OrderBook");

    // Prediction Markets (Polymarket, Kalshi, …)
    qRegisterMetaType<finterm::services::prediction::MarketKey>("finterm::services::prediction::MarketKey");
    qRegisterMetaType<finterm::services::prediction::PredictionMarket>(
        "finterm::services::prediction::PredictionMarket");
    qRegisterMetaType<finterm::services::prediction::PredictionEvent>("finterm::services::prediction::PredictionEvent");
    qRegisterMetaType<finterm::services::prediction::PredictionOrderBook>(
        "finterm::services::prediction::PredictionOrderBook");
    qRegisterMetaType<finterm::services::prediction::PriceHistory>("finterm::services::prediction::PriceHistory");
    qRegisterMetaType<finterm::services::prediction::PredictionTrade>("finterm::services::prediction::PredictionTrade");
    qRegisterMetaType<finterm::services::prediction::PredictionPosition>(
        "finterm::services::prediction::PredictionPosition");
    qRegisterMetaType<finterm::services::prediction::OpenOrder>("finterm::services::prediction::OpenOrder");
    qRegisterMetaType<finterm::services::prediction::OrderResult>("finterm::services::prediction::OrderResult");
    qRegisterMetaType<finterm::services::prediction::AccountBalance>("finterm::services::prediction::AccountBalance");
    qRegisterMetaType<QVector<finterm::services::prediction::PredictionMarket>>(
        "QVector<finterm::services::prediction::PredictionMarket>");
    qRegisterMetaType<QVector<finterm::services::prediction::PredictionEvent>>(
        "QVector<finterm::services::prediction::PredictionEvent>");
    qRegisterMetaType<QVector<finterm::services::prediction::PredictionTrade>>(
        "QVector<finterm::services::prediction::PredictionTrade>");
    qRegisterMetaType<QVector<finterm::services::prediction::PredictionPosition>>(
        "QVector<finterm::services::prediction::PredictionPosition>");
    qRegisterMetaType<QVector<finterm::services::prediction::OpenOrder>>(
        "QVector<finterm::services::prediction::OpenOrder>");
    qRegisterMetaType<finterm::services::DbnDataPoint>("finterm::services::DbnDataPoint");
    qRegisterMetaType<finterm::services::GovDataResult>("finterm::services::GovDataResult");
    qRegisterMetaType<finterm::trading::BrokerPosition>("finterm::trading::BrokerPosition");
    qRegisterMetaType<finterm::trading::BrokerHolding>("finterm::trading::BrokerHolding");
    qRegisterMetaType<finterm::trading::BrokerOrderInfo>("finterm::trading::BrokerOrderInfo");
    qRegisterMetaType<finterm::trading::BrokerQuote>("finterm::trading::BrokerQuote");
    qRegisterMetaType<finterm::trading::BrokerFunds>("finterm::trading::BrokerFunds");
    qRegisterMetaType<QVector<finterm::trading::BrokerPosition>>("QVector<finterm::trading::BrokerPosition>");
    qRegisterMetaType<QVector<finterm::trading::BrokerHolding>>("QVector<finterm::trading::BrokerHolding>");
    qRegisterMetaType<QVector<finterm::trading::BrokerOrderInfo>>("QVector<finterm::trading::BrokerOrderInfo>");
    qRegisterMetaType<QVector<finterm::trading::BrokerQuote>>("QVector<finterm::trading::BrokerQuote>");

    // Phase 8 — Geopolitics / Maritime / RelationshipMap
    qRegisterMetaType<finterm::services::geo::NewsEvent>("finterm::services::geo::NewsEvent");
    qRegisterMetaType<finterm::services::geo::HDXDataset>("finterm::services::geo::HDXDataset");
    qRegisterMetaType<finterm::services::geo::UniqueCountry>("finterm::services::geo::UniqueCountry");
    qRegisterMetaType<finterm::services::geo::UniqueCategory>("finterm::services::geo::UniqueCategory");
    qRegisterMetaType<QVector<finterm::services::geo::NewsEvent>>("QVector<finterm::services::geo::NewsEvent>");
    qRegisterMetaType<QVector<finterm::services::geo::HDXDataset>>("QVector<finterm::services::geo::HDXDataset>");
    qRegisterMetaType<QVector<finterm::services::geo::UniqueCountry>>("QVector<finterm::services::geo::UniqueCountry>");
    qRegisterMetaType<QVector<finterm::services::geo::UniqueCategory>>(
        "QVector<finterm::services::geo::UniqueCategory>");
    qRegisterMetaType<finterm::services::maritime::VesselData>("finterm::services::maritime::VesselData");
    qRegisterMetaType<QVector<finterm::services::maritime::VesselData>>(
        "QVector<finterm::services::maritime::VesselData>");
    qRegisterMetaType<finterm::relmap::RelationshipData>("finterm::relmap::RelationshipData");

    LOG_INFO("DataHub", "Registered 51 payload meta-types");
}

} // namespace finterm::datahub
