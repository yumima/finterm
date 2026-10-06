#include "services/workflow/adapters/ServiceBridges.h"

#include "core/logging/Logger.h"
#include "mcp/McpService.h"
#include "network/http/HttpClient.h"
#include "python/PythonRunner.h"
#include "services/workflow/AuditLogger.h"
#include "services/workflow/ConfirmationService.h"
#include "services/workflow/NodeRegistry.h"
#include "services/workflow/RiskManager.h"
#include "services/markets/MarketDataService.h"
#include "trading/AccountManager.h"
#include "trading/ExchangeService.h"
#include "trading/OrderMatcher.h"
#include "trading/PaperTrading.h"
#include "trading/UnifiedTrading.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QtConcurrent/QtConcurrent>

#include <optional>

using fincept::python::extract_json;
using fincept::python::PythonResult;
using fincept::python::PythonRunner;

namespace fincept::workflow {

// ── Market Data Bridge ─────────────────────────────────────────────────

void wire_market_data_bridges(NodeRegistry& registry) {
    // market.get_quote and market.get_historical already have real Python-backed
    // executors from MarketDataNodes.cpp — do NOT overwrite them.

    // Crypto Price — uses ExchangeService (Kraken public API, no key needed)
    auto* crypto_price_def = const_cast<NodeTypeDef*>(registry.find("market.get_crypto_price"));
    if (crypto_price_def) {
        crypto_price_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&,
                                       std::function<void(bool, QJsonValue, QString)> cb) {
            QString base = params.value("symbol").toString("BTC").toUpper();
            QString quote = params.value("quote").toString("USD").toUpper();
            QString symbol = base + "/" + quote; // Kraken format: BTC/USD

            (void)QtConcurrent::run([symbol, cb]() {
                auto& svc = trading::ExchangeService::instance();
                trading::TickerData t = svc.fetch_ticker(symbol);

                if (t.last <= 0.0) {
                    cb(false, {}, QString("No price data for %1").arg(symbol));
                    return;
                }

                QJsonObject out;
                out["symbol"] = symbol;
                out["price"] = t.last;
                out["bid"] = t.bid;
                out["ask"] = t.ask;
                out["high"] = t.high;
                out["low"] = t.low;
                out["change"] = t.change;
                out["change_pct"] = t.percentage;
                out["volume"] = t.base_volume;
                out["exchange"] = "kraken";
                out["timestamp"] = static_cast<qint64>(t.timestamp);
                cb(true, out, {});
                LOG_DEBUG("MarketBridge", QString("Crypto price fetched: %1 = %2").arg(symbol).arg(t.last));
            });
        };
    }

    // market.get_news already has a real executor from MarketDataNodes.cpp.
    // Remaining market nodes (depth, stats, fundamentals, economics) are wired
    // in MarketDataNodes.cpp directly — no stub overwrite needed here.

    LOG_INFO("ServiceBridges", "Market data bridges wired");
}

// ── Trading Bridge ─────────────────────────────────────────────────────

namespace {

using NodeCallback = std::function<void(bool, QJsonValue, QString)>;

// Resolve the broker account a trading node acts on. An explicit
// `account_id` param wins; otherwise the node's `broker` param must match
// exactly one active account ("paper" = any account in paper mode with a
// paper portfolio). Ambiguity or absence is an error — never guess.
std::optional<trading::BrokerAccount> resolve_trading_account(const QJsonObject& params, QString* err) {
    auto& am = trading::AccountManager::instance();
    const QString account_id = params.value("account_id").toString().trimmed();
    if (!account_id.isEmpty()) {
        auto acct = am.get_account(account_id);
        if (acct.account_id.isEmpty()) {
            *err = "Account not found: " + account_id;
            return std::nullopt;
        }
        return acct;
    }

    const QString broker = params.value("broker").toString("paper");
    QVector<trading::BrokerAccount> matches;
    for (const auto& a : am.active_accounts()) {
        const bool ok = (broker == "paper") ? (a.trading_mode == "paper" && !a.paper_portfolio_id.isEmpty())
                                            : (a.broker_id == broker);
        if (ok)
            matches.append(a);
    }
    if (matches.isEmpty()) {
        *err = broker == "paper"
                   ? QString("No paper trading account configured — add one in Equity Trading → Accounts")
                   : QString("No active '%1' account configured — add one in Equity Trading → Accounts").arg(broker);
        return std::nullopt;
    }
    if (matches.size() > 1) {
        QStringList names;
        for (const auto& a : matches)
            names << QString("%1 (%2)").arg(a.display_name, a.account_id);
        *err = QString("Multiple '%1' accounts match (%2) — set 'account_id' to choose one")
                   .arg(broker, names.join(", "));
        return std::nullopt;
    }
    return matches.first();
}

QJsonValue opt_num(const std::optional<double>& v) {
    return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
}

QJsonObject pt_order_json(const trading::PtOrder& o) {
    return QJsonObject{{"order_id", o.id},
                       {"symbol", o.symbol},
                       {"side", o.side},
                       {"order_type", o.order_type},
                       {"quantity", o.quantity},
                       {"price", opt_num(o.price)},
                       {"stop_price", opt_num(o.stop_price)},
                       {"filled_qty", o.filled_qty},
                       {"avg_price", opt_num(o.avg_price)},
                       {"status", o.status},
                       {"created_at", o.created_at},
                       {"filled_at", o.filled_at ? QJsonValue(*o.filled_at) : QJsonValue(QJsonValue::Null)}};
}

std::optional<trading::PtOrder> find_pt_order(const QString& portfolio_id, const QString& order_id) {
    for (const auto& o : trading::pt_get_orders(portfolio_id))
        if (o.id == order_id)
            return o;
    return std::nullopt;
}

// Fetch a real last price for a paper market fill. Crypto pairs ("BTC/USD")
// come from the configured exchange; everything else from MarketDataService.
// cb(price, error) — price <= 0 means unavailable (error says why).
void fetch_fill_price(const QString& symbol, std::function<void(double, QString)> cb) {
    if (symbol.contains('/')) {
        (void)QtConcurrent::run([symbol, cb]() {
            auto& svc = trading::ExchangeService::instance();
            if (svc.get_exchange().isEmpty()) {
                cb(0.0, "No crypto exchange configured");
                return;
            }
            const auto t = svc.fetch_ticker(symbol);
            if (t.symbol.isEmpty() || !(t.last > 0.0))
                cb(0.0, QString("No market price available for %1").arg(symbol));
            else
                cb(t.last, {});
        });
        return;
    }
    services::MarketDataService::instance().fetch_quotes(
        {symbol}, [symbol, cb](bool ok, QVector<services::QuoteData> quotes) {
            for (const auto& q : quotes) {
                if (ok && q.symbol.compare(symbol, Qt::CaseInsensitive) == 0 && q.price > 0.0) {
                    cb(q.price, {});
                    return;
                }
            }
            cb(0.0, QString("No market price available for %1").arg(symbol));
        });
}

// Route a confirmed order through UnifiedTrading (paper engine or live broker)
// and report the real outcome. Never fabricates an id or a status.
void route_order(const trading::BrokerAccount& acct, const trading::UnifiedOrder& order, const QJsonObject& params,
                 NodeCallback cb) {
    const bool paper = acct.trading_mode == "paper";
    QJsonObject base{{"symbol", order.symbol},
                     {"side", trading::order_side_str(order.side)},
                     {"order_type", trading::order_type_str(order.order_type)},
                     {"quantity", order.quantity},
                     {"broker", acct.broker_id},
                     {"account_id", acct.account_id},
                     {"mode", paper ? "paper" : "live"}};

    if (!paper) {
        const QString account_id = acct.account_id;
        (void)QtConcurrent::run([account_id, order, base, params, cb]() {
            // Broker network call off the GUI thread; result handled back on it
            // (the audit log writes to the main-thread SQLite connection).
            const auto r = trading::UnifiedTrading::instance().place_order(account_id, order);
            QMetaObject::invokeMethod(
                qApp,
                [r, order, base, params, cb]() {
                    if (!r.success || r.order_id.isEmpty()) {
                        cb(false, {}, r.message.isEmpty() ? QString("Broker rejected the order") : r.message);
                        return;
                    }
                    AuditLogger::instance().log(AuditAction::OrderPlaced, {}, {}, order.symbol,
                                                QString("%1 %2 x%3 order_id=%4")
                                                    .arg(base.value("side").toString(), order.symbol)
                                                    .arg(order.quantity)
                                                    .arg(r.order_id),
                                                params, false);
                    QJsonObject out = base;
                    out["order_id"] = r.order_id;
                    out["status"] = "accepted_by_broker";
                    if (!r.message.isEmpty())
                        out["broker_message"] = r.message;
                    cb(true, out, {});
                },
                Qt::QueuedConnection);
        });
        return;
    }

    auto place_paper = [acct, base, params, cb](trading::UnifiedOrder o) {
        auto r = trading::UnifiedTrading::instance().place_order(acct.account_id, o);
        if (!r.success || r.order_id.isEmpty()) {
            cb(false, {}, r.message.isEmpty() ? QString("Paper order failed") : r.message);
            return;
        }
        // Report the order exactly as the paper engine recorded it.
        auto rec = find_pt_order(acct.paper_portfolio_id, r.order_id);
        if (!rec) {
            cb(false, {}, QString("Paper order %1 placed but could not be read back").arg(r.order_id));
            return;
        }
        if (rec->status == "pending")
            trading::OrderMatcher::instance().add_order(*rec);
        AuditLogger::instance().log(AuditAction::OrderPlaced, {}, {}, o.symbol,
                                    QString("%1 %2 x%3 order_id=%4 (paper)")
                                        .arg(base.value("side").toString(), o.symbol)
                                        .arg(o.quantity)
                                        .arg(r.order_id),
                                    params, true);
        QJsonObject out = base;
        const QJsonObject recj = pt_order_json(*rec);
        for (auto it = recj.begin(); it != recj.end(); ++it)
            out[it.key()] = it.value();
        cb(true, out, {});
    };

    if (order.order_type != trading::OrderType::Market) {
        place_paper(order);
        return;
    }
    // Paper market orders fill at a real quote fetched now — never a placeholder.
    fetch_fill_price(order.symbol, [order, place_paper, cb](double px, const QString& err) {
        if (!(px > 0.0)) {
            cb(false, {}, err.isEmpty() ? QString("No market price available — cannot fill market order") : err);
            return;
        }
        auto o = order;
        o.price = px;
        // Hop to the main thread: the paper engine's DB access lives there.
        QMetaObject::invokeMethod(qApp, [place_paper, o]() { place_paper(o); }, Qt::QueuedConnection);
    });
}

} // namespace

void wire_trading_bridges(NodeRegistry& registry) {
    // Place Order — confirmation, then routed through UnifiedTrading to the
    // paper engine or the account's live broker. Output carries the real
    // order id / status; any failure is an error.
    auto* place_def = const_cast<NodeTypeDef*>(registry.find("trading.place_order"));
    if (place_def) {
        place_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&, NodeCallback cb) {
            const QString symbol = params.value("symbol").toString().trimmed().toUpper();
            const QString side = params.value("side").toString("buy").toLower();
            const QString type = params.value("order_type").toString("market").toLower();
            const double qty = params.value("quantity").toDouble(0);
            const double price = params.value("price").toDouble(0);

            if (symbol.isEmpty()) {
                cb(false, {}, "Place Order: 'symbol' is required");
                return;
            }
            if (side != "buy" && side != "sell") {
                cb(false, {}, "Place Order: side must be buy or sell");
                return;
            }
            if (!(qty > 0)) {
                cb(false, {}, "Place Order: quantity must be > 0");
                return;
            }

            trading::UnifiedOrder order;
            order.symbol = symbol;
            order.side = side == "buy" ? trading::OrderSide::Buy : trading::OrderSide::Sell;
            order.quantity = qty;
            if (type == "market") {
                order.order_type = trading::OrderType::Market;
            } else if (type == "limit") {
                if (!(price > 0)) {
                    cb(false, {}, "Place Order: limit orders need a limit price > 0");
                    return;
                }
                order.order_type = trading::OrderType::Limit;
                order.price = price;
            } else if (type == "stop") {
                if (!(price > 0)) {
                    cb(false, {}, "Place Order: stop orders need a stop price > 0 (set 'price')");
                    return;
                }
                order.order_type = trading::OrderType::StopLoss;
                order.stop_price = price;
            } else {
                // stop_limit needs both a stop and a limit price; this node only exposes one.
                cb(false, {}, QString("Place Order: order type '%1' is not supported by this node").arg(type));
                return;
            }

            QString err;
            auto acct = resolve_trading_account(params, &err);
            if (!acct) {
                cb(false, {}, "Place Order: " + err);
                return;
            }
            const bool paper = acct->trading_mode == "paper";

            ConfirmationRequest req;
            req.type = ConfirmationType::Trade;
            req.risk = paper ? RiskLevel::Low : RiskLevel::High;
            req.title = QString("%1 %2 x%3").arg(side.toUpper(), symbol).arg(qty);
            req.message = QString("Place %1 %2 order for %3 of %4 via %5 (%6)")
                              .arg(type, side, QString::number(qty), symbol, acct->display_name,
                                   paper ? "paper" : "LIVE");
            req.details = params;
            req.paper_trading = paper;

            const trading::BrokerAccount account = *acct;
            ConfirmationService::instance().request(
                req, [cb, params, order, account](bool approved, const QString& notes) {
                    if (!approved) {
                        cb(false, {}, "Order rejected by user: " + notes);
                        return;
                    }
                    route_order(account, order, params, cb);
                });
        };
    }

    // Cancel Order — paper engine or live broker via UnifiedTrading.
    if (auto* def = const_cast<NodeTypeDef*>(registry.find("trading.cancel_order"))) {
        def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs, NodeCallback cb) {
            QString order_id = params.value("order_id").toString().trimmed();
            if (order_id.isEmpty() && !inputs.isEmpty() && inputs[0].isObject())
                order_id = inputs[0].toObject().value("order_id").toString().trimmed();
            if (order_id.isEmpty()) {
                cb(false, {}, "Cancel Order: 'order_id' is required");
                return;
            }
            QString err;
            auto acct = resolve_trading_account(params, &err);
            if (!acct) {
                cb(false, {}, "Cancel Order: " + err);
                return;
            }
            auto finish = [cb, order_id, account = *acct](const trading::UnifiedOrderResponse& r) {
                if (!r.success) {
                    cb(false, {}, r.message.isEmpty() ? QString("Cancel failed") : r.message);
                    return;
                }
                cb(true,
                   QJsonObject{{"order_id", order_id},
                               {"status", "cancelled"},
                               {"account_id", account.account_id},
                               {"mode", account.trading_mode}},
                   {});
            };
            if (acct->trading_mode == "paper") {
                auto r = trading::UnifiedTrading::instance().cancel_order(acct->account_id, order_id);
                if (r.success)
                    trading::OrderMatcher::instance().remove_order(order_id);
                finish(r);
                return;
            }
            const QString account_id = acct->account_id;
            (void)QtConcurrent::run([account_id, order_id, finish]() {
                finish(trading::UnifiedTrading::instance().cancel_order(account_id, order_id));
            });
        };
    }

    // Modify Order — live brokers only (paper engine has no modify; UnifiedTrading errors).
    if (auto* def = const_cast<NodeTypeDef*>(registry.find("trading.modify_order"))) {
        def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&, NodeCallback cb) {
            const QString order_id = params.value("order_id").toString().trimmed();
            const double qty = params.value("quantity").toDouble(0);
            const double price = params.value("price").toDouble(0);
            if (order_id.isEmpty()) {
                cb(false, {}, "Modify Order: 'order_id' is required");
                return;
            }
            QJsonObject mods;
            if (qty > 0)
                mods["qty"] = qty;
            if (price > 0)
                mods["price"] = price;
            if (mods.isEmpty()) {
                cb(false, {}, "Modify Order: set a new quantity and/or price");
                return;
            }
            QString err;
            auto acct = resolve_trading_account(params, &err);
            if (!acct) {
                cb(false, {}, "Modify Order: " + err);
                return;
            }
            const QString account_id = acct->account_id;
            (void)QtConcurrent::run([account_id, order_id, mods, cb]() {
                auto r = trading::UnifiedTrading::instance().modify_order(account_id, order_id, mods);
                if (!r.success) {
                    cb(false, {}, r.message.isEmpty() ? QString("Modify failed") : r.message);
                    return;
                }
                QJsonObject out{{"order_id", order_id}, {"account_id", account_id}, {"modifications", mods}};
                if (!r.message.isEmpty())
                    out["broker_message"] = r.message;
                cb(true, out, {});
            });
        };
    }

    // Get Orders / Positions / Balance — real paper-engine reads. Live-broker
    // reads are not wired into workflows yet and fail explicitly.
    if (auto* def = const_cast<NodeTypeDef*>(registry.find("trading.get_orders"))) {
        def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&, NodeCallback cb) {
            QString err;
            auto acct = resolve_trading_account(params, &err);
            if (!acct) {
                cb(false, {}, "Get Orders: " + err);
                return;
            }
            if (acct->trading_mode != "paper") {
                cb(false, {}, "Get Orders: live broker orders are not implemented in workflows yet");
                return;
            }
            const QString status = params.value("status").toString("all");
            QVector<trading::PtOrder> orders;
            if (status == "open") {
                orders = trading::pt_get_orders(acct->paper_portfolio_id, "pending");
                orders += trading::pt_get_orders(acct->paper_portfolio_id, "partial");
            } else {
                orders = trading::pt_get_orders(acct->paper_portfolio_id, status == "all" ? QString() : status);
            }
            QJsonArray arr;
            for (const auto& o : orders)
                arr.append(pt_order_json(o));
            cb(true,
               QJsonObject{{"account_id", acct->account_id}, {"mode", "paper"}, {"count", arr.size()}, {"orders", arr}},
               {});
        };
    }

    if (auto* def = const_cast<NodeTypeDef*>(registry.find("trading.get_positions"))) {
        def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&, NodeCallback cb) {
            QString err;
            auto acct = resolve_trading_account(params, &err);
            if (!acct) {
                cb(false, {}, "Get Positions: " + err);
                return;
            }
            if (acct->trading_mode != "paper") {
                cb(false, {}, "Get Positions: live broker positions are not implemented in workflows yet");
                return;
            }
            QJsonArray arr;
            for (const auto& p : trading::pt_get_positions(acct->paper_portfolio_id)) {
                const bool priced = p.current_price > 0.0;
                arr.append(QJsonObject{{"symbol", p.symbol},
                                       {"side", p.side},
                                       {"quantity", p.quantity},
                                       {"entry_price", p.entry_price},
                                       {"current_price", priced ? QJsonValue(p.current_price) : QJsonValue()},
                                       {"unrealized_pnl", priced ? QJsonValue(p.unrealized_pnl) : QJsonValue()},
                                       {"realized_pnl", p.realized_pnl},
                                       {"leverage", p.leverage},
                                       {"opened_at", p.opened_at}});
            }
            cb(true,
               QJsonObject{
                   {"account_id", acct->account_id}, {"mode", "paper"}, {"count", arr.size()}, {"positions", arr}},
               {});
        };
    }

    if (auto* def = const_cast<NodeTypeDef*>(registry.find("trading.get_balance"))) {
        def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&, NodeCallback cb) {
            QString err;
            auto acct = resolve_trading_account(params, &err);
            if (!acct) {
                cb(false, {}, "Get Balance: " + err);
                return;
            }
            if (acct->trading_mode != "paper") {
                cb(false, {}, "Get Balance: live broker balance is not implemented in workflows yet");
                return;
            }
            try {
                const auto pf = trading::pt_get_portfolio(acct->paper_portfolio_id);
                cb(true,
                   QJsonObject{{"account_id", acct->account_id},
                               {"mode", "paper"},
                               {"balance", pf.balance},
                               {"initial_balance", pf.initial_balance},
                               {"currency", pf.currency}},
                   {});
            } catch (const std::exception& e) {
                cb(false, {}, QString("Get Balance: %1").arg(e.what()));
            }
        };
    }

    // get_holdings, close_position, bracket_order, trailing_stop, scale_in have
    // no real execution path yet — they fall to the "not implemented" error
    // fallback in wire_all_bridges (never a fake success).

    LOG_INFO("ServiceBridges", "Trading bridges wired");
}

// ── Agent Bridge ───────────────────────────────────────────────────────

void wire_agent_bridges(NodeRegistry& /*registry*/) {
    // agent.run and agent.tool_picker have real executors from AgentNodes.cpp.
    // agent.single/agent.multi/agent.mediator are legacy stubs — leave their
    // executors to the fallback pass-through in wire_all_bridges if still null.
    LOG_INFO("ServiceBridges", "Agent bridges wired");
}

// ── Wire All ───────────────────────────────────────────────────────────

static void wire_utility_bridges(NodeRegistry& registry) {
    // ── HTTP Request — via HttpClient ──────────────────────────────
    auto* http_def = const_cast<NodeTypeDef*>(registry.find("utility.http_request"));
    if (http_def && !http_def->execute) {
        http_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                               std::function<void(bool, QJsonValue, QString)> cb) {
            QString url = params.value("url").toString();
            QString method = params.value("method").toString("GET");
            if (url.isEmpty()) {
                cb(false, {}, "URL is required");
                return;
            }

            auto& http = HttpClient::instance();
            if (method == "POST") {
                QJsonObject body = params.value("body").toObject();
                if (body.isEmpty() && !inputs.isEmpty() && inputs[0].isObject())
                    body = inputs[0].toObject();
                http.post(url, body, [cb, url](Result<QJsonDocument> result) {
                    if (result.is_err()) {
                        cb(false, {}, QString::fromStdString(result.error()));
                        return;
                    }
                    QJsonObject out;
                    out["url"] = url;
                    // HttpClient doesn't expose the status code (and passes non-2xx
                    // JSON bodies through as ok), so no status_code is reported.
                    out["data"] = result.value().isObject() ? QJsonValue(result.value().object())
                                                            : QJsonValue(result.value().array());
                    cb(true, out, {});
                });
            } else {
                http.get(url, [cb, url](Result<QJsonDocument> result) {
                    if (result.is_err()) {
                        cb(false, {}, QString::fromStdString(result.error()));
                        return;
                    }
                    QJsonObject out;
                    out["url"] = url;
                    // HttpClient doesn't expose the status code (and passes non-2xx
                    // JSON bodies through as ok), so no status_code is reported.
                    out["data"] = result.value().isObject() ? QJsonValue(result.value().object())
                                                            : QJsonValue(result.value().array());
                    cb(true, out, {});
                });
            }
            LOG_DEBUG("UtilityBridge", QString("HTTP %1 %2").arg(method, url));
        };
    }

    // ── Code node — via PythonRunner ──────────────────────────────
    auto* code_def = const_cast<NodeTypeDef*>(registry.find("utility.code"));
    if (code_def && !code_def->execute) {
        code_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                               std::function<void(bool, QJsonValue, QString)> cb) {
            QString language = params.value("language").toString("python");
            QString code = params.value("code").toString();
            if (code.isEmpty()) {
                cb(false, {}, "Code is empty");
                return;
            }

            // Inject upstream input as a variable the script can access
            if (!inputs.isEmpty() && !inputs[0].isNull()) {
                QString input_json =
                    QString::fromUtf8(QJsonDocument(inputs[0].isObject() ? QJsonDocument(inputs[0].toObject())
                                                                         : QJsonDocument(inputs[0].toArray()))
                                          .toJson(QJsonDocument::Compact));
                code = QString("import json\n_input = json.loads('%1')\n").arg(input_json.replace("'", "\\'")) + code;
            }

            // Append a JSON output wrapper if user doesn't print JSON themselves
            if (!code.contains("json.dumps") && !code.contains("print(")) {
                code += "\nimport json as _json\ntry:\n    print(_json.dumps({'result': result}))\nexcept NameError:\n "
                        "   print(_json.dumps({'executed': True}))\n";
            }

            PythonRunner::instance().run_code(code, [cb](const PythonResult& res) {
                if (!res.success) {
                    cb(false, {}, res.error.isEmpty() ? "Code execution failed" : res.error);
                    return;
                }
                QString json_str = extract_json(res.output).trimmed();
                if (!json_str.isEmpty()) {
                    auto doc = QJsonDocument::fromJson(json_str.toUtf8());
                    if (!doc.isNull()) {
                        cb(true, doc.isObject() ? QJsonValue(doc.object()) : QJsonValue(doc.array()), {});
                        return;
                    }
                }
                // Return raw output as text
                QJsonObject out;
                out["output"] = res.output;
                cb(true, out, {});
            });
        };
    }

    // ── Analytics nodes already have executors from AnalyticsNodes.cpp — skip

    // ── File operations — via Qt file I/O ─────────────────────────
    auto* file_def = const_cast<NodeTypeDef*>(registry.find("file.operations"));
    if (file_def && !file_def->execute) {
        file_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                               std::function<void(bool, QJsonValue, QString)> cb) {
            QString operation = params.value("operation").toString("read");
            QString path = params.value("path").toString();
            if (path.isEmpty()) {
                cb(false, {}, "File path is required");
                return;
            }

            if (operation == "read") {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    cb(false, {}, QString("Cannot open file: %1").arg(file.errorString()));
                    return;
                }
                QJsonObject out;
                out["path"] = path;
                out["content"] = QString::fromUtf8(file.readAll());
                out["size"] = file.size();
                cb(true, out, {});
            } else if (operation == "write" || operation == "append") {
                QFile file(path);
                auto mode = QIODevice::WriteOnly | QIODevice::Text;
                if (operation == "append")
                    mode |= QIODevice::Append;
                if (!file.open(mode)) {
                    cb(false, {}, QString("Cannot open file for writing: %1").arg(file.errorString()));
                    return;
                }
                QString content;
                if (!inputs.isEmpty()) {
                    if (inputs[0].isString())
                        content = inputs[0].toString();
                    else
                        content =
                            QString::fromUtf8(QJsonDocument(inputs[0].isObject() ? QJsonDocument(inputs[0].toObject())
                                                                                 : QJsonDocument(inputs[0].toArray()))
                                                  .toJson(QJsonDocument::Indented));
                }
                file.write(content.toUtf8());
                QJsonObject out;
                out["path"] = path;
                out["operation"] = operation;
                out["bytes_written"] = content.toUtf8().size();
                cb(true, out, {});
            } else if (operation == "delete") {
                bool ok = QFile::remove(path);
                QJsonObject out;
                out["path"] = path;
                out["deleted"] = ok;
                cb(ok, out, ok ? QString{} : "Failed to delete file");
            } else if (operation == "exists") {
                QJsonObject out;
                out["path"] = path;
                out["exists"] = QFile::exists(path);
                cb(true, out, {});
            } else {
                cb(false, {}, "Unknown file operation: " + operation);
            }
        };
    }

    // ── File convert — export data as JSON/CSV ────────────────────
    auto* convert_def = const_cast<NodeTypeDef*>(registry.find("file.convert"));
    if (convert_def && !convert_def->execute) {
        convert_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                                  std::function<void(bool, QJsonValue, QString)> cb) {
            QString format = params.value("format").toString("json");
            QString path = params.value("path").toString();
            if (path.isEmpty()) {
                cb(false, {}, "Output path is required");
                return;
            }
            if (inputs.isEmpty()) {
                cb(false, {}, "No input data to convert");
                return;
            }

            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                cb(false, {}, QString("Cannot open: %1").arg(file.errorString()));
                return;
            }

            if (format == "json") {
                QJsonDocument doc =
                    inputs[0].isObject() ? QJsonDocument(inputs[0].toObject()) : QJsonDocument(inputs[0].toArray());
                file.write(doc.toJson(QJsonDocument::Indented));
            } else if (format == "csv") {
                // Convert JSON array of objects to CSV
                QString csv;
                if (inputs[0].isArray()) {
                    QJsonArray arr = inputs[0].toArray();
                    if (!arr.isEmpty() && arr[0].isObject()) {
                        QStringList headers = arr[0].toObject().keys();
                        csv += headers.join(",") + "\n";
                        for (const QJsonValue& row : arr) {
                            QStringList vals;
                            for (const QString& h : headers) {
                                QJsonValue v = row.toObject().value(h);
                                // Missing/null cells stay empty — never written as a fake 0.
                                QString s;
                                if (v.isString())
                                    s = "\"" + v.toString().replace("\"", "\"\"") + "\"";
                                else if (v.isDouble())
                                    s = QString::number(v.toDouble(), 'g', 15);
                                else if (v.isBool())
                                    s = v.toBool() ? "true" : "false";
                                vals << s;
                            }
                            csv += vals.join(",") + "\n";
                        }
                    }
                }
                file.write(csv.toUtf8());
            } else {
                file.write(QString::fromUtf8(QJsonDocument(inputs[0].isObject() ? QJsonDocument(inputs[0].toObject())
                                                                                : QJsonDocument(inputs[0].toArray()))
                                                 .toJson(QJsonDocument::Compact))
                               .toUtf8());
            }
            file.close();

            QJsonObject out;
            out["path"] = path;
            out["format"] = format;
            out["size"] = QFileInfo(path).size();
            cb(true, out, {});
        };
    }

    // ── File binary — read/write raw bytes ────────────────────────
    auto* binary_def = const_cast<NodeTypeDef*>(registry.find("file.binary"));
    if (binary_def && !binary_def->execute) {
        binary_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&,
                                 std::function<void(bool, QJsonValue, QString)> cb) {
            QString op = params.value("operation").toString("read");
            QString path = params.value("path").toString();
            if (path.isEmpty()) {
                cb(false, {}, "File path is required");
                return;
            }

            if (op == "read") {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) {
                    cb(false, {}, "Cannot open file: " + file.errorString());
                    return;
                }
                QByteArray data = file.readAll();
                QJsonObject out;
                out["path"] = path;
                out["size"] = data.size();
                out["base64"] = QString::fromLatin1(data.toBase64());
                cb(true, out, {});
            } else {
                // Only 'read' is implemented — never report a write that didn't happen.
                cb(false, {}, QString("File binary: operation '%1' is not implemented").arg(op));
            }
        };
    }

    // ── File compress — zip/gzip ──────────────────────────────────
    auto* compress_def = const_cast<NodeTypeDef*>(registry.find("file.compress"));
    if (compress_def && !compress_def->execute) {
        compress_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&,
                                   std::function<void(bool, QJsonValue, QString)> cb) {
            QString path = params.value("path").toString();
            if (path.isEmpty()) {
                cb(false, {}, "File path is required");
                return;
            }

            // Use qCompress for basic gzip-style compression
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                cb(false, {}, "Cannot open file: " + file.errorString());
                return;
            }
            QByteArray data = file.readAll();
            QByteArray compressed = qCompress(data);
            QString out_path = path + ".qz";
            QFile out_file(out_path);
            if (!out_file.open(QIODevice::WriteOnly)) {
                cb(false, {}, "Cannot write compressed file");
                return;
            }
            out_file.write(compressed);

            QJsonObject out;
            out["input_path"] = path;
            out["output_path"] = out_path;
            out["original_size"] = data.size();
            out["compressed_size"] = compressed.size();
            cb(true, out, {});
        };
    }

    // ── HTML Extract — regex-based extraction ─────────────────────
    auto* html_def = const_cast<NodeTypeDef*>(registry.find("format.html_extract"));
    if (html_def && !html_def->execute) {
        html_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                               std::function<void(bool, QJsonValue, QString)> cb) {
            if (inputs.isEmpty()) {
                cb(false, {}, "No HTML input");
                return;
            }
            QString html;
            if (inputs[0].isString())
                html = inputs[0].toString();
            else if (inputs[0].isObject())
                html = inputs[0].toObject().value("content").toString(inputs[0].toObject().value("data").toString());
            if (html.isEmpty()) {
                cb(false, {}, "No HTML content found in input");
                return;
            }

            QString selector = params.value("selector").toString();
            QString attr = params.value("attribute").toString("text");

            // Basic tag extraction — match tags by element name from selector
            // e.g. selector "div" extracts all <div>...</div> content
            QString tag_name = selector.section('.', 0, 0).section('#', 0, 0).trimmed();
            if (tag_name.isEmpty())
                tag_name = "div";

            QRegularExpression re(QString("<%1[^>]*>(.*?)</%1>").arg(QRegularExpression::escape(tag_name)),
                                  QRegularExpression::DotMatchesEverythingOption);
            auto it = re.globalMatch(html);
            QJsonArray results;
            while (it.hasNext()) {
                auto m = it.next();
                results.append(m.captured(1).trimmed());
            }

            QJsonObject out;
            out["selector"] = selector;
            out["matches"] = results;
            out["count"] = results.size();
            cb(true, out, {});
        };
    }

    // ── RSS Read — fetch and parse RSS/Atom XML ───────────────────
    auto* rss_def = const_cast<NodeTypeDef*>(registry.find("utility.rss_read"));
    if (rss_def && !rss_def->execute) {
        rss_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&,
                              std::function<void(bool, QJsonValue, QString)> cb) {
            QString url = params.value("url").toString();
            if (url.isEmpty()) {
                cb(false, {}, "RSS URL is required");
                return;
            }

            HttpClient::instance().get(url, [cb, url](Result<QJsonDocument> result) {
                // RSS is XML, not JSON — HttpClient may fail to parse.
                // Pass through what we get as a text result for downstream XML parsing.
                QJsonObject out;
                out["url"] = url;
                if (result.is_ok()) {
                    out["data"] = result.value().isObject() ? QJsonValue(result.value().object())
                                                            : QJsonValue(result.value().array());
                } else {
                    out["error"] = QString::fromStdString(result.error());
                    out["note"] = "RSS feeds are XML; connect to an XML parse node for structured data";
                }
                cb(true, out, {});
            });
        };
    }

    // ── Crypto/Hash — QCryptographicHash ──────────────────────────
    auto* crypto_def = const_cast<NodeTypeDef*>(registry.find("utility.crypto"));
    if (crypto_def && !crypto_def->execute) {
        crypto_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                                 std::function<void(bool, QJsonValue, QString)> cb) {
            QString op = params.value("operation").toString("sha256");
            QString input_str;
            if (!inputs.isEmpty()) {
                if (inputs[0].isString())
                    input_str = inputs[0].toString();
                else
                    input_str =
                        QString::fromUtf8(QJsonDocument(inputs[0].isObject() ? QJsonDocument(inputs[0].toObject())
                                                                             : QJsonDocument(inputs[0].toArray()))
                                              .toJson(QJsonDocument::Compact));
            }

            QCryptographicHash::Algorithm algo = QCryptographicHash::Sha256;
            if (op == "md5")
                algo = QCryptographicHash::Md5;
            else if (op == "sha1")
                algo = QCryptographicHash::Sha1;
            else if (op == "sha512")
                algo = QCryptographicHash::Sha512;

            QByteArray hash = QCryptographicHash::hash(input_str.toUtf8(), algo);

            QJsonObject out;
            out["algorithm"] = op;
            out["hash"] = QString::fromLatin1(hash.toHex());
            out["input_length"] = input_str.length();
            cb(true, out, {});
        };
    }

    // ── Database — SQLite via Qt SQL ──────────────────────────────
    auto* db_def = const_cast<NodeTypeDef*>(registry.find("utility.database"));
    if (db_def && !db_def->execute) {
        db_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>&,
                             std::function<void(bool, QJsonValue, QString)> cb) {
            QString op = params.value("operation").toString("query");
            QString query = params.value("query").toString();
            if (query.isEmpty()) {
                cb(false, {}, "SQL query is required");
                return;
            }

            QSqlDatabase db = QSqlDatabase::database();
            if (!db.isOpen()) {
                cb(false, {}, "No database connection available");
                return;
            }

            QSqlQuery q(db);
            if (!q.exec(query)) {
                cb(false, {}, "SQL error: " + q.lastError().text());
                return;
            }

            QJsonArray rows;
            QSqlRecord rec = q.record();
            while (q.next()) {
                QJsonObject row;
                for (int i = 0; i < rec.count(); ++i) {
                    QVariant val = q.value(i);
                    if (val.typeId() == QMetaType::Double || val.typeId() == QMetaType::Int ||
                        val.typeId() == QMetaType::LongLong)
                        row[rec.fieldName(i)] = val.toDouble();
                    else
                        row[rec.fieldName(i)] = val.toString();
                }
                rows.append(row);
            }

            QJsonObject out;
            out["query"] = query;
            out["row_count"] = rows.size();
            out["rows"] = rows;
            cb(true, out, {});
        };
    }

    // ── API Call — via HttpClient with auth ───────────────────────
    auto* api_def = const_cast<NodeTypeDef*>(registry.find("utility.api_call"));
    if (api_def && !api_def->execute) {
        api_def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                              std::function<void(bool, QJsonValue, QString)> cb) {
            QString url = params.value("url").toString();
            QString method = params.value("method").toString("GET");
            if (url.isEmpty()) {
                cb(false, {}, "URL is required");
                return;
            }

            auto& http = HttpClient::instance();
            auto handler = [cb, url, method](Result<QJsonDocument> result) {
                if (result.is_err()) {
                    cb(false, {}, QString::fromStdString(result.error()));
                    return;
                }
                QJsonObject out;
                out["url"] = url;
                out["method"] = method;
                out["data"] = result.value().isObject() ? QJsonValue(result.value().object())
                                                        : QJsonValue(result.value().array());
                cb(true, out, {});
            };

            if (method == "POST") {
                QJsonObject body = params.value("body").toObject();
                if (body.isEmpty() && !inputs.isEmpty() && inputs[0].isObject())
                    body = inputs[0].toObject();
                http.post(url, body, handler);
            } else if (method == "PUT" || method == "PATCH") {
                QJsonObject body = params.value("body").toObject();
                if (body.isEmpty() && !inputs.isEmpty() && inputs[0].isObject())
                    body = inputs[0].toObject();
                http.put(url, body, handler);
            } else if (method == "DELETE") {
                http.del(url, handler);
            } else {
                http.get(url, handler);
            }
        };
    }

    LOG_INFO("ServiceBridges", "Utility bridges wired");
}

static void wire_mcp_bridges(NodeRegistry& registry) {
    auto* def = const_cast<NodeTypeDef*>(registry.find("mcp.tool_call"));
    if (!def)
        return;

    def->execute = [](const QJsonObject& params, const QVector<QJsonValue>& inputs,
                      std::function<void(bool, QJsonValue, QString)> cb) {
        QString tool = params.value("tool").toString().trimmed();
        if (tool.isEmpty()) {
            cb(false, {}, "MCP Tool: 'tool' parameter is required");
            return;
        }

        // If input is from a Tool Picker node it carries { "tool": "...", "args": {...} }
        // — use those values when the param isn't explicitly set.
        QJsonObject input_obj;
        if (!inputs.isEmpty() && inputs[0].isObject())
            input_obj = inputs[0].toObject();

        if (tool.isEmpty())
            tool = input_obj.value("tool").toString().trimmed();
        if (tool.isEmpty()) {
            cb(false, {}, "MCP Tool: no tool selected — set the 'Tool' param or connect a Tool Picker node");
            return;
        }

        // Args: prefer "args" sub-object from Tool Picker, else pass the whole input object
        QJsonObject args;
        if (input_obj.contains("args") && input_obj.value("args").isObject())
            args = input_obj.value("args").toObject();
        else if (!input_obj.contains("tool"))
            args = input_obj; // plain data input, not a Tool Picker output

        (void)QtConcurrent::run([tool, args, cb]() {
            auto& svc = mcp::McpService::instance();
            mcp::ToolResult result;

            // Support both "serverId__toolName" and bare tool name
            if (tool.contains("__")) {
                result = svc.execute_openai_function(tool, args);
            } else {
                result = svc.execute_tool(mcp::INTERNAL_SERVER_ID, tool, args);
            }

            if (!result.success) {
                cb(false, {}, result.error.isEmpty() ? "MCP tool failed" : result.error);
                return;
            }

            // Return the result data; wrap primitive values in an object
            QJsonValue out =
                result.data.isNull() || result.data.isUndefined() ? QJsonValue(result.to_json()) : result.data;
            cb(true, out, {});
            LOG_DEBUG("McpBridge", QString("Tool '%1' executed successfully").arg(tool));
        });
    };

    LOG_INFO("ServiceBridges", "MCP bridge wired");
}

void wire_all_bridges(NodeRegistry& registry) {
    wire_mcp_bridges(registry);
    wire_market_data_bridges(registry);
    wire_trading_bridges(registry);
    wire_agent_bridges(registry);
    wire_utility_bridges(registry);

    // Wire any remaining nullptr executors with pass-through
    int wired_count = 0;
    for (const auto& def : registry.all()) {
        if (!def.execute) {
            auto* mutable_def = const_cast<NodeTypeDef*>(registry.find(def.type_id));
            if (mutable_def) {
                // No real implementation exists for this node. Fail loudly rather
                // than pass inputs through as a fake success (a "cancelled" order
                // or "generated" PDF that never happened).
                mutable_def->execute = [type_id = def.type_id](const QJsonObject&, const QVector<QJsonValue>&,
                                                               std::function<void(bool, QJsonValue, QString)> cb) {
                    cb(false, {}, QString("Node '%1' is not implemented").arg(type_id));
                };
                wired_count++;
            }
        }
    }

    LOG_INFO("ServiceBridges", QString("All bridges wired (%1 not-implemented fallbacks)").arg(wired_count));
}

} // namespace fincept::workflow
