#include "services/markets/MarketDataService.h"
#include "services/workflow/nodes/MarketDataNodes.h"

#include "screens/economics/panels/EconomicsPresets.h"
#include "services/economics/EconomicsService.h"
#include "services/equity/EquityResearchService.h"
#include "services/news/NewsService.h"
#include "services/ownership/OwnershipService.h"
#include "services/workflow/NodeRegistry.h"

#include <memory>
#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <optional>

namespace finterm::workflow {

namespace {

// An EconomicsService answer as a node result.
void emit_economics(const services::EconomicsResult& r, const std::function<void(bool, QJsonValue, QString)>& cb) {
    if (r.success) {
        QJsonObject out = r.data;
        out.insert("as_of", r.as_of.toUTC().toString(Qt::ISODate)); // when FRED was asked
        cb(true, out, {});
    } else {
        cb(false, {}, r.error);
    }
}

// A JSON number for a value that was filed, null for one that was not.
QJsonValue opt_json(const std::optional<double>& v) {
    return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
}

// One Form 4 row, keyed as sec_ownership_data.py reports it.
QJsonObject insider_row_json(const ownership::InsiderTransaction& t) {
    return QJsonObject{
        {"insider", t.insider},
        {"roles", QJsonArray::fromStringList(t.roles)},
        {"date", t.date.toString(Qt::ISODate)},
        {"filed_date", t.filed_date.isValid() ? QJsonValue(t.filed_date.toString(Qt::ISODate)) : QJsonValue()},
        {"code", t.code},
        {"code_label", t.code_label},
        {"security", t.security},
        {"derivative", t.derivative},
        {"open_market", t.open_market},
        {"direction", t.acquired ? QStringLiteral("acquired") : QStringLiteral("disposed")},
        {"ten_percent_owner", t.ten_percent_owner},
        // Absent on pre-2023 filings: unknown, which is not false.
        {"plan_10b5_1", t.plan_10b5_1 ? QJsonValue(*t.plan_10b5_1) : QJsonValue()},
        {"shares", opt_json(t.shares)},
        {"price", opt_json(t.price)},
        {"value", opt_json(t.value)},
        {"shares_held_after", opt_json(t.shares_held_after)},
        {"source_url", t.source_url},
    };
}

// The info dict through the app's one company-info read (shared cache and
// retry with Equity Research, the portfolio and the market screens).
void emit_info(const QString& symbol, const std::function<void(bool, QJsonValue, QString)>& cb) {
    services::MarketDataService::instance().fetch_info_raw(
        symbol, [cb](const services::MarketDataService::InfoReply& r) {
            if (!r.ok)
                cb(false, {}, r.error);
            else
                cb(true, r.info, {});
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
        .version = 2, // v2: one quote shape with MCP (MarketDataService::quote_json)
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
                const QString symbol = params.value("symbol").toString("AAPL");
                // The app's one market quote (same source as every screen).
                services::MarketDataService::instance().fetch_quotes(
                    {symbol},
                    [cb, symbol](bool ok, QVector<services::QuoteData> quotes, const QString& err) {
                        if (!ok || quotes.isEmpty()) {
                            cb(false, {}, err.isEmpty() ? QStringLiteral("no quote for %1").arg(symbol) : err);
                            return;
                        }
                        cb(true, services::MarketDataService::quote_json(quotes.first()), {});
                    },
                    /*stale_on_failure=*/false);
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
                const QString symbol = params.value("symbol").toString("AAPL");
                const QString period = params.value("period").toString("1y");
                const QString interval = params.value("interval").toString("1d");
                // The app's one price-history read (priced rows only).
                services::MarketDataService::instance().fetch_bars(
                    symbol, period, interval, [cb](const services::MarketDataService::BarsReply& r) {
                        if (!r.ok)
                            cb(false, {}, r.error);
                        else
                            cb(true, r.rows, {});
                    });
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
                // Both reads in parallel; the node answers when both have.
                // No book (refused, or none outside hours) is null bid/ask,
                // not a failure, as long as there is a last price.
                struct Depth {
                    int pending = 2;
                    services::MarketDataService::OrderbookData book;
                    bool book_ok = false;
                    QVector<services::QuoteData> quotes;
                    QString error;
                };
                auto st = std::make_shared<Depth>();
                auto finish = [cb, symbol, st] {
                    if (--st->pending > 0)
                        return;
                    auto pos = [](double v) { return v > 0 ? QJsonValue(v) : QJsonValue(); };
                    const QJsonValue last = st->quotes.isEmpty() ? QJsonValue() : pos(st->quotes.first().price);
                    if (!st->book_ok && last.isNull()) {
                        cb(false, {}, st->error.isEmpty() ? QStringLiteral("no market data for %1").arg(symbol)
                                                          : st->error);
                        return;
                    }
                    QJsonObject out;
                    out["symbol"] = symbol;
                    out["level"] = "top_of_book";
                    out["bid"] = st->book_ok ? pos(st->book.bid) : QJsonValue();
                    out["ask"] = st->book_ok ? pos(st->book.ask) : QJsonValue();
                    out["bid_size"] = st->book_ok ? pos(st->book.bid_size) : QJsonValue();
                    out["ask_size"] = st->book_ok ? pos(st->book.ask_size) : QJsonValue();
                    out["last"] = last;
                    out["source"] = "yfinance";
                    cb(true, out, {});
                };
                auto& mds = services::MarketDataService::instance();
                mds.fetch_orderbook(symbol, [st, finish](bool ok, services::MarketDataService::OrderbookData ob) {
                    st->book_ok = ok;
                    st->book = ob;
                    finish();
                });
                mds.fetch_quotes(
                    {symbol},
                    [st, finish](bool, QVector<services::QuoteData> quotes, const QString& err) {
                        st->quotes = quotes;
                        st->error = err;
                        finish();
                    },
                    /*stale_on_failure=*/false);
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
                const QString symbol = params.value("symbol").toString("AAPL");
                // The app's one company-info read: 52w high/low, volume,
                // market cap, etc.
                emit_info(symbol, cb);
            },
    });

    registry.register_type({
        .type_id = "market.get_fundamentals",
        .display_name = "Fundamentals",
        .category = "Market Data",
        .description = "Company profile and ratios (overview), or an annual financial statement",
        .icon_text = "$",
        .accent_color = "#2563eb",
        // v2: statements are Equity Research's (one cache with the Financials
        // tab) as {symbol, basis, statement, periods: [{period, items}]}; the
        // "earnings" type, which no source ever answered, is gone.
        .version = 2,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::FundamentalData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
                {"type", "Type", "select", "overview", {"overview", "income", "balance", "cashflow"}, ""},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString symbol = params.value("symbol").toString("AAPL");
                QString type = params.value("type").toString("overview");
                if (type == "overview") {
                    emit_info(symbol, cb);
                    return;
                }
                if (type != "income" && type != "balance" && type != "cashflow") {
                    cb(false, {}, QString("Fundamentals: unknown type '%1'").arg(type));
                    return;
                }
                // The Financials tab's read: same cache, same in-flight request.
                services::equity::EquityResearchService::instance().read_financials(
                    symbol, [cb, type, symbol](bool ok, const services::equity::FinancialsData& d, const QString& err,
                                               const QDateTime& as_of) {
                        if (!ok) {
                            cb(false, {}, err);
                            return;
                        }
                        const auto& stmt = type == "income"    ? d.income_statement
                                           : type == "balance" ? d.balance_sheet
                                                               : d.cash_flow;
                        if (stmt.isEmpty()) {
                            cb(false, {}, QString("Fundamentals: %1 has no %2 statement on file").arg(symbol, type));
                            return;
                        }
                        cb(true,
                           QJsonObject{{"symbol", d.symbol},
                                       {"basis", "annual"},
                                       {"statement", type},
                                       {"as_of", as_of.toUTC().toString(Qt::ISODate)},
                                       {"periods", services::equity::EquityResearchService::statement_json(stmt)}},
                           {});
                    });
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
                {"preset", "Preset", "select", "GDPC1", finterm::screens::fred_preset_series_ids(),
                 "Pick a common series, or use Series ID below for any FRED code"},
                {"series_id",
                 "Series ID (override)",
                 "string",
                 "",
                 {},
                 "Optional FRED series id (e.g. GS10). Overrides Preset when set."},
                {"start_date", "Start Date", "string", "", {}, "YYYY-MM-DD (optional)"},
                {"end_date", "End Date", "string", "", {}, "YYYY-MM-DD (optional)"},
                {"frequency",
                 "Frequency",
                 "select",
                 "",
                 {"", "d", "w", "m", "q", "a"},
                 "Output frequency: d=daily, w=weekly, m=monthly, q=quarterly, a=annual"},
                {"transform",
                 "Transform",
                 "select",
                 "",
                 {"", "chg", "pch", "log"},
                 "chg=change, pch=percent change, log=natural log"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                QString series_id = params.value("series_id").toString().trimmed();
                if (series_id.isEmpty())
                    series_id = params.value("preset").toString("GDPC1");

                QStringList args = {series_id};
                const QString start = params.value("start_date").toString().trimmed();
                const QString end = params.value("end_date").toString().trimmed();
                const QString freq = params.value("frequency").toString().trimmed();
                const QString transform = params.value("transform").toString().trimmed();

                // fred_data.py series <id> [start] [end] [frequency] [transform] — positional.
                // Through EconomicsService: the FRED panel's cache and run.
                // Only emit trailing args if needed; pad earlier ones with empty strings.
                if (!start.isEmpty() || !end.isEmpty() || !freq.isEmpty() || !transform.isEmpty())
                    args.append(start);
                if (!end.isEmpty() || !freq.isEmpty() || !transform.isEmpty())
                    args.append(end);
                if (!freq.isEmpty() || !transform.isEmpty())
                    args.append(freq);
                if (!transform.isEmpty())
                    args.append(transform);

                services::EconomicsService::instance().fetch(
                    "fred_data.py", "series", args,
                    [cb](const services::EconomicsResult& r) { emit_economics(r, cb); });
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
                QStringList args;
                const QString start = params.value("start_date").toString().trimmed();
                const QString end = params.value("end_date").toString().trimmed();
                if (!start.isEmpty() || !end.isEmpty())
                    args.append(start);
                if (!end.isEmpty())
                    args.append(end);
                // The FRED Analytics panel's yield curve: same cache and run.
                services::EconomicsService::instance().fetch(
                    "fred_economic_data.py", "yield_curve", args,
                    [cb](const services::EconomicsResult& r) { emit_economics(r, cb); });
            },
    });

    registry.register_type({
        .type_id = "market.get_news",
        .display_name = "Market News",
        .category = "Market Data",
        .description = "Latest news: a ticker's (Equity Research) or, with no symbol, the News feed",
        .icon_text = "$",
        .accent_color = "#2563eb",
        .version = 1,
        .inputs = {{"input_0", "Data In", PortDirection::Input, ConnectionType::Main}},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::NewsData}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "", {}, "Ticker; empty = the News screen's feed"},
                {"limit", "Limit", "number", 10, {}, "Number of articles"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                const QString symbol = params.value("symbol").toString().trimmed().toUpper();
                const int limit = std::max(1, static_cast<int>(params.value("limit").toDouble(10)));
                if (!symbol.isEmpty()) {
                    // The Equity Research News tab's read (one entry per symbol).
                    services::equity::EquityResearchService::instance().read_news(
                        symbol, std::max(limit, 20),
                        [cb, symbol, limit](bool ok, const QVector<services::equity::NewsArticle>& all,
                                            const QString& err, const QDateTime& as_of) {
                            if (!ok) {
                                cb(false, {}, err);
                                return;
                            }
                            const QJsonArray articles = services::equity::EquityResearchService::news_json(
                                all.mid(0, limit));
                            cb(true,
                               QJsonObject{{"symbol", symbol},
                                           {"source", "yahoo"},
                                           {"as_of", as_of.toUTC().toString(Qt::ISODate)},
                                           {"count", articles.size()},
                                           {"articles", articles}},
                               {});
                        });
                    return;
                }
                // The News screen's merged feed (newest first).
                services::NewsService::instance().fetch_all_news(
                    false, [cb, limit](bool ok, const QVector<services::NewsArticle>& all) {
                        // Dozens of feeds with not one article between them is
                        // every feed failing or refusing, not a quiet news day.
                        if (!ok || all.isEmpty()) {
                            cb(false, {}, "News feed: no feed returned an article (failed or refused)");
                            return;
                        }
                        QJsonArray articles;
                        for (const auto& a : all) {
                            if (articles.size() >= limit)
                                break;
                            articles.append(QJsonObject{
                                {"title", a.headline},
                                {"description", a.summary},
                                {"url", a.link},
                                {"publisher", a.source},
                                // EVENT-STAMP: a publication instant, in UTC
                                {"published_date",
                                 a.sort_ts > 0
                                     ? QJsonValue(QDateTime::fromSecsSinceEpoch(a.sort_ts, QTimeZone::UTC)
                                                      .toString(Qt::ISODate))
                                     : QJsonValue()},
                                {"tickers", QJsonArray::fromStringList(a.tickers)},
                            });
                        }
                        cb(true,
                           QJsonObject{{"source", "news_feed"}, {"count", articles.size()}, {"articles", articles}},
                           {});
                    });
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
                // The app's full-chain read: CBOE's delayed chain with greeks.
                services::MarketDataService::instance().fetch_options_chain(
                    symbol, [cb, symbol, expiry, type](const services::MarketDataService::OptionsChainReply& r) {
                        if (!r.ok) {
                            cb(false, {}, r.error);
                            return;
                        }
                        const QJsonObject& data = r.chain;
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
                        out["as_of"] = r.as_of.toUTC().toString(Qt::ISODate);
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
        .version = 2, // v2: one quote shape with MCP (MarketDataService::quote_json)
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
                services::MarketDataService::instance().fetch_quotes(
                    {symbol},
                    [cb, base, quote, symbol](bool ok, QVector<services::QuoteData> quotes, const QString& err) {
                        if (!ok || quotes.isEmpty()) {
                            cb(false, {}, err.isEmpty() ? QStringLiteral("no rate for %1").arg(symbol) : err);
                            return;
                        }
                        QJsonObject out = services::MarketDataService::quote_json(quotes.first());
                        out["base"] = base;
                        out["quote"] = quote;
                        out["rate"] = out.value("price");
                        cb(true, out, {});
                    },
                    /*stale_on_failure=*/false);
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
        .description = "Insider transactions (Form 4) from the Ownership register",
        .icon_text = "$",
        .accent_color = "#2563eb",
        // v2: rows come from the Ownership register (its 24-month window and
        // cache), with `window_months`; a filed-but-absent number is null.
        .version = 2,
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
                const QDate since = QDate::currentDate().addDays(-days_back);
                // The Ownership screen's register: one EDGAR read and cache.
                services::OwnershipService::instance().read_edgar(
                    symbol, [cb, symbol, tx_type, since, days_back, min_value](const ownership::OwnershipSnapshot& snap) {
                        if (!snap.edgar_ok) {
                            cb(false, {},
                               QString("Insider trades for %1: %2")
                                   .arg(symbol, snap.edgar_error.isEmpty() ? QStringLiteral("EDGAR did not answer")
                                                                           : snap.edgar_error));
                            return;
                        }
                        if (snap.window_months > 0 && since < QDate::currentDate().addMonths(-snap.window_months)) {
                            cb(false, {},
                               QString("Insider trades: %1 days reaches past the %2-month Form 4 window")
                                   .arg(days_back)
                                   .arg(snap.window_months));
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
                        for (const auto& t : snap.transactions) {
                            if (!t.date.isValid() || t.date < since)
                                continue;
                            if (!codes.isEmpty() && !codes.contains(t.code))
                                continue;
                            if (min_value > 0) {
                                // Rows without a reported value can't be shown to meet the minimum.
                                if (!t.value) {
                                    ++unvalued;
                                    continue;
                                }
                                if (std::abs(*t.value) < min_value)
                                    continue;
                            }
                            rows.append(insider_row_json(t));
                        }
                        QJsonObject out;
                        out["symbol"] = symbol;
                        out["source"] = "sec_edgar_form4";
                        out["since"] = since.toString(Qt::ISODate);
                        out["transaction_type"] = tx_type;
                        out["window_months"] = snap.window_months;
                        out["count"] = rows.size();
                        out["transactions"] = rows;
                        if (unvalued > 0)
                            out["excluded_without_value"] = unvalued;
                        out["coverage"] = QJsonObject{{"filings_found", snap.filings_found},
                                                      {"filings_parsed", snap.filings_parsed},
                                                      {"filings_truncated", snap.filings_truncated}};
                        cb(true, out, {});
                    });
            },
    });

    registry.register_type({
        .type_id = "market.sec_filings",
        .display_name = "SEC Filings",
        .category = "Market Data",
        .description = "Recent SEC filings (10-K, 10-Q, 8-K, etc.) from EDGAR",
        .icon_text = "$",
        .accent_color = "#2563eb",
        // v2: filings are the app's EDGAR submissions read (shared with IPO
        // Watch) as [{form, filing_date, accession, primary_doc, url}].
        .version = 2,
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
                const int limit = std::max(1, static_cast<int>(params.value("limit").toDouble(10)));
                services::MarketDataService::instance().fetch_sec_filings_of_form(
                    symbol, form, limit, [cb, symbol, form](const services::MarketDataService::SecFilingsReply& r) {
                        if (!r.ok) {
                            cb(false, {}, r.error);
                            return;
                        }
                        QJsonArray filings;
                        for (const auto& f : r.filings) {
                            filings.append(QJsonObject{{"form", f.form},
                                                       {"filing_date", f.filing_date},
                                                       {"accession", f.accession},
                                                       {"primary_doc", f.primary_doc},
                                                       {"url", f.url}});
                        }
                        QJsonObject out;
                        out["symbol"] = symbol;
                        out["source"] = "sec_edgar";
                        out["filing_type"] = form.isEmpty() ? QString("all") : form;
                        out["count"] = filings.size();
                        out["filings"] = filings;
                        cb(true, out, {});
                    });
            },
    });
}

} // namespace finterm::workflow
