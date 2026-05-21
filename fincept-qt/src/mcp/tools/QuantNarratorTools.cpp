// QuantNarratorTools.cpp — LLM-driven commentary on quant outputs.
//
// Tools are stateless: input is the result / trade object itself,
// not an id to look up.  Callers (typically `quant_critic` agent
// runs) fetch the data via existing tools first, then pass it in.
// Keeps these handlers cheap, decoupled from storage layout, and
// trivially testable.
//
// Each tool builds a focused system + user prompt and routes
// through ctx.sample.  When sampling isn't wired (production
// AgentService hasn't installed ctx.on_sample yet), the tools
// return a clear actionable error rather than silently failing.

#include "mcp/tools/QuantNarratorTools.h"

#include "core/logging/Logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>

#include <memory>

namespace fincept::mcp::tools {

namespace {

constexpr const char* TAG = "QuantNarratorTools";

QString json_to_string(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

/// Common dispatch — assembles a SampleRequest, calls ctx.sample,
/// wraps the response as a ToolResult.  Returns a clear actionable
/// error when sampling isn't wired so the user knows what to fix.
ToolResult sample_and_wrap(ToolContext ctx, const QString& system_prompt,
                            const QString& user_prompt, int max_tokens = 768,
                            double temperature = 0.4) {
    SampleRequest req;
    req.system_prompt = system_prompt;
    req.prompt = user_prompt;
    req.max_tokens = max_tokens;
    req.temperature = temperature;

    const SampleResponse resp = ctx.sample(req);
    if (!resp.error.isEmpty()) {
        // Surface the "not wired" path with a one-liner the user can
        // act on.  Anything else (rate limit, model error) passes
        // through as-is.
        if (resp.error.contains("not wired")) {
            return ToolResult::fail(
                "LLM narration unavailable: " + resp.error +
                ".  Wire ctx.on_sample on the active profile (Anthropic "
                "Agent SDK exposes this automatically; local-runtime "
                "support lands with Track 2).");
        }
        return ToolResult::fail("sample failed: " + resp.error);
    }
    QJsonObject data{
        {"narration", resp.text},
        {"model", resp.model},
    };
    return ToolResult::ok(resp.text, data);
}

} // anonymous namespace

std::vector<ToolDef> get_quant_narrator_tools() {
    std::vector<ToolDef> tools;
    tools.reserve(3);

    // ── narrate_backtest_result ───────────────────────────────────────
    {
        ToolDef t;
        t.name = "narrate_backtest_result";
        t.description =
            "Generate English commentary on a backtest run — what worked, "
            "what went wrong, signal quality, regime sensitivity.  Input "
            "is the full backtest result object (call run_quant_module / "
            "backtest tools first to get it).  Stateless — does not "
            "load from storage.";
        t.category = "quant-narrate";
        t.input_schema.properties = QJsonObject{
            {"result", QJsonObject{
                {"type", "object"},
                {"description", "Full backtest result object (metrics, "
                                "params, equity curve summary, etc.)"}}},
            {"focus", QJsonObject{
                {"type", "string"},
                {"description", "Optional focus area — 'drawdown', "
                                "'sharpe', 'regime', 'turnover'.  "
                                "Defaults to a general overview."}}},
        };
        t.input_schema.required = {"result"};
        // async_handler shape because we need ToolContext for ctx.sample;
        // ctx isn't threaded through the sync handler signature.
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QJsonObject result = args.value("result").toObject();
            if (result.isEmpty()) {
                promise->addResult(ToolResult::fail(
                    "Pass the backtest result object via `result`."));
                promise->finish();
                return;
            }
            const QString focus = args.value("focus").toString().trimmed();

            QString system =
                "You are a quant critic reviewing a backtest run.  Write "
                "<= 180 words.  Identify the strongest one or two findings "
                "and the most concerning weakness.  Be concrete about "
                "magnitudes — cite Sharpe, drawdown, win rate, turnover "
                "where they appear in the data.  Avoid hedging language "
                "without numbers behind it.";
            if (!focus.isEmpty())
                system += "  Focus the commentary on: " + focus + ".";

            const QString user =
                "Backtest result:\n```json\n" + json_to_string(result) +
                "\n```\n\nWrite the commentary now.";

            promise->addResult(sample_and_wrap(std::move(ctx), system, user));
            promise->finish();
        };
        tools.push_back(std::move(t));
    }

    // ── propose_param_sweep ───────────────────────────────────────────
    {
        ToolDef t;
        t.name = "propose_param_sweep";
        t.description =
            "Given a backtest result, propose a parameter sweep — which "
            "params to vary, over what ranges, and why.  Stateless.";
        t.category = "quant-narrate";
        t.input_schema.properties = QJsonObject{
            {"result", QJsonObject{
                {"type", "object"},
                {"description", "Full backtest result object — must include "
                                "the params used and the primary metrics."}}},
            {"target_metric", QJsonObject{
                {"type", "string"},
                {"description", "Metric to optimise — 'sharpe', 'calmar', "
                                "'sortino', 'cagr'.  Defaults to sharpe."}}},
        };
        t.input_schema.required = {"result"};
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QJsonObject result = args.value("result").toObject();
            if (result.isEmpty()) {
                promise->addResult(ToolResult::fail(
                    "Pass the backtest result object via `result`."));
                promise->finish();
                return;
            }
            const QString target = args.value("target_metric").toString().trimmed();
            const QString target_clause = target.isEmpty()
                ? QStringLiteral("sharpe")
                : target;

            const QString system =
                "You are a quant researcher proposing the next iteration of "
                "a strategy.  Output a JSON array of param sweeps to run; "
                "each entry is {param, range: [min, max, step], rationale}.  "
                "Cap at 5 sweeps.  Prefer ranges around the current value "
                "(±50%) unless the result suggests the optimum is far away.  "
                "Optimise for " + target_clause + ".  No preamble — start "
                "with `[` directly.";
            const QString user =
                "Current backtest result:\n```json\n" + json_to_string(result) +
                "\n```\n\nPropose the next sweep.";

            promise->addResult(
                sample_and_wrap(std::move(ctx), system, user, /*max_tokens=*/512));
            promise->finish();
        };
        tools.push_back(std::move(t));
    }

    // ── narrate_live_trade ────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "narrate_live_trade";
        t.description =
            "Generate English commentary on a single executed trade — was "
            "it consistent with the strategy's signal, how it sits against "
            "the position's basis, what to watch next.  Stateless.";
        t.category = "quant-narrate";
        t.input_schema.properties = QJsonObject{
            {"trade", QJsonObject{
                {"type", "object"},
                {"description", "Trade record — symbol, side, qty, price, "
                                "timestamp, and any signal / strategy "
                                "context the caller wants surfaced."}}},
            {"position", QJsonObject{
                {"type", "object"},
                {"description", "Optional current position context "
                                "(shares, avg cost, unrealized PnL)."}}},
        };
        t.input_schema.required = {"trade"};
        t.async_handler = [](const QJsonObject& args, ToolContext ctx,
                              std::shared_ptr<QPromise<ToolResult>> promise) {
            const QJsonObject trade = args.value("trade").toObject();
            if (trade.isEmpty()) {
                promise->addResult(ToolResult::fail(
                    "Pass the trade record via `trade`."));
                promise->finish();
                return;
            }
            const QJsonObject pos = args.value("position").toObject();

            const QString system =
                "You are a quant critic reviewing a single executed trade.  "
                "Write <= 120 words.  State whether the trade was "
                "consistent with the strategy's stated signal, how it sits "
                "against the current position (if provided), and one "
                "specific thing to watch next.  No hedging without numbers.";

            QString user = "Trade:\n```json\n" + json_to_string(trade) + "\n```";
            if (!pos.isEmpty())
                user += "\n\nPosition context:\n```json\n" + json_to_string(pos) + "\n```";
            user += "\n\nWrite the commentary now.";

            promise->addResult(
                sample_and_wrap(std::move(ctx), system, user, /*max_tokens=*/512));
            promise->finish();
        };
        tools.push_back(std::move(t));
    }

    LOG_INFO(TAG, QString("Registered %1 quant-narrator tools").arg(tools.size()));
    return tools;
}

} // namespace fincept::mcp::tools
