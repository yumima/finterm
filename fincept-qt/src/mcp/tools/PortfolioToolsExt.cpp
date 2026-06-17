// PortfolioToolsExt.cpp — Investment portfolio holdings MCP tools (Qt port)
// Sourced from the canonical multi-portfolio schema (PortfolioRepository →
// portfolios + portfolio_assets). Holdings tools default to the FIRST portfolio
// when no portfolio_id is supplied. The legacy flat `portfolio_holdings` table
// is no longer read or written here.
//
// NOTE: get_investment_portfolio_tools() is currently not registered by
// McpInit.cpp (only get_portfolio_tools() is). It is kept in sync with the
// canonical layer so it stays correct if ever wired up.

#include "mcp/tools/PortfolioToolsExt.h"

#include "core/logging/Logger.h"
#include "storage/repositories/PortfolioRepository.h"

namespace fincept::mcp::tools {

static constexpr const char* TAG = "PortfolioExtTools";

namespace {

// See PortfolioTools.cpp for the canonical version of these helpers; duplicated
// here (file-local) to avoid a header dependency between the two tool TUs.
QString resolve_portfolio_id(const QString& requested, QString& err) {
    if (!requested.trimmed().isEmpty())
        return requested.trimmed();

    auto r = PortfolioRepository::instance().list_portfolios();
    if (r.is_err()) {
        err = "Failed to list portfolios: " + QString::fromStdString(r.error());
        return {};
    }
    if (r.value().isEmpty()) {
        err = "No portfolio exists — create one first, then retry.";
        return {};
    }
    return r.value().first().id;
}

QString symbol_for_asset_id(const QString& portfolio_id, int asset_id, QString& err) {
    auto r = PortfolioRepository::instance().get_assets(portfolio_id);
    if (r.is_err()) {
        err = "Failed to load assets: " + QString::fromStdString(r.error());
        return {};
    }
    for (const auto& a : r.value())
        if (a.id == asset_id)
            return a.symbol;
    err = QString("No holding with id %1 in portfolio %2").arg(asset_id).arg(portfolio_id);
    return {};
}

} // namespace

std::vector<ToolDef> get_investment_portfolio_tools() {
    std::vector<ToolDef> tools;

    // ── get_holdings ───────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "get_holdings";
        t.description = "Get all holdings (symbol, shares, avg cost) for a portfolio. "
                        "Omit portfolio_id to use the first portfolio.";
        t.category = "portfolio";
        t.input_schema.properties = QJsonObject{
            {"portfolio_id",
             QJsonObject{{"type", "string"}, {"description", "Portfolio ID (optional; defaults to first portfolio)"}}}};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString err;
            QString portfolio_id = resolve_portfolio_id(args["portfolio_id"].toString(), err);
            if (portfolio_id.isEmpty())
                return ToolResult::fail(err);

            auto result = PortfolioRepository::instance().get_assets(portfolio_id);
            if (result.is_err())
                return ToolResult::fail("Failed to load holdings: " + QString::fromStdString(result.error()));

            QJsonArray arr;
            for (const auto& a : result.value()) {
                arr.append(QJsonObject{{"id", a.id},
                                       {"symbol", a.symbol},
                                       {"name", a.symbol},
                                       {"shares", a.quantity},
                                       {"avg_cost", a.avg_buy_price},
                                       {"added_at", a.first_purchase_date},
                                       {"updated_at", a.last_updated}});
            }
            return ToolResult::ok_data(arr);
        };
        tools.push_back(std::move(t));
    }

    // ── add_holding ────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "add_holding";
        t.description = "Add or increase a position in a portfolio. Re-adding an existing symbol "
                        "increases the quantity and recomputes the weighted average cost. "
                        "Omit portfolio_id to use the first portfolio.";
        t.category = "portfolio";
        t.input_schema.properties =
            QJsonObject{{"portfolio_id",
                         QJsonObject{{"type", "string"},
                                     {"description", "Portfolio ID (optional; defaults to first portfolio)"}}},
                        {"symbol", QJsonObject{{"type", "string"}, {"description", "Ticker symbol (e.g. AAPL)"}}},
                        {"shares", QJsonObject{{"type", "number"}, {"description", "Number of shares/units"}}},
                        {"avg_cost", QJsonObject{{"type", "number"}, {"description", "Average cost per share/unit"}}}};
        t.input_schema.required = {"symbol", "shares", "avg_cost"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            QString symbol = args["symbol"].toString().trimmed().toUpper();
            double shares = args["shares"].toDouble(0.0);
            double avg_cost = args["avg_cost"].toDouble(0.0);

            if (symbol.isEmpty() || shares <= 0 || avg_cost <= 0)
                return ToolResult::fail("Missing or invalid: symbol, shares (>0), avg_cost (>0)");

            QString err;
            QString portfolio_id = resolve_portfolio_id(args["portfolio_id"].toString(), err);
            if (portfolio_id.isEmpty())
                return ToolResult::fail(err);

            auto r = PortfolioRepository::instance().add_asset(portfolio_id, symbol, shares, avg_cost);
            if (r.is_err())
                return ToolResult::fail("Failed to add holding: " + QString::fromStdString(r.error()));

            LOG_INFO(TAG, QString("Added holding: %1 x%2 @ %3 (portfolio %4)")
                              .arg(symbol).arg(shares).arg(avg_cost).arg(portfolio_id));

            return ToolResult::ok("Holding added", QJsonObject{{"id", static_cast<int>(r.value())},
                                                               {"portfolio_id", portfolio_id},
                                                               {"symbol", symbol},
                                                               {"shares", shares},
                                                               {"avg_cost", avg_cost}});
        };
        tools.push_back(std::move(t));
    }

    // ── update_holding ─────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "update_holding";
        t.description = "Set the absolute shares and average cost for an existing holding by ID "
                        "(the id from get_holdings). Omit portfolio_id to use the first portfolio.";
        t.category = "portfolio";
        t.input_schema.properties =
            QJsonObject{{"portfolio_id",
                         QJsonObject{{"type", "string"},
                                     {"description", "Portfolio ID (optional; defaults to first portfolio)"}}},
                        {"id", QJsonObject{{"type", "integer"}, {"description", "Holding ID (from get_holdings)"}}},
                        {"shares", QJsonObject{{"type", "number"}, {"description", "Updated share count"}}},
                        {"avg_cost", QJsonObject{{"type", "number"}, {"description", "Updated average cost"}}}};
        t.input_schema.required = {"id", "shares", "avg_cost"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            int id = args["id"].toInt(-1);
            double shares = args["shares"].toDouble(0.0);
            double avg_cost = args["avg_cost"].toDouble(0.0);

            if (id < 0 || shares <= 0 || avg_cost <= 0)
                return ToolResult::fail("Missing or invalid: id, shares (>0), avg_cost (>0)");

            QString err;
            QString portfolio_id = resolve_portfolio_id(args["portfolio_id"].toString(), err);
            if (portfolio_id.isEmpty())
                return ToolResult::fail(err);

            QString symbol = symbol_for_asset_id(portfolio_id, id, err);
            if (symbol.isEmpty())
                return ToolResult::fail(err);

            auto r = PortfolioRepository::instance().update_asset(portfolio_id, symbol, shares, avg_cost);
            if (r.is_err())
                return ToolResult::fail("Failed to update holding: " + QString::fromStdString(r.error()));

            return ToolResult::ok("Holding updated",
                                  QJsonObject{{"id", id}, {"portfolio_id", portfolio_id}, {"symbol", symbol}});
        };
        tools.push_back(std::move(t));
    }

    // ── remove_holding ─────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "remove_holding";
        t.description = "Remove a holding from a portfolio by ID (the id from get_holdings). "
                        "Omit portfolio_id to use the first portfolio.";
        t.category = "portfolio";
        t.input_schema.properties =
            QJsonObject{{"portfolio_id",
                         QJsonObject{{"type", "string"},
                                     {"description", "Portfolio ID (optional; defaults to first portfolio)"}}},
                        {"id", QJsonObject{{"type", "integer"}, {"description", "Holding ID (from get_holdings)"}}}};
        t.input_schema.required = {"id"};
        t.handler = [](const QJsonObject& args) -> ToolResult {
            int id = args["id"].toInt(-1);
            if (id < 0)
                return ToolResult::fail("Missing or invalid 'id'");

            QString err;
            QString portfolio_id = resolve_portfolio_id(args["portfolio_id"].toString(), err);
            if (portfolio_id.isEmpty())
                return ToolResult::fail(err);

            QString symbol = symbol_for_asset_id(portfolio_id, id, err);
            if (symbol.isEmpty())
                return ToolResult::fail(err);

            auto r = PortfolioRepository::instance().remove_asset(portfolio_id, symbol);
            if (r.is_err())
                return ToolResult::fail("Failed to remove holding: " + QString::fromStdString(r.error()));

            return ToolResult::ok("Holding removed",
                                  QJsonObject{{"id", id}, {"portfolio_id", portfolio_id}, {"symbol", symbol}});
        };
        tools.push_back(std::move(t));
    }

    return tools;
}

} // namespace fincept::mcp::tools
