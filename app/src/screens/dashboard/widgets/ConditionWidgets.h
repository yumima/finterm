#pragma once
// ConditionWidgets.h — the market-condition widgets, built ONE way for both
// places they appear: the dashboard (WidgetRegistry, category "Market
// Conditions") and Markets ▸ OVERVIEW. Default lists are the ones asked for
// in the "Dashboard" design doc; every list is user-editable in place.
//
// What the doc asked for that has no free, real source is NOT shown (and the
// widget's note says so): DX / UC futures (not on Yahoo, no SGX feed),
// sovereign CDS spreads, the Baltic Dry Index itself (BDRY, the dry-bulk
// freight-futures ETF, is listed and labelled as what it is), CAPE / forward
// P/E (no free official series).

#include "screens/dashboard/widgets/ConditionTableWidget.h"
#include "screens/dashboard/widgets/CorrelationWidget.h"
#include "screens/dashboard/widgets/MacroOddsWidget.h"
#include "screens/dashboard/widgets/YieldCurveWidget.h"
#include "screens/markets/world/WorldMarketsModel.h"
#include "services/futures/FuturesCatalog.h"
#include "ui/theme/Theme.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cmath>

namespace finterm::screens::widgets {

struct ConditionWidgetDef {
    QString type_id;
    QString name;
    QString description;
    int w = 4, h = 5;
};

/// Every condition widget, in reading order.
inline const QVector<ConditionWidgetDef>& condition_widget_defs() {
    static const QVector<ConditionWidgetDef> k = {
        {"cond_futures", "Futures Majors", "Front-month futures — live price, trend, 200-day and 1-year position", 4, 6},
        {"cond_equity", "ETFs & SPDR Sectors", "SPY, QQQ, DIA and the 11 SPDR sectors with trend, 200-day, percentile", 4, 6},
        {"cond_vol", "Volatility", "VIX, VVIX, VXN and MOVE with their 1-year percentile and VIX term structure", 4, 4},
        {"cond_curve", "US Yield Curve", "Treasury par curve vs 1M / 1Y ago, and spreads (2s10s, 3M10Y…)", 4, 6},
        {"cond_credit", "Credit Stress", "High-yield and investment-grade OAS (FRED), MOVE, HYG / LQD / BND", 4, 5},
        {"cond_macro", "Macro Indicators", "Fed funds, CPI YoY, US debt/GDP, CAPE — level and 10-year percentile", 4, 5},
        {"cond_commod", "Commodity Conditions", "Gold & miners, silver, gold futures, crude, gas, copper, GSCI, dry bulk", 4, 6},
        {"cond_fx", "FX Spot", "Dollar index and major pairs incl. USD/CNH, with trend and percentile", 4, 4},
        {"cond_crypto", "Crypto Spot", "Bitcoin, Ether and MSTR with trend and percentile", 4, 3},
        {"cond_corr", "Cross-Asset Correlation", "63-day correlation of daily changes — S&P, 10Y, BTC, gold, oil, copper",
         6, 5},
        {"cond_sector_corr", "Sector Correlation", "63-day correlation of the S&P 500 and its 11 sector ETFs", 6, 6},
        {"cond_odds", "Prediction Odds (Kalshi)", "Kalshi odds on Fed decisions, recession, CPI — a reference", 4, 6},
    };
    return k;
}

namespace condition_detail {

inline QVector<ConditionRowDef> futures_majors() {
    QVector<ConditionRowDef> out;
    for (const auto* c : services::futures::contracts_in(services::futures::majors_group()))
        // Root first: in a narrow tile the name elides, the root stays.
        out.append({services::futures::continuous_symbol(c->root), c->root + QStringLiteral(" · ") + c->name});
    return out;
}

inline QVector<ConditionRowDef> sectors_with_spx() {
    return {{"^GSPC", "S&P 500"}, {"XLK", "Tech"},       {"XLF", "Financials"}, {"XLV", "Health"},
            {"XLE", "Energy"},    {"XLI", "Industrials"}, {"XLY", "Discretionary"}, {"XLP", "Staples"},
            {"XLU", "Utilities"}, {"XLB", "Materials"},  {"XLRE", "Real Estate"}, {"XLC", "Comm Svcs"}};
}

} // namespace condition_detail

/// Build condition widget `type_id` with its persisted config (empty = the
/// default list). nullptr for an unknown id.
inline BaseWidget* create_condition_widget(const QString& type_id, const QJsonObject& cfg, QWidget* parent = nullptr) {
    using M = ConditionTableWidget::Mode;
    if (type_id == QLatin1String("quote_list")) {
        // Markets ▸ OVERVIEW's list (what the PANELS tiles were): live quotes,
        // columns chosen per list (right-click ▸ Columns), condition columns too.
        auto* w = new ConditionTableWidget(QStringLiteral("QUOTE LIST"), M::Market,
                                           {{"^GSPC", "S&P 500"},
                                            {"^NDX", "Nasdaq 100"},
                                            {"^DJI", "Dow Jones"},
                                            {"^RUT", "Russell 2000"},
                                            {"^VIX", "VIX"}},
                                           {}, ui::colors::AMBER(), parent);
        w->set_default_columns({"last", "chg", "pct", "high", "low"});
        w->apply_config(cfg);
        return w;
    }
    if (type_id == QLatin1String("cond_futures")) {
        auto* w = new ConditionTableWidget(QStringLiteral("FUTURES MAJORS"), M::Market, condition_detail::futures_majors(),
                                           cfg, ui::colors::AMBER(), parent);
        w->set_note(QStringLiteral("Continuous front month (Yahoo, ~10 min delayed). Click a row: its chart, and its "
                                   "curve and months on the screen's futures widgets."));
        return w;
    }
    if (type_id == QLatin1String("cond_equity"))
        return new ConditionTableWidget(QStringLiteral("ETFs & SPDR SECTORS"), M::Market,
                                        {{"SPY", "S&P 500 (SPY)"},
                                         {"QQQ", "Nasdaq 100 (QQQ)"},
                                         {"DIA", "Dow (DIA)"},
                                         {"XLK", "Tech (XLK)"},
                                         {"XLF", "Financials (XLF)"},
                                         {"XLV", "Health (XLV)"},
                                         {"XLE", "Energy (XLE)"},
                                         {"XLI", "Industrials (XLI)"},
                                         {"XLY", "Discretionary (XLY)"},
                                         {"XLP", "Staples (XLP)"},
                                         {"XLU", "Utilities (XLU)"},
                                         {"XLB", "Materials (XLB)"},
                                         {"XLRE", "Real Estate (XLRE)"},
                                         {"XLC", "Comm Svcs (XLC)"}},
                                        cfg, ui::colors::POSITIVE(), parent);
    if (type_id == QLatin1String("cond_vol")) {
        auto* w = new ConditionTableWidget(
            QStringLiteral("VOLATILITY"), M::Market,
            {{"^VIX", "VIX"}, {"^VVIX", "VVIX (vol of VIX)"}, {"^VXN", "VXN (Nasdaq vol)"}, {"^MOVE", "MOVE (Treasury vol)"}},
            cfg, ui::colors::NEGATIVE(), parent);
        // The WORLD board's term-structure reading, from the same live quotes.
        w->set_footer({"^VIX", "^VIX3M"}, [](const QHash<QString, services::QuoteData>& q) {
            const double vix = q.contains("^VIX") ? q.value("^VIX").price : world::kNaN;
            const double v3m = q.contains("^VIX3M") ? q.value("^VIX3M").price : world::kNaN;
            const auto t = world::vix_term(vix, v3m);
            if (t.label.isEmpty())
                return QString();
            return QStringLiteral("VIX / VIX3M %1 — %2").arg(QString::number(t.ratio, 'f', 2), t.label);
        });
        return w;
    }
    if (type_id == QLatin1String("cond_curve"))
        return new YieldCurveWidget(cfg, parent);
    if (type_id == QLatin1String("cond_credit")) {
        auto* w = new ConditionTableWidget(QStringLiteral("BONDS & CREDIT"), M::Market,
                                           {{"FRED:BAMLH0A0HYM2", "High-yield OAS, %"},
                                            {"FRED:BAMLC0A0CM", "Investment-grade OAS, %"},
                                            {"^MOVE", "MOVE (Treasury vol)"},
                                            {"HYG", "High yield (HYG)"},
                                            {"LQD", "IG corporate (LQD)"},
                                            {"BND", "Total bond (BND)"}},
                                           cfg, ui::colors::CYAN(), parent);
        w->set_note(QStringLiteral("OAS: ICE BofA option-adjusted spreads via FRED (daily, a day behind) — the "
                                   "credit-stress gauges; a high percentile is stress."));
        return w;
    }
    if (type_id == QLatin1String("cond_macro")) {
        auto* w = new ConditionTableWidget(QStringLiteral("MACRO INDICATORS"), M::Macro,
                                           {{"FRED:DFF", "Fed funds (effective), %"},
                                            {"FRED:CPIAUCSL@YOY", "CPI YoY"},
                                            {"FRED:GFDEGDQ188S", "Federal debt / GDP, %"},
                                            {"SHILLER:CAPE", "CAPE (Shiller P/E)"}},
                                           cfg, ui::colors::AMBER(), parent);
        w->set_note(QStringLiteral("FRED, St. Louis Fed; CAPE: Robert Shiller's data (shillerdata.com, monthly — "
                                   "S&P 500 price / 10-year average real earnings). Add any FRED series with + ADD."));
        return w;
    }
    if (type_id == QLatin1String("cond_commod")) {
        auto* w = new ConditionTableWidget(QStringLiteral("COMMODITY CONDITIONS"), M::Market,
                                           {{"GLD", "Gold ETF (GLD)"},
                                            {"GDX", "Gold miners (GDX)"},
                                            {"SLV", "Silver ETF (SLV)"},
                                            {"GC=F", "Gold futures (GC)"},
                                            {"CL=F", "WTI crude (CL)"},
                                            {"NG=F", "Natural gas (NG)"},
                                            {"HG=F", "Copper (HG)"},
                                            {"^SPGSCI", "S&P GSCI"},
                                            {"BDRY", "Dry-bulk freight ETF (BDRY)"}},
                                           cfg, ui::colors::WARNING(), parent);
        w->set_note(QStringLiteral("BDRY holds Capesize / Panamax / Supramax freight futures — it tracks dry-bulk "
                                   "rates, not the Baltic Dry Index itself, which has no free feed. Gold is COMEX GC: "
                                   "Yahoo's E-mini gold (QO=F) history is not a gold series."));
        return w;
    }
    if (type_id == QLatin1String("cond_fx")) {
        auto* w = new ConditionTableWidget(QStringLiteral("FX SPOT"), M::Market,
                                           {{"DX-Y.NYB", "Dollar index (DXY)"},
                                            {"EURUSD=X", "EUR/USD"},
                                            {"USDJPY=X", "USD/JPY"},
                                            {"GBPUSD=X", "GBP/USD"},
                                            {"CNH=X", "USD/CNH"}},
                                           cfg, ui::colors::CYAN(), parent);
        w->set_note(QStringLiteral("ICE DX futures and SGX USD/CNH (UC) futures have no free feed; the spot index "
                                   "and spot USD/CNH are shown."));
        return w;
    }
    if (type_id == QLatin1String("cond_crypto"))
        return new ConditionTableWidget(QStringLiteral("CRYPTO SPOT"), M::Market,
                                        {{"BTC-USD", "Bitcoin"}, {"ETH-USD", "Ether"}, {"MSTR", "Strategy (MSTR)"}}, cfg,
                                        ui::colors::AMBER(), parent);
    if (type_id == QLatin1String("cond_corr"))
        return new CorrelationWidget(QStringLiteral("CROSS-ASSET CORRELATION"),
                                     {{"^GSPC", "S&P 500"},
                                      {"^TNX", "US 10Y"},
                                      {"BTC-USD", "Bitcoin"},
                                      {"GC=F", "Gold"},
                                      {"CL=F", "Oil"},
                                      {"HG=F", "Copper"}},
                                     cfg, parent);
    if (type_id == QLatin1String("cond_sector_corr"))
        return new CorrelationWidget(QStringLiteral("SECTOR CORRELATION"), condition_detail::sectors_with_spx(), cfg,
                                     parent);
    if (type_id == QLatin1String("sov_spreads")) {
        auto* w = new ConditionTableWidget(
            QStringLiteral("SOVEREIGN 10Y SPREADS"), M::Macro,
            {{"FRED:IRLTLT01ITM156N-IRLTLT01DEM156N", "Italy – Germany 10Y, pp"},
             {"FRED:IRLTLT01FRM156N-IRLTLT01DEM156N", "France – Germany 10Y, pp"},
             {"FRED:IRLTLT01ESM156N-IRLTLT01DEM156N", "Spain – Germany 10Y, pp"},
             {"FRED:IRLTLT01GBM156N-IRLTLT01USM156N", "UK – US 10Y, pp"},
             {"FRED:IRLTLT01JPM156N-IRLTLT01USM156N", "Japan – US 10Y, pp"}},
            cfg, ui::colors::NEGATIVE(), parent);
        w->set_add_sovereign(true);
        w->set_note(QStringLiteral("Government bond yield spreads (OECD, monthly, via FRED) — the free stand-in for "
                                   "sovereign CDS, which has no free feed. + ADD: any country."));
        return w;
    }
    if (type_id == QLatin1String("cond_odds"))
        return new MacroOddsWidget(cfg, parent);
    return nullptr;
}

} // namespace finterm::screens::widgets
