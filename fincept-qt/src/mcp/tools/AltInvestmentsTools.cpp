// AltInvestmentsTools.cpp — Alternative Investments tab MCP tools (27 analyzers)

#include "mcp/tools/AltInvestmentsTools.h"

#include "core/logging/Logger.h"
#include "mcp/tools/ThreadHelper.h"
#include "python/PythonRunner.h"
#include "storage/cache/CacheManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <vector>

namespace fincept::mcp::tools {

static constexpr int kTimeoutMs = 30000;
static constexpr int kAltTtlSec = 10 * 60; // 10 min — deterministic given same inputs

// ── Shared async helper ──────────────────────────────────────────────────────
// Calls cli.py synchronously (blocks calling thread via QEventLoop).
// cli_args = full argv after the script path, e.g. {"high-yield", "--data", "{...}"}.
// Subcommand names and data keys MUST match scripts/Analytics/alternateInvestment/cli.py.

static QString extract_error(const QString& out) {
    const int start = out.indexOf('{');
    const int end = out.lastIndexOf('}');
    if (start < 0 || end <= start)
        return {};
    const auto doc = QJsonDocument::fromJson(out.mid(start, end - start + 1).toUtf8());
    return doc.isObject() ? doc.object().value("error").toString() : QString();
}

static ToolResult run_alt_sync(const QStringList& cli_args) {
    const QString cache_key = "alt:" + cli_args.join('\x1f');
    const QVariant cached = fincept::CacheManager::instance().get(cache_key);
    if (!cached.isNull()) {
        auto doc = QJsonDocument::fromJson(cached.toString().toUtf8());
        if (!doc.isNull() && doc.isObject())
            return ToolResult::ok_data(doc.object());
    }

    ToolResult tool_result = ToolResult::fail("Python runner failed to respond");

    // Marshal onto PythonRunner's thread (main). Worker-thread QEventLoop
    // would never be woken by main-thread QProcess::finished signals.
    auto* runner = &python::PythonRunner::instance();
    detail::run_async_wait(runner, [&](auto signal_done) {
        runner->run("Analytics/alternateInvestment/cli.py", cli_args,
                    [&, signal_done](const python::PythonResult& result) {
                        if (!result.success) {
                            // cli.py prints {"success":false,"error":...} and exits 1.
                            QString err = extract_error(result.output);
                            if (err.isEmpty())
                                err = result.error.isEmpty() ? "Analysis failed" : result.error;
                            tool_result = ToolResult::fail(err);
                            signal_done();
                            return;
                        }
                        // Extract JSON from stdout
                        QString out = result.output;
                        int start = out.indexOf('{');
                        int end = out.lastIndexOf('}');
                        if (start < 0 || end < 0 || end <= start) {
                            tool_result = ToolResult::fail("No JSON in output");
                            signal_done();
                            return;
                        }
                        QJsonParseError err;
                        auto doc = QJsonDocument::fromJson(out.mid(start, end - start + 1).toUtf8(), &err);
                        if (doc.isNull() || !doc.isObject()) {
                            tool_result = ToolResult::fail("Invalid JSON: " + err.errorString());
                            signal_done();
                            return;
                        }
                        auto obj = doc.object();
                        if (obj.contains("error") || (obj.contains("success") && !obj["success"].toBool(true))) {
                            tool_result = ToolResult::fail(obj["error"].toString("Analysis failed"));
                            signal_done();
                            return;
                        }
                        auto metrics = obj.contains("metrics") ? obj["metrics"].toObject() : obj;
                        // Analyzers report insufficient input inside metrics.
                        if (metrics.contains("error")) {
                            tool_result = ToolResult::fail(metrics["error"].toString("Analysis failed"));
                            signal_done();
                            return;
                        }
                        fincept::CacheManager::instance().put(
                            cache_key,
                            QVariant(QString::fromUtf8(QJsonDocument(metrics).toJson(QJsonDocument::Compact))),
                            kAltTtlSec, "alt_investments");
                        tool_result = ToolResult::ok_data(metrics);
                        signal_done();
                    });
    });

    return tool_result;
}

// ── Per-tool spec ────────────────────────────────────────────────────────────
// Parameter keys are passed through verbatim as cli.py's --data JSON. All
// rates/percentages are DECIMALS (0.05 = 5%). Required inputs are enforced
// here so the analyzer never falls back to a baked-in default value.

struct AltParam {
    const char* key;
    const char* type; // "number" | "string" | "array"
    const char* desc;
    bool required;
};

struct AltSpec {
    const char* tool;
    const char* cmd; // cli.py subcommand
    const char* desc;
    const char* methods; // allowed --method values (first = default)
    std::vector<AltParam> params;
    bool returns_mode = false; // performance/risk take --returns instead of --data
};

static ToolDef make_tool(const AltSpec& spec) {
    ToolDef t;
    t.name = spec.tool;
    t.description = QString::fromUtf8(spec.desc) +
                    " All rates are decimals (0.05 = 5%). Every required input must come from the caller; "
                    "nothing is defaulted.";
    t.category = "alt-investments";

    ToolSchema s;
    s.properties["method"] = QJsonObject{
        {"type", "string"},
        {"description", QString("Analysis method: %1 (default: first)").arg(QString::fromUtf8(spec.methods))}};
    if (!spec.returns_mode)
        s.properties["name"] = QJsonObject{{"type", "string"}, {"description", "Optional label for the position"}};
    for (const auto& p : spec.params) {
        QJsonObject prop{{"type", p.type}, {"description", QString::fromUtf8(p.desc)}};
        if (QString(p.type) == "array")
            prop["items"] = QJsonObject{};
        s.properties[p.key] = prop;
        if (p.required)
            s.required << p.key;
    }
    t.input_schema = s;

    const QString cmd = spec.cmd;
    const bool returns_mode = spec.returns_mode;
    std::vector<AltParam> params = spec.params;
    t.handler = [cmd, returns_mode, params](const QJsonObject& args) -> ToolResult {
        QStringList missing;
        for (const auto& p : params)
            if (p.required && (!args.contains(p.key) || args.value(p.key).isNull()))
                missing << p.key;
        if (!missing.isEmpty())
            return ToolResult::fail("Missing required input(s): " + missing.join(", "));

        QStringList argv{cmd};
        auto compact = [](const QJsonValue& v) {
            return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                                                 : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        };
        if (returns_mode) {
            argv << "--returns" << compact(args.value("returns"));
            if (args.contains("benchmark"))
                argv << "--benchmark" << compact(args.value("benchmark"));
            if (args.contains("confidence_level"))
                argv << "--confidence-level" << QString::number(args.value("confidence_level").toDouble());
        } else {
            QJsonObject data;
            if (args.contains("name"))
                data["name"] = args.value("name");
            for (const auto& p : params)
                if (args.contains(p.key))
                    data[p.key] = args.value(p.key);
            argv << "--data" << QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact));
        }
        const QString method = args.value("method").toString();
        if (!method.isEmpty())
            argv << "--method" << method;
        return run_alt_sync(argv);
    };
    return t;
}

// ── Tool registration ────────────────────────────────────────────────────────

std::vector<ToolDef> get_alt_investments_tools() {
    const std::vector<AltSpec> specs = {
        // ── Bonds & Fixed Income ─────────────────────────────────────────────
        {"alt_high_yield_bond", "high-yield",
         "Analyze a high-yield (junk) bond: yield to maturity, spread over a caller-supplied Treasury yield, "
         "default probability, equity-like behavior.",
         "credit_analysis, default_prob, equity_behavior",
         {{"face_value", "number", "Face/par value ($)", true},
          {"current_market_value", "number", "Current market price ($)", true},
          {"coupon_rate", "number", "Annual coupon rate (decimal)", true},
          {"maturity_years", "number", "Years to maturity", true},
          {"treasury_yield", "number", "Benchmark Treasury yield (decimal) — required for credit_analysis", false},
          {"credit_rating", "string", "Credit rating, e.g. BB, B, CCC", false},
          {"equity_returns", "array", "Equity returns (decimals) for equity_behavior", false}}},
        {"alt_emerging_market_bond", "em-bonds",
         "Analyze an emerging-market bond: yield metrics, sovereign default risk, currency risk.",
         "yield_spread, default_risk, currency_risk",
         {{"face_value", "number", "Face value ($)", true},
          {"current_market_value", "number", "Current market price ($)", true},
          {"coupon_rate", "number", "Annual coupon rate (decimal)", true},
          {"maturity_years", "number", "Years to maturity", true},
          {"credit_spread", "number", "Credit spread (decimal)", true},
          {"sovereign_rating", "string", "Sovereign rating", false},
          {"country", "string", "Issuer country", false},
          {"historical_default_rate", "number", "Historical default rate (decimal) for default_risk", false},
          {"recovery_rate", "number", "Recovery rate (decimal) for default_risk", false},
          {"local_currency_volatility", "number", "FX volatility (decimal) for currency_risk", false},
          {"fx_correlation", "number", "FX correlation for currency_risk", false}}},
        {"alt_convertible_bond", "convertible-bonds",
         "Analyze a convertible bond: conversion premium, bond floor, upside participation.",
         "conversion_premium, bond_floor, upside_participation",
         {{"face_value", "number", "Face value ($)", true},
          {"current_market_value", "number", "Current bond price ($)", true},
          {"coupon_rate", "number", "Annual coupon rate (decimal)", true},
          {"maturity_years", "number", "Years to maturity", true},
          {"conversion_ratio", "number", "Shares per bond", true},
          {"stock_price", "number", "Current underlying stock price ($)", true},
          {"credit_spread", "number", "Issuer credit spread (decimal)", false},
          {"market_yield", "number", "Straight-bond market yield (decimal) for bond_floor", false},
          {"stock_price_scenarios", "array", "Stock price scenarios for upside_participation", false}}},
        {"alt_preferred_stock", "preferred-stocks",
         "Analyze preferred stock: current yield, yield to call, call risk, dividend safety.",
         "yield_analysis, call_risk, dividend_safety",
         {{"par_value", "number", "Par value ($)", true},
          {"current_price", "number", "Current market price ($)", true},
          {"dividend_rate", "number", "Dividend rate on par (decimal)", true},
          {"call_price", "number", "Call price ($)", false},
          {"years_to_call", "number", "Years to first call", false}}},

        // ── Real Estate ──────────────────────────────────────────────────────
        {"alt_real_estate", "real-estate",
         "Analyze a direct real-estate investment: NOI, cap rate, DCF valuation.",
         "noi, caprate, dcf",
         {{"acquisition_price", "number", "Purchase price ($)", true},
          {"gross_rental_income", "number", "Gross rental income ($/yr)", true},
          {"operating_expenses", "number", "Operating expenses ($/yr)", true},
          {"vacancy_rate", "number", "Vacancy rate (decimal)", true},
          {"current_market_value", "number", "Current market value ($)", false},
          {"projection_years", "number", "DCF projection years", false},
          {"terminal_cap_rate", "number", "DCF terminal cap rate (decimal)", false},
          {"discount_rate", "number", "DCF discount rate (decimal)", false}}},
        {"alt_reit", "intl-reit",
         "Analyze an international REIT: diversification, currency effect, expense drag, regional view.",
         "diversification, currency, expense, regional",
         {{"region", "string", "Region (Europe, Asia-Pacific, Emerging)", true},
          {"currency", "string", "REIT local currency (e.g. EUR)", true},
          {"local_return", "number", "Local-currency return (decimal)", true},
          {"currency_return", "number", "Currency return vs USD (decimal)", true},
          {"expense_ratio", "number", "Expense ratio (decimal)", true},
          {"correlation_us", "number", "Correlation with US REITs", true}}},

        // ── Hedge Funds ──────────────────────────────────────────────────────
        {"alt_long_short_equity", "hedge-funds",
         "Analyze a hedge fund from its own price history: strategy metrics, performance, fee impact.",
         "metrics, performance, fees",
         {{"market_data", "array", "Fund price history: [{timestamp, price}, ...]", true},
          {"strategy", "string", "Strategy label", false},
          {"management_fee", "number", "Management fee (decimal) for fees", false},
          {"performance_fee", "number", "Performance fee (decimal) for fees", false},
          {"hurdle_rate", "number", "Hurdle rate (decimal)", false},
          {"months", "number", "Months for fee impact", false}}},
        {"alt_managed_futures", "managed-futures",
         "Analyze a managed futures / CTA fund: trend-following on a supplied price series, crisis alpha, "
         "fee impact on a supplied gross return.",
         "trend_following, crisis_alpha, fee_impact",
         {{"management_fee", "number", "Management fee (decimal)", true},
          {"performance_fee", "number", "Performance fee (decimal)", true},
          {"price_series", "array", "Price series for trend_following", false},
          {"returns", "array", "Periodic fund returns (decimals)", false},
          {"crisis_periods", "array", "[{name,start,end,fund_return,market_return,bond_return}] for crisis_alpha",
           false},
          {"gross_return", "number", "Gross annual return (decimal) for fee_impact", false},
          {"years", "number", "Horizon years for fee_impact", false}}},
        {"alt_market_neutral", "market-neutral",
         "Analyze a market-neutral fund from supplied returns: beta, factor exposure, leverage risk.",
         "beta_analysis, factor_exposure, leverage_risk",
         {{"returns", "array", "Fund periodic returns (decimals)", true},
          {"long_exposure", "number", "Long exposure (x NAV)", true},
          {"short_exposure", "number", "Short exposure (x NAV)", true},
          {"management_fee", "number", "Management fee (decimal)", false},
          {"performance_fee", "number", "Performance fee (decimal)", false},
          {"market_returns", "array", "Market returns for beta_analysis", false},
          {"value_returns", "array", "Value factor returns", false},
          {"size_returns", "array", "Size factor returns", false},
          {"momentum_returns", "array", "Momentum factor returns", false}}},

        // ── Commodities ──────────────────────────────────────────────────────
        {"alt_precious_metals", "pme",
         "Analyze precious-metals equities from supplied return series: inflation correlation, drawdowns, "
         "crisis performance.",
         "correlation, drawdowns, crisis_performance",
         {{"stock_returns", "array", "PME periodic returns (decimals)", true},
          {"inflation_rates", "array", "Matching inflation rates (decimals)", true},
          {"market_data", "array", "Price history [{timestamp, price}] for drawdowns", false}}},
        {"alt_natural_resources", "natural-resources",
         "Analyze a commodity futures curve point: basis, contango/backwardation.",
         "basis, contango, futures",
         {{"spot_price", "number", "Spot price", true},
          {"futures_price", "number", "Futures price", true},
          {"expiry_months", "number", "Months to futures expiry", true},
          {"commodity_sector", "string", "energy, metals, agriculture, livestock", false}}},

        // ── Private Capital ──────────────────────────────────────────────────
        {"alt_private_equity", "private-capital",
         "Analyze a private equity / VC fund from its cash flows: IRR, MOIC, DPI, RVPI.",
         "metrics, irr, moic",
         {{"cash_flows", "array", "Cash flows [{date, amount}] (calls negative, distributions positive)", true},
          {"current_nav", "number", "Current NAV ($)", false},
          {"nav_date", "string", "NAV date (YYYY-MM-DD) — required with current_nav", false}}},

        // ── Annuities ────────────────────────────────────────────────────────
        {"alt_fixed_annuity", "annuities",
         "Analyze a fixed annuity: total payouts, inflation erosion, self-insurance comparison.",
         "payouts, inflation_erosion, self_insurance",
         {{"acquisition_price", "number", "Premium ($)", true},
          {"annuity_rate", "number", "Annual payout rate (decimal)", true},
          {"payout_years", "number", "Payout term (years)", true},
          {"inflation_rate", "number", "Inflation rate (decimal) for inflation_erosion", false},
          {"alternative_return", "number", "Alternative return (decimal) for self_insurance", false},
          {"withdrawal_rate", "number", "Withdrawal rate (decimal) for self_insurance", false}}},
        {"alt_variable_annuity", "variable-annuities",
         "Analyze a variable annuity: total fee drag, tax-deferral value, alternatives.",
         "fees, tax, alternatives, verdict",
         {{"premium", "number", "Premium ($)", true},
          {"me_fee", "number", "M&E fee (decimal)", true},
          {"investment_fee", "number", "Investment management fee (decimal)", true},
          {"admin_fee", "number", "Admin fee (decimal)", true},
          {"surrender_period", "number", "Surrender period (years)", false},
          {"years", "number", "Horizon years for tax", false},
          {"gross_return", "number", "Gross return (decimal) for tax", false}}},
        {"alt_equity_indexed_annuity", "eia",
         "Analyze an equity-indexed annuity: credited return, upside limitation, surrender charges.",
         "crediting, upside_limitation, surrender_charges",
         {{"index_return", "number", "Index return (decimal) for crediting", true},
          {"participation_rate", "number", "Participation rate (decimal)", true},
          {"cap_rate", "number", "Cap rate (decimal)", true},
          {"floor_rate", "number", "Floor rate (decimal)", true},
          {"acquisition_price", "number", "Premium ($)", false},
          {"market_scenarios", "array", "Index return scenarios for upside_limitation", false}}},
        {"alt_inflation_annuity", "inflation-annuity",
         "Analyze an inflation-indexed annuity vs a fixed annuity or TIPS ladder.",
         "compare_fixed, compare_tips, longevity, inflation_value",
         {{"real_payout_rate", "number", "Real payout rate (decimal)", true},
          {"inflation_rate", "number", "Assumed inflation (decimal)", true},
          {"payout_years", "number", "Payout term (years)", true},
          {"fixed_payout_rate", "number", "Fixed-annuity payout rate (decimal) for compare_fixed", false},
          {"tips_real_yield", "number", "TIPS real yield (decimal) for compare_tips", false}}},

        // ── Structured Products ──────────────────────────────────────────────
        {"alt_structured_note", "structured-products",
         "Analyze a structured note: complexity, embedded cost, verdict.",
         "complexity, costs, verdict",
         {{"principal", "number", "Principal ($)", true},
          {"participation_rate", "number", "Participation rate (decimal)", true},
          {"cap_rate", "number", "Upside cap (decimal)", true},
          {"maturity_years", "number", "Term (years)", true}}},
        {"alt_leveraged_fund", "leveraged-funds",
         "Analyze a leveraged ETF: volatility decay and long-hold suitability.",
         "decay, volatility, verdict",
         {{"leverage_ratio", "number", "Leverage multiple (e.g. 2, 3)", true},
          {"expense_ratio", "number", "Expense ratio (decimal)", false}}},

        // ── Inflation Protected ──────────────────────────────────────────────
        {"alt_tips", "tips",
         "Analyze a TIPS position: real yield, inflation scenarios, tax efficiency.",
         "real_yield, inflation_scenarios, tax_efficiency",
         {{"face_value", "number", "Face (principal) value ($)", true},
          {"current_market_value", "number", "Current market price ($)", true},
          {"coupon_rate", "number", "Real coupon rate (decimal)", true},
          {"maturity_years", "number", "Years to maturity", true},
          {"inflation_scenarios", "array", "Inflation scenarios (decimals) for inflation_scenarios", false},
          {"tax_rate_ordinary", "number", "Ordinary tax rate (decimal) for tax_efficiency", false},
          {"tax_rate_capital_gains", "number", "Capital-gains tax rate (decimal) for tax_efficiency", false}}},
        {"alt_ibond", "ibonds",
         "Analyze a Series I savings bond: composite rate, early-redemption penalty, vs TIPS.",
         "composite_rate, penalty, compare_tips, tax_efficiency",
         {{"purchase_price", "number", "Purchase amount ($)", true},
          {"fixed_rate", "number", "Fixed rate (decimal), from TreasuryDirect", true},
          {"inflation_rate", "number", "Semiannual CPI-U inflation rate (decimal), from TreasuryDirect", true},
          {"years_held", "number", "Years held", false},
          {"tips_yield", "number", "TIPS real yield (decimal) for compare_tips", false}}},
        {"alt_stable_value_fund", "stable-value",
         "Analyze a stable value fund: market-to-book, crediting rate, suitability.",
         "market_to_book, crediting_rate, suitability",
         {{"acquisition_price", "number", "Book value ($)", true},
          {"current_market_value", "number", "Market value ($)", true},
          {"crediting_rate", "number", "Crediting rate (decimal)", true},
          {"treasury_yield", "number", "Treasury yield (decimal) for crediting_rate", false},
          {"credit_spread", "number", "Credit spread (decimal) for crediting_rate", false}}},

        // ── Strategies ───────────────────────────────────────────────────────
        {"alt_covered_call", "covered-calls",
         "Analyze a covered call position: tax consequences, opportunity cost, alternatives.",
         "tax, opportunity_cost, alternative, verdict",
         {{"stock_price", "number", "Current stock price ($)", true},
          {"strike_price", "number", "Call strike ($)", true},
          {"option_premium", "number", "Premium per share ($)", true},
          {"shares_owned", "number", "Shares owned", true},
          {"holding_period_days", "number", "Days the stock has been held", true},
          {"days_to_expiration", "number", "Days to option expiration", false},
          {"ordinary_tax_rate", "number", "Ordinary tax rate (decimal)", false},
          {"ltcg_rate", "number", "Long-term capital gains rate (decimal)", false}}},
        {"alt_sri_fund", "sri",
         "Analyze an SRI / ESG fund vs its benchmark: performance gap, screening, expenses.",
         "performance, screening, expenses, approaches, verdict",
         {{"fund_return", "number", "Fund annual return (decimal)", true},
          {"benchmark_return", "number", "Benchmark annual return (decimal)", true},
          {"expense_ratio", "number", "Fund expense ratio (decimal)", true},
          {"time_period_years", "number", "Comparison horizon (years)", false},
          {"benchmark_expense", "number", "Benchmark expense ratio (decimal)", false}}},
        {"alt_asset_location", "asset-location",
         "Tax-efficient asset location: optimal account type for an asset class at a given tax bracket.",
         "optimal, value_added, portfolio, muni_bond",
         {{"asset_class", "string", "reits, high_yield_bonds, stocks, municipal_bonds, tips, commodities, cash", true},
          {"tax_bracket", "number", "Marginal tax rate (decimal)", true},
          {"value", "number", "Asset value ($) for value_added", false},
          {"years", "number", "Horizon (years) for value_added", false},
          {"municipal_yield", "number", "Muni yield (decimal) for muni_bond", false},
          {"taxable_yield", "number", "Taxable yield (decimal) for muni_bond", false}}},
        {"alt_performance_metrics", "performance",
         "Performance analysis of a supplied return series: TWR, MWR, Sharpe.", "twr, mwr, sharpe",
         {{"returns", "array", "Periodic returns (decimals)", true},
          {"benchmark", "array", "Benchmark returns (decimals)", false}},
         true},
        {"alt_risk_analysis", "risk",
         "Risk analysis of a supplied return series: VaR, CVaR, stress.", "var, cvar, stress",
         {{"returns", "array", "Periodic returns (decimals)", true},
          {"confidence_level", "number", "VaR confidence level (e.g. 0.95)", false}},
         true},

        // ── Digital Assets ───────────────────────────────────────────────────
        {"alt_digital_asset", "digital-assets",
         "Analyze a digital asset from caller-supplied supply/market-cap figures (fundamental) or price "
         "history (volatility).",
         "fundamental, volatility, onchain",
         {{"market_cap", "number", "Market cap ($)", true},
          {"circulating_supply", "number", "Circulating supply (units)", true},
          {"total_supply", "number", "Total / max supply (units)", false},
          {"trading_volume_24h", "number", "24h trading volume ($)", false},
          {"market_data", "array", "Price history [{timestamp, price}] for volatility", false}}},
    };

    std::vector<ToolDef> tools;
    tools.reserve(specs.size());
    for (const auto& spec : specs)
        tools.push_back(make_tool(spec));
    return tools;
}

} // namespace fincept::mcp::tools
