#include "services/workflow/nodes/TriggerNodes.h"

#include "services/markets/MarketDataService.h"
#include "services/news/NewsService.h"
#include "services/workflow/NodeRegistry.h"

#include <QDateTime>
#include <QJsonArray>

#include <algorithm>

namespace fincept::workflow {

namespace {
// Triggers with no real event source must not "fire" when run — a fake
// trigger would push downstream nodes (including order placement) to execute.
void trigger_not_implemented(const QString& type_id, const std::function<void(bool, QJsonValue, QString)>& cb) {
    cb(false, {},
       QString("Trigger '%1' is not implemented — no real event source is wired, so it never fires").arg(type_id));
}
} // namespace

void register_trigger_nodes(NodeRegistry& registry) {
    // ManualTrigger — already registered as builtin in NodeRegistry constructor.
    // ScheduleTrigger — already registered as builtin.

    // ── Price Alert Trigger ────────────────────────────────────────
    registry.register_type({
        .type_id = "trigger.price_alert",
        .display_name = "Price Alert",
        .category = "Triggers",
        .description = "Trigger when price crosses a threshold",
        .icon_text = ">>",
        .accent_color = "#d97706",
        .version = 1,
        .inputs = {},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"symbol", "Symbol", "string", "AAPL", {}, "Ticker symbol", true},
                {"condition", "Condition", "select", "above", {"above", "below"}, ""},
                {"price", "Price", "number", 0.0, {}, "Threshold price", true},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Fetch a real quote and compare it with the threshold. The node
                // only fires (succeeds) when the condition holds; otherwise it
                // fails so nothing downstream runs.
                const QString symbol = params.value("symbol").toString().trimmed().toUpper();
                const QString condition = params.value("condition").toString("above");
                const double threshold = params.value("price").toDouble(0.0);
                if (symbol.isEmpty()) {
                    cb(false, {}, "Price Alert: 'symbol' is required");
                    return;
                }
                if (!(threshold > 0.0)) {
                    cb(false, {}, "Price Alert: threshold price must be > 0");
                    return;
                }
                if (condition != "above" && condition != "below") {
                    // "crosses" needs the previous observation; no state is kept between runs.
                    cb(false, {}, QString("Price Alert: condition '%1' is not supported").arg(condition));
                    return;
                }
                services::MarketDataService::instance().fetch_quotes(
                    {symbol}, [symbol, condition, threshold, cb](bool ok, QVector<services::QuoteData> quotes) {
                        double price = 0.0;
                        for (const auto& q : quotes)
                            if (q.symbol.compare(symbol, Qt::CaseInsensitive) == 0 && q.price > 0.0)
                                price = q.price;
                        if (!ok || !(price > 0.0)) {
                            cb(false, {}, QString("Price Alert: no market price available for %1").arg(symbol));
                            return;
                        }
                        const bool hit = condition == "above" ? price > threshold : price < threshold;
                        if (!hit) {
                            cb(false, {},
                               QString("Price Alert not triggered: %1 at %2 is not %3 %4")
                                   .arg(symbol)
                                   .arg(price, 0, 'f', 2)
                                   .arg(condition)
                                   .arg(threshold, 0, 'f', 2));
                            return;
                        }
                        QJsonObject out;
                        out["symbol"] = symbol;
                        out["condition"] = condition;
                        out["threshold"] = threshold;
                        out["price"] = price; // the real quote, never the threshold
                        out["triggered"] = true;
                        out["checked_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
                        cb(true, out, {});
                    });
            },
    });

    // ── News Event Trigger ─────────────────────────────────────────
    registry.register_type({
        .type_id = "trigger.news_event",
        .display_name = "News Event",
        .category = "Triggers",
        .description = "Trigger on news keyword match",
        .icon_text = ">>",
        .accent_color = "#d97706",
        .version = 1,
        .inputs = {},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"keywords", "Keywords", "string", "", {}, "Comma-separated keywords", true},
                {"lookback_hours", "Lookback (hours)", "number", 24, {}, "Only match articles published this recently"},
            },
        .execute =
            [](const QJsonObject& params, const QVector<QJsonValue>&,
               std::function<void(bool, QJsonValue, QString)> cb) {
                // Match keywords against the real news feed; fire only on a match.
                QStringList keywords;
                for (const QString& k : params.value("keywords").toString().split(',', Qt::SkipEmptyParts)) {
                    const QString t = k.trimmed();
                    if (!t.isEmpty())
                        keywords << t;
                }
                if (keywords.isEmpty()) {
                    cb(false, {}, "News Event: at least one keyword is required");
                    return;
                }
                const double lookback_h = params.value("lookback_hours").toDouble(24);
                const qint64 cutoff =
                    QDateTime::currentSecsSinceEpoch() - static_cast<qint64>(std::max(0.0, lookback_h) * 3600.0);

                services::NewsService::instance().fetch_all_news(
                    false, [keywords, cutoff, cb](bool ok, QVector<services::NewsArticle> articles) {
                        if (!ok) {
                            cb(false, {}, "News Event: news feed unavailable");
                            return;
                        }
                        QJsonArray matches;
                        QStringList hit_keywords;
                        for (const auto& a : articles) {
                            if (a.sort_ts <= 0 || a.sort_ts < cutoff)
                                continue; // unknown or too old publish time
                            const QString text = a.headline + ' ' + a.summary;
                            QStringList found;
                            for (const QString& k : keywords)
                                if (text.contains(k, Qt::CaseInsensitive))
                                    found << k;
                            if (found.isEmpty())
                                continue;
                            for (const QString& k : found)
                                if (!hit_keywords.contains(k, Qt::CaseInsensitive))
                                    hit_keywords << k;
                            matches.append(QJsonObject{{"headline", a.headline},
                                                       {"source", a.source},
                                                       {"link", a.link},
                                                       {"time", a.time},
                                                       {"published_ts", static_cast<double>(a.sort_ts)},
                                                       {"matched", QJsonArray::fromStringList(found)}});
                        }
                        if (matches.isEmpty()) {
                            cb(false, {},
                               QString("News Event not triggered: no recent articles match %1")
                                   .arg(keywords.join(", ")));
                            return;
                        }
                        QJsonObject out;
                        out["keywords"] = QJsonArray::fromStringList(keywords);
                        out["matched_keywords"] = QJsonArray::fromStringList(hit_keywords);
                        out["match_count"] = matches.size();
                        out["articles"] = matches;
                        out["triggered"] = true;
                        cb(true, out, {});
                    });
            },
    });

    // ── Webhook Trigger ────────────────────────────────────────────
    registry.register_type({
        .type_id = "trigger.webhook",
        .display_name = "Webhook",
        .category = "Triggers",
        .description = "Trigger from external webhook call",
        .icon_text = ">>",
        .accent_color = "#d97706",
        .version = 1,
        .inputs = {},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"path", "Path", "string", "/webhook", {}, "/my-hook"},
                {"method", "Method", "select", "POST", {"GET", "POST", "PUT"}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                trigger_not_implemented("trigger.webhook", cb);
            },
    });

    // ── Tier 1 additions ───────────────────────────────────────────

    registry.register_type({
        .type_id = "trigger.cron_market",
        .display_name = "Market Hours Cron",
        .category = "Triggers",
        .description = "Scheduled trigger that only fires during market hours",
        .icon_text = ">>",
        .accent_color = "#d97706",
        .version = 1,
        .inputs = {},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"cron", "Cron Expression", "string", "*/15 * * * *", {}, ""},
                {"exchange", "Exchange", "select", "NYSE", {"NYSE", "NASDAQ", "LSE", "TSE", "NSE"}, ""},
                {"include_premarket", "Include Pre-Market", "boolean", false, {}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                trigger_not_implemented("trigger.cron_market", cb);
            },
    });

    registry.register_type({
        .type_id = "trigger.portfolio_drift",
        .display_name = "Portfolio Drift",
        .category = "Triggers",
        .description = "Trigger when portfolio drifts from target allocation",
        .icon_text = ">>",
        .accent_color = "#d97706",
        .version = 1,
        .inputs = {},
        .outputs = {{"output_main", "Main", PortDirection::Output, ConnectionType::Main}},
        .parameters =
            {
                {"drift_threshold_pct",
                 "Drift Threshold %",
                 "number",
                 5.0,
                 {},
                 "Fire when any position drifts by this %"},
                {"check_interval_min", "Check Interval (min)", "number", 60, {}, ""},
            },
        .execute =
            [](const QJsonObject&, const QVector<QJsonValue>&, std::function<void(bool, QJsonValue, QString)> cb) {
                trigger_not_implemented("trigger.portfolio_drift", cb);
            },
    });
}

} // namespace fincept::workflow
