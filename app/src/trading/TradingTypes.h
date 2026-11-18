#pragma once
// Trading Types — shared data types for paper trading, brokers, and exchange services.
// Qt-adapted port from the ImGui codebase (portfolio_types.h + broker_types.h + exchange_service types).

// QJsonObject is a value member of one struct below; QJsonArray was previously
// imported here too but had no use in this header — dropped to save the
// transitive include cost across the ~36 translation units that pull this in.
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace finterm::trading {

// ============================================================================
// Paper Trading Types
// ============================================================================

struct PtPortfolio {
    QString id;
    QString name;
    double initial_balance = 0.0;
    double balance = 0.0;
    QString currency = "USD";
    double leverage = 1.0;
    QString margin_mode = "cross";
    double fee_rate = 0.001;
    QString exchange;
    QString created_at;
};

struct PtOrder {
    QString id;
    QString portfolio_id;
    QString symbol;
    QString side;       // "buy" | "sell"
    QString order_type; // "market" | "limit" | "stop" | "stop_limit"
    double quantity = 0.0;
    std::optional<double> price;
    std::optional<double> stop_price;
    double filled_qty = 0.0;
    std::optional<double> avg_price;
    QString status = "pending";
    bool reduce_only = false;
    QString created_at;
    std::optional<QString> filled_at;
};

struct PtPosition {
    QString id;
    QString portfolio_id;
    QString symbol;
    QString side; // "long" | "short"
    double quantity = 0.0;
    double entry_price = 0.0;
    double current_price = 0.0;
    double unrealized_pnl = 0.0;
    double realized_pnl = 0.0;
    double leverage = 1.0;
    std::optional<double> liquidation_price;
    QString opened_at;
};

struct PtTrade {
    QString id;
    QString portfolio_id;
    QString order_id;
    QString symbol;
    QString side;
    double price = 0.0;
    double quantity = 0.0;
    double fee = 0.0;
    double pnl = 0.0;
    QString timestamp;
};

struct PtStats {
    double total_pnl = 0.0;
    double win_rate = 0.0;
    int64_t total_trades = 0;
    int64_t winning_trades = 0;
    int64_t losing_trades = 0;
    double largest_win = 0.0;
    double largest_loss = 0.0;
};

struct PriceData {
    double last = 0.0;
    double bid = 0.0;
    double ask = 0.0;
    double high = 0.0;
    double low = 0.0;
    double volume = 0.0;
    double change = 0.0;
    double change_percent = 0.0;
    int64_t timestamp = 0;
};

// ============================================================================
// Broker Types
// ============================================================================

template <typename T>
struct ApiResponse {
    bool success = false;
    std::optional<T> data;
    QString error;
    int64_t timestamp = 0;
};

struct TokenExchangeResponse {
    bool success = false;
    QString access_token;
    QString refresh_token;
    QString user_id;
    QString additional_data;
    QString error;
};

struct OrderPlaceResponse {
    bool success = false;
    QString order_id;
    QString error;
};

struct BrokerCredentials {
    QString broker_id;
    QString api_key;
    QString api_secret;
    QString access_token;
    QString refresh_token;
    QString user_id;
    QString additional_data;
};

enum class OrderSide { Buy, Sell };
enum class OrderType { Market, Limit, StopLoss, StopLossLimit };
enum class ProductType { Intraday, Delivery, Margin, CoverOrder, BracketOrder, MTF };

inline const char* order_side_str(OrderSide s) {
    return s == OrderSide::Buy ? "buy" : "sell";
}

inline const char* order_type_str(OrderType t) {
    switch (t) {
        case OrderType::Market:
            return "market";
        case OrderType::Limit:
            return "limit";
        case OrderType::StopLoss:
            return "stop_loss";
        case OrderType::StopLossLimit:
            return "stop_loss_limit";
    }
    return "market";
}

struct UnifiedOrder {
    QString symbol;
    QString exchange;
    OrderSide side = OrderSide::Buy;
    OrderType order_type = OrderType::Market;
    double quantity = 0;
    double price = 0;
    double stop_price = 0;
    ProductType product_type = ProductType::Intraday;
    QString validity = "DAY";
    double stop_loss = 0;
    double take_profit = 0;
    QString instrument_token; // broker-specific numeric token
};

struct BrokerPosition {
    QString symbol;
    QString exchange;
    QString product_type;
    double quantity = 0;
    double avg_price = 0;
    double ltp = 0;
    double pnl = 0;
    double pnl_pct = 0;
    /// Today's P&L only. Two adapters used to fill this with REALIZED P&L,
    /// which put the same figure in the Day and Realized tiles and made the
    /// day number meaningless.
    double day_pnl = 0;
    /// False when the broker's API does not report a day figure at all. The
    /// UI must render "—" rather than a green +$0.00, which is
    /// indistinguishable from a genuinely flat day.
    bool day_pnl_reported = true;
    /// Cumulative realized P&L on this position, when the broker reports it.
    double realized_pnl = 0;
    bool realized_pnl_reported = false;
    QString side;
};

struct BrokerHolding {
    QString symbol;
    QString exchange;
    double quantity = 0;
    double avg_price = 0;
    double ltp = 0;
    double pnl = 0;
    double pnl_pct = 0;
    double invested_value = 0;
    double current_value = 0;
};

struct BrokerOrderInfo {
    QString order_id;
    QString symbol;
    QString exchange;
    QString side;
    QString order_type;
    QString product_type;
    double quantity = 0;
    double price = 0;
    double stop_price = 0;
    double filled_qty = 0;
    double avg_price = 0;
    QString status;
    QString timestamp;
    QString message;
};

struct BrokerQuote {
    QString symbol;
    double ltp = 0;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
    double change = 0;
    double change_pct = 0;
    double bid = 0;
    double ask = 0;
    double bid_size = 0;
    double ask_size = 0;
    int64_t timestamp = 0;
    // F&O fields — populated when the symbol is a derivative (CE/PE/FUT)
    // and the broker quote response carries OI. Default zero is safe for
    // equity / spot quotes where OI is not applicable.
    qint64 oi = 0;
};

struct BrokerCandle {
    int64_t timestamp = 0;
    double open = 0;
    double high = 0;
    double low = 0;
    double close = 0;
    double volume = 0;
    // Open Interest — populated when the broker historical endpoint returns it
    // (futures/options contracts only). Defaults to 0
    // so existing 6-arg brace-init call sites remain source-compatible.
    double oi = 0;
};

struct BrokerFunds {
    double available_balance = 0;
    double used_margin = 0;
    double total_balance = 0;
    double collateral = 0;
    QJsonObject raw_data;
};

struct MarketCalendarDay {
    QString date;          // "YYYY-MM-DD"
    QString open;          // "09:30"
    QString close;         // "16:00"
    QString session_open;  // pre-market open if present
    QString session_close; // after-hours close if present
};

struct MarketClock {
    QString timestamp; // ISO8601 current time
    bool is_open = false;
    QString next_open;  // ISO8601
    QString next_close; // ISO8601
};

// Individual trade print (time & sales)
struct BrokerTrade {
    QString symbol;
    double price = 0;
    double size = 0;
    QString exchange;
    QString timestamp; // ISO8601
    QStringList conditions;
    QString tape; // "A"/"B"/"C"
};

// ── Margin Calculator ─────────────────────────────────────────────────────────

/// Per-order margin breakdown returned by the broker's pre-trade margin API.
struct OrderMargin {
    QString symbol;
    QString exchange;
    QString side; // "BUY" / "SELL"
    double quantity = 0.0;
    double price = 0.0;

    // Margin components (all in account currency)
    double total = 0.0;      // total margin required
    double additional = 0.0; // additional margin (futures/options)
    double cash = 0.0;       // cash component
    double pnl = 0.0;        // unrealised P&L used as collateral
    double leverage = 0.0;   // implied leverage ratio (total / (price * qty))
    QString error;           // per-order error if margin could not be calculated
};

enum class BrokerId { Alpaca, IBKR, Tradier, SaxoBank };

inline const char* broker_id_str(BrokerId id) {
    switch (id) {
        case BrokerId::Alpaca:
            return "alpaca";
        case BrokerId::IBKR:
            return "ibkr";
        case BrokerId::Tradier:
            return "tradier";
        case BrokerId::SaxoBank:
            return "saxobank";
    }
    return "unknown";
}

// ============================================================================
// Exchange Service Types (crypto/ccxt)
// ============================================================================

struct TickerData {
    QString symbol;
    double last = 0.0;
    double bid = 0.0;
    double ask = 0.0;
    double high = 0.0;
    double low = 0.0;
    double open = 0.0;
    double close = 0.0;
    double change = 0.0;
    double percentage = 0.0;
    double base_volume = 0.0;
    double quote_volume = 0.0;
    int64_t timestamp = 0;
};

struct Candle {
    int64_t timestamp = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
};

struct OrderBookData {
    QString symbol;
    QVector<QPair<double, double>> bids;
    QVector<QPair<double, double>> asks;
    double best_bid = 0.0;
    double best_ask = 0.0;
    double spread = 0.0;
    double spread_pct = 0.0;
};

struct MarketInfo {
    QString symbol;
    QString base;
    QString quote;
    QString type;
    bool active = true;
    double maker_fee = 0.0;
    double taker_fee = 0.0;
    double min_amount = 0.0;
    double min_cost = 0.0;
};

struct TradeData {
    QString id;
    QString symbol;
    QString side;
    double price = 0.0;
    double amount = 0.0;
    double cost = 0.0;
    int64_t timestamp = 0;
};

struct FundingRateData {
    QString symbol;
    double funding_rate = 0.0;
    double mark_price = 0.0;
    double index_price = 0.0;
    int64_t funding_timestamp = 0;
    int64_t next_funding_timestamp = 0;
};

struct OpenInterestData {
    QString symbol;
    double open_interest = 0.0;
    double open_interest_value = 0.0;
    int64_t timestamp = 0;
};

struct MarkPriceData {
    QString symbol;
    double mark_price = 0.0;
    double index_price = 0.0;
    int64_t timestamp = 0;
};

struct ExchangeCredentials {
    QString api_key;
    QString secret;
    QString password;
};

// ============================================================================
// Equity Stream Types
// ============================================================================

// ============================================================================
// Unified Trading Types
// ============================================================================

struct TradingSession {
    QString broker;
    QString mode; // "live" or "paper"
    QString paper_portfolio_id;
    bool is_connected = false;
};

struct UnifiedOrderResponse {
    bool success = false;
    QString order_id;
    QString message;
    QString mode;
};

} // namespace finterm::trading

#include <QMetaType>
Q_DECLARE_METATYPE(finterm::trading::BrokerPosition)
Q_DECLARE_METATYPE(finterm::trading::BrokerHolding)
Q_DECLARE_METATYPE(finterm::trading::BrokerOrderInfo)
Q_DECLARE_METATYPE(finterm::trading::BrokerQuote)
Q_DECLARE_METATYPE(finterm::trading::BrokerFunds)
Q_DECLARE_METATYPE(QVector<finterm::trading::BrokerPosition>)
Q_DECLARE_METATYPE(QVector<finterm::trading::BrokerHolding>)
Q_DECLARE_METATYPE(QVector<finterm::trading::BrokerOrderInfo>)
Q_DECLARE_METATYPE(QVector<finterm::trading::BrokerQuote>)
