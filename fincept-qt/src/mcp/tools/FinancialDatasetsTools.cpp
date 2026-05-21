// FinancialDatasetsTools.cpp — financialdatasets.ai REST wrappers
//
// Six tools covering the surfaces upstream's MCP server doesn't expose:
//   get_sec_filing_section        /filings/section
//   get_insider_trades            /insider-trades
//   get_institutional_holdings    /institutional-ownership
//   get_segmented_financials      /financials/segmented
//   get_operational_kpis          /financials/kpis
//   get_earnings                  /earnings
//
// Pattern: file-local QNetworkAccessManager (shared across calls);
// API key fetched from SecureStorage at request time; async_handler
// resolves the QPromise<ToolResult> when the reply finishes.  GET-only
// in this revision; financial-datasets is a read-only API.

#include "mcp/tools/FinancialDatasetsTools.h"

#include "core/logging/Logger.h"
#include "storage/secure/SecureStorage.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPromise>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

namespace fincept::mcp::tools {

namespace {

constexpr const char* TAG = "FinancialDatasetsTools";
constexpr const char* kBaseUrl = "https://api.financialdatasets.ai";
constexpr const char* kSecureKey = "mcp.tools.financial-datasets";
constexpr int kRequestTimeoutMs = 30000;

/// File-local QNAM shared across all tools in this family.  Parented to
/// qApp so it lives as long as the process and is destroyed before
/// QCoreApplication::exit completes (avoids dangling on shutdown).
QNetworkAccessManager* fd_nam() {
    static QNetworkAccessManager* nam = [] {
        auto* m = new QNetworkAccessManager(qApp);
        m->setObjectName(QStringLiteral("FinancialDatasetsNam"));
        return m;
    }();
    return nam;
}

QString fd_api_key() {
    auto r = SecureStorage::instance().retrieve(QString::fromUtf8(kSecureKey));
    return r.is_ok() ? r.value() : QString();
}

/// Async GET helper.  Calls `done` exactly once with the parsed JSON
/// object or an error.  Auth header X-API-KEY is sourced from
/// SecureStorage; missing key short-circuits to a clear error.
void fd_get(const QString& path,
            const QHash<QString, QString>& params,
            std::function<void(Result<QJsonObject>)> done) {
    const QString api_key = fd_api_key();
    if (api_key.isEmpty()) {
        done(Result<QJsonObject>::err(
            "financialdatasets.ai API key not configured.  Store the key in "
            "SecureStorage under id 'mcp.tools.financial-datasets' "
            "(free tier at financialdatasets.ai)"));
        return;
    }

    QUrl url(QString::fromUtf8(kBaseUrl) + path);
    if (!params.isEmpty()) {
        QUrlQuery q;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            q.addQueryItem(it.key(), it.value());
        url.setQuery(q);
    }

    QNetworkRequest req(url);
    req.setRawHeader("X-API-KEY", api_key.toUtf8());
    req.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = fd_nam()->get(req);

    // Hard timeout — financialdatasets.ai is normally fast, but a
    // hung reply would otherwise leak the QPromise.
    QTimer::singleShot(kRequestTimeoutMs, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [reply, done = std::move(done), path]() {
                         const QByteArray body = reply->readAll();
                         const int status =
                             reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         const auto err = reply->error();
                         reply->deleteLater();

                         if (err != QNetworkReply::NoError) {
                             done(Result<QJsonObject>::err(
                                 QString("network error %1 on %2 (status=%3): %4")
                                     .arg(static_cast<int>(err))
                                     .arg(path)
                                     .arg(status)
                                     .arg(QString::fromUtf8(body).left(200))
                                     .toStdString()));
                             return;
                         }
                         QJsonParseError perr;
                         const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
                         if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
                             done(Result<QJsonObject>::err(
                                 ("invalid JSON response: " + perr.errorString())
                                     .toStdString()));
                             return;
                         }
                         done(Result<QJsonObject>::ok(doc.object()));
                     });
}

/// Glue: bridge fd_get's callback to a QPromise<ToolResult>.
void fd_dispatch(const QString& path, const QHash<QString, QString>& params,
                 std::shared_ptr<QPromise<ToolResult>> promise) {
    fd_get(path, params, [promise](Result<QJsonObject> r) {
        if (r.is_err()) {
            promise->addResult(ToolResult::fail(QString::fromStdString(r.error())));
        } else {
            promise->addResult(ToolResult::ok_data(r.value()));
        }
        promise->finish();
    });
}

QString req_string(const QJsonObject& args, const char* key) {
    return args.value(QString::fromUtf8(key)).toString().trimmed();
}

} // anonymous namespace

std::vector<ToolDef> get_financial_datasets_tools() {
    std::vector<ToolDef> tools;
    tools.reserve(6);

    // ── get_sec_filing_section ────────────────────────────────────────
    {
        ToolDef t;
        t.name = "fd_get_sec_filing_section";
        t.description = "Fetch a specific section (e.g. Item 1A risk factors, "
                        "Item 7 MD&A) from a 10-K / 10-Q / 8-K filing.  "
                        "Section-level access not in upstream's MCP server.";
        t.category = "finance";
        t.default_timeout_ms = kRequestTimeoutMs;
        t.input_schema.properties = QJsonObject{
            {"ticker", QJsonObject{{"type", "string"},
                                   {"description", "Ticker symbol (e.g. AAPL)"}}},
            {"filing_type", QJsonObject{{"type", "string"},
                                        {"description", "10-K | 10-Q | 8-K"}}},
            {"item", QJsonObject{{"type", "string"},
                                 {"description", "Section identifier (e.g. \"1A\", \"7\")"}}},
            {"year", QJsonObject{{"type", "integer"},
                                 {"description", "Filing year (optional; defaults to latest)"}}},
        };
        t.input_schema.required = {"ticker", "filing_type", "item"};
        t.async_handler = [](const QJsonObject& args, ToolContext /*ctx*/,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            QHash<QString, QString> params{
                {"ticker", req_string(args, "ticker").toUpper()},
                {"filing_type", req_string(args, "filing_type")},
                {"item", req_string(args, "item")},
            };
            const int year = args.value("year").toInt(0);
            if (year > 0)
                params.insert("year", QString::number(year));
            fd_dispatch("/filings/section", params, promise);
        };
        tools.push_back(std::move(t));
    }

    // ── get_insider_trades ────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "fd_get_insider_trades";
        t.description = "Pre-normalized Form 4 insider transactions for a "
                        "ticker — buys, sells, share counts, prices.";
        t.category = "finance";
        t.default_timeout_ms = kRequestTimeoutMs;
        t.input_schema.properties = QJsonObject{
            {"ticker", QJsonObject{{"type", "string"},
                                   {"description", "Ticker symbol"}}},
            {"limit", QJsonObject{{"type", "integer"},
                                  {"description", "Max rows (default 100)"}}},
        };
        t.input_schema.required = {"ticker"};
        t.async_handler = [](const QJsonObject& args, ToolContext /*ctx*/,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            QHash<QString, QString> params{
                {"ticker", req_string(args, "ticker").toUpper()},
            };
            const int limit = args.value("limit").toInt(100);
            params.insert("limit", QString::number(limit));
            fd_dispatch("/insider-trades", params, promise);
        };
        tools.push_back(std::move(t));
    }

    // ── get_institutional_holdings ────────────────────────────────────
    {
        ToolDef t;
        t.name = "fd_get_institutional_holdings";
        t.description = "Pre-normalized 13-F institutional holdings for a "
                        "ticker (or for a specific filer's full portfolio).";
        t.category = "finance";
        t.default_timeout_ms = kRequestTimeoutMs;
        t.input_schema.properties = QJsonObject{
            {"ticker", QJsonObject{{"type", "string"},
                                   {"description", "Ticker (omit for filer-based query)"}}},
            {"investor", QJsonObject{{"type", "string"},
                                     {"description", "Investor/filer name "
                                                     "(e.g. \"Berkshire Hathaway\")"}}},
            {"limit", QJsonObject{{"type", "integer"},
                                  {"description", "Max rows (default 100)"}}},
        };
        t.async_handler = [](const QJsonObject& args, ToolContext /*ctx*/,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            QHash<QString, QString> params;
            const QString ticker = req_string(args, "ticker").toUpper();
            if (!ticker.isEmpty())
                params.insert("ticker", ticker);
            const QString investor = req_string(args, "investor");
            if (!investor.isEmpty())
                params.insert("investor", investor);
            const int limit = args.value("limit").toInt(100);
            params.insert("limit", QString::number(limit));
            if (params.isEmpty() ||
                (params.size() == 1 && params.contains("limit"))) {
                promise->addResult(ToolResult::fail(
                    "Provide at least one of `ticker` or `investor`."));
                promise->finish();
                return;
            }
            fd_dispatch("/institutional-ownership", params, promise);
        };
        tools.push_back(std::move(t));
    }

    // ── get_segmented_financials ──────────────────────────────────────
    {
        ToolDef t;
        t.name = "fd_get_segmented_financials";
        t.description = "Business + geographic segment-level revenue / "
                        "operating income — what raw XBRL has but isn't "
                        "first-class in finterm's existing tools.";
        t.category = "finance";
        t.default_timeout_ms = kRequestTimeoutMs;
        t.input_schema.properties = QJsonObject{
            {"ticker", QJsonObject{{"type", "string"},
                                   {"description", "Ticker symbol"}}},
            {"period", QJsonObject{{"type", "string"},
                                   {"description", "annual | quarterly (default annual)"}}},
            {"limit", QJsonObject{{"type", "integer"},
                                  {"description", "Max periods (default 5)"}}},
        };
        t.input_schema.required = {"ticker"};
        t.async_handler = [](const QJsonObject& args, ToolContext /*ctx*/,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            QHash<QString, QString> params{
                {"ticker", req_string(args, "ticker").toUpper()},
                {"period", req_string(args, "period").isEmpty()
                               ? QStringLiteral("annual")
                               : req_string(args, "period")},
            };
            const int limit = args.value("limit").toInt(5);
            params.insert("limit", QString::number(limit));
            fd_dispatch("/financials/segmented", params, promise);
        };
        tools.push_back(std::move(t));
    }

    // ── get_operational_kpis ──────────────────────────────────────────
    {
        ToolDef t;
        t.name = "fd_get_operational_kpis";
        t.description = "Company-specific operational KPIs (subscriber counts, "
                        "ARPU, store count, etc.) + non-GAAP / guidance "
                        "figures — surface entirely missing from finterm's "
                        "existing tools today.";
        t.category = "finance";
        t.default_timeout_ms = kRequestTimeoutMs;
        t.input_schema.properties = QJsonObject{
            {"ticker", QJsonObject{{"type", "string"},
                                   {"description", "Ticker symbol"}}},
            {"period", QJsonObject{{"type", "string"},
                                   {"description", "annual | quarterly"}}},
            {"limit", QJsonObject{{"type", "integer"},
                                  {"description", "Max periods (default 5)"}}},
        };
        t.input_schema.required = {"ticker"};
        t.async_handler = [](const QJsonObject& args, ToolContext /*ctx*/,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            QHash<QString, QString> params{
                {"ticker", req_string(args, "ticker").toUpper()},
                {"period", req_string(args, "period").isEmpty()
                               ? QStringLiteral("quarterly")
                               : req_string(args, "period")},
            };
            const int limit = args.value("limit").toInt(5);
            params.insert("limit", QString::number(limit));
            fd_dispatch("/financials/kpis", params, promise);
        };
        tools.push_back(std::move(t));
    }

    // ── get_earnings ──────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "fd_get_earnings";
        t.description = "Earnings estimates, surprises, and history "
                        "(consensus vs reported, beat/miss).  Partially "
                        "available via FMP in finterm today — financial-"
                        "datasets gives a cleaner LLM-shaped schema.";
        t.category = "finance";
        t.default_timeout_ms = kRequestTimeoutMs;
        t.input_schema.properties = QJsonObject{
            {"ticker", QJsonObject{{"type", "string"},
                                   {"description", "Ticker symbol"}}},
            {"limit", QJsonObject{{"type", "integer"},
                                  {"description", "Max past quarters (default 8)"}}},
        };
        t.input_schema.required = {"ticker"};
        t.async_handler = [](const QJsonObject& args, ToolContext /*ctx*/,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            QHash<QString, QString> params{
                {"ticker", req_string(args, "ticker").toUpper()},
            };
            const int limit = args.value("limit").toInt(8);
            params.insert("limit", QString::number(limit));
            fd_dispatch("/earnings", params, promise);
        };
        tools.push_back(std::move(t));
    }

    LOG_INFO(TAG, QString("Registered %1 financial-datasets tools").arg(tools.size()));
    return tools;
}

} // namespace fincept::mcp::tools
