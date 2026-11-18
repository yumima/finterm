#include "trading/brokers/alpaca/AlpacaBroker.h"

#include "core/logging/Logger.h"
#include "trading/adapter/BrokerEnumMap.h"
#include "trading/brokers/BrokerHttp.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace finterm::trading {

static int64_t now_ts() {
    return QDateTime::currentSecsSinceEpoch();
}
// ═══════════════════════════════════════════════════════════════════════════════
// ALPACA — US broker, API key/secret in headers (no OAuth)
// additional_data == "live"  → https://api.alpaca.markets
// additional_data == "paper" → https://paper-api.alpaca.markets  (default)
// Market data always → https://data.alpaca.markets
// ═══════════════════════════════════════════════════════════════════════════════

QString AlpacaBroker::trading_url(const BrokerCredentials& creds) {
    // additional_data stores "live"/"paper" set during exchange_token.
    // Fallback: detect from key prefix (AK=live, PK=paper) for stale/missing additional_data.
    bool is_live = (creds.additional_data == "live") ||
                   (creds.additional_data.isEmpty() && creds.api_key.startsWith("AK", Qt::CaseInsensitive));
    return is_live ? "https://api.alpaca.markets" : "https://paper-api.alpaca.markets";
}

QMap<QString, QString> AlpacaBroker::auth_headers(const BrokerCredentials& creds) const {
    return {{"APCA-API-KEY-ID", creds.api_key},
            {"APCA-API-SECRET-KEY", creds.api_secret},
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}};
}

// exchange_token: validate credentials by hitting /v2/account.
// Auto-detects environment from key prefix: AK* = live, PK* = paper.
// auth_code from UI combo is used as a hint but key prefix wins.
TokenExchangeResponse AlpacaBroker::exchange_token(const QString& api_key, const QString& api_secret,
                                                   const QString& auth_code) {
    // Key prefix detection: AK = live key, PK = paper key
    QString env;
    if (api_key.startsWith("AK", Qt::CaseInsensitive))
        env = "live";
    else if (api_key.startsWith("PK", Qt::CaseInsensitive))
        env = "paper";
    else
        env = (auth_code == "live") ? "live" : "paper"; // fallback to UI selection

    QMap<QString, QString> headers = {
        {"APCA-API-KEY-ID", api_key}, {"APCA-API-SECRET-KEY", api_secret}, {"Accept", "application/json"}};

    auto try_env = [&](const QString& e) -> TokenExchangeResponse {
        const QString base = (e == "live") ? "https://api.alpaca.markets" : "https://paper-api.alpaca.markets";
        auto resp = BrokerHttp::instance().get(base + "/v2/account", headers);
        TokenExchangeResponse r;
        if (!resp.success) {
            r.error = resp.error.isEmpty() ? resp.raw_body : resp.error;
            return r;
        }
        r.success = true;
        r.access_token = api_secret;
        r.user_id = resp.json.value("account_number").toString();
        r.additional_data = e;
        return r;
    };

    auto result = try_env(env);
    if (!result.success) {
        // Try opposite env as fallback
        const QString other = (env == "live") ? "paper" : "live";
        LOG_INFO("AlpacaBroker", QString("Auth failed on %1, trying %2").arg(env, other));
        result = try_env(other);
    }
    return result;
}

namespace {
const BrokerEnumMap<QString>& alpaca_enum_map() {
    static const auto m = [] {
        BrokerEnumMap<QString> x;
        x.set(OrderType::Market, "market");
        x.set(OrderType::Limit, "limit");
        x.set(OrderType::StopLoss, "stop");
        x.set(OrderType::StopLossLimit, "stop_limit");
        x.set(OrderSide::Buy, "buy");
        x.set(OrderSide::Sell, "sell");
        return x;
    }();
    return m;
}
} // namespace

OrderPlaceResponse AlpacaBroker::place_order(const BrokerCredentials& creds, const UnifiedOrder& order) {
    // time_in_force rules:
    // - Market orders: "day" + extended_hours=true so they work pre/post market
    // - Limit/Stop orders: "gtc" so they queue and fill when market opens
    const bool is_market = (order.order_type == OrderType::Market);
    const QString tif = is_market ? "day" : "gtc";

    QJsonObject payload{{"symbol", order.symbol},
                        {"qty", QString::number(order.quantity)},
                        {"side", alpaca_enum_map().side_or(order.side, "buy")},
                        {"type", alpaca_enum_map().order_type_or(order.order_type, "market")},
                        {"time_in_force", tif}};
    if (is_market)
        payload["extended_hours"] = true; // allow pre/post market execution
    if (order.price > 0)
        payload["limit_price"] = QString::number(order.price, 'f', 2);
    if (order.stop_price > 0)
        payload["stop_price"] = QString::number(order.stop_price, 'f', 2);

    auto resp = BrokerHttp::instance().post_json(trading_url(creds) + "/v2/orders", payload, auth_headers(creds));
    OrderPlaceResponse result;
    if (!resp.success) {
        result.error = resp.error;
        return result;
    }
    result.success = true;
    result.order_id = resp.json.value("id").toString();
    return result;
}

ApiResponse<QJsonObject> AlpacaBroker::modify_order(const BrokerCredentials& creds, const QString& order_id,
                                                    const QJsonObject& mods) {
    auto resp =
        BrokerHttp::instance().put_json(trading_url(creds) + "/v2/orders/" + order_id, mods, auth_headers(creds));
    int64_t ts = now_ts();
    return resp.success ? ApiResponse<QJsonObject>{true, resp.json, "", ts}
                        : ApiResponse<QJsonObject>{false, std::nullopt, resp.error, ts};
}

ApiResponse<QJsonObject> AlpacaBroker::cancel_order(const BrokerCredentials& creds, const QString& order_id) {
    auto resp = BrokerHttp::instance().del(trading_url(creds) + "/v2/orders/" + order_id, auth_headers(creds));
    int64_t ts = now_ts();
    return resp.success ? ApiResponse<QJsonObject>{true, resp.json, "", ts}
                        : ApiResponse<QJsonObject>{false, std::nullopt, resp.error, ts};
}

ApiResponse<QVector<BrokerOrderInfo>> AlpacaBroker::get_orders(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(trading_url(creds) + "/v2/orders?status=all&limit=100", auth_headers(creds));
    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};
    QVector<BrokerOrderInfo> orders;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    auto jdo = [](const QJsonValue& v) -> double { return v.isDouble() ? v.toDouble() : v.toString().toDouble(); };
    if (doc.isArray()) {
        for (const auto& v : doc.array()) {
            auto o = v.toObject();
            BrokerOrderInfo info;
            info.order_id = o.value("id").toString();
            info.symbol = o.value("symbol").toString();
            info.side = o.value("side").toString();
            info.order_type = o.value("type").toString();
            info.quantity = jdo(o.value("qty"));
            info.price = jdo(o.value("limit_price"));
            info.filled_qty = jdo(o.value("filled_qty"));
            info.avg_price = jdo(o.value("filled_avg_price"));
            info.status = o.value("status").toString();
            info.timestamp = o.value("submitted_at").toString();
            orders.append(info);
        }
    }
    return {true, orders, "", ts};
}

ApiResponse<QJsonObject> AlpacaBroker::get_trade_book(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(trading_url(creds) + "/v2/account/activities/FILL", auth_headers(creds));
    int64_t ts = now_ts();
    return resp.success ? ApiResponse<QJsonObject>{true, resp.json, "", ts}
                        : ApiResponse<QJsonObject>{false, std::nullopt, resp.error, ts};
}

ApiResponse<QVector<BrokerPosition>> AlpacaBroker::get_positions(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(trading_url(creds) + "/v2/positions", auth_headers(creds));
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_positions failed: %1 | body: %2").arg(resp.error, resp.raw_body));
        return {false, std::nullopt, resp.error, ts};
    }
    QVector<BrokerPosition> positions;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    auto jd = [](const QJsonValue& v) -> double { return v.isDouble() ? v.toDouble() : v.toString().toDouble(); };
    if (doc.isArray()) {
        for (const auto& v : doc.array()) {
            auto p = v.toObject();
            BrokerPosition pos;
            pos.symbol = p.value("symbol").toString();
            pos.quantity = jd(p.value("qty"));
            pos.avg_price = jd(p.value("avg_entry_price"));
            pos.ltp = jd(p.value("current_price"));
            pos.pnl = jd(p.value("unrealized_pl"));
            pos.pnl_pct = jd(p.value("unrealized_plpc")) * 100.0;
            pos.side = p.value("side").toString();
            positions.append(pos);
        }
    }
    LOG_INFO("AlpacaBroker", QString("get_positions: %1 positions").arg(positions.size()));
    return {true, positions, "", ts};
}

ApiResponse<QVector<BrokerHolding>> AlpacaBroker::get_holdings(const BrokerCredentials& creds) {
    // Alpaca: positions are holdings for equities — re-fetch raw to get plpc directly
    auto resp = BrokerHttp::instance().get(trading_url(creds) + "/v2/positions", auth_headers(creds));
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_holdings failed: %1 | body: %2").arg(resp.error, resp.raw_body));
        return {false, std::nullopt, resp.error, ts};
    }
    QVector<BrokerHolding> holdings;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    auto jd2 = [](const QJsonValue& v) -> double { return v.isDouble() ? v.toDouble() : v.toString().toDouble(); };
    if (doc.isArray()) {
        for (const auto& v : doc.array()) {
            auto p = v.toObject();
            BrokerHolding h;
            h.symbol = p.value("symbol").toString();
            h.quantity = jd2(p.value("qty"));
            h.avg_price = jd2(p.value("avg_entry_price"));
            h.ltp = jd2(p.value("current_price"));
            h.invested_value = jd2(p.value("cost_basis"));
            h.current_value = jd2(p.value("market_value"));
            h.pnl = jd2(p.value("unrealized_pl"));
            h.pnl_pct = jd2(p.value("unrealized_plpc")) * 100.0;
            holdings.append(h);
        }
    }
    LOG_INFO("AlpacaBroker", QString("get_holdings: %1 holdings").arg(holdings.size()));
    return {true, holdings, "", ts};
}

ApiResponse<BrokerFunds> AlpacaBroker::get_funds(const BrokerCredentials& creds) {
    auto resp = BrokerHttp::instance().get(trading_url(creds) + "/v2/account", auth_headers(creds));
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_funds failed: %1 | body: %2").arg(resp.error, resp.raw_body));
        return {false, std::nullopt, resp.error, ts};
    }
    // Alpaca returns numeric fields as JSON strings in paper env, numbers in live env — handle both
    auto jval = [](const QJsonValue& v) -> double {
        if (v.isDouble())
            return v.toDouble();
        return v.toString().toDouble();
    };
    auto& j = resp.json;
    BrokerFunds funds;
    funds.total_balance = jval(j.value("equity"));
    funds.available_balance = jval(j.value("buying_power"));
    funds.used_margin = jval(j.value("initial_margin"));
    funds.collateral = jval(j.value("long_market_value"));
    funds.raw_data = j;
    LOG_INFO("AlpacaBroker",
             QString("get_funds: equity=%1 buying_power=%2").arg(funds.total_balance).arg(funds.available_balance));
    return {true, funds, "", ts};
}

// Quotes — GET https://data.alpaca.markets/v2/stocks/snapshots?symbols=AAPL,MSFT&feed=iex
// Snapshots give LTP (latestTrade.p), bid/ask, OHLCV, prev close for change% — all in one call.
ApiResponse<QVector<BrokerQuote>> AlpacaBroker::get_quotes(const BrokerCredentials& creds,
                                                           const QVector<QString>& symbols) {
    if (symbols.isEmpty())
        return {false, std::nullopt, "No symbols", now_ts()};

    QMap<QString, QString> data_headers = {
        {"APCA-API-KEY-ID", creds.api_key}, {"APCA-API-SECRET-KEY", creds.api_secret}, {"Accept", "application/json"}};
    QStringList sym_list;
    for (const auto& s : symbols)
        sym_list.append(s);

    const QString url = data_url() + "/v2/stocks/snapshots?symbols=" + sym_list.join(",") + "&feed=iex";
    auto resp = BrokerHttp::instance().get(url, data_headers);
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_quotes failed: %1 | body: %2").arg(resp.error, resp.raw_body));
        return {false, std::nullopt, resp.error, ts};
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return {false, std::nullopt, "JSON parse error: " + err.errorString(), ts};

    auto snap_obj = doc.object();
    QVector<BrokerQuote> quotes;
    for (const auto& sym : symbols) {
        auto s = snap_obj.value(sym).toObject();
        if (s.isEmpty())
            continue;
        auto daily = s.value("dailyBar").toObject();
        auto prev = s.value("prevDailyBar").toObject();
        auto latest_trade = s.value("latestTrade").toObject();
        auto latest_quote = s.value("latestQuote").toObject();

        BrokerQuote q;
        q.symbol = sym;
        q.ltp = latest_trade.value("p").toDouble();
        q.bid = latest_quote.value("bp").toDouble();
        q.ask = latest_quote.value("ap").toDouble();
        q.bid_size = latest_quote.value("bs").toDouble();
        q.ask_size = latest_quote.value("as").toDouble();
        q.open = daily.value("o").toDouble();
        q.high = daily.value("h").toDouble();
        q.low = daily.value("l").toDouble();
        q.close = daily.value("c").toDouble();
        q.volume = daily.value("v").toDouble();

        // If LTP is missing (pre-market), use mid of bid/ask
        if (q.ltp <= 0 && q.bid > 0 && q.ask > 0)
            q.ltp = (q.bid + q.ask) / 2.0;

        double prev_close = prev.value("c").toDouble();
        if (prev_close > 0 && q.ltp > 0) {
            q.change = q.ltp - prev_close;
            q.change_pct = (q.change / prev_close) * 100.0;
        }
        q.timestamp = ts;
        quotes.append(q);
    }

    return {true, quotes, "", ts};
}

// Historical bars — GET https://data.alpaca.markets/v2/stocks/{symbol}/bars
// timeframe mapping: "1m"→"1Min", "5m"→"5Min", "15m"→"15Min", "1h"→"1Hour", "1D"→"1Day"
ApiResponse<QVector<BrokerCandle>> AlpacaBroker::get_history(const BrokerCredentials& creds, const QString& symbol,
                                                             const QString& resolution, const QString& from_date,
                                                             const QString& to_date) {
    QMap<QString, QString> data_headers = {
        {"APCA-API-KEY-ID", creds.api_key}, {"APCA-API-SECRET-KEY", creds.api_secret}, {"Accept", "application/json"}};

    // Map finterm timeframe strings to Alpaca timeframe format
    static const QMap<QString, QString> tf_map = {
        {"1", "1Min"},   {"1m", "1Min"},     {"1min", "1Min"},   {"5", "5Min"},   {"5m", "5Min"},   {"5min", "5Min"},
        {"15", "15Min"}, {"15m", "15Min"},   {"15min", "15Min"}, {"30", "30Min"}, {"30m", "30Min"}, {"60", "1Hour"},
        {"1h", "1Hour"}, {"1hour", "1Hour"}, {"D", "1Day"},      {"1D", "1Day"},  {"day", "1Day"},  {"1day", "1Day"},
        {"W", "1Week"},  {"1W", "1Week"},    {"week", "1Week"},  {"M", "1Month"}, {"1M", "1Month"}, {"month", "1Month"},
    };
    QString alpaca_tf = tf_map.value(resolution, "1Day");

    // Default date range: last 200 bars worth of time
    QString start = from_date;
    QString end = to_date;
    if (start.isEmpty()) {
        // Go back far enough to get ~200 candles
        int days_back = (alpaca_tf == "1Min" || alpaca_tf == "5Min")     ? 3
                        : (alpaca_tf == "15Min" || alpaca_tf == "30Min") ? 7
                        : (alpaca_tf == "1Hour")                         ? 30
                                                                         : 400; // 1Day and above
        start = QDateTime::currentDateTimeUtc().addDays(-days_back).toString("yyyy-MM-dd");
    }
    if (end.isEmpty())
        end = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd");

    QString url = data_url() + "/v2/stocks/" + symbol.toUpper() + "/bars" + "?timeframe=" + alpaca_tf +
                  "&start=" + start + "&end=" + end + "&limit=500" + "&adjustment=split" + "&feed=iex" + "&sort=asc";

    auto resp = BrokerHttp::instance().get(url, data_headers);
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_history failed: %1 | body: %2").arg(resp.error, resp.raw_body));
        return {false, std::nullopt, resp.error, ts};
    }

    QVector<BrokerCandle> candles;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return {false, std::nullopt, "JSON parse error: " + err.errorString(), ts};

    auto bars_arr = doc.object().value("bars").toArray();
    for (const auto& v : bars_arr) {
        auto b = v.toObject();
        BrokerCandle c;
        // Parse RFC3339 timestamp string to epoch seconds
        c.timestamp = QDateTime::fromString(b.value("t").toString(), Qt::ISODateWithMs).toSecsSinceEpoch();
        c.open = b.value("o").toDouble();
        c.high = b.value("h").toDouble();
        c.low = b.value("l").toDouble();
        c.close = b.value("c").toDouble();
        c.volume = b.value("v").toDouble();
        candles.append(c);
    }
    return {true, candles, "", ts};
}

// /v2/calendar — available on both trading URLs; use trading_url so auth matches the active env
// start/end: "YYYY-MM-DD". Returns trading days with open/close times (ET).
ApiResponse<QVector<MarketCalendarDay>> AlpacaBroker::get_calendar(const BrokerCredentials& creds, const QString& start,
                                                                   const QString& end) {
    QMap<QString, QString> headers = {
        {"APCA-API-KEY-ID", creds.api_key}, {"APCA-API-SECRET-KEY", creds.api_secret}, {"Accept", "application/json"}};

    // /v2/calendar is available on both live and paper trading URLs
    QString url = trading_url(creds) + "/v2/calendar?start=" + start + "&end=" + end;
    auto resp = BrokerHttp::instance().get(url, headers);
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_calendar failed: %1").arg(resp.error));
        return {false, std::nullopt, resp.error, ts};
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return {false, std::nullopt, "JSON parse error: " + err.errorString(), ts};

    QVector<MarketCalendarDay> days;
    for (const auto& v : doc.array()) {
        auto o = v.toObject();
        MarketCalendarDay d;
        d.date = o.value("date").toString();
        d.open = o.value("open").toString();
        d.close = o.value("close").toString();
        d.session_open = o.value("session_open").toString();
        d.session_close = o.value("session_close").toString();
        days.append(d);
    }
    return {true, days, "", ts};
}

// /v2/clock — real-time market open/close status + next open/close times
ApiResponse<MarketClock> AlpacaBroker::get_clock(const BrokerCredentials& creds) {
    QMap<QString, QString> headers = {
        {"APCA-API-KEY-ID", creds.api_key}, {"APCA-API-SECRET-KEY", creds.api_secret}, {"Accept", "application/json"}};
    auto resp = BrokerHttp::instance().get(trading_url(creds) + "/v2/clock", headers);
    int64_t ts = now_ts();
    if (!resp.success) {
        LOG_ERROR("AlpacaBroker", QString("get_clock failed: %1").arg(resp.error));
        return {false, std::nullopt, resp.error, ts};
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return {false, std::nullopt, "JSON parse error: " + err.errorString(), ts};

    auto o = doc.object();
    MarketClock clock;
    clock.timestamp = o.value("timestamp").toString();
    clock.is_open = o.value("is_open").toBool();
    clock.next_open = o.value("next_open").toString();
    clock.next_close = o.value("next_close").toString();
    return {true, clock, "", ts};
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpaca stock data helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build standard data API headers
static QMap<QString, QString> alpaca_data_headers(const BrokerCredentials& creds) {
    return {
        {"APCA-API-KEY-ID", creds.api_key}, {"APCA-API-SECRET-KEY", creds.api_secret}, {"Accept", "application/json"}};
}

// Parse a single trade JSON object → BrokerTrade
static BrokerTrade parse_trade(const QJsonObject& t, const QString& symbol) {
    BrokerTrade tr;
    tr.symbol = symbol;
    tr.price = t.value("p").toDouble();
    tr.size = t.value("s").toDouble();
    tr.exchange = t.value("x").toString();
    tr.timestamp = t.value("t").toString();
    tr.tape = t.value("z").toString();
    for (const auto& c : t.value("c").toArray())
        tr.conditions.append(c.toString());
    return tr;
}

// Parse a single quote JSON object → BrokerQuote (bid/ask only — no OHLCV)
static BrokerQuote parse_quote_ba(const QJsonObject& q, const QString& symbol) {
    BrokerQuote bq;
    bq.symbol = symbol;
    bq.bid = q.value("bp").toDouble();
    bq.ask = q.value("ap").toDouble();
    bq.bid_size = q.value("bs").toDouble();
    bq.ask_size = q.value("as").toDouble();
    bq.timestamp = QDateTime::fromString(q.value("t").toString(), Qt::ISODateWithMs).toSecsSinceEpoch();
    if (bq.bid > 0 && bq.ask > 0)
        bq.ltp = (bq.bid + bq.ask) / 2.0;
    return bq;
}

// ─── Single-symbol: Historical trades ────────────────────────────────────────
ApiResponse<QVector<BrokerTrade>> AlpacaBroker::get_historical_trades_single(const BrokerCredentials& creds,
                                                                             const QString& symbol,
                                                                             const QString& start, const QString& end,
                                                                             int limit) {
    QString url = data_url() + "/v2/stocks/" + symbol.toUpper() + "/trades?feed=iex&limit=" + QString::number(limit) +
                  "&sort=asc";
    if (!start.isEmpty())
        url += "&start=" + start;
    if (!end.isEmpty())
        url += "&end=" + end;

    auto resp = BrokerHttp::instance().get(url, alpaca_data_headers(creds));
    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return {false, std::nullopt, err.errorString(), ts};

    QVector<BrokerTrade> result;
    for (const auto& v : doc.object().value("trades").toArray())
        result.append(parse_trade(v.toObject(), symbol));
    return {true, result, "", ts};
}

// ─── Single-symbol: Historical quotes ────────────────────────────────────────
ApiResponse<QVector<BrokerQuote>> AlpacaBroker::get_historical_quotes_single(const BrokerCredentials& creds,
                                                                             const QString& symbol,
                                                                             const QString& start, const QString& end,
                                                                             int limit) {
    QString url = data_url() + "/v2/stocks/" + symbol.toUpper() + "/quotes?feed=iex&limit=" + QString::number(limit) +
                  "&sort=asc";
    if (!start.isEmpty())
        url += "&start=" + start;
    if (!end.isEmpty())
        url += "&end=" + end;

    auto resp = BrokerHttp::instance().get(url, alpaca_data_headers(creds));
    int64_t ts = now_ts();
    if (!resp.success)
        return {false, std::nullopt, resp.error, ts};

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(resp.raw_body.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return {false, std::nullopt, err.errorString(), ts};

    QVector<BrokerQuote> result;
    for (const auto& v : doc.object().value("quotes").toArray())
        result.append(parse_quote_ba(v.toObject(), symbol));
    return {true, result, "", ts};
}

} // namespace finterm::trading
