// EquityResearchTools.cpp — Tools that drive the Equity Research screen.
//
// 12 tools in category "equity-research":
//   1. search_equity_symbols
//   2. load_equity_symbol            — combined quote + info + historical
//   3. get_equity_quote              — quote only (price/change/vol)
//   4. get_equity_info               — full company / valuation profile
//   5. get_equity_historical         — OHLCV candles for a period
//   6. get_equity_financials         — income / balance / cashflow
//   7. get_equity_technicals         — indicators + the trend they describe
//   8. get_equity_peers              — peer-group comparison
//   9. get_equity_news               — recent news articles for a symbol
//  10. compute_equity_talipp         — run a talipp indicator (generic)
//  11. list_equity_talipp_indicators — talipp indicator catalog (sync)
//  12. get_equity_earnings_outlook   — the ER Earnings tab's three answers
//
// EquityResearchService signals do NOT carry a per-call request_id; most
// carry the symbol (or indicator) so we filter by that. Concurrent calls
// for the SAME symbol can race — caller must serialise per-symbol if it
// matters.

#include "mcp/tools/EquityResearchTools.h"

#include "core/logging/Logger.h"
#include "mcp/AsyncDispatch.h"
#include "mcp/ToolSchemaBuilder.h"
#include "services/equity/EarningsSignal.h"
#include "services/equity/EquityResearchService.h"
#include "services/query/QueryStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <cmath>
#include <memory>

namespace fincept::mcp::tools {

namespace {

// Names suffixed per file so the unity build doesn't trip on duplicates
// when it groups multiple tools' anonymous namespaces into one TU.
static constexpr const char* kEquityResearchTag = "EquityResearchTools";

// Calls hit yfinance via the Python daemon, which already caps each action at
// PythonWorker::kNetworkActionTimeoutMs (10s). 90s here just made a stuck/
// dropped daemon callback hang the chat for a minute and a half before failing
// (e.g. when Yahoo rate-limits yfinance). Cap the outer watchdog just above the
// daemon's own timeout so the tool fails fast and the model can answer.
static constexpr int kEquityResearchTimeoutMs = 20000;

// yfinance's held_percent_* / short_percent_of_float are FRACTIONS (0.0123 =
// 1.23%) despite their names. Publishing them under *_pct unconverted had a
// model report "0.01% insider ownership". Convert at the tool boundary so the
// field name and the unit agree; NaN (vendor gave nothing) stays NaN → null.
double fraction_to_pct(double f) {
    return std::isnan(f) ? f : f * 100.0;
}

QJsonObject quote_to_json(const services::equity::QuoteData& q) {
    // The feed's `timestamp` is when finterm FETCHED the quote
    // (datetime.now() in yfinance_data.py), not the exchange's last-trade
    // time — labelled as such so a model never reports it as the market time.
    // The daemon does not supply the market time, so none is claimed.
    const QString fetched_iso =  // EVENT-STAMP: quote fetch instant, reported as ISO UTC
        q.timestamp > 0 ? QDateTime::fromSecsSinceEpoch(q.timestamp).toUTC().toString(Qt::ISODate) : QString();
    return QJsonObject{
        {"symbol", q.symbol},
        {"price", q.price},
        {"change", q.change},
        {"change_pct", q.change_pct},
        {"open", q.open},
        {"high", q.high},
        {"low", q.low},
        {"prev_close", q.prev_close},
        {"volume", q.volume},
        {"exchange", q.exchange},
        {"fetched_at", q.timestamp > 0 ? QJsonValue(fetched_iso) : QJsonValue()},
        {"fetched_at_unix", q.timestamp > 0 ? QJsonValue(q.timestamp) : QJsonValue()},
        {"market_time", QJsonValue()},
        {"market_time_note", QStringLiteral("not supplied by the feed; fetched_at is finterm's fetch time")},
    };
}

QJsonObject info_to_json(const services::equity::StockInfo& i) {
    return QJsonObject{
        {"symbol", i.symbol},
        {"company_name", i.company_name},
        {"sector", i.sector},
        {"industry", i.industry},
        {"description", i.description},
        {"website", i.website},
        {"country", i.country},
        {"currency", i.currency},
        {"exchange", i.exchange},
        {"employees", i.employees},
        {"market_cap", i.market_cap},
        {"enterprise_value", i.enterprise_value},
        {"pe_ratio", i.pe_ratio},
        {"forward_pe", i.forward_pe},
        {"peg_ratio", i.peg_ratio},
        {"price_to_book", i.price_to_book},
        {"ev_to_revenue", i.ev_to_revenue},
        {"ev_to_ebitda", i.ev_to_ebitda},
        {"gross_margins", i.gross_margins},
        {"operating_margins", i.operating_margins},
        {"ebitda_margins", i.ebitda_margins},
        {"profit_margins", i.profit_margins},
        {"roe", i.roe},
        {"roa", i.roa},
        {"gross_profits", i.gross_profits},
        {"book_value", i.book_value},
        {"revenue_per_share", i.revenue_per_share},
        {"free_cashflow", i.free_cashflow},
        {"operating_cashflow", i.operating_cashflow},
        {"total_cash", i.total_cash},
        {"total_debt", i.total_debt},
        {"total_revenue", i.total_revenue},
        {"earnings_growth", i.earnings_growth},
        {"revenue_growth", i.revenue_growth},
        {"shares_outstanding", i.shares_outstanding},
        {"float_shares", i.float_shares},
        {"held_insiders_pct", fraction_to_pct(i.held_insiders_pct)}, // percent (0-100)
        {"held_institutions_pct", fraction_to_pct(i.held_institutions_pct)}, // percent (0-100)
        {"short_ratio", i.short_ratio},
        {"short_pct_of_float", fraction_to_pct(i.short_pct_of_float)}, // percent (0-100)
        {"week52_high", i.week52_high},
        {"week52_low", i.week52_low},
        {"avg_volume", i.avg_volume},
        {"beta", i.beta},
        {"dividend_yield", i.dividend_yield},
        {"current_price", i.current_price},
        {"target_high", i.target_high},
        {"target_low", i.target_low},
        {"target_mean", i.target_mean},
        // Raw vendor orientation, kept as-is so existing consumers don't shift
        // under them — but the Overview panel renders 6 - this on Bloomberg's
        // ANR footing (5 = Strong Buy), so an agent quoting the bare number
        // would contradict the tab for the same stock. The scale travels with
        // the value rather than living only in a tooltip a model never sees.
        {"recommendation_mean", i.recommendation_mean},
        {"recommendation_mean_scale",
         QStringLiteral("1=strong buy … 5=strong sell (vendor orientation; the "
                        "Overview panel shows 6 minus this, so that 5=strong buy)")},
        {"recommendation_key", i.recommendation_key},
        {"analyst_count", i.analyst_count},
    };
}

QJsonArray candles_to_json(const QVector<services::equity::Candle>& cs) {
    QJsonArray arr;
    for (const auto& c : cs) {
        arr.append(QJsonObject{
            {"timestamp", c.timestamp},
            {"open", c.open},
            {"high", c.high},
            {"low", c.low},
            {"close", c.close},
            {"volume", static_cast<double>(c.volume)},
        });
    }
    return arr;
}

QJsonArray financials_section_to_json(const QVector<QPair<QString, QJsonObject>>& xs) {
    QJsonArray arr;
    for (const auto& kv : xs)
        arr.append(QJsonObject{{"period", kv.first}, {"items", kv.second}});
    return arr;
}

const char* tech_signal_str(services::equity::TechSignal s) {
    using TS = services::equity::TechSignal;
    switch (s) {
        case TS::StrongBuy:  return "strong_buy";
        case TS::Buy:        return "buy";
        case TS::Neutral:    return "neutral";
        case TS::Sell:       return "sell";
        case TS::StrongSell: return "strong_sell";
    }
    return "neutral";
}

QJsonArray indicators_to_json(const QVector<services::equity::TechIndicator>& xs) {
    QJsonArray arr;
    for (const auto& x : xs) {
        arr.append(QJsonObject{
            {"name", x.name},
            {"value", x.value},
            {"signal", tech_signal_str(x.signal)},
            {"category", x.category},
            // Without this a consumer counts ATR's "neutral" as evidence and
            // treats MACD Signal / Stoch %D / Aroon Down as independent votes
            // rather than the second line of an indicator already counted.
            {"counts_toward_rating", x.votes},
        });
    }
    return arr;
}

QJsonObject technicals_to_json(const services::equity::TechnicalsData& t) {
    return QJsonObject{
        {"symbol", t.symbol},
        // The window the indicators were actually computed from, which is not
        // necessarily the one requested — see the `period` argument's schema.
        {"period_used", t.period},
        {"trend", indicators_to_json(t.trend)},
        {"momentum", indicators_to_json(t.momentum)},
        {"volatility", indicators_to_json(t.volatility)},
        {"volume", indicators_to_json(t.volume)},
        // NOT_RATED, not NEUTRAL, when the scorer declined: a consuming model
        // summarizes this one field and nothing forces it to cross-read the
        // `rated` boolean — the claim has to be right at the source.
        {"overall_signal", t.rated ? tech_signal_str(t.overall_signal) : QStringLiteral("NOT_RATED")},
        // Weighted composite in [-1, +1] the verdict was cut from, and the
        // per-bucket scores behind it — the counts alone do not reconstruct it,
        // since the buckets carry different weights.
        {"net_score", t.net_score},
        // False = the scorer declined (insufficient history); overall_signal
        // is then a filler Neutral and must not be read as a flat rating.
        {"rated", t.rated},
        {"rating_basis", t.rating_basis},
        {"data_warning", t.data_warning},
        {"voting_count", t.voting_count},
        {"strong_buy", t.strong_buy},
        {"buy", t.buy},
        {"neutral", t.neutral},
        {"sell", t.sell},
        {"strong_sell", t.strong_sell},
    };
}

QJsonArray peers_to_json(const QVector<services::equity::PeerData>& ps) {
    QJsonArray arr;
    for (const auto& p : ps) {
        arr.append(QJsonObject{
            {"symbol", p.symbol},
            {"name", p.name},
            {"sector", p.sector},
            {"market_cap", p.market_cap},
            {"pe_ratio", p.pe_ratio},
            {"forward_pe", p.forward_pe},
            {"price_to_book", p.price_to_book},
            {"price_to_sales", p.price_to_sales},
            {"peg_ratio", p.peg_ratio},
            {"roe", p.roe},
            {"roa", p.roa},
            {"profit_margin", p.profit_margin},
            {"operating_margin", p.operating_margin},
            {"gross_margin", p.gross_margin},
            {"revenue_growth", p.revenue_growth},
            {"earnings_growth", p.earnings_growth},
            {"debt_to_equity", p.debt_to_equity},
            {"current_ratio", p.current_ratio},
            {"quick_ratio", p.quick_ratio},
            {"dividend_yield", p.dividend_yield},
            {"beta", p.beta},
            {"price", p.price},
            {"change_pct", p.change_pct},
        });
    }
    return arr;
}

QJsonArray news_to_json(const QVector<services::equity::NewsArticle>& xs) {
    QJsonArray arr;
    for (const auto& a : xs) {
        arr.append(QJsonObject{
            {"title", a.title},
            {"description", a.description},
            {"url", a.url},
            {"publisher", a.publisher},
            {"published_date", a.published_date},
        });
    }
    return arr;
}

// Hand-curated talipp catalogue mirrored from EquityTalippTab::categories().
// Kept in this TU to avoid pulling the UI header into the tools layer.
struct TalippEntry { const char* id; const char* label; const char* data_type; const char* category; };
static const TalippEntry kTalipp[] = {
    // trend
    {"sma","SMA","prices","trend"}, {"ema","EMA","prices","trend"}, {"wma","WMA","prices","trend"},
    {"dema","DEMA","prices","trend"}, {"tema","TEMA","prices","trend"}, {"hma","HMA","prices","trend"},
    {"kama","KAMA","prices","trend"}, {"alma","ALMA","prices","trend"}, {"t3","T3","prices","trend"},
    {"zlema","ZLEMA","prices","trend"},
    // trend advanced
    {"adx","ADX","ohlcv","trend_advanced"}, {"aroon","Aroon","ohlcv","trend_advanced"},
    {"ichimoku","Ichimoku","ohlcv","trend_advanced"}, {"parabolic_sar","Parabolic SAR","ohlcv","trend_advanced"},
    {"supertrend","SuperTrend","ohlcv","trend_advanced"},
    // momentum
    {"rsi","RSI","prices","momentum"}, {"macd","MACD","prices","momentum"},
    {"stoch","Stochastic","ohlcv","momentum"}, {"stoch_rsi","StochRSI","prices","momentum"},
    {"cci","CCI","ohlcv","momentum"}, {"roc","ROC","prices","momentum"},
    {"tsi","TSI","prices","momentum"}, {"williams","Williams %R","ohlcv","momentum"},
    // volatility
    {"atr","ATR","ohlcv","volatility"}, {"bb","Bollinger Bands","prices","volatility"},
    {"keltner","Keltner Channels","ohlcv","volatility"}, {"donchian","Donchian Channels","ohlcv","volatility"},
    {"chandelier_stop","Chandelier Stop","ohlcv","volatility"}, {"natr","NATR","ohlcv","volatility"},
    // volume
    {"obv","OBV","ohlcv","volume"}, {"vwap","VWAP","ohlcv","volume"},
    {"vwma","VWMA","ohlcv","volume"}, {"mfi","MFI","ohlcv","volume"},
    {"chaikin_osc","Chaikin Osc","ohlcv","volume"}, {"force_index","Force Index","ohlcv","volume"},
    // specialized
    {"ao","Awesome Osc","ohlcv","specialized"}, {"accu_dist","Accum/Dist","ohlcv","specialized"},
    {"bop","Balance of Pwr","ohlcv","specialized"}, {"chop","CHOP","ohlcv","specialized"},
    {"coppock_curve","Coppock Curve","prices","specialized"}, {"dpo","DPO","prices","specialized"},
    {"emv","EMV","ohlcv","specialized"}, {"ibs","IBS","ohlcv","specialized"},
    {"kst","KST","prices","specialized"}, {"kvo","KVO","ohlcv","specialized"},
    {"mass_index","Mass Index","ohlcv","specialized"}, {"mcginley","McGinley","prices","specialized"},
    {"mean_dev","Mean Dev","prices","specialized"}, {"smma","SMMA","prices","specialized"},
    {"sobv","Smoothed OBV","ohlcv","specialized"}, {"stc","STC","prices","specialized"},
    {"std_dev","Std Dev","prices","specialized"}, {"trix","TRIX","prices","specialized"},
    {"ttm","TTM Squeeze","ohlcv","specialized"}, {"uo","Ultimate Osc","ohlcv","specialized"},
    {"vtx","Vortex","ohlcv","specialized"}, {"zigzag","ZigZag","ohlcv","specialized"},
};


/// The ER Earnings tab's outlook as JSON. Absent values are omitted rather
/// than zeroed — "no options data" and "options price no move" differ.
QJsonObject earnings_outlook_to_json(const services::equity::EarningsAnalysis& a) {
    using namespace services::equity;
    const EarningsOutlook o = evaluate_outlook(a);
    auto put = [](QJsonObject& obj, const char* k, const std::optional<double>& v) {
        if (v.has_value()) obj[QLatin1String(k)] = *v;
    };
    QJsonObject out{{"symbol", a.symbol}, {"has_earnings", o.valid}, {"headline", o.headline},
                    {"direction_call", "none — not predictable from pre-print data"}};
    if (!o.valid) return out;
    QJsonObject next;
    if (a.next.timestamp) {
        next["timestamp"] = static_cast<qint64>(*a.next.timestamp);
        next["days_to_report"] = o.days_to_report;
        next["date_confirmed"] = !a.next.is_estimated;
    }
    put(next, "consensus_eps", a.next.eps_avg);
    put(next, "eps_low", a.next.eps_low);
    put(next, "eps_high", a.next.eps_high);
    put(next, "consensus_revenue", a.next.rev_avg);
    out["next_report"] = next;

    QJsonObject beat;
    put(beat, "p_beat", o.p_beat);
    beat["pooled_beat_rate"] = o.pooled_beat_rate;
    beat["beats"] = o.beats;
    beat["scored_quarters"] = o.scored_quarters;
    put(beat, "median_surprise_pct", o.typical_surprise_pct);
    beat["estimate_drift"] = o.drift == EstimateDrift::Rising    ? "rising"
                             : o.drift == EstimateDrift::Falling ? "falling"
                             : o.drift == EstimateDrift::Flat    ? "flat"
                                                                 : "unknown";
    beat["estimate_drift_detail"] = o.drift_detail;
    put(beat, "dispersion_pct", o.dispersion_pct);
    out["will_they_beat"] = beat;

    QJsonObject size;
    put(size, "expected_move_pct", o.expected_move_pct);
    put(size, "trailing_avg_move_pct", o.trailing_move_pct);
    put(size, "half_of_prints_within_pct", o.half_within_pct);
    put(size, "four_in_five_within_pct", o.most_within_pct);
    put(size, "one_in_ten_beyond_pct", o.tail_beyond_pct);
    put(size, "options_implied_move_pct", o.implied_move_pct);
    put(size, "implied_to_expected_ratio", o.implied_ratio);
    out["how_big_a_move"] = size;

    QJsonArray scen;
    for (const auto& sc : o.scenarios) {
        QJsonObject j{{"outcome", sc.label}, {"eps_vs_consensus", sc.range},
                      {"probability", sc.probability}, {"move_multiple_of_expected", sc.move_multiple},
                      {"pooled_rise_rate", sc.up_rate}};
        if (o.expected_move_pct) j["typical_move_pct"] = sc.typical_move_pct;
        scen.append(j);
    }
    out["if_eps_lands"] = scen;
    out["priced_in"] = QJsonArray::fromStringList(o.priced_in);
    out["caveats"] = QJsonArray::fromStringList(o.caveats);
    return out;
}

} // namespace

std::vector<ToolDef> get_equity_research_tools() {
    std::vector<ToolDef> tools;

    // ── 1. search_equity_symbols ────────────────────────────────────────
    {
        ToolDef t;
        t.name = "search_equity_symbols";
        t.description = "Search for tradable equity symbols matching a query (ticker, name, fragment).";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("query", "Search query").required().length(1, 128)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString q = args["query"].toString();
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, q](auto resolve) {
                    auto* holder = new QObject(svc);
                    QObject::connect(svc, &services::equity::EquityResearchService::search_results_loaded, holder,
                                      [resolve, holder](QVector<services::equity::SearchResult> rs) {
                                          QJsonArray arr;
                                          for (const auto& r : rs) {
                                              arr.append(QJsonObject{
                                                  {"symbol", r.symbol},
                                                  {"name", r.name},
                                                  {"exchange", r.exchange},
                                                  {"type", r.type},
                                                  {"currency", r.currency},
                                                  {"industry", r.industry},
                                              });
                                          }
                                          resolve(ToolResult::ok_data(arr));
                                          holder->deleteLater();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [resolve, holder](QString, QString context, QString msg) {
                                          // Search errors are the only ones meant for this call;
                                          // a quote/info failure for some symbol on the shared
                                          // service must not fail an unrelated search.
                                          if (context != QLatin1String("Search")) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->search_symbols(q);
                });
        };
        tools.push_back(std::move(t));
    }

    // ── 2. load_equity_symbol (combined quote + info + historical) ─────
    // Kicks load_symbol() which spawns three parallel Python calls
    // emitting three different signals. We collect all three (filtering
    // by symbol where the signal carries it) and resolve when all are in.
    {
        ToolDef t;
        t.name = "load_equity_symbol";
        t.description = "Fetch quote + info + historical OHLCV for a symbol in one call (parallel under the hood).";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol (e.g. AAPL)").required().length(1, 32)
            .string("period", "Historical period (1d, 5d, 1mo, 3mo, 6mo, 1y, 2y, 5y, max)")
                .default_str("1y").length(1, 8)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            const QString period = args["period"].toString("1y");
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym, period](auto resolve) {
                    struct State {
                        QJsonObject quote;
                        QJsonObject info;
                        QJsonArray candles;
                        bool got_quote = false;
                        bool got_info = false;
                        bool got_hist = false;
                    };
                    auto state = std::make_shared<State>();
                    auto* holder = new QObject(svc);
                    auto try_finish = [resolve, holder, state, sym]() {
                        if (state->got_quote && state->got_info && state->got_hist) {
                            resolve(ToolResult::ok_data(QJsonObject{
                                {"symbol", sym},
                                {"quote", state->quote},
                                {"info", state->info},
                                {"historical", state->candles},
                            }));
                            holder->deleteLater();
                        }
                    };
                    QObject::connect(svc, &services::equity::EquityResearchService::quote_loaded, holder,
                                      [sym, state, try_finish](services::equity::QuoteData q) {
                                          if (q.symbol.toUpper() != sym) return;
                                          state->quote = quote_to_json(q);
                                          state->got_quote = true;
                                          try_finish();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::info_loaded, holder,
                                      [sym, state, try_finish](services::equity::StockInfo i) {
                                          if (i.symbol.toUpper() != sym) return;
                                          state->info = info_to_json(i);
                                          state->got_info = true;
                                          try_finish();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::historical_loaded, holder,
                                      [sym, period, state, try_finish](QString s, QString p, QVector<services::equity::Candle> cs) {
                                          // A refresh-timer reload of the default period must not
                                          // stand in for the period this call asked for.
                                          if (s.toUpper() != sym || p != period) return;
                                          state->candles = candles_to_json(cs);
                                          state->got_hist = true;
                                          try_finish();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->load_symbol(sym, period);
                });
        };
        tools.push_back(std::move(t));
    }

    // Generator for the three "single-signal" variants (quote / info /
    // historical). They all kick load_symbol() and listen for one signal.
    auto make_single = [](const QString& tool_name, const QString& desc, char which) {
        ToolDef t;
        t.name = tool_name;
        t.description = desc;
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol").required().length(1, 32)
            .string("period", "Historical period (only used for historical)").default_str("1y").length(1, 8)
            .build();
        t.async_handler = [which](const QJsonObject& args, ToolContext ctx,
                                   std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            const QString period = args["period"].toString("1y");
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym, period, which](auto resolve) {
                    auto* holder = new QObject(svc);
                    if (which == 'q') {
                        QObject::connect(svc, &services::equity::EquityResearchService::quote_loaded, holder,
                                          [sym, resolve, holder](services::equity::QuoteData q) {
                                              if (q.symbol.toUpper() != sym) return;
                                              resolve(ToolResult::ok_data(quote_to_json(q)));
                                              holder->deleteLater();
                                          });
                    } else if (which == 'i') {
                        QObject::connect(svc, &services::equity::EquityResearchService::info_loaded, holder,
                                          [sym, resolve, holder](services::equity::StockInfo i) {
                                              if (i.symbol.toUpper() != sym) return;
                                              resolve(ToolResult::ok_data(info_to_json(i)));
                                              holder->deleteLater();
                                          });
                    } else { // 'h'
                        QObject::connect(svc, &services::equity::EquityResearchService::historical_loaded, holder,
                                          [sym, period, resolve, holder](QString s, QString p, QVector<services::equity::Candle> cs) {
                                              if (s.toUpper() != sym || p != period) return;
                                              resolve(ToolResult::ok_data(QJsonObject{
                                                  {"symbol", s},
                                                  {"period", p},
                                                  {"candles", candles_to_json(cs)},
                                                  {"count", static_cast<int>(cs.size())},
                                              }));
                                              holder->deleteLater();
                                          });
                    }
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->load_symbol(sym, period);
                });
        };
        return t;
    };

    // ── 3-5. get_equity_quote / get_equity_info / get_equity_historical ─
    tools.push_back(make_single("get_equity_quote",
                                  "Get current quote (price/change/volume) for a symbol. fetched_at is when "
                                  "finterm fetched it, not the exchange's last-trade time.", 'q'));
    tools.push_back(make_single("get_equity_info",
                                  "Get full company info + valuation + analyst targets for a symbol. Units: "
                                  "held_insiders_pct, held_institutions_pct and short_pct_of_float are "
                                  "percents (0-100); *_margins, roe, roa, revenue_growth and "
                                  "earnings_growth are vendor fractions (0.25 = 25%). null = not supplied.", 'i'));
    tools.push_back(make_single("get_equity_historical",
                                  "Get OHLCV historical candles for a symbol over a period.", 'h'));

    // ── 6. get_equity_financials ────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_equity_financials";
        t.description = "Get income statement, balance sheet, and cash flow for a symbol. "
                        "Statements are ANNUAL (fiscal-year) periods, most recent first — not "
                        "quarterly. Trailing-twelve-month revenue and net income are reported "
                        "separately as ttm_revenue / ttm_net_income, summed from the last four "
                        "reported quarters (zero when fewer than four are available).";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol").required().length(1, 32)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym](auto resolve) {
                    auto* holder = new QObject(svc);
                    QObject::connect(svc, &services::equity::EquityResearchService::financials_loaded, holder,
                                      [sym, resolve, holder](services::equity::FinancialsData f) {
                                          if (f.symbol.toUpper() != sym) return;
                                          resolve(ToolResult::ok_data(QJsonObject{
                                              {"symbol", f.symbol},
                                              // Name the basis in the payload, not just the
                                              // description: an agent reading these columns
                                              // has no other way to tell years from quarters.
                                              {"basis", "annual"},
                                              {"income_statement", financials_section_to_json(f.income_statement)},
                                              {"balance_sheet", financials_section_to_json(f.balance_sheet)},
                                              {"cash_flow", financials_section_to_json(f.cash_flow)},
                                              {"ttm_revenue", f.ttm_revenue},
                                              {"ttm_net_income", f.ttm_net_income},
                                              {"ttm_operating_income", f.ttm_operating_income},
                                              {"ttm_ebitda", f.ttm_ebitda},
                                              {"ttm_period", f.ttm_period},
                                          }));
                                          holder->deleteLater();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->fetch_financials(sym);
                });
        };
        tools.push_back(std::move(t));
    }

    // ── 7. get_equity_technicals ────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_equity_technicals";
        t.description =
            "Technical indicators (trend/momentum/volatility/volume) and the overall trend they "
            "describe. This is a description of price action that has already happened, not a "
            "forecast and not a trade recommendation: measured over 178 large caps and twelve "
            "years, it agrees with the trend already in place ~96% of the time and with the "
            "direction of the next 40 days ~53% \xe2\x80\x94 no better than chance. Treat the "
            "signal values as a description of price action "
            "that has already happened; do not present them to a user as trade advice.";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol").required().length(1, 32)
            .string("period",
                    "Daily history for the indicator calc. Anything under 2y is raised to 2y so "
                    "the 50/200-period averages, MACD and ADX are warmed up — without them no "
                    "rating is produced at all. The window actually used comes back as "
                    "`period_used`, and since every indicator reads the latest bar with a fixed "
                    "lookback, all sub-2y requests return the same rating.")
                .default_str("1y").length(1, 8)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            const QString period = args["period"].toString("1y");
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym, period](auto resolve) {
                    auto* holder = new QObject(svc);
                    // Match on the full identity, not the symbol alone. The UI
                    // can have a weekly fetch for the same symbol in flight
                    // (same floored period), and a symbol-only match would
                    // resolve this daily request with weekly-bar indicators.
                    const QString want_period =
                        services::equity::EquityResearchService::technicals_history_period(
                            period, QStringLiteral("1d"));
                    QObject::connect(svc, &services::equity::EquityResearchService::technicals_loaded, holder,
                                      [sym, want_period, resolve, holder](services::equity::TechnicalsData td) {
                                          if (td.symbol.toUpper() != sym || td.period != want_period ||
                                              td.interval != QLatin1String("1d"))
                                              return;
                                          resolve(ToolResult::ok_data(technicals_to_json(td)));
                                          holder->deleteLater();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->fetch_technicals(sym, period);
                });
        };
        tools.push_back(std::move(t));
    }

    // ── 8. get_equity_peers ─────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_equity_peers";
        t.description = "Get peer comparison metrics for a symbol against a caller-supplied list of peer symbols.";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Anchor symbol").required().length(1, 32)
            .array("peer_symbols", "List of peer ticker symbols", QJsonObject{{"type", "string"}})
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            QStringList peers;
            for (const auto& v : args["peer_symbols"].toArray())
                peers.append(v.toString().toUpper());
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym, peers](auto resolve) {
                    auto* holder = new QObject(svc);
                    QObject::connect(svc, &services::equity::EquityResearchService::peers_loaded, holder,
                                      [sym, resolve, holder](QString s, QVector<services::equity::PeerData> ps) {
                                          // The anchor symbol routes the emission: another caller's
                                          // peer fetch must not resolve this one with its data.
                                          if (s.toUpper() != sym) return;
                                          resolve(ToolResult::ok_data(peers_to_json(ps)));
                                          holder->deleteLater();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->fetch_peers(sym, peers);
                });
        };
        tools.push_back(std::move(t));
    }

    // ── 9. get_equity_news ──────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_equity_news";
        t.description = "Get recent news articles for a symbol.";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol").required().length(1, 32)
            .integer("count", "Max articles").default_int(20).between(1, 100)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            const int count = args["count"].toInt(20);
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym, count](auto resolve) {
                    auto* holder = new QObject(svc);
                    QObject::connect(svc, &services::equity::EquityResearchService::news_loaded, holder,
                                      [sym, resolve, holder](QString s, QVector<services::equity::NewsArticle> as) {
                                          if (s.toUpper() != sym) return;
                                          resolve(ToolResult::ok_data(QJsonObject{
                                              {"symbol", s},
                                              {"articles", news_to_json(as)},
                                              {"count", static_cast<int>(as.size())},
                                          }));
                                          holder->deleteLater();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->fetch_news(sym, count);
                });
        };
        tools.push_back(std::move(t));
    }

    // ── 10. compute_equity_talipp ───────────────────────────────────────
    {
        ToolDef t;
        t.name = "compute_equity_talipp";
        t.description = "Compute a talipp technical indicator over historical data for a symbol.";
        t.category = "equity-research";
        t.default_timeout_ms = kEquityResearchTimeoutMs;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol").required().length(1, 32)
            .string("indicator", "Indicator id (use list_equity_talipp_indicators)").required().length(1, 32)
            .object("params", "Indicator-specific params (e.g. {period: 20})")
            .string("period", "Historical period").default_str("2y").length(1, 8)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            const QString ind = args["indicator"].toString();
            const QJsonObject params = args["params"].toObject();
            const QString period = args["period"].toString("2y");

            // Convert JSON params object → QVariantMap for the service API.
            QVariantMap pmap;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it)
                pmap.insert(it.key(), it.value().toVariant());

            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym, ind, pmap, period](auto resolve) {
                    auto* holder = new QObject(svc);
                    QObject::connect(svc, &services::equity::EquityResearchService::talipp_result, holder,
                                      [ind, resolve, holder](QString got_ind, QVector<double> vs,
                                                              QVector<qint64> ts) {
                                          if (got_ind != ind) return;
                                          QJsonArray values;
                                          for (double v : vs) values.append(v);
                                          QJsonArray timestamps;
                                          for (qint64 t : ts) timestamps.append(t);
                                          resolve(ToolResult::ok_data(QJsonObject{
                                              {"indicator", got_ind},
                                              {"values", values},
                                              {"timestamps", timestamps},
                                              {"count", static_cast<int>(vs.size())},
                                          }));
                                          holder->deleteLater();
                                      });
                    QObject::connect(svc, &services::equity::EquityResearchService::error_occurred, holder,
                                      [sym, resolve, holder](QString s, QString, QString msg) {
                                          // finterm's signal: (symbol, context, message). The service
                                          // is shared, so an error for ANOTHER symbol (the user's own
                                          // tab, a concurrent tool call) must not fail this call.
                                          if (!s.isEmpty() && s.toUpper() != sym) return;
                                          resolve(ToolResult::fail(msg));
                                          holder->deleteLater();
                                      });
                    svc->compute_talipp(sym, ind, pmap, period);
                });
        };
        tools.push_back(std::move(t));
    }

    // ── 11. list_equity_talipp_indicators ───────────────────────────────
    {
        ToolDef t;
        t.name = "list_equity_talipp_indicators";
        t.description = "List all talipp indicators (id, label, category, data_type: 'prices' or 'ohlcv').";
        t.category = "equity-research";
        t.input_schema = ToolSchemaBuilder()
            .string("category", "Optional category filter (trend, trend_advanced, momentum, volatility, volume, specialized)")
                .default_str("").length(0, 32)
            .build();
        t.handler = [](const QJsonObject& args) -> ToolResult {
            const QString filter = args["category"].toString();
            QJsonArray arr;
            for (const auto& e : kTalipp) {
                if (!filter.isEmpty() && filter != QString::fromUtf8(e.category))
                    continue;
                arr.append(QJsonObject{
                    {"id", QString::fromUtf8(e.id)},
                    {"label", QString::fromUtf8(e.label)},
                    {"data_type", QString::fromUtf8(e.data_type)},
                    {"category", QString::fromUtf8(e.category)},
                });
            }
            return ToolResult::ok_data(arr);
        };
        tools.push_back(std::move(t));
    }

    // ── 12. get_equity_earnings_outlook ─────────────────────────────────
    {
        ToolDef t;
        t.name = "get_equity_earnings_outlook";
        t.description =
            "Pre-earnings outlook for a symbol \xe2\x80\x94 the same numbers as the ER Earnings tab. "
            "Answers three questions: how likely the company is to beat EPS consensus (a "
            "probability calibrated walk-forward on 9,665 prints), how big the next-session move "
            "is likely to be (with the ranges that forecast actually achieves, and the "
            "options-implied move when one can be separated), and what each outcome \xe2\x80\x94 miss, "
            "slight beat, solid beat, big beat \xe2\x80\x94 has typically meant for the price. It "
            "deliberately makes NO directional call: nothing available before a print predicts "
            "the direction better than a coin flip. Do not present the scenarios as a forecast "
            "of direction, and note they are EPS-only (revenue and guidance also move stocks).";
        t.category = "equity-research";
        // The daemon allows the earnings fan-out 25 s; leave room above it.
        t.default_timeout_ms = 30000;
        t.input_schema = ToolSchemaBuilder()
            .string("symbol", "Ticker symbol").required().length(1, 32)
            .build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString sym = args["symbol"].toString().toUpper();
            auto* svc = &services::equity::EquityResearchService::instance();
            AsyncDispatch::callback_to_promise(
                svc, std::move(ctx), promise,
                [svc, sym](auto resolve) {
                    auto* holder = new QObject(svc);
                    auto done = std::make_shared<bool>(false);
                    // Same cached query the tab reads, so a tool call while the
                    // tab is open costs nothing and the two can never disagree.
                    svc->subscribe_earnings_analysis(
                        holder, sym,
                        [resolve, holder, done](const services::query::QueryStore::State& s) {
                            if (*done) return;
                            const bool have = s.data.isValid() && !s.data.isNull();
                            if (!have && s.error.isEmpty()) return;   // still loading
                            *done = true;
                            if (!have)
                                resolve(ToolResult::fail(s.error));
                            else
                                resolve(ToolResult::ok_data(earnings_outlook_to_json(
                                    s.data.value<services::equity::EarningsAnalysis>())));
                            // Off the callback's stack: unsubscribing inside the
                            // store's own dispatch would mutate what it iterates.
                            QMetaObject::invokeMethod(holder, [holder]() {
                                services::query::QueryStore::instance().unsubscribe_all(holder);
                                holder->deleteLater();
                            }, Qt::QueuedConnection);
                        });
                });
        };
        tools.push_back(std::move(t));
    }

    LOG_INFO(kEquityResearchTag, QString("Defined %1 equity-research tools").arg(tools.size()));
    return tools;
}

} // namespace fincept::mcp::tools
