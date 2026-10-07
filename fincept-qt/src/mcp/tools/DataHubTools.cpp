// DataHubTools.cpp — DataHub introspection MCP tools.
//
// Exposes the in-process DataHub pub/sub layer to LLM tool callers so
// models can inspect live topic state, subscriber counts, and last-value
// snapshots during debugging / agent workflows.

#include "mcp/tools/DataHubTools.h"

#include "core/logging/Logger.h"
#include "datahub/DataHub.h"
#include "datahub/DataHubMetaTypes.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

#include <cmath>

namespace fincept::mcp::tools {

static constexpr const char* TAG = "DataHubTools";


static QJsonValue dh_num(double v) {
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue();
}

static QJsonValue dh_truncate(QJsonValue j) {
    if (j.isString()) {
        const QString s = j.toString();
        if (s.size() > 4096) return QJsonValue(s.left(4096) + "...[truncated]");
    }
    return j;
}

/// Serialise the struct payloads the hub actually carries. QJsonValue::
/// fromVariant() knows none of them, and QVariant::toString() on a custom
/// struct is "" — so datahub_peek used to return an empty string for
/// market:quote:AAPL, its own documented example. Unknown structs now say
/// what they are instead of looking like an empty value.
static QJsonValue variant_to_json(const QVariant& v) {
    using services::QuoteData;
    using services::InfoData;
    using services::HistoryPoint;
    if (v.metaType() == QMetaType::fromType<QuoteData>()) {
        const auto q = v.value<QuoteData>();
        return QJsonObject{{"symbol", q.symbol}, {"name", q.name}, {"price", dh_num(q.price)},
                           {"change", dh_num(q.change)}, {"change_pct", dh_num(q.change_pct)},
                           {"high", dh_num(q.high)}, {"low", dh_num(q.low)}, {"volume", dh_num(q.volume)},
                           {"bid", dh_num(q.bid)}, {"ask", dh_num(q.ask)}};
    }
    if (v.metaType() == QMetaType::fromType<InfoData>()) {
        const auto i = v.value<InfoData>();
        return QJsonObject{{"symbol", i.symbol}, {"name", i.name}, {"sector", i.sector},
                           {"industry", i.industry}, {"country", i.country}, {"currency", i.currency},
                           {"market_cap", dh_num(i.market_cap)}, {"pe_ratio", dh_num(i.pe_ratio)},
                           {"forward_pe", dh_num(i.forward_pe)}, {"price_to_book", dh_num(i.price_to_book)},
                           {"dividend_yield", dh_num(i.dividend_yield)}, {"beta", dh_num(i.beta)},
                           {"week52_high", dh_num(i.week52_high)}, {"week52_low", dh_num(i.week52_low)},
                           {"avg_volume", dh_num(i.avg_volume)}};
    }
    if (v.metaType() == QMetaType::fromType<QVector<HistoryPoint>>()) {
        const auto pts = v.value<QVector<HistoryPoint>>();
        QJsonArray arr;
        // Most recent 250 bars bound the context cost.
        const qsizetype from = pts.size() > 250 ? pts.size() - 250 : 0;
        for (qsizetype k = from; k < pts.size(); ++k) {
            const auto& h = pts[k];
            arr.append(QJsonObject{{"date", h.date().toString(Qt::ISODate)}, {"open", h.open},
                                   {"high", h.high}, {"low", h.low}, {"close", h.close},
                                   {"volume", static_cast<double>(h.volume)}});
        }
        return QJsonObject{{"bars", arr}, {"total_bars", static_cast<int>(pts.size())},
                           {"truncated", from > 0}};
    }
    if (v.metaType() == QMetaType::fromType<trading::TickerData>()) {
        const auto t = v.value<trading::TickerData>();
        return QJsonObject{{"symbol", t.symbol}, {"last", t.last}, {"bid", t.bid}, {"ask", t.ask},
                           {"high", t.high}, {"low", t.low}, {"open", t.open}, {"close", t.close},
                           {"change", t.change}, {"change_pct", t.percentage},
                           {"base_volume", t.base_volume}, {"quote_volume", t.quote_volume},
                           {"timestamp", static_cast<qint64>(t.timestamp)}};
    }
    if (v.metaType() == QMetaType::fromType<trading::Candle>()) {
        const auto c = v.value<trading::Candle>();
        return QJsonObject{{"timestamp", static_cast<qint64>(c.timestamp)}, {"open", c.open},
                           {"high", c.high}, {"low", c.low}, {"close", c.close}, {"volume", c.volume}};
    }
    if (v.metaType() == QMetaType::fromType<services::EconomicsResult>()) {
        const auto e = v.value<services::EconomicsResult>();
        QJsonObject o{{"success", e.success}, {"source_id", e.source_id}};
        if (!e.error.isEmpty()) o["error"] = e.error;
        o["data"] = e.data;
        return o;
    }

    // Direct QVariant -> QJsonValue for Qt-native types.
    auto j = QJsonValue::fromVariant(v);
    if (!j.isNull() || !v.isValid())
        return dh_truncate(j);
    const QString s = v.toString();
    if (!s.isEmpty())
        return dh_truncate(QJsonValue(s));
    return QJsonObject{{"unserialized_type", QString::fromLatin1(v.typeName())},
                       {"note", "this payload type has no JSON serializer in datahub_peek"}};
}


std::vector<ToolDef> get_datahub_tools() {
    std::vector<ToolDef> tools;

    // ── datahub_list_topics ─────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "datahub_list_topics";
        t.description = "List every active DataHub topic with subscriber counts and "
                        "last-publish age. Useful to see what live data the terminal "
                        "is currently streaming.";
        t.category = "datahub";
        t.handler = [](const QJsonObject&) -> ToolResult {
            const auto snap = fincept::datahub::DataHub::instance().stats();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            QJsonArray arr;
            for (const auto& s : snap) {
                QJsonObject e;
                e["topic"] = s.topic;
                e["subscribers"] = s.subscriber_count;
                e["publishes"] = s.total_publishes;
                e["push_only"] = s.push_only;
                e["in_flight"] = s.in_flight;
                e["age_ms"] = s.last_publish_ms > 0 ? static_cast<qint64>(now - s.last_publish_ms) : -1;
                arr.append(e);
            }
            return ToolResult::ok_data(arr);
        };
        tools.push_back(std::move(t));
    }

    // ── datahub_peek ────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "datahub_peek";
        t.description = "Read the current cached value for a DataHub topic without "
                        "triggering a refresh. Returns {value, age_ms} — check age_ms "
                        "before trusting the value for fresh decisions. Returns null "
                        "if the topic is unknown or has never been published.";
        t.category = "datahub";
        t.input_schema.properties = QJsonObject{
            {"topic", QJsonObject{{"type", "string"}, {"description", "Topic name (e.g. market:quote:AAPL)"}}}};
        t.input_schema.required = {"topic"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            const QString topic = args["topic"].toString().trimmed();
            if (topic.isEmpty()) return ToolResult::fail("Missing 'topic'");
            auto& hub = fincept::datahub::DataHub::instance();
            // peek_raw: MCP diagnostic tool shows stale data rather than
            // nothing — callers use stats() / last_publish_ms to judge freshness.
            const QVariant v = hub.peek_raw(topic);
            QJsonObject out;
            out["topic"] = topic;
            if (!v.isValid()) {
                out["value"] = QJsonValue::Null;
                out["age_ms"] = -1;
                return ToolResult::ok("Topic unknown or unset", out);
            }
            qint64 age = -1;
            for (const auto& s : hub.stats()) {
                if (s.topic == topic) {
                    age = QDateTime::currentMSecsSinceEpoch() - s.last_publish_ms;
                    break;
                }
            }
            out["value"] = variant_to_json(v);
            out["age_ms"] = age;
            return ToolResult::ok_data(out);
        };
        tools.push_back(std::move(t));
    }

    // ── datahub_request ─────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "datahub_request";
        t.description = "Ask the DataHub to refresh a topic now. Returns immediately — "
                        "the refresh runs asynchronously and subscribers are notified "
                        "when the producer publishes. Use 'force' to bypass the topic's "
                        "min_interval rate gate.";
        t.category = "datahub";
        t.input_schema.properties = QJsonObject{
            {"topic", QJsonObject{{"type", "string"}, {"description", "Topic name to refresh"}}},
            {"force", QJsonObject{{"type", "boolean"}, {"description", "Bypass min_interval_ms (default false)"}}}};
        t.input_schema.required = {"topic"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            const QString topic = args["topic"].toString().trimmed();
            if (topic.isEmpty()) return ToolResult::fail("Missing 'topic'");
            const bool force = args["force"].toBool(false);
            fincept::datahub::DataHub::instance().request(topic, force);
            return ToolResult::ok(QString("Refresh requested for %1 (force=%2)")
                                      .arg(topic, force ? "true" : "false"),
                                  QJsonObject{{"topic", topic}, {"force", force}});
        };
        tools.push_back(std::move(t));
    }

    // ── datahub_subscribe_briefly ───────────────────────────────────────
    // Subscribes a disposable QObject-owned listener to the topic for
    // `duration_ms` (clamped to [100, 30000]). Accumulates every delivered
    // value then returns the vector. Synchronous on purpose — MCP tools
    // are called from a thread where a local QEventLoop is the simplest
    // way to wait without blocking the Qt UI thread.
    {
        ToolDef t;
        t.name = "datahub_subscribe_briefly";
        t.description = "Subscribe to a topic for a short window and return every value "
                        "delivered during that window. Duration clamped to 100-30000 ms. "
                        "Useful for sampling a volatile topic (e.g. a live ticker) to "
                        "reason about its recent behaviour.";
        t.category = "datahub";
        t.input_schema.properties = QJsonObject{
            {"topic", QJsonObject{{"type", "string"}, {"description", "Topic name"}}},
            {"duration_ms", QJsonObject{{"type", "integer"}, {"description", "Window length in milliseconds"}}}};
        t.input_schema.required = {"topic", "duration_ms"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            const QString topic = args["topic"].toString().trimmed();
            if (topic.isEmpty()) return ToolResult::fail("Missing 'topic'");
            int duration = args["duration_ms"].toInt();
            duration = std::clamp(duration, 100, 30000);
            auto& hub = fincept::datahub::DataHub::instance();
            QObject owner;
            QJsonArray collected;
            QElapsedTimer clock;
            clock.start();
            hub.subscribe(&owner, topic, [&collected, &clock](const QVariant& v) {
                QJsonObject e;
                e["t_ms"] = static_cast<qint64>(clock.elapsed());
                e["value"] = variant_to_json(v);
                collected.append(e);
            });

            QEventLoop loop;
            QTimer::singleShot(duration, &loop, &QEventLoop::quit);
            loop.exec();
            hub.unsubscribe(&owner);

            QJsonObject out;
            out["topic"] = topic;
            out["duration_ms"] = duration;
            out["count"] = collected.size();
            out["samples"] = collected;
            return ToolResult::ok_data(out);
        };
        tools.push_back(std::move(t));
    }

    LOG_INFO(TAG, QString("Defined %1 DataHub introspection tools").arg(tools.size()));
    return tools;
}

} // namespace fincept::mcp::tools
