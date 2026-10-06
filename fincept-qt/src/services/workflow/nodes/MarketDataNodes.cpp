#include "services/workflow/nodes/MarketDataNodes.h"

#include "python/PythonRunner.h"
#include "screens/economics/panels/EconomicsPresets.h"
#include "services/workflow/NodeRegistry.h"

#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <optional>

namespace fincept::workflow {

using fincept::python::extract_json;
using fincept::python::PythonResult;
using fincept::python::PythonRunner;

namespace {

// Extract an error message from a script's JSON object, or nullopt when the
// "error" field is absent / null / false / empty.
std::optional<QString> script_error_message(const QJsonObject& obj) {
    const QJsonValue e = obj.value("error");
    if (e.isUndefined() || e.isNull())
        return std::nullopt;
    if (e.isBool()) {
        if (!e.toBool())
            return std::nullopt;
        const QString msg = obj.value("message").toString();
        return msg.isEmpty() ? QString("Python script reported an error") : msg;
    }
    if (e.isString())
        return e.toString().isEmpty() ? std::nullopt : std::optional<QString>(e.toString());
    if (e.isObject()) {
        const QJsonObject eo = e.toObject();
        QString msg = eo.value("error").toString();
        if (msg.isEmpty())
            msg = eo.value("message").toString();
        if (msg.isEmpty())
            msg = QString::fromUtf8(QJsonDocument(eo).toJson(QJsonDocument::Compact));
        return msg;
    }
    return QString("Python script reported an error");
}

// JSON number when > 0, else null — a 0 bid/ask/price means "not reported".
QJsonValue positive_or_null(const QJsonValue& v) {
    return (v.isDouble() && v.toDouble() > 0.0) ? v : QJsonValue(QJsonValue::Null);
}

// Helper: run a Python script and parse the JSON result, calling cb with the outcome.
void run_python_json(const QString& script, const QStringList& args,
                     std::function<void(bool, QJsonValue, QString)> cb) {
    PythonRunner::instance().run(script, args, [cb](const PythonResult& res) {
        if (!res.success) {
            cb(false, {}, res.error);
            return;
        }
        QString json_str = extract_json(res.output).trimmed();
        auto doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isNull()) {
            cb(false, {}, "Invalid JSON: " + res.output.left(200));
            return;
        }
        // Check for Python-level error. Scripts use several shapes:
        //   {"success": false, "error": "..."}, {"error": "..."},
        //   {"error": true, "message": "..."} (cboe), {"error": {"error": "..."}} (sec).
        if (doc.isObject()) {
            auto obj = doc.object();
            if (obj.contains("success") && !obj.value("success").toBool(true)) {
                cb(false, {}, script_error_message(obj).value_or("Python script returned failure"));
                return;
            }
            if (!obj.value("success").toBool(false)) {
                if (auto err = script_error_message(obj)) {
                    cb(false, {}, *err);
                    return;
                }
            }
        }
        cb(true, doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), {});
    });
}

} // anonymous namespace

void register_market_data_nodes(NodeRegistry& registry) {
    registry.register_type({
        .type_id = "market.get_quote",
        .display_name = "Get Quote",
        .category = "Market Data",
        .description = "Fetch real-time market quote for a symbol",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::MarketData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
                {"source", "Source", "select", "yahoo", {"yahoo", "alpha_vantage", "polygon", "databento"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString("AAPL");
                run_python_json("yfinance_data.py", {"quote", symbol}, cb);
            },
    });

    registry.register_type({
        .type_id = "market.get_historical",
        .display_name = "Historical Data",
        .category = "Market Data",
        .description = "Fetch OHLCV historical price data",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::PriceData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
                {"period", "Period", "select", "1y", {"1d", "5d", "1mo", "3mo", "6mo", "1y", "2y", "5y", "max"}, ""},
                {"interval", "Interval", "select", "1d", {"1m", "5m", "15m", "1h", "1d", "1wk", "1mo"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString("AAPL");
                QString period = params.value("period").toString("1y");
                QString interval = params.value("interval").toString("1d");
                run_python_json("yfinance_data.py", {"historical_period", symbol, period, interval}, cb);
            },
    });

    registry.register_type({
        .type_id = "market.get_depth",
        .display_name = "Market Depth",
        .category = "Market Data",
        .description = "Top-of-book bid/ask (Level 1). Level 2 depth is not available from this source.",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::MarketData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString("AAPL");
                // yfinance has no L2 book — report the real top-of-book only.
                // A missing bid/ask is null, never substituted with last price.
                run_python_json("yfinance_data.py", {"quote", symbol},
                                [cb, symbol](bool ok, QJsonValue val, QString err) {
                                    if (!ok) {
                                        cb(false, {}, err);
                                        return;
                                    }
                                    QJsonObject quote = val.toObject();
                                    QJsonObject out;
                                    out["symbol"] = symbol;
                                    out["level"] = "top_of_book";
                                    out["bid"] = positive_or_null(quote.value("bid"));
                                    out["ask"] = positive_or_null(quote.value("ask"));
                                    out["bid_size"] = positive_or_null(quote.value("bid_size"));
                                    out["ask_size"] = positive_or_null(quote.value("ask_size"));
                                    out["last"] = positive_or_null(quote.value("price"));
                                    out["source"] = "yfinance";
                                    cb(true, out, {});
                                });
            },
    });

    registry.register_type({
        .type_id = "market.get_stats",
        .display_name = "Ticker Stats",
        .category = "Market Data",
        .description = "Fetch ticker statistics (52w high/low, volume, etc.)",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::MarketData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString("AAPL");
                // info command returns 52w high/low, volume, market cap, etc.
                run_python_json("yfinance_data.py", {"info", symbol}, cb);
            },
    });

    registry.register_type({
        .type_id = "market.get_fundamentals",
        .display_name = "Fundamentals",
        .category = "Market Data",
        .description = "Fetch company fundamentals (P/E, EPS, revenue, etc.)",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::FundamentalData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
                {"type", "Type", "select", "overview", {"overview", "income", "balance", "cashflow", "earnings"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString("AAPL");
                QString type = params.value("type").toString("overview");
                if (type == "overview") {
                    run_python_json("yfinance_data.py", {"info", symbol}, cb);
                } else {
                    // income, balance, cashflow, earnings → financials command
                    run_python_json("yfinance_data.py", {"financials", symbol},
                                    [cb, type](bool ok, QJsonValue val, QString err) {
                                        if (!ok) {
                                            cb(false, {}, err);
                                            return;
                                        }
                                        // yfinance_data.py financials section keys.
                                        const QString key = type == "income"     ? QString("income_statement")
                                                            : type == "balance"  ? QString("balance_sheet")
                                                            : type == "cashflow" ? QString("cash_flow")
                                                                                 : type;
                                        if (val.isObject() && val.toObject().contains(key)) {
                                            cb(true, val.toObject().value(key), {});
                                        } else {
                                            // Don't hand back the whole payload labelled as `type`.
                                            cb(false, {},
                                               QString("Fundamentals: '%1' is not available from the financials source")
                                                   .arg(type));
                                        }
                                    });
                }
            },
    });

    registry.register_type({
        .type_id = "market.get_economics",
        .display_name = "FRED Economic Data",
        .category = "Market Data",
        .description = "Fetch a FRED macroeconomic series (GDP, CPI, Treasury yields, Fed Funds, etc.)",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::EconomicData}},
        .parameters =
            {
                {"preset",
                 "Preset",
                 "select",
                 "GDPC1",
                 fincept::screens::fred_preset_series_ids(),
                 "Pick a common series, or use Series ID below for any FRED code"},
                {"series_id", "Series ID (override)", "string", "", {},
                 "Optional FRED series id (e.g. GS10). Overrides Preset when set."},
                {"start_date", "Start Date", "string", "", {}, "YYYY-MM-DD (optional)"},
                {"end_date", "End Date", "string", "", {}, "YYYY-MM-DD (optional)"},
                {"frequency", "Frequency", "select", "",
                 {"", "d", "w", "m", "q", "a"},
                 "Output frequency: d=daily, w=weekly, m=monthly, q=quarterly, a=annual"},
                {"transform", "Transform", "select", "",
                 {"", "chg", "pch", "log"},
                 "chg=change, pch=percent change, log=natural log"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString series_id = params.value("series_id").toString().trimmed();
                if (series_id.isEmpty())
                    series_id = params.value("preset").toString("GDPC1");

                QStringList args = {"series", series_id};
                const QString start = params.value("start_date").toString().trimmed();
                const QString end = params.value("end_date").toString().trimmed();
                const QString freq = params.value("frequency").toString().trimmed();
                const QString transform = params.value("transform").toString().trimmed();

                // fred_data.py series <id> [start] [end] [frequency] [transform] — positional.
                // Only emit trailing args if needed; pad earlier ones with empty strings.
                if (!start.isEmpty() || !end.isEmpty() || !freq.isEmpty() || !transform.isEmpty())
                    args.append(start);
                if (!end.isEmpty() || !freq.isEmpty() || !transform.isEmpty())
                    args.append(end);
                if (!freq.isEmpty() || !transform.isEmpty())
                    args.append(freq);
                if (!transform.isEmpty())
                    args.append(transform);

                run_python_json("fred_data.py", args, cb);
            },
    });

    registry.register_type({
        .type_id = "market.get_yield_curve",
        .display_name = "US Treasury Yield Curve",
        .category = "Market Data",
        .description = "Fetch the full US Treasury yield curve (1m–30y) plus 10y-2y / 10y-3m spreads from FRED",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::EconomicData}},
        .parameters =
            {
                {"start_date", "Start Date", "string", "", {}, "YYYY-MM-DD (optional)"},
                {"end_date", "End Date", "string", "", {}, "YYYY-MM-DD (optional)"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QStringList args = {"yield_curve"};
                const QString start = params.value("start_date").toString().trimmed();
                const QString end = params.value("end_date").toString().trimmed();
                if (!start.isEmpty() || !end.isEmpty())
                    args.append(start);
                if (!end.isEmpty())
                    args.append(end);
                run_python_json("fred_economic_data.py", args, cb);
            },
    });

    registry.register_type({
        .type_id = "market.get_news",
        .display_name = "Market News",
        .category = "Market Data",
        .description = "Fetch latest financial news",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::NewsData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "", {}, "Optional ticker filter"},
                {"limit", "Limit", "number", 10, {}, "Number of articles"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString();
                QString limit = QString::number(static_cast<int>(params.value("limit").toDouble(10)));
                run_python_json("yfinance_data.py", {"news", symbol, limit}, cb);
            },
    });

    // ── Tier 1 additions ───────────────────────────────────────────

    registry.register_type({
        .type_id = "market.get_options_chain",
        .display_name = "Options Chain",
        .category = "Market Data",
        .description = "Fetch options chain with greeks",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::OptionsData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "", true},
                {"expiry", "Expiry", "string", "", {}, "YYYY-MM-DD, 'nearest', or empty for all"},
                {"type", "Type", "select", "both", {"calls", "puts", "both"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                const QString symbol = params.value("symbol").toString("AAPL").trimmed().toUpper();
                const QString expiry = params.value("expiry").toString().trimmed();
                const QString type = params.value("type").toString("both");
                // Real chain (with greeks) from CBOE's delayed quotes feed.
                run_python_json(
                    "cboe_data.py", {"options_chains", symbol},
                    [cb, symbol, expiry, type](bool ok, QJsonValue val, QString err) {
                        if (!ok) {
                            cb(false, {}, err);
                            return;
                        }
                        const QJsonObject data = val.toObject().value("data").toObject();
                        const QJsonArray all = data.value("options").toArray();
                        if (all.isEmpty()) {
                            cb(false, {}, QString("No options chain available for %1").arg(symbol));
                            return;
                        }
                        // CBOE expirations are yymmdd.
                        QString want_exp;
                        if (expiry.compare("nearest", Qt::CaseInsensitive) == 0) {
                            for (const auto& v : all) {
                                const QString e = v.toObject().value("expiration").toString();
                                const QDate d = QDate::fromString("20" + e, "yyyyMMdd");
                                if (d.isValid() && d >= QDate::currentDate() && (want_exp.isEmpty() || e < want_exp))
                                    want_exp = e;
                            }
                            if (want_exp.isEmpty()) {
                                cb(false, {}, QString("No unexpired options found for %1").arg(symbol));
                                return;
                            }
                        } else if (!expiry.isEmpty()) {
                            const QDate d = QDate::fromString(expiry, "yyyy-MM-dd");
                            if (!d.isValid()) {
                                cb(false, {}, "Options Chain: expiry must be YYYY-MM-DD, 'nearest', or empty");
                                return;
                            }
                            want_exp = d.toString("yyMMdd");
                        }
                        QJsonArray filtered;
                        for (const auto& v : all) {
                            const QJsonObject o = v.toObject();
                            if (!want_exp.isEmpty() && o.value("expiration").toString() != want_exp)
                                continue;
                            const QString ot = o.value("option_type").toString();
                            if ((type == "calls" && ot != "call") || (type == "puts" && ot != "put"))
                                continue;
                            filtered.append(o);
                        }
                        if (filtered.isEmpty()) {
                            cb(false, {},
                               QString("No %1 options for %2%3")
                                   .arg(type, symbol, expiry.isEmpty() ? QString() : " expiring " + expiry));
                            return;
                        }
                        QJsonObject out;
                        out["symbol"] = symbol;
                        out["source"] = "cboe";
                        out["metadata"] = data.value("metadata");
                        out["expiry_filter"] = expiry.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(expiry);
                        out["type_filter"] = type;
                        out["count"] = filtered.size();
                        out["options"] = filtered;
                        cb(true, out, {});
                    });
            },
    });

    registry.register_type({
        .type_id = "market.get_crypto_price",
        .display_name = "Crypto Price",
        .category = "Market Data",
        .description = "Real-time cryptocurrency prices",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::PriceData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "BTC", {}, "BTC, ETH, SOL...", true},
                {"quote", "Quote Currency", "select", "USD", {"USD", "USDT", "EUR", "BTC"}, ""},
                {"exchange", "Exchange", "select", "binance", {"binance", "kraken", "coinbase", "hyperliquid"}, ""},
            },
        .execute = nullptr,
    });

    registry.register_type({
        .type_id = "market.get_forex_rate",
        .display_name = "Forex Rate",
        .category = "Market Data",
        .description = "Foreign exchange rates",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::PriceData}},
        .parameters =
            {
                {"base", "Base Currency", "string", "USD", {}, "", true},
                {"quote", "Quote Currency", "string", "EUR", {}, "", true},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString base = params.value("base").toString("USD");
                QString quote = params.value("quote").toString("EUR");
                // yfinance supports forex via "USDEUR=X" format
                QString symbol = base + quote + "=X";
                run_python_json("yfinance_data.py", {"quote", symbol},
                                [cb, base, quote](bool ok, QJsonValue val, QString err) {
                                    if (!ok) {
                                        cb(false, {}, err);
                                        return;
                                    }
                                    QJsonObject out = val.toObject();
                                    out["base"] = base;
                                    out["quote"] = quote;
                                    out["rate"] = out.value("price");
                                    cb(true, out, {});
                                });
            },
    });

    // ── Tier 3: Advanced Market Data ───────────────────────────────

    registry.register_type({
        .type_id = "market.screener",
        .display_name = "Stock Screener",
        .category = "Market Data",
        .description = "Screen stocks by financial criteria",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"market_cap_min", "Min Market Cap ($M)", "number", 1000, {}, ""},
                {"pe_max", "Max P/E Ratio", "number", 30, {}, ""},
                {"volume_min", "Min Avg Volume", "number", 500000, {}, ""},
                {"sector",
                 "Sector",
                 "select",
                 "any",
                 {"any", "technology", "healthcare", "finance", "energy", "consumer", "industrial", "utilities",
                  "materials", "real_estate"},
                 ""},
                {"country", "Country", "select", "US", {"US", "UK", "IN", "JP", "DE", "FR", "CA", "AU"}, ""},
                {"limit", "Max Results", "number", 50, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // No criteria-based screener data source is wired. The previous
                // implementation ran a text search and ignored every criterion,
                // returning results that did not satisfy the screen.
                Q_UNUSED(params);
                cb(false, {},
                   "Stock Screener is not implemented — no screener data source is wired to apply the "
                   "market cap / P/E / volume / sector / country criteria");
            },
    });

    registry.register_type({
        .type_id = "market.insider_trades",
        .display_name = "Insider Trades",
        .category = "Market Data",
        .description = "Fetch insider trading activity (Form 4 filings)",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "", {}, "Ticker symbol", true},
                {"transaction_type", "Type", "select", "all", {"all", "buy", "sell", "exercise"}, ""},
                {"days_back", "Days Back", "number", 30, {}, ""},
                {"min_value", "Min Transaction Value ($)", "number", 100000, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                const QString symbol = params.value("symbol").toString().trimmed().toUpper();
                if (symbol.isEmpty()) {
                    cb(false, {}, "Symbol is required for insider trades");
                    return;
                }
                const QString tx_type = params.value("transaction_type").toString("all");
                const int days_back = std::max(1, static_cast<int>(params.value("days_back").toDouble(30)));
                const double min_value = params.value("min_value").toDouble(0);
                const QString since = QDate::currentDate().addDays(-days_back).toString("yyyy-MM-dd");
                // Real Form 4 transactions parsed from EDGAR (sec_data.py → sec_ownership_data).
                run_python_json(
                    "sec_data.py", {"insider_trading", symbol, "", since, "", "100"},
                    [cb, symbol, tx_type, since, min_value](bool ok, QJsonValue val, QString err) {
                        if (!ok) {
                            cb(false, {}, err);
                            return;
                        }
                        const QJsonObject res = val.toObject();
                        if (!res.value("data").isArray()) {
                            cb(false, {}, QString("No insider transaction data returned for %1").arg(symbol));
                            return;
                        }
                        // Form 4 codes: P = open-market buy, S = open-market sale,
                        // M/X = option exercise / conversion.
                        QStringList codes;
                        if (tx_type == "buy")
                            codes = {"P"};
                        else if (tx_type == "sell")
                            codes = {"S"};
                        else if (tx_type == "exercise")
                            codes = {"M", "X"};
                        QJsonArray rows;
                        int unvalued = 0;
                        for (const auto& v : res.value("data").toArray()) {
                            const QJsonObject t = v.toObject();
                            const QString date = t.value("date").toString();
                            if (date.isEmpty() || date < since)
                                continue;
                            if (!codes.isEmpty() && !codes.contains(t.value("code").toString()))
                                continue;
                            if (min_value > 0) {
                                // Rows without a reported value can't be shown to meet the minimum.
                                if (!t.value("value").isDouble()) {
                                    ++unvalued;
                                    continue;
                                }
                                if (std::abs(t.value("value").toDouble()) < min_value)
                                    continue;
                            }
                            rows.append(t);
                        }
                        QJsonObject out;
                        out["symbol"] = symbol;
                        out["source"] = "sec_edgar_form4";
                        out["since"] = since;
                        out["transaction_type"] = tx_type;
                        out["count"] = rows.size();
                        out["transactions"] = rows;
                        if (unvalued > 0)
                            out["excluded_without_value"] = unvalued;
                        if (res.contains("coverage"))
                            out["coverage"] = res.value("coverage");
                        cb(true, out, {});
                    });
            },
    });

    registry.register_type({
        .type_id = "market.sec_filings",
        .display_name = "SEC Filings",
        .category = "Market Data",
        .description = "Fetch SEC filings (10-K, 10-Q, 8-K, etc.)",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "", {}, "Company ticker", true},
                {"filing_type",
                 "Filing Type",
                 "select",
                 "10-K",
                 {"10-K", "10-Q", "8-K", "13F", "S-1", "DEF 14A", "all"},
                 ""},
                {"limit", "Limit", "number", 10, {}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                const QString symbol = params.value("symbol").toString().trimmed().toUpper();
                if (symbol.isEmpty()) {
                    cb(false, {}, "Symbol is required for SEC filings");
                    return;
                }
                QString form = params.value("filing_type").toString("10-K");
                if (form == "all")
                    form.clear();
                else if (form == "13F")
                    form = "13F-HR";
                const QString limit = QString::number(std::max(1, static_cast<int>(params.value("limit").toDouble(10))));
                // Real filing index from EDGAR submissions API.
                run_python_json("sec_data.py", {"company_filings", symbol, "", form, "", "", limit},
                                [cb, symbol, form](bool ok, QJsonValue val, QString err) {
                                    if (!ok) {
                                        cb(false, {}, err);
                                        return;
                                    }
                                    const QJsonValue filings = val.toObject().value("data");
                                    if (!filings.isArray()) {
                                        cb(false, {}, QString("No SEC filings returned for %1").arg(symbol));
                                        return;
                                    }
                                    QJsonObject out;
                                    out["symbol"] = symbol;
                                    out["source"] = "sec_edgar";
                                    out["filing_type"] = form.isEmpty() ? QString("all") : form;
                                    out["count"] = filings.toArray().size();
                                    out["filings"] = filings;
                                    cb(true, out, {});
                                });
            },
    });
}

} // namespace fincept::workflow
