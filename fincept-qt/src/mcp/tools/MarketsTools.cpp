// MarketsTools.cpp — Markets MCP tools (real quotes, top movers, symbol search)

#include "mcp/tools/MarketsTools.h"

#include "mcp/AsyncDispatch.h"
#include "services/markets/MarketDataService.h"

#include <QDateTime>
#include <QJsonArray>
#include <QPromise>

#include <cmath>
#include <memory>

namespace fincept::mcp::tools {

namespace {

// NaN / non-finite = the vendor supplied nothing → JSON null, never 0.
QJsonValue markets_num_or_null(double v) {
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue();
}

QString markets_now_utc_iso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QJsonObject markets_quote_json(const services::QuoteData& q) {
    QJsonObject o{
        {"symbol", q.symbol},
        {"price", q.price > 0 ? QJsonValue(q.price) : QJsonValue()},
        {"change", markets_num_or_null(q.change)},
        {"change_pct", markets_num_or_null(q.change_pct)},
        {"day_high", q.high > 0 ? QJsonValue(q.high) : QJsonValue()},
        {"day_low", q.low > 0 ? QJsonValue(q.low) : QJsonValue()},
        {"volume", markets_num_or_null(q.volume)},
    };
    if (!q.name.isEmpty())
        o["name"] = q.name;
    return o;
}

} // namespace

std::vector<ToolDef> get_markets_tools() {
    std::vector<ToolDef> tools;

    // ── get_quote ───────────────────────────────────────────────────────
    // Used to publish a "market.get_quote" event that nothing subscribes to
    // and return "Quote request sent" — a success carrying no price, which
    // left the model answering price questions from memory. Now returns the
    // real quote from MarketDataService (yfinance), or fails visibly.
    {
        ToolDef t;
        t.name = "get_quote";
        t.description = "Get the latest quote for one or more stock/ETF/index/crypto symbols "
                        "(price, change, change_pct, day high/low, volume). fetched_at is when "
                        "finterm retrieved the data (quotes may be served from a short cache), "
                        "not the exchange's last-trade time.";
        t.category = "markets";
        t.default_timeout_ms = 20000;
        t.input_schema.properties = QJsonObject{
            {"symbol", QJsonObject{{"type", "string"},
                                   {"description", "Ticker symbol, or several comma-separated "
                                                   "(e.g. AAPL or AAPL,MSFT,BTC-USD)"}}}};
        t.input_schema.required = {"symbol"};
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                             std::shared_ptr<QPromise<ToolResult>> promise) {
            QStringList symbols;
            for (const QString& part : args["symbol"].toString().split(',', Qt::SkipEmptyParts)) {
                const QString s = part.trimmed().toUpper();
                if (!s.isEmpty() && !symbols.contains(s))
                    symbols.append(s);
            }
            if (symbols.isEmpty()) {
                promise->addResult(ToolResult::fail("Missing 'symbol'"));
                promise->finish();
                return;
            }
            auto* svc = &services::MarketDataService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise, [svc, symbols](auto resolve) {
                    svc->fetch_quotes(symbols, [resolve, symbols](bool ok, QVector<services::QuoteData> qs) {
                        if (!ok) {
                            resolve(ToolResult::fail("Quote fetch failed for " + symbols.join(", ") +
                                                     " — data unavailable right now."));
                            return;
                        }
                        const QString fetched_at = markets_now_utc_iso();
                        QJsonArray quotes;
                        QStringList found;
                        for (const auto& q : qs) {
                            if (q.symbol.isEmpty() || !(q.price > 0))
                                continue; // price 0 = vendor returned nothing usable
                            QJsonObject o = markets_quote_json(q);
                            o["fetched_at"] = fetched_at;
                            quotes.append(o);
                            found.append(q.symbol.toUpper());
                        }
                        QStringList missing;
                        for (const auto& s : symbols)
                            if (!found.contains(s))
                                missing.append(s);
                        if (quotes.isEmpty()) {
                            resolve(ToolResult::fail("No quote data returned for " + symbols.join(", ") +
                                                     " (unknown symbol or data source unavailable)."));
                            return;
                        }
                        QJsonObject out{{"quotes", quotes}, {"fetched_at", fetched_at}};
                        if (!missing.isEmpty())
                            out["unavailable"] = QJsonArray::fromStringList(missing);
                        resolve(ToolResult::ok_data(out));
                    });
                });
        };
        tools.push_back(std::move(t));
    }

    // ── get_top_movers ──────────────────────────────────────────────────
    // The day's real top gainers / losers from yfinance's predefined US
    // screeners (same source as the dashboard Top Movers widget).
    {
        ToolDef t;
        t.name = "get_top_movers";
        t.description = "Get today's top US stock gainers and losers (yfinance day_gainers / "
                        "day_losers screeners): symbol, name, price, change, change_pct, volume. "
                        "fetched_at is when finterm retrieved the list (cached up to 60s).";
        t.category = "markets";
        t.default_timeout_ms = 20000;
        t.input_schema.properties = QJsonObject{
            {"count", QJsonObject{{"type", "integer"},
                                  {"description", "Movers per side (1-25, default 10)"},
                                  {"minimum", 1},
                                  {"maximum", 25},
                                  {"default", 10}}}};
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                             std::shared_ptr<QPromise<ToolResult>> promise) {
            const int count = qBound(1, args["count"].toInt(10), 25);
            auto* svc = &services::MarketDataService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise, [svc, count](auto resolve) {
                    svc->fetch_top_movers(count, [resolve](bool ok, services::MarketDataService::TopMovers tm) {
                        if (!ok) {
                            resolve(ToolResult::fail("Top movers unavailable — the screener request failed."));
                            return;
                        }
                        if (tm.gainers.isEmpty() && tm.losers.isEmpty()) {
                            resolve(ToolResult::fail("Top movers unavailable — the screener returned no rows."));
                            return;
                        }
                        auto side = [](const QVector<services::QuoteData>& v) {
                            QJsonArray arr;
                            for (const auto& q : v)
                                arr.append(markets_quote_json(q));
                            return arr;
                        };
                        resolve(ToolResult::ok_data(QJsonObject{
                            {"gainers", side(tm.gainers)},
                            {"losers", side(tm.losers)},
                            {"universe", "US equities (yfinance day_gainers / day_losers screeners)"},
                            {"fetched_at", markets_now_utc_iso()},
                        }));
                    });
                });
        };
        tools.push_back(std::move(t));
    }

    // ── search_symbol ───────────────────────────────────────────────────
    // Used to publish an event no one subscribes to and report "Symbol search
    // sent" as success. Kept registered (callers may know the name) but it
    // now says plainly that it returns nothing and where the real search is.
    {
        ToolDef t;
        t.name = "search_symbol";
        t.description = "Deprecated: returns no results. Use search_equity_symbols to look up a "
                        "ticker by company name or partial symbol.";
        t.category = "markets";
        t.input_schema.properties =
            QJsonObject{{"query", QJsonObject{{"type", "string"},
                                              {"description", "Search query (company name or partial symbol)"}}}};
        t.input_schema.required = {"query"};
        t.handler = [](const QJsonObject&) -> ToolResult {
            return ToolResult::fail("search_symbol does not perform a search; call search_equity_symbols "
                                    "with the same query instead.");
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools
