// FuturesTools.cpp — what the Futures screen reads, for agents.
//
// 3 tools in category "futures":
//   1. list_futures_contracts   — the catalog: roots, classes, contract terms
//   2. get_futures_curve        — every listed month of one root, with the
//                                 curve's shape and roll yield
//   3. get_futures_positioning  — CFTC Commitments of Traders, summarised
//
// A value the vendor did not supply is null, never 0. Quotes are Yahoo's,
// about ten minutes behind the exchange; every reply says so.

#include "mcp/tools/FuturesTools.h"

#include "mcp/AsyncDispatch.h"
#include "mcp/ToolSchemaBuilder.h"
#include "services/futures/FuturesService.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

namespace finterm::mcp::tools {

namespace {

using namespace services::futures;

static constexpr int kFuturesToolTimeoutMs = 40000;

QJsonValue num_or_null(double v) {
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue(QJsonValue::Null);
}

QJsonObject spec_json(const ContractSpec& c) {
    return {{"root", c.root},
            {"name", c.name},
            {"asset_class", c.group},
            {"continuous_symbol", continuous_symbol(c.root)},
            {"quoted_in", c.unit},
            {"contract_size", c.size},
            {"tick", c.tick},
            {"usd_per_tick", c.tick * c.point_value},
            {"usd_per_1_00_move", c.point_value},
            {"has_positioning_report", !c.cftc.isEmpty()}};
}

const ContractSpec* spec_arg(const QJsonObject& args, ToolResult* failure) {
    const QString root = args.value("root").toString().trimmed().toUpper().remove(QStringLiteral("=F"));
    const ContractSpec* c = find_contract(root);
    if (!c)
        *failure = ToolResult::fail(QStringLiteral("unknown futures root '%1' — list_futures_contracts gives the ones carried").arg(root));
    return c;
}

} // namespace

std::vector<ToolDef> get_futures_tools() {
    std::vector<ToolDef> tools;

    {
        ToolDef t;
        t.name = "list_futures_contracts";
        t.description = "The futures the terminal carries: root, name, asset class, how the price is quoted, contract "
                        "size, tick and its dollar value. Margin is not included (there is no feed for it).";
        t.category = "futures";
        t.input_schema = ToolSchemaBuilder().string("asset_class", "Optional: INDEX, RATES, ENERGY, METALS, GRAINS, SOFTS, LIVESTOCK, FX or CRYPTO").build();
        t.handler = [](const QJsonObject& args) -> ToolResult {
            const QString group = args.value("asset_class").toString().trimmed().toUpper();
            QJsonArray rows;
            for (const auto& c : all_contracts())
                if (group.isEmpty() || c.group == group)
                    rows.append(spec_json(c));
            return ToolResult::ok_data(QJsonObject{{"contracts", rows}});
        };
        tools.push_back(std::move(t));
    }

    {
        ToolDef t;
        t.name = "get_futures_curve";
        t.description =
            "Forward curve of one futures root (e.g. CL, ES, GC): every listed delivery month with last price, change, "
            "volume, open interest and last trading day, plus the curve's shape (contango / backwardation) and the "
            "annualised roll yield from the front month into the next. Prices are Yahoo's, ~10 minutes delayed. A month "
            "marked stale has not traded since the session before the newest print: its price is an old trade. null = "
            "not supplied.";
        t.category = "futures";
        t.default_timeout_ms = kFuturesToolTimeoutMs;
        t.input_schema = ToolSchemaBuilder().string("root", "Futures root, e.g. CL").required().length(1, 8).build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx, std::shared_ptr<QPromise<ToolResult>> promise) {
            ToolResult failure;
            const ContractSpec* spec = spec_arg(args, &failure);
            AsyncDispatch::callback_to_promise(qApp, std::move(ctx), promise, [spec, failure](auto resolve) {
                if (!spec) {
                    resolve(failure);
                    return;
                }
                fetch_curve(spec->root, qApp, [spec, resolve](bool ok, Curve curve, QString err) {
                    if (!ok) {
                        resolve(ToolResult::fail(err));
                        return;
                    }
                    const CurveSummary sum = summarize(curve);
                    QJsonArray months;
                    for (int i = 0; i < curve.contracts.size(); ++i) {
                        const auto& c = curve.contracts[i];
                        const Carry carry = sum.anchor >= 0 && i != sum.anchor && !c.stale
                                                ? carry_between(curve.contracts[sum.anchor], c)
                                                : Carry{};
                        QJsonObject row{{"contract", c.symbol},
                                        {"month", c.month_label()},
                                        {"is_front", i == sum.anchor},
                                        {"stale", c.stale},
                                        {"last", num_or_null(c.price)},
                                        {"change", num_or_null(c.stale ? kNaN : c.change)},
                                        {"change_pct", num_or_null(c.stale ? kNaN : c.pct)},
                                        {"spread_to_front", num_or_null(carry.spread)},
                                        {"carry_pct_per_year", num_or_null(carry.annualized_pct)},
                                        {"volume", num_or_null(c.volume)},
                                        {"open_interest", num_or_null(c.oi)},
                                        {"last_trading_day", c.expiry.isValid() ? QJsonValue(c.expiry.toString(Qt::ISODate))
                                                                                : QJsonValue(QJsonValue::Null)}};
                        if (spec->rate_quoted)
                            row.insert("implied_rate_pct", num_or_null(implied_rate(c.price)));
                        months.append(row);
                    }
                    resolve(ToolResult::ok_data(QJsonObject{
                        {"contract", spec_json(*spec)},
                        {"front", curve.front},
                        {"shape", sum.shape == Shape::Unknown ? QJsonValue(QJsonValue::Null) : QJsonValue(shape_label(sum.shape).toLower())},
                        {"roll_yield_pct_per_year", num_or_null(sum.roll_yield_pct)},
                        {"months", months},
                        {"source", "Yahoo Finance, about 10 minutes delayed"},
                        {"as_of_unix", static_cast<double>(curve.as_of)},
                    }));
                });
            });
        };
        tools.push_back(std::move(t));
    }

    {
        ToolDef t;
        t.name = "get_futures_positioning";
        t.description =
            "CFTC Commitments of Traders for one futures root (legacy futures-only report, weekly, positions as of the "
            "Tuesday given): net positions of large speculators (non-commercial), commercials and small traders, the "
            "week's change, and where the speculators' net sits in its own three-year range (0 = most net short, 100 = "
            "most net long). Describes positions held, not a forecast.";
        t.category = "futures";
        t.default_timeout_ms = 60000;
        t.input_schema = ToolSchemaBuilder().string("root", "Futures root, e.g. GC").required().length(1, 8).build();
        t.async_handler = [](const QJsonObject& args, ToolContext ctx, std::shared_ptr<QPromise<ToolResult>> promise) {
            ToolResult failure;
            const ContractSpec* spec = spec_arg(args, &failure);
            AsyncDispatch::callback_to_promise(qApp, std::move(ctx), promise, [spec, failure](auto resolve) {
                if (!spec) {
                    resolve(failure);
                    return;
                }
                if (spec->cftc.isEmpty()) {
                    resolve(ToolResult::fail(QStringLiteral("no Commitments of Traders report is paired with %1").arg(spec->root)));
                    return;
                }
                fetch_cot(spec->cftc, qApp, [spec, resolve](bool ok, CotReport report, QString err) {
                    if (!ok) {
                        resolve(ToolResult::fail(err));
                        return;
                    }
                    const CotSummary s = summarize_cot(report);
                    resolve(ToolResult::ok_data(QJsonObject{
                        {"root", spec->root},
                        {"market", report.market},
                        {"positions_as_of", s.date.toString(Qt::ISODate)},
                        {"speculators_net", num_or_null(s.nc_net)},
                        {"speculators_net_change_week", num_or_null(s.nc_net_change)},
                        {"speculators_net_pct_of_open_interest", num_or_null(s.nc_net_pct_oi)},
                        {"speculators_net_place_in_range_pct", num_or_null(s.nc_range_pct)},
                        {"range_weeks", s.weeks},
                        {"commercials_net", num_or_null(s.c_net)},
                        {"commercials_net_change_week", num_or_null(s.c_net_change)},
                        {"small_traders_net", num_or_null(s.nr_net)},
                        {"open_interest", num_or_null(s.oi)},
                        {"open_interest_change_week", num_or_null(s.oi_change)},
                        {"source", "CFTC Commitments of Traders, legacy futures-only"},
                    }));
                });
            });
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace finterm::mcp::tools
