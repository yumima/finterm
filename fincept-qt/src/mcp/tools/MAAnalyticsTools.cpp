// MAAnalyticsTools.cpp — M&A Analytics tab MCP tools
// Covers all 8 modules: Valuation, Merger, Deal Structure, Deal Database,
// Startup, Fairness, Industry, Advanced Analytics, Deal Comparison.
//
// Each analytic tool forwards its arguments unchanged to the same script the
// M&A screen runs (MAAnalyticsService), so the schemas below ARE the scripts'
// JSON contract (scripts/Analytics/corporateFinance/_cli.py). Rates and
// probabilities are decimals unless a key ends in _pct. Model assumptions are
// required inputs — the scripts never substitute defaults.

#include "mcp/tools/MAAnalyticsTools.h"

#include "core/logging/Logger.h"
#include "mcp/AsyncDispatch.h"
#include "services/ma_analytics/MAAnalyticsService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPromise>
#include <QTimer>

#include <memory>

namespace fincept::mcp::tools {

static constexpr const char* TAG = "MAAnalyticsTools";

using MaSvc = fincept::services::ma::MAAnalyticsService;

// Every M&A tool is async: the provider's watchdog (default_timeout_ms) fails
// the call with a visible timeout error if the script never reports back,
// matching the geopolitics tools. The result is matched on `context`, which
// must be the exact context string the service method emits.
static constexpr int kMaTimeoutMs = 120000;

static void ma_dispatch(const QString& context, std::shared_ptr<QPromise<ToolResult>> promise, ToolContext ctx,
                        std::function<void()> trigger) {
    auto* svc = &MaSvc::instance();
    AsyncDispatch::callback_to_promise(svc, std::move(ctx), promise, [svc, context, trigger](auto resolve) {
        // Scoped connections; the gate also expires itself so a script that
        // never reports back can't leave them connected forever.
        auto* gate = new QObject(svc);
        QObject::connect(svc, &MaSvc::result_ready, gate,
                         [resolve, gate, context](const QString& c, const QJsonObject& data) {
                             if (c != context)
                                 return;
                             resolve(ToolResult::ok_data(data));
                             gate->deleteLater();
                         });
        QObject::connect(svc, &MaSvc::error_occurred, gate,
                         [resolve, gate, context](const QString& c, const QString& msg) {
                             if (c != context)
                                 return;
                             resolve(ToolResult::fail(msg.isEmpty() ? "M&A analytics failed: " + context : msg));
                             gate->deleteLater();
                         });
        QTimer::singleShot(kMaTimeoutMs, gate, [resolve, gate, context]() {
            LOG_WARN(TAG, QString("M&A analytics '%1' timed out").arg(context));
            resolve(ToolResult::fail(QString("M&A analytics '%1' timed out after %2 s (the script did not finish)")
                                         .arg(context)
                                         .arg(kMaTimeoutMs / 1000)));
            gate->deleteLater();
        });
        trigger();
    });
}

namespace {

QJsonObject num(const char* d) { return {{"type", "number"}, {"description", d}}; }
QJsonObject integer(const char* d) { return {{"type", "integer"}, {"description", d}}; }
QJsonObject str(const char* d) { return {{"type", "string"}, {"description", d}}; }
QJsonObject boolean(const char* d) { return {{"type", "boolean"}, {"description", d}}; }
QJsonObject object(const char* d) { return {{"type", "object"}, {"description", d}}; }
QJsonObject num_array(const char* d) {
    return {{"type", "array"}, {"items", QJsonObject{{"type", "number"}}}, {"description", d}};
}
QJsonObject obj_array(const char* d) {
    return {{"type", "array"}, {"items", QJsonObject{{"type", "object"}}}, {"description", d}};
}
QJsonObject enum_str(const char* d, const QStringList& values) {
    return {{"type", "string"}, {"enum", QJsonArray::fromStringList(values)}, {"description", d}};
}

// One analytic tool: forwards args to `method` and waits on `context`.
ToolDef ma_tool(const char* name, const char* context, void (MaSvc::*method)(const QJsonObject&),
                const QString& description, const QJsonObject& props, const QStringList& required) {
    ToolDef t;
    t.name = name;
    t.description = description;
    t.category = "ma-analytics";
    t.input_schema.properties = props;
    t.input_schema.required = required;
    t.default_timeout_ms = kMaTimeoutMs;
    const QString context_name = QString::fromLatin1(context);
    t.async_handler = [context_name, method](const QJsonObject& args, ToolContext ctx,
                                             std::shared_ptr<QPromise<ToolResult>> promise) {
        QJsonObject params = args;
        params.remove("_meta");
        ma_dispatch(context_name, promise, std::move(ctx), [method, params]() { (MaSvc::instance().*method)(params); });
    };
    return t;
}

// Shared property blocks ------------------------------------------------------

QJsonObject merge(std::initializer_list<QJsonObject> parts) {
    QJsonObject o;
    for (const auto& p : parts)
        for (auto it = p.begin(); it != p.end(); ++it)
            o[it.key()] = it.value();
    return o;
}

QJsonObject dcf_props() {
    return QJsonObject{
        {"wacc", num("WACC (decimal). If omitted it is built by CAPM from the component inputs below")},
        {"risk_free_rate", num("Risk-free rate (decimal)")},
        {"beta", num("Levered equity beta")},
        {"market_risk_premium", num("Equity risk premium (decimal)")},
        {"cost_of_debt", num("Pre-tax cost of debt (decimal)")},
        {"tax_rate", num("Tax rate (decimal); also used for NOPAT")},
        {"market_cap", num("Market value of equity (WACC weight)")},
        {"debt", num("Total debt (WACC weight and equity bridge)")},
        {"cash", num("Cash (equity bridge)")},
        {"net_debt", num("Net debt, instead of debt and cash for the bridge")},
        {"fcf_projections", num_array("Explicit FCFF per projection year, instead of the base-year build")},
        {"ebit", num("Base-year EBIT")},
        {"d_and_a", num("Base-year D&A")},
        {"capex", num("Base-year capex")},
        {"change_in_nwc", num("Base-year increase in net working capital")},
        {"growth_rates", num_array("FCF growth per projection year (decimals); its length sets the horizon")},
        {"terminal_method", enum_str("Terminal value method", {"perpetuity", "exit_multiple"})},
        {"terminal_growth", num("Perpetuity growth (decimal, -5%..5%, below WACC)")},
        {"exit_multiple", num("Exit EV/EBITDA multiple (exit_multiple method)")},
        {"terminal_ebitda", num("Final-year EBITDA for the exit multiple")},
        {"ebitda", num("Base-year EBITDA, grown at growth_rates when terminal_ebitda is not given")},
        {"mid_year_convention",
         boolean("Discount flows mid-year. A Gordon TV is then discounted N-0.5 years; an exit-multiple TV N years")},
        {"minority_interest", num("Minority interest (optional, 0 = none)")},
        {"preferred_stock", num("Preferred stock (optional, 0 = none)")},
        {"shares_outstanding", num("Diluted shares outstanding")},
    };
}

QJsonObject lbo_props() {
    return QJsonObject{
        {"ebitda", num("LTM EBITDA at entry")},
        {"revenue", num("LTM revenue at entry")},
        {"entry_multiple", num("Entry EV/EBITDA")},
        {"exit_multiple", num("Exit EV/EBITDA")},
        {"holding_period", integer("Holding period in years")},
        {"revenue_growth", num("Annual revenue growth (decimal; a per-year array is also accepted)")},
        {"ebitda_margin", num("Projected EBITDA margin (decimal)")},
        {"d_and_a_pct", num("D&A as a fraction of revenue")},
        {"capex_pct", num("Capex as a fraction of revenue")},
        {"nwc_pct", num("NWC investment as a fraction of the revenue change")},
        {"tax_rate", num("Tax rate (decimal)")},
        {"senior_debt", num("Senior term debt")},
        {"senior_rate", num("Senior interest rate (decimal)")},
        {"senior_amort_pct", num("Senior mandatory amortization per year, fraction of original principal")},
        {"sub_debt", num("Subordinated (bullet) debt, optional")},
        {"sub_rate", num("Subordinated rate (decimal), required with sub_debt")},
        {"revolver", num("Revolver drawn at close, optional")},
        {"revolver_rate", num("Revolver rate (decimal), required with revolver")},
        {"sweep_pct", num("Share of FCF after mandatory amortization swept to revolver/senior (decimal)")},
        {"transaction_fees", num("Transaction fees (optional, 0 = none)")},
        {"financing_fees", num("Financing fees (optional, 0 = none)")},
    };
}

QJsonObject ad_props() {
    return QJsonObject{
        {"acquirer_net_income", num("Acquirer net income")},
        {"acquirer_eps", num("Acquirer EPS, instead of acquirer_net_income")},
        {"acquirer_shares", num("Acquirer diluted shares")},
        {"acquirer_share_price", num("Acquirer share price (required when any stock is issued)")},
        {"target_net_income", num("Target net income")},
        {"deal_value", num("Equity purchase price for the target")},
        {"cash_pct", num("Cash share of the consideration (decimal 0-1)")},
        {"cash_from_balance_sheet", num("Acquirer cash used (rest of the cash portion is new debt)")},
        {"debt_rate", num("Pre-tax rate on new acquisition debt (decimal)")},
        {"cash_yield", num("Pre-tax interest yield forgone on cash used (decimal)")},
        {"tax_rate", num("Tax rate (decimal)")},
        {"synergies", num("Run-rate pre-tax synergies (optional)")},
        {"synergies_after_tax", num("Run-rate after-tax synergies, instead of synergies")},
        {"synergy_phase_in", num("Fraction of run-rate synergies realized in the year analysed (optional, 1 = full)")},
        {"integration_costs", num("Pre-tax integration costs in the year analysed (optional)")},
        {"new_amortization", num("Pre-tax D&A on purchase-price step-ups (optional)")},
    };
}

QJsonObject synergy_timing_props() {
    return QJsonObject{
        {"tax_rate", num("Tax rate (decimal)")},
        {"discount_rate", num("Discount rate (decimal)")},
        {"ramp_years", integer("Years to reach run-rate (linear ramp)")},
        {"projection_years", integer("Explicit projection years")},
        {"terminal_growth", num("Terminal growth (decimal, optional; omitted = no terminal value)")},
    };
}

QJsonObject deals_prop() {
    return QJsonObject{{"deals", obj_array("Deals: acquirer, target, deal_value, premium (PERCENT, 45.3 = 45.3%), "
                                           "ev_revenue, ev_ebitda, cash_pct / stock_pct (PERCENT), synergies, "
                                           "industry")}};
}

} // namespace

std::vector<ToolDef> get_ma_analytics_tools() {
    std::vector<ToolDef> tools;

    // ════════════════════════════════════════════════════════════════════
    // VALUATION
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool(
        "ma_dcf", "dcf", &MaSvc::calculate_dcf,
        "DCF (FCFF) valuation: WACC (given, or CAPM-built), projected free cash flow, Gordon-growth or exit-multiple "
        "terminal value, optional mid-year convention, and the EV -> equity bridge (less net debt, minority "
        "interest, preferred). Returns EV, equity value, value per share and the projection table.",
        dcf_props(), {"terminal_method", "mid_year_convention", "shares_outstanding"}));

    tools.push_back(ma_tool("ma_dcf_sensitivity", "dcf_sensitivity", &MaSvc::calculate_dcf_sensitivity,
                            "DCF value per share over a WACC x terminal-growth grid (WACC only for the exit-multiple "
                            "method).",
                            QJsonObject{{"base_params", object("ma_dcf inputs")},
                                        {"wacc_range", num_array("WACC values (decimals)")},
                                        {"tgr_range", num_array("Terminal growth values (decimals)")}},
                            {"base_params", "wacc_range"}));

    tools.push_back(ma_tool(
        "ma_lbo_returns", "lbo_returns", &MaSvc::calculate_lbo_returns,
        "Sponsor IRR and MOIC from equity invested and exit equity (exit EV - net debt at exit, or given directly).",
        QJsonObject{{"equity_invested", num("Sponsor equity invested at entry")},
                    {"holding_period", integer("Holding period in years")},
                    {"exit_valuation", num("Exit enterprise value")},
                    {"exit_net_debt", num("Net debt at exit")},
                    {"exit_equity_value", num("Exit equity value, instead of exit_valuation - exit_net_debt")},
                    {"entry_valuation", num("Entry enterprise value (optional, for implied entry leverage)")},
                    {"interim_distributions", num_array("Distributions in years 1..N-1 (optional)")}},
        {"equity_invested", "holding_period"}));

    tools.push_back(ma_tool(
        "ma_lbo_model", "lbo_model", &MaSvc::build_lbo_model,
        "Full LBO: sources & uses (sponsor equity is the plug), revenue/EBITDA projection, debt waterfall with "
        "mandatory amortization and cash sweep, exit at an EV/EBITDA multiple, IRR and MOIC.",
        lbo_props(),
        {"ebitda", "revenue", "entry_multiple", "exit_multiple", "holding_period", "revenue_growth", "ebitda_margin",
         "d_and_a_pct", "capex_pct", "nwc_pct", "tax_rate", "senior_debt", "senior_rate", "senior_amort_pct",
         "sweep_pct"}));

    tools.push_back(ma_tool(
        "ma_lbo_debt_schedule", "lbo_debt_schedule", &MaSvc::analyze_lbo_debt_schedule,
        "Debt schedule: interest on opening balances, taxes after interest, levered FCF, mandatory amortization and "
        "cash sweep (revolver then senior; subordinated is a bullet).",
        QJsonObject{{"years", integer("Years")},
                    {"ebitda", num("Year-1 EBITDA")},
                    {"ebitda_growth", num("Annual EBITDA growth (decimal)")},
                    {"d_and_a", num("Annual D&A")},
                    {"capex", num("Annual capex")},
                    {"nwc_change", num("Annual increase in NWC")},
                    {"tax_rate", num("Tax rate (decimal)")},
                    {"sweep_pct", num("Cash sweep share (decimal)")},
                    {"senior_debt", num("Senior debt")},
                    {"senior_rate", num("Senior rate (decimal)")},
                    {"senior_amort_pct", num("Senior amortization per year, fraction of original")},
                    {"sub_debt", num("Subordinated debt (optional)")},
                    {"sub_rate", num("Subordinated rate (decimal)")},
                    {"revolver", num("Revolver drawn (optional)")},
                    {"revolver_rate", num("Revolver rate (decimal)")}},
        {"years", "ebitda", "ebitda_growth", "d_and_a", "capex", "nwc_change", "tax_rate", "sweep_pct",
         "senior_debt"}));

    tools.push_back(ma_tool("ma_lbo_sensitivity", "lbo_sensitivity", &MaSvc::calculate_lbo_sensitivity,
                            "LBO IRR / MOIC grid over entry x exit multiples around the base case (all ma_lbo_model "
                            "inputs plus the grid).",
                            merge({lbo_props(),
                                   QJsonObject{{"range", num("Grid half-width in multiple turns")},
                                               {"steps", integer("Points per axis (2-15)")},
                                               {"entry_base", num("Grid centre entry multiple (default: entry_multiple)")},
                                               {"exit_base", num("Grid centre exit multiple (default: exit_multiple)")}}}),
                            {"ebitda", "revenue", "entry_multiple", "exit_multiple", "holding_period", "range", "steps"}));

    tools.push_back(ma_tool(
        "ma_trading_comps", "trading_comps", &MaSvc::calculate_trading_comps,
        "Trading comparables. Either live (target_ticker + comp_tickers, LTM data from yfinance) or manual (peers "
        "with market_cap, total_debt, cash, revenue, ebitda, ebit, net_income — or explicit multiples — plus target "
        "metrics). EV = market cap + debt - cash; non-positive multiples are excluded; EV multiples imply EV, P/E "
        "implies equity.",
        QJsonObject{{"target_ticker", str("Target ticker (live mode)")},
                    {"comp_tickers", str("Comma-separated peer tickers (live mode)")},
                    {"peers", obj_array("Manual peers")},
                    {"target_revenue", num("Target LTM revenue (manual mode)")},
                    {"target_ebitda", num("Target LTM EBITDA (manual mode)")},
                    {"target_ebit", num("Target LTM EBIT (manual mode)")},
                    {"target_net_income", num("Target LTM net income (manual mode)")},
                    {"target_net_debt", num("Target net debt (manual mode, for the equity bridge)")},
                    {"target_shares", num("Target diluted shares (manual mode)")},
                    {"target_price", num("Target share price (manual mode)")}},
        {}));

    tools.push_back(ma_tool(
        "ma_precedent_transactions", "precedent_txns", &MaSvc::calculate_precedent_transactions,
        "Value a target off precedent deals: multiples = deal EV / target LTM metric (or given), quartiles across "
        "deals, implied EV / equity / per share. The control premium is already in precedent multiples.",
        QJsonObject{{"transactions", obj_array("Deals: target, acquirer, date, enterprise_value, revenue, ebitda, "
                                               "ebit, or ev_revenue / ev_ebitda; premium_1day_pct optional")},
                    {"target_revenue", num("Target LTM revenue")},
                    {"target_ebitda", num("Target LTM EBITDA")},
                    {"target_ebit", num("Target LTM EBIT")},
                    {"target_net_debt", num("Target net debt (optional)")},
                    {"target_shares", num("Target diluted shares (optional)")},
                    {"target_price", num("Target share price (optional)")}},
        {"transactions"}));

    tools.push_back(ma_tool(
        "ma_football_field", "football_field", &MaSvc::generate_football_field,
        "Football field: valuation ranges per method on one basis (per share or equity value), overlap, and where "
        "the current / offer price sits.",
        QJsonObject{{"methods", obj_array("Ranges: {method, low, high, midpoint?}")},
                    {"dcf_low", num("DCF low")},
                    {"dcf_high", num("DCF high")},
                    {"comps_low", num("Trading comps low")},
                    {"comps_high", num("Trading comps high")},
                    {"precedent_low", num("Precedent transactions low")},
                    {"precedent_high", num("Precedent transactions high")},
                    {"current_price", num("Current price (optional)")},
                    {"offer_price", num("Offer price (optional)")}},
        {}));

    // ════════════════════════════════════════════════════════════════════
    // MERGER ANALYSIS
    // ════════════════════════════════════════════════════════════════════
    const QStringList ad_required = {"acquirer_shares", "target_net_income", "deal_value", "cash_pct", "tax_rate"};
    const QString ad_desc =
        "EPS accretion/dilution: pro forma NI = acquirer NI + target NI + after-tax (phased synergies - integration "
        "costs - new amortization - interest on new debt - interest forgone on cash); pro forma shares = acquirer "
        "shares + stock consideration / acquirer price. Also breakeven synergies and purchase vs acquirer P/E.";
    tools.push_back(ma_tool("ma_accretion_dilution", "accretion_dilution", &MaSvc::calculate_accretion_dilution,
                            ad_desc, ad_props(), ad_required));
    tools.push_back(ma_tool("ma_merger_model", "merger_model", &MaSvc::build_merger_model,
                            "Merger model (same engine and inputs as ma_accretion_dilution). " + ad_desc, ad_props(),
                            ad_required));

    tools.push_back(ma_tool(
        "ma_pro_forma", "pro_forma", &MaSvc::build_pro_forma,
        "Multi-year pro forma accretion/dilution: standalone net incomes grown at the given rates, synergies phased "
        "per year, integration costs per year; acquisition debt held constant.",
        merge({ad_props(),
               QJsonObject{{"years", integer("Projection years (1-10)")},
                           {"acquirer_ni_growth", num("Acquirer standalone NI growth (decimal)")},
                           {"target_ni_growth", num("Target standalone NI growth (decimal)")},
                           {"synergy_phase_in_by_year", num_array("Fraction of run-rate synergies per year")},
                           {"integration_costs_by_year", num_array("Pre-tax integration costs per year")},
                           {"revenue_growth", num("Combined revenue growth (optional, decimal)")},
                           {"acquirer_revenue", num("Acquirer revenue (optional)")},
                           {"target_revenue", num("Target revenue (optional)")}}}),
        QStringList(ad_required) << "years" << "acquirer_ni_growth" << "target_ni_growth"));

    tools.push_back(ma_tool(
        "ma_sources_uses", "sources_uses", &MaSvc::calculate_sources_uses,
        "Sources & uses as entered (no plug, no estimated fees); reports any funding shortfall or surplus.",
        QJsonObject{{"purchase_price", num("Equity purchase price")},
                    {"target_debt_refinanced", num("Target debt refinanced")},
                    {"transaction_fees", num("Transaction fees")},
                    {"financing_fees", num("Financing fees")},
                    {"acquirer_cash", num("Acquirer cash used")},
                    {"new_debt", num("New debt raised")},
                    {"new_equity", num("New equity issued")}},
        {"purchase_price"}));

    tools.push_back(ma_tool(
        "ma_contribution_analysis", "contribution", &MaSvc::analyze_contribution,
        "Contribution analysis: each party's share of combined revenue / EBITDA / net income / assets vs the "
        "ownership split.",
        QJsonObject{{"acquirer", object("Acquirer financials: revenue, ebitda, net_income, total_assets")},
                    {"target", object("Target financials: revenue, ebitda, net_income, total_assets")},
                    {"ownership_split", num("Target holders' share of the combined company (decimal)")}},
        {"acquirer", "target", "ownership_split"}));

    tools.push_back(ma_tool(
        "ma_revenue_synergies", "revenue_synergies", &MaSvc::calculate_revenue_synergies,
        "Value revenue synergies (ramped, taxed at the synergy margin, discounted). type: simple (combined_revenue x "
        "synergy_pct), cross_sell, market_expansion, pricing_power.",
        merge({synergy_timing_props(),
               QJsonObject{{"type", enum_str("Synergy model", {"simple", "cross_sell", "market_expansion",
                                                               "pricing_power"})},
                           {"combined_revenue", num("Combined revenue (simple, pricing_power)")},
                           {"synergy_pct", num("Run-rate synergy as a fraction of revenue (simple)")},
                           {"revenue_synergy_margin", num("Margin earned on synergy revenue (decimal)")},
                           {"acquirer_customers", num("cross_sell")},
                           {"target_customers", num("cross_sell")},
                           {"acquirer_arpu", num("cross_sell")},
                           {"target_arpu", num("cross_sell")},
                           {"cross_sell_rate", num("cross_sell (decimal)")},
                           {"target_revenue_new_markets", num("market_expansion")},
                           {"acquirer_product_penetration", num("market_expansion (decimal)")},
                           {"market_share_gain", num("market_expansion (decimal)")},
                           {"price_increase", num("pricing_power (decimal)")},
                           {"volume_elasticity", num("pricing_power")}}}),
        {"revenue_synergy_margin", "tax_rate", "discount_rate", "ramp_years", "projection_years"}));

    tools.push_back(ma_tool(
        "ma_cost_synergies", "cost_synergies", &MaSvc::calculate_cost_synergies,
        "Value cost synergies (ramped, after tax, discounted; one-time costs tax-deductible). type: simple "
        "(combined_opex x synergy_pct), headcount, facilities, procurement.",
        merge({synergy_timing_props(),
               QJsonObject{{"type", enum_str("Synergy model", {"simple", "headcount", "facilities", "procurement"})},
                           {"combined_opex", num("Combined cost base (simple)")},
                           {"synergy_pct", num("Run-rate saving as a fraction of the cost base (simple)")},
                           {"one_time_cost", num("One-time cost to achieve (simple, optional)")},
                           {"duplicate_roles", num("headcount")},
                           {"average_loaded_cost", num("headcount")},
                           {"severance_multiple", num("headcount")},
                           {"facilities_to_close", num("facilities")},
                           {"annual_cost_per_facility", num("facilities")},
                           {"closure_cost_per_facility", num("facilities")},
                           {"lease_termination_cost", num("facilities (optional)")},
                           {"combined_spend", num("procurement")},
                           {"volume_discount", num("procurement (decimal)")},
                           {"supplier_rationalization_benefit", num("procurement (decimal, optional)")},
                           {"implementation_cost", num("procurement (optional)")}}}),
        {"tax_rate", "discount_rate", "ramp_years", "projection_years"}));

    tools.push_back(ma_tool(
        "ma_synergies_dcf", "synergy_dcf", &MaSvc::value_synergies_dcf,
        "NPV of revenue and cost synergies: linear ramp, revenue synergies at revenue_synergy_margin, taxed, "
        "integration cost spread and tax-deducted, terminal value only if terminal_growth is given.",
        merge({synergy_timing_props(),
               QJsonObject{{"revenue_synergy", num("Run-rate revenue synergy")},
                           {"revenue_synergy_pct", num("Revenue synergy as a fraction of combined_revenue")},
                           {"combined_revenue", num("Combined revenue")},
                           {"revenue_synergy_margin", num("Margin on synergy revenue (required with revenue synergy)")},
                           {"cost_synergy", num("Run-rate cost synergy")},
                           {"annual_synergies", num("Run-rate cost synergy (alias)")},
                           {"cost_synergy_pct", num("Cost synergy as a fraction of combined_cost_base")},
                           {"combined_cost_base", num("Combined cost base")},
                           {"integration_cost", num("One-time integration cost (optional)")},
                           {"integration_cost_years", integer("Years the integration cost is spread over")}}}),
        {"tax_rate", "discount_rate", "ramp_years", "projection_years"}));

    tools.push_back(ma_tool(
        "ma_integration_costs", "integration_costs", &MaSvc::estimate_integration_costs,
        "Integration cost build-up from user-supplied cost items and drivers (no built-in unit costs).",
        QJsonObject{{"deal_value", num("Deal value")},
                    {"cost_items", obj_array("Line items: {category, amount, year?, capitalized?}")},
                    {"integration_cost_pct", num("Total integration cost as a fraction of deal value")},
                    {"systems_to_integrate", num("IT systems (with cost_per_system)")},
                    {"cost_per_system", num("Cost per IT system")},
                    {"key_employees", num("Retention: key employees")},
                    {"average_compensation", num("Retention: average compensation")},
                    {"retention_bonus_pct", num("Retention bonus as a fraction of compensation")},
                    {"severance_roles", num("Roles eliminated")},
                    {"severance_cost_per_role", num("Severance per role")},
                    {"training_hours_per_employee", num("Training hours per employee")},
                    {"training_cost_per_hour", num("Training cost per hour")},
                    {"employees_to_train", num("Employees to train")},
                    {"advisory_fee_pct", num("Advisory fees as a fraction of deal value")},
                    {"legal_fees", num("Legal fees")},
                    {"accounting_fees", num("Accounting fees")},
                    {"rebranding_cost", num("Rebranding cost")},
                    {"facilities_cost", num("Facilities cost")},
                    {"other_costs", num("Other costs")},
                    {"contingency_pct", num("Contingency as a fraction (optional)")},
                    {"combined_revenue", num("Combined revenue (optional, for ratios)")},
                    {"combined_headcount", num("Combined headcount (optional)")}},
        {"deal_value"}));

    // ════════════════════════════════════════════════════════════════════
    // DEAL STRUCTURE
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool(
        "ma_payment_structure", "payment_structure", &MaSvc::analyze_payment_structure,
        "Cash / stock consideration mix: cash funding vs cash on hand and new debt, shares issued and dilution.",
        QJsonObject{{"purchase_price", num("Purchase price")},
                    {"cash_pct", num("Cash share (decimal 0-1)")},
                    {"stock_pct", num("Stock share (decimal 0-1), alternative to cash_pct")},
                    {"cash_on_hand", num("Acquirer cash available")},
                    {"new_debt", num("New debt available")},
                    {"acquirer_share_price", num("Acquirer share price (required if any stock)")},
                    {"acquirer_shares_outstanding", num("Acquirer shares (required if any stock)")},
                    {"target_shares_outstanding", num("Target shares (optional)")}},
        {"purchase_price"}));

    tools.push_back(ma_tool(
        "ma_earnout", "earnout", &MaSvc::value_earnout,
        "Expected PV of an earnout: probability x payment discounted over the years to payment (or per tranche).",
        QJsonObject{{"earnout_amount", num("Payment if the target is met")},
                    {"probability", num("Probability the target is met (decimal)")},
                    {"years", num("Years until payment")},
                    {"discount_rate", num("Discount rate (decimal)")},
                    {"threshold", num("Performance threshold (informational)")},
                    {"base_price", num("Upfront price (optional)")},
                    {"tranches", obj_array("Tranches: {payment, probability, years}")}},
        {"discount_rate"}));

    tools.push_back(ma_tool(
        "ma_exchange_ratio", "exchange_ratio", &MaSvc::calculate_exchange_ratio,
        "Exchange ratio = offer price per target share / acquirer share price (offer = target price x (1+premium) "
        "or given), shares issued and pro forma ownership.",
        QJsonObject{{"acquirer_price", num("Acquirer share price")},
                    {"target_price", num("Target unaffected share price")},
                    {"premium", num("Offer premium (decimal)")},
                    {"offer_price", num("Offer price per target share, instead of target_price + premium")},
                    {"target_shares_outstanding", num("Target shares (optional)")},
                    {"acquirer_shares_outstanding", num("Acquirer shares (optional)")}},
        {"acquirer_price"}));

    tools.push_back(ma_tool(
        "ma_collar_mechanism", "collar", &MaSvc::analyze_collar_mechanism,
        "Collar on a stock deal: fixed-ratio (value floats inside the band, ratio adjusts outside) or fixed-value "
        "(ratio floats inside, fixed outside), across acquirer price scenarios.",
        QJsonObject{{"base_ratio", num("Base exchange ratio")},
                    {"floor_price", num("Lower acquirer price bound")},
                    {"cap_price", num("Upper acquirer price bound")},
                    {"acquirer_price", num("Reference acquirer price the collar is struck at")},
                    {"collar_type", enum_str("Collar type", {"fixed_ratio", "fixed_value"})},
                    {"target_shares", num("Target shares (optional)")},
                    {"price_scenarios", num_array("Acquirer prices to evaluate (optional)")}},
        {"base_ratio", "floor_price", "cap_price", "acquirer_price"}));

    tools.push_back(ma_tool(
        "ma_cvr", "cvr", &MaSvc::value_cvr,
        "Contingent value right: probability x payment discounted to today (optional appeal leg).",
        QJsonObject{{"payment", num("Total payment if triggered")},
                    {"probability", num("Trigger probability (decimal)")},
                    {"years", num("Years to the trigger")},
                    {"discount_rate", num("Discount rate (decimal)")},
                    {"type", str("Label: milestone / revenue / regulatory")},
                    {"shares_outstanding", num("CVRs outstanding (optional, for per-CVR value)")},
                    {"appeal_probability", num("Probability of a later payment after appeal (optional)")},
                    {"appeal_delay_years", num("Extra years for the appeal leg (optional)")}},
        {"payment", "probability", "years", "discount_rate"}));

    // ════════════════════════════════════════════════════════════════════
    // DEAL DATABASE
    // ════════════════════════════════════════════════════════════════════

    // ── ma_get_deals ────────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "ma_get_deals";
        t.description = "Get all deals from the M&A deal database.";
        t.category = "ma-analytics";
        t.default_timeout_ms = kMaTimeoutMs;
        t.async_handler = [](const QJsonObject&, ToolContext ctx, std::shared_ptr<QPromise<ToolResult>> promise) {
            ma_dispatch("all_deals", promise, std::move(ctx), []() { MaSvc::instance().get_all_deals(); });
        };
        tools.push_back(std::move(t));
    }

    // ── ma_search_deals ─────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "ma_search_deals";
        t.description = "Search the deal database by company name, industry, or deal type.";
        t.category = "ma-analytics";
        t.input_schema.properties = QJsonObject{{"query", str("Search query")}};
        t.input_schema.required = {"query"};
        t.default_timeout_ms = kMaTimeoutMs;
        t.async_handler = [](const QJsonObject& args, ToolContext ctx, std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString query = args["query"].toString().trimmed();
            if (query.isEmpty()) {
                promise->addResult(ToolResult::fail("Missing 'query'"));
                promise->finish();
                return;
            }
            ma_dispatch("search_deals", promise, std::move(ctx), [query]() { MaSvc::instance().search_deals(query); });
        };
        tools.push_back(std::move(t));
    }

    // ── ma_create_deal ──────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "ma_create_deal";
        t.description = "Add a new deal record to the M&A deal database (any other ma_deals column, e.g. "
                        "payment_method, premium_1day, enterprise_value, may also be passed).";
        t.category = "ma-analytics";
        t.input_schema.properties =
            QJsonObject{{"acquirer", str("Acquirer company name")},
                        {"target", str("Target company name")},
                        {"deal_value", num("Deal value in USD (absolute, not millions)")},
                        {"deal_type", str("Acquisition, Merger, LBO, etc.")},
                        {"industry", str("Industry sector")},
                        {"announced_date", str("Announcement date YYYY-MM-DD")},
                        {"status", str("Announced, Pending, Completed, Failed")}};
        t.input_schema.required = {"acquirer", "target", "announced_date", "deal_type", "status"};
        t.default_timeout_ms = kMaTimeoutMs;
        t.async_handler = [](const QJsonObject& args, ToolContext ctx, std::shared_ptr<QPromise<ToolResult>> promise) {
            QJsonObject params = args;
            params.remove("_meta");
            ma_dispatch("create_deal", promise, std::move(ctx), [params]() { MaSvc::instance().create_deal(params); });
        };
        tools.push_back(std::move(t));
    }

    // ── ma_update_deal ──────────────────────────────────────────────────
    {
        ToolDef t;
        t.name = "ma_update_deal";
        t.description = "Update an existing deal record in the M&A database.";
        t.category = "ma-analytics";
        t.input_schema.properties =
            QJsonObject{{"deal_id", str("Deal ID to update")},
                        {"updates", object("Fields to update: status, deal_value (USD), or any ma_deals column")}};
        t.input_schema.required = {"deal_id", "updates"};
        t.default_timeout_ms = kMaTimeoutMs;
        t.async_handler = [](const QJsonObject& args, ToolContext ctx, std::shared_ptr<QPromise<ToolResult>> promise) {
            const QString deal_id = args["deal_id"].toString().trimmed();
            if (deal_id.isEmpty()) {
                promise->addResult(ToolResult::fail("Missing 'deal_id'"));
                promise->finish();
                return;
            }
            const QJsonObject updates = args["updates"].toObject();
            ma_dispatch("update_deal", promise, std::move(ctx),
                        [deal_id, updates]() { MaSvc::instance().update_deal(deal_id, updates); });
        };
        tools.push_back(std::move(t));
    }

    // ════════════════════════════════════════════════════════════════════
    // STARTUP VALUATION
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool(
        "ma_startup_berkus", "berkus", &MaSvc::calculate_berkus,
        "Berkus method: sum over 5 factors (sound idea, prototype, quality team, strategic relationships, product "
        "rollout) of score (0-1) x the maximum value per factor.",
        QJsonObject{{"scores", num_array("5 scores in [0,1], in the order above")},
                    {"max_value_per_factor", num("Maximum value credited per factor ($)")}},
        {"scores", "max_value_per_factor"}));

    tools.push_back(ma_tool(
        "ma_startup_vc", "vc_method", &MaSvc::calculate_vc_method,
        "VC method: post-money = exit metric x exit multiple / (1+target return)^years (x retention); pre-money = "
        "post - investment; required ownership = investment / post.",
        QJsonObject{{"exit_metric", num("Exit-year revenue or earnings")},
                    {"exit_multiple", num("Exit multiple")},
                    {"years", num("Years to exit")},
                    {"investment", num("Investment amount")},
                    {"target_return", num("Target annual return (decimal)")},
                    {"retention_ratio", num("Ownership retained after future dilution (decimal, optional)")}},
        {"exit_metric", "exit_multiple", "years", "investment", "target_return"}));

    tools.push_back(ma_tool(
        "ma_startup_scorecard", "scorecard", &MaSvc::calculate_scorecard,
        "Scorecard (Payne) method: benchmark pre-money x sum(weight x factor ratio), 7 factors (team, market size, "
        "product, competition, marketing/sales, need for funding, other); ratio 1.0 = average.",
        QJsonObject{{"benchmark_pre_money", num("Average pre-money of comparable deals ($)")},
                    {"assessments", num_array("7 ratios (0-3), in the order above")},
                    {"weights", num_array("7 weights summing to 1 (optional; default Payne 30/25/15/10/10/5/5)")},
                    {"stage", str("Label (optional)")}},
        {"benchmark_pre_money", "assessments"}));

    tools.push_back(ma_tool(
        "ma_startup_first_chicago", "first_chicago", &MaSvc::calculate_first_chicago,
        "First Chicago: sum over scenarios of probability x exit value discounted at the discount rate; "
        "probabilities must sum to 1.",
        QJsonObject{{"discount_rate", num("Discount rate (decimal)")},
                    {"years", num("Years to exit (used when a scenario has no exit_year)")},
                    {"scenarios", obj_array("{name, probability, exit_value, exit_year?}")}},
        {"discount_rate", "scenarios"}));

    tools.push_back(ma_tool(
        "ma_startup_risk_factor", "risk_factor", &MaSvc::calculate_risk_factor,
        "Risk factor summation: base pre-money + sum(score x adjustment per step) over 12 risks scored -2..+2 "
        "(management, stage, legislation, manufacturing, sales, funding, competition, technology, litigation, "
        "international, reputation, exit).",
        QJsonObject{{"base_valuation", num("Base pre-money ($)")},
                    {"adjustment_per_step", num("$ adjustment per +/-1 step")},
                    {"assessments", num_array("12 integer scores -2..+2 in the order above")}},
        {"base_valuation", "adjustment_per_step", "assessments"}));

    tools.push_back(ma_tool(
        "ma_startup_comprehensive", "startup_comprehensive", &MaSvc::calculate_comprehensive_startup,
        "Runs the startup methods whose payloads are given and reports the range, mean and median (weighted only if "
        "weights are given).",
        QJsonObject{{"berkus", object("ma_startup_berkus inputs")},
                    {"scorecard", object("ma_startup_scorecard inputs")},
                    {"vc", object("ma_startup_vc inputs")},
                    {"first_chicago", object("ma_startup_first_chicago inputs")},
                    {"risk_factor", object("ma_startup_risk_factor inputs")},
                    {"weights", object("{method: weight} (optional)")}},
        {}));

    // ════════════════════════════════════════════════════════════════════
    // FAIRNESS OPINION
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool(
        "ma_fairness_opinion", "fairness_opinion", &MaSvc::generate_fairness_opinion,
        "Fairness analysis: offer per share vs the valuation range of each method (low/high or point), reference "
        "range and midpoint, and whether the offer supports fairness for the target's holders.",
        QJsonObject{{"offer_price", num("Offer price per share")},
                    {"methods", obj_array("{method, low, high} or {method, valuation}; optional weight")},
                    {"week52_low", num("52-week low (optional)")},
                    {"week52_high", num("52-week high (optional)")}},
        {"offer_price", "methods"}));

    tools.push_back(ma_tool(
        "ma_premium_analysis", "premium_analysis", &MaSvc::analyze_premium,
        "Offer premium vs UNAFFECTED reference prices: premium = offer / reference - 1.",
        QJsonObject{{"offer_price", num("Offer price per share")},
                    {"price_1d", num("Unaffected price 1 day before announcement")},
                    {"price_1w", num("1 week prior (optional)")},
                    {"price_4w", num("4 weeks prior (optional)")},
                    {"price_1m", num("1 month prior (optional)")},
                    {"price_3m", num("3 months prior (optional)")},
                    {"price_52w", num("52-week high (optional)")},
                    {"price_52w_low", num("52-week low (optional)")},
                    {"shares_outstanding", num("Diluted shares (optional)")}},
        {"offer_price", "price_1d"}));

    tools.push_back(ma_tool(
        "ma_process_quality", "process_quality", &MaSvc::assess_process_quality,
        "Sale process quality: either 8 scores (1-5: board independence, special committee, independent advisor, "
        "market check, negotiation, due diligence, disclosure, timing) or a factual checklist.",
        QJsonObject{{"factors", num_array("8 integer scores 1-5 in the order above")},
                    {"num_bidders_contacted", num("Checklist: bidders contacted")},
                    {"num_bidders_participated", num("Checklist: bidders participating (optional)")},
                    {"market_check_conducted", boolean("Checklist: market check conducted")},
                    {"go_shop_period", num("Checklist: go-shop days (optional)")},
                    {"independent_committee", boolean("Checklist: independent committee (optional)")},
                    {"financial_advisor_engaged", boolean("Checklist: financial advisor (optional)")}},
        {}));

    // ════════════════════════════════════════════════════════════════════
    // INDUSTRY METRICS
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool(
        "ma_tech_metrics", "tech_metrics", &MaSvc::calculate_tech_metrics,
        "Technology KPIs computed from the inputs (no benchmark tables). saas: arr, growth, gross_margin, "
        "profit_margin (+ optional nrr, sm_expense, net_new_arr, new_customers, arpa, annual_churn); marketplace: "
        "gmv, revenue; semiconductor: revenue, gross_margin, rd_spend (+ backlog, ebitda).",
        QJsonObject{{"sector", enum_str("Sub-sector", {"saas", "marketplace", "semiconductor"})},
                    {"arr", num("ARR")},
                    {"revenue", num("Revenue")},
                    {"gmv", num("Gross merchandise value")},
                    {"growth", num("Growth (decimal)")},
                    {"gross_margin", num("Gross margin (decimal)")},
                    {"profit_margin", num("FCF or EBITDA margin (decimal)")},
                    {"nrr", num("Net revenue retention (decimal)")},
                    {"sm_expense", num("Sales & marketing expense")},
                    {"net_new_arr", num("Net new ARR")},
                    {"new_customers", num("New customers")},
                    {"arpa", num("ARR per account")},
                    {"annual_churn", num("Annual logo churn (decimal)")},
                    {"rd_spend", num("R&D spend")},
                    {"backlog", num("Backlog")},
                    {"ebitda", num("EBITDA")},
                    {"enterprise_value", num("Enterprise value (optional, for multiples)")}},
        {"sector"}));

    tools.push_back(ma_tool(
        "ma_healthcare_metrics", "healthcare_metrics", &MaSvc::calculate_healthcare_metrics,
        "Healthcare KPIs computed from the inputs (no benchmark tables).",
        QJsonObject{{"sector", enum_str("Sub-sector", {"pharma", "biotech", "devices", "services"})},
                    {"revenue", num("Revenue")},
                    {"ebitda_margin", num("EBITDA margin (decimal)")},
                    {"ebitda", num("EBITDA, instead of ebitda_margin")},
                    {"rd_spend", num("R&D spend")},
                    {"pipeline_npv", num("User's risk-adjusted pipeline NPV")},
                    {"phase3_candidates", integer("Phase 3 candidates")},
                    {"patent_expiry_revenue", num("Share of revenue losing exclusivity (decimal)")},
                    {"enterprise_value", num("Enterprise value (optional)")}},
        {"sector", "revenue"}));

    tools.push_back(ma_tool(
        "ma_financial_metrics", "finserv_metrics", &MaSvc::calculate_financial_services_metrics,
        "Financial-services KPIs computed from the inputs. banking: total_assets, deposits (+ equity, net_income, "
        "gross_loans, nim, efficiency_ratio, cet1_ratio, npl_ratio, market_cap); insurance: net_premiums_earned, "
        "losses_incurred, underwriting_expenses; asset_management: aum, revenue (+ net_flows, ebitda).",
        QJsonObject{{"sector", enum_str("Sub-sector", {"banking", "insurance", "asset_management"})},
                    {"total_assets", num("Total assets")},
                    {"deposits", num("Deposits")},
                    {"equity", num("Shareholders' equity")},
                    {"tangible_equity", num("Tangible equity")},
                    {"net_income", num("Net income")},
                    {"roe", num("ROE (decimal), if net income / equity are not given")},
                    {"gross_loans", num("Gross loans")},
                    {"nim", num("Net interest margin (decimal)")},
                    {"efficiency_ratio", num("Efficiency ratio (decimal)")},
                    {"cet1_ratio", num("CET1 ratio (decimal)")},
                    {"npl_ratio", num("NPL ratio (decimal)")},
                    {"market_cap", num("Market cap")},
                    {"net_premiums_earned", num("Net premiums earned")},
                    {"losses_incurred", num("Losses incurred")},
                    {"underwriting_expenses", num("Underwriting expenses")},
                    {"aum", num("Assets under management")},
                    {"revenue", num("Revenue")},
                    {"net_flows", num("Net flows")},
                    {"ebitda", num("EBITDA")},
                    {"enterprise_value", num("Enterprise value")}},
        {"sector"}));

    // ════════════════════════════════════════════════════════════════════
    // ADVANCED ANALYTICS
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool(
        "ma_monte_carlo", "monte_carlo", &MaSvc::run_monte_carlo,
        "Monte Carlo DCF SIMULATION: each path draws annual revenue growth and FCF margin from normal "
        "distributions; Gordon terminal value. Returns the EV distribution (mean, percentiles) and the "
        "zero-volatility EV.",
        QJsonObject{{"base_revenue", num("Current revenue")},
                    {"rev_growth_mean", num("Mean annual revenue growth (decimal)")},
                    {"rev_growth_std", num("Std dev of annual growth (decimal)")},
                    {"margin_mean", num("Mean FCF margin (FCF / revenue, decimal)")},
                    {"margin_std", num("Std dev of FCF margin (decimal)")},
                    {"discount_rate", num("Discount rate (decimal)")},
                    {"terminal_growth", num("Terminal growth (decimal, below discount_rate)")},
                    {"projection_years", integer("Projection years (1-30)")},
                    {"simulations", integer("Paths (100-200000)")},
                    {"seed", integer("Random seed (optional)")}},
        {"base_revenue", "rev_growth_mean", "rev_growth_std", "margin_mean", "margin_std", "discount_rate",
         "terminal_growth", "projection_years", "simulations"}));

    tools.push_back(ma_tool(
        "ma_regression", "regression", &MaSvc::run_regression,
        "OLS valuation regression on user-supplied comparables (EV ~ EBITDA, or EV ~ revenue + EBITDA + growth) with "
        "an exact prediction interval for the subject; or raw y_values / x_values.",
        QJsonObject{{"type", enum_str("Specification", {"ols", "multiple"})},
                    {"comparables", obj_array("{name, ev, revenue, ebitda, growth (decimal)}; at least k+2")},
                    {"subject", object("{revenue, ebitda, growth}")},
                    {"features", QJsonObject{{"type", "array"},
                                             {"items", QJsonObject{{"type", "string"}}},
                                             {"description", "Override regressors (optional)"}}},
                    {"confidence", num("Prediction-interval confidence (optional, default 0.95)")},
                    {"y_values", num_array("Raw mode: dependent values")},
                    {"x_values", QJsonObject{{"type", "array"},
                                             {"description", "Raw mode: regressor values (numbers or arrays)"}}},
                    {"variable_names", QJsonObject{{"type", "array"},
                                                   {"items", QJsonObject{{"type", "string"}}},
                                                   {"description", "Raw mode: regressor names"}}}},
        {}));

    // ════════════════════════════════════════════════════════════════════
    // DEAL COMPARISON
    // ════════════════════════════════════════════════════════════════════
    tools.push_back(ma_tool("ma_compare_deals", "compare_deals", &MaSvc::compare_deals,
                            "Compare deals: median / mean deal value, premium and multiples (missing fields skipped).",
                            deals_prop(), {"deals"}));

    tools.push_back(ma_tool("ma_rank_deals", "rank_deals", &MaSvc::rank_deals,
                            "Rank deals by a metric; deals missing it are listed separately; ties share a rank.",
                            merge({deals_prop(),
                                   QJsonObject{{"criteria", enum_str("Metric", {"premium", "deal_value", "ev_revenue",
                                                                                "ev_ebitda", "synergies"})},
                                               {"ascending", boolean("Sort ascending (optional)")}}}),
                            {"deals", "criteria"}));

    tools.push_back(ma_tool("ma_benchmark_premium", "benchmark_premium", &MaSvc::benchmark_deal_premium,
                            "Benchmark a deal premium against comparable deals: percentile rank and quartiles.",
                            QJsonObject{{"target_premium_pct", num("Deal premium in PERCENT (32.5 = 32.5%)")},
                                        {"comparables", deals_prop().value("deals")},
                                        {"industry", str("Filter comparables by industry (optional)")}},
                            {"target_premium_pct", "comparables"}));

    tools.push_back(ma_tool("ma_payment_structures_analysis", "payment_structures", &MaSvc::analyze_payment_structures,
                            "Payment mix across deals: all-cash / all-stock / mixed counts and average cash/stock %.",
                            deals_prop(), {"deals"}));

    tools.push_back(ma_tool("ma_industry_deals", "industry_deals", &MaSvc::analyze_industry_deals,
                            "Deal statistics by industry (optionally filtered to one industry).",
                            merge({deals_prop(), QJsonObject{{"industry", str("Industry filter (optional)")}}}),
                            {"deals"}));

    LOG_INFO(TAG, QString("Registered %1 M&A analytics tools").arg(tools.size()));
    return tools;
}

} // namespace fincept::mcp::tools
