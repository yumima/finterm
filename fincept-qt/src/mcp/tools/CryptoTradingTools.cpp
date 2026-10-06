// CryptoTradingTools.cpp — Crypto Trading tab MCP tools (ticker, order book, candles, exchange info)

#include "mcp/tools/CryptoTradingTools.h"

#include "core/logging/Logger.h"
#include "trading/ExchangeService.h"

namespace fincept::mcp::tools {

namespace {
// TickerData/Candle store ccxt nulls as 0. A price of 0 is never real (and a
// 24h volume of exactly 0 is indistinguishable from null), so report those as
// JSON null ("unavailable") instead of a fabricated 0.
QJsonValue positive_or_null(double v) {
    return v > 0.0 ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
}
} // namespace

std::vector<ToolDef> get_crypto_trading_tools() {
    std::vector<ToolDef> tools;

    // ── get_ticker ─────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_ticker";
        t.description = "Get the latest price, volume, and change for a symbol from the configured exchange.";
        t.category = "crypto-trading";
        t.input_schema.properties = QJsonObject{
            {"symbol", QJsonObject{{"type", "string"}, {"description", "Trading pair (e.g. BTC/USDT, ETH/USDT)"}}}};
        t.input_schema.required = {"symbol"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString symbol = args["symbol"].toString().trimmed();
            if (symbol.isEmpty())
                return ToolResult::fail("Missing 'symbol'");

            auto& svc = trading::ExchangeService::instance();
            if (svc.get_exchange().isEmpty())
                return ToolResult::fail("No exchange configured — set one in Crypto Trading first");

            try {
                auto ticker = svc.fetch_ticker(symbol);
                // Empty symbol is ExchangeSession's failure sentinel (exchange error).
                if (ticker.symbol.isEmpty())
                    return ToolResult::fail(QString("Exchange returned no ticker for %1 (%2)")
                                                .arg(symbol, svc.get_exchange()));
                if (!(ticker.last > 0.0))
                    return ToolResult::fail(QString("No last price available for %1").arg(symbol));
                // change/percentage: ccxt null parses as 0. A genuine 0 change is
                // only reportable when it is corroborated by open == last.
                const bool flat_confirmed = ticker.open > 0.0 && ticker.last == ticker.open;
                const QJsonValue change = (ticker.change != 0.0 || flat_confirmed)
                                              ? QJsonValue(ticker.change)
                                              : QJsonValue(QJsonValue::Null);
                const QJsonValue change_pct = (ticker.percentage != 0.0 || flat_confirmed)
                                                  ? QJsonValue(ticker.percentage)
                                                  : QJsonValue(QJsonValue::Null);
                return ToolResult::ok_data(QJsonObject{
                    {"symbol", ticker.symbol},
                    {"last", ticker.last},
                    {"bid", positive_or_null(ticker.bid)},
                    {"ask", positive_or_null(ticker.ask)},
                    {"high", positive_or_null(ticker.high)},
                    {"low", positive_or_null(ticker.low)},
                    {"open", positive_or_null(ticker.open)},
                    {"close", positive_or_null(ticker.close)},
                    {"change", change},
                    {"change_pct", change_pct},
                    {"volume", positive_or_null(ticker.base_volume)},
                    {"quote_volume", positive_or_null(ticker.quote_volume)},
                    {"timestamp", ticker.timestamp > 0 ? QJsonValue(static_cast<double>(ticker.timestamp))
                                                       : QJsonValue(QJsonValue::Null)}});
            } catch (const std::exception& e) {
                return ToolResult::fail(e.what());
            }
        };
        tools.push_back(std::move(t));
    }

    // ── get_order_book ─────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_order_book";
        t.description = "Get the order book (bids and asks) for a symbol.";
        t.category = "crypto-trading";
        t.input_schema.properties =
            QJsonObject{{"symbol", QJsonObject{{"type", "string"}, {"description", "Trading pair"}}},
                        {"limit", QJsonObject{{"type", "integer"}, {"description", "Number of levels (default: 20)"}}}};
        t.input_schema.required = {"symbol"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString symbol = args["symbol"].toString().trimmed();
            int limit = args["limit"].toInt(20);
            if (symbol.isEmpty())
                return ToolResult::fail("Missing 'symbol'");

            auto& svc = trading::ExchangeService::instance();
            if (svc.get_exchange().isEmpty())
                return ToolResult::fail("No exchange configured");

            try {
                auto ob = svc.fetch_orderbook(symbol, limit);
                // Empty symbol is ExchangeSession's failure sentinel (exchange error).
                if (ob.symbol.isEmpty())
                    return ToolResult::fail(QString("Exchange returned no order book for %1 (%2)")
                                                .arg(symbol, svc.get_exchange()));
                QJsonArray bids, asks;
                for (const auto& level : ob.bids)
                    bids.append(QJsonObject{{"price", level.first}, {"amount", level.second}});
                for (const auto& level : ob.asks)
                    asks.append(QJsonObject{{"price", level.first}, {"amount", level.second}});

                // Spread is only meaningful when both sides exist; otherwise null.
                const bool two_sided = ob.best_bid > 0.0 && ob.best_ask > 0.0;
                return ToolResult::ok_data(QJsonObject{
                    {"symbol", ob.symbol},
                    {"best_bid", positive_or_null(ob.best_bid)},
                    {"best_ask", positive_or_null(ob.best_ask)},
                    {"spread", two_sided ? QJsonValue(ob.spread) : QJsonValue(QJsonValue::Null)},
                    {"spread_pct", two_sided ? QJsonValue(ob.spread_pct) : QJsonValue(QJsonValue::Null)},
                    {"bids", bids},
                    {"asks", asks}});
            } catch (const std::exception& e) {
                return ToolResult::fail(e.what());
            }
        };
        tools.push_back(std::move(t));
    }

    // ── get_candles ────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_candles";
        t.description = "Get OHLCV candle data for a symbol. Timeframes: 1m, 5m, 15m, 1h, 4h, 1d.";
        t.category = "crypto-trading";
        t.input_schema.properties = QJsonObject{
            {"symbol", QJsonObject{{"type", "string"}, {"description", "Trading pair"}}},
            {"timeframe", QJsonObject{{"type", "string"}, {"description", "Candle interval (default: 1h)"}}},
            {"limit", QJsonObject{{"type", "integer"}, {"description", "Number of candles (default: 100)"}}}};
        t.input_schema.required = {"symbol"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString symbol = args["symbol"].toString().trimmed();
            QString timeframe = args["timeframe"].toString("1h");
            int limit = args["limit"].toInt(100);
            if (symbol.isEmpty())
                return ToolResult::fail("Missing 'symbol'");

            auto& svc = trading::ExchangeService::instance();
            if (svc.get_exchange().isEmpty())
                return ToolResult::fail("No exchange configured");

            try {
                auto candles = svc.fetch_ohlcv(symbol, timeframe, limit);
                // Empty result means the exchange call failed (or returned nothing).
                if (candles.isEmpty())
                    return ToolResult::fail(QString("Exchange returned no candles for %1 %2 (%3)")
                                                .arg(symbol, timeframe, svc.get_exchange()));
                QJsonArray result;
                for (const auto& c : candles) {
                    result.append(QJsonObject{{"timestamp", static_cast<double>(c.timestamp)},
                                              {"open", positive_or_null(c.open)},
                                              {"high", positive_or_null(c.high)},
                                              {"low", positive_or_null(c.low)},
                                              {"close", positive_or_null(c.close)},
                                              {"volume", c.volume}});
                }
                return ToolResult::ok_data(result);
            } catch (const std::exception& e) {
                return ToolResult::fail(e.what());
            }
        };
        tools.push_back(std::move(t));
    }

    // ── get_exchange_info ──────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_exchange_info";
        t.description = "Get the currently configured exchange name and list all available exchange IDs.";
        t.category = "crypto-trading";
        t.handler = [](const QJsonObject&) -> ToolResult {
            auto& svc = trading::ExchangeService::instance();
            QString current = svc.get_exchange();

            try {
                auto ids = svc.list_exchange_ids();
                QJsonArray id_arr;
                for (const auto& id : ids)
                    id_arr.append(id);

                return ToolResult::ok_data(QJsonObject{{"current_exchange", current.isEmpty() ? "none" : current},
                                                       {"available_exchanges", id_arr}});
            } catch (const std::exception& e) {
                return ToolResult::fail(QString("Failed to list exchanges: %1").arg(e.what()));
            } catch (...) {
                return ToolResult::fail("Failed to list exchanges");
            }
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools
