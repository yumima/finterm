#include "screens/markets/overview/MarketTemplates.h"

#include "screens/dashboard/canvas/WidgetRegistry.h"
#include "services/futures/FuturesCatalog.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>
#include <optional>

namespace finterm::screens::markets {

namespace {

/// Tiles at explicit cells of the 12-column grid.
struct Packer {
    QVector<GridItem> items;
    void at(const QString& id, int x, int y, int w, int h, const QJsonObject& cfg = {}) {
        const auto* meta = WidgetRegistry::instance().find(id);
        if (!meta)
            return; // a widget this build does not have
        GridItem it;
        it.id = id;
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        it.cell = {x, y, w, h, meta->min_w, meta->min_h};
        it.config = cfg;
        items.append(it);
    }
    /// `rows`: the page's saved row count (it may leave empty rows below the
    /// tiles); never fewer than the tiles reach.
    GridLayout layout(int rows = 0) const {
        GridLayout l;
        l.cols = 12;
        l.items = items;
        l.rows = std::max(rows, max_row(items)); // the page is divided into exactly these rows
        return l;
    }
};

QJsonObject cfg(const char* json) {
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

QJsonObject root(const QString& r) {
    return QJsonObject{{"root", r}};
}

/// INDEX CONDITIONS with every index row (the template's own list is
/// replaced, whatever the generated snapshot held).
QJsonObject with_index_rows(QJsonObject c) {
    QJsonArray rows;
    for (const auto& [sym, label] : index_condition_rows())
        rows.append(QJsonObject{{"symbol", sym}, {"label", label}});
    c.insert(QStringLiteral("rows"), rows);
    return c;
}

// ── The desk's arrangement of each page ─────────────────────────────────────
// BEGIN GENERATED
// Generated from the desk's own arrangement of each page (2026-11-23; FUTURES groups re-taken
// that evening, all ten).
GridLayout arranged_markets() {
    Packer p;
    p.at("market_regime", 0, 0, 3, 3);
    p.at("market_chart", 3, 0, 6, 5, cfg(R"j({"label":"VVIX (vol of VIX)","symbol":"^VVIX"})j"));
    p.at("cond_vol", 9, 0, 3, 2);
    p.at("mstr_mnav", 9, 2, 3, 2);
    p.at("cond_fx", 0, 3, 3, 2);
    p.at("cond_crypto", 9, 4, 3, 3, cfg(R"j({"columns":["pct","p1y","trend"]})j"));
    p.at("quote_list", 0, 5, 3, 3, with_index_rows(cfg(R"j({"columns":["last","pct","vs200","p1y","trend"],"rows":[{"label":"S&P 500","symbol":"^GSPC"},{"label":"Nasdaq 100","symbol":"^NDX"},{"label":"Dow","symbol":"^DJI"},{"label":"Russell 2000","symbol":"^RUT"},{"label":"Euro Stoxx 50","symbol":"^STOXX50E"},{"label":"DAX","symbol":"^GDAXI"},{"label":"FTSE 100","symbol":"^FTSE"},{"label":"Nikkei 225","symbol":"^N225"},{"label":"Hang Seng","symbol":"^HSI"},{"label":"CSI 300","symbol":"000300.SS"}],"title":"INDEX CONDITIONS"})j")));
    return p.layout();
}

GridLayout arranged_equity() {
    Packer p;
    p.at("cond_equity", 0, 0, 3, 4);
    p.at("market_chart", 3, 0, 6, 5, cfg(R"j({"label":"Nasdaq 100 (QQQ)","symbol":"QQQ"})j"));
    p.at("top_movers", 9, 0, 3, 3);
    p.at("sector_heatmap", 0, 4, 3, 3);
    p.at("cond_sector_corr", 3, 5, 6, 4);
    return p.layout();
}

GridLayout arranged_rates() {
    Packer p;
    p.at("cond_macro", 0, 0, 3, 2);
    p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"France – Germany 10Y, pp","symbol":"FRED:IRLTLT01FRM156N-IRLTLT01DEM156N"})j"));
    p.at("cond_curve", 8, 0, 4, 6);
    p.at("cond_credit", 0, 2, 3, 4);
    p.at("cond_corr", 3, 6, 5, 4);
    p.at("sov_spreads", 0, 6, 3, 4);
    return p.layout();
}

GridLayout arranged_world() {
    Packer p;
    p.at("world_sessions", 0, 0, 12, 2);
    p.at("world_americas", 0, 2, 3, 3);
    p.at("market_chart", 3, 2, 6, 8, cfg(R"j({"label":"S&P 500","symbol":"^GSPC"})j"));
    p.at("world_asia_pacific", 9, 2, 3, 3);
    p.at("world_europe", 0, 5, 3, 4);
    p.at("world_us_rates", 9, 5, 3, 3);
    p.at("world_crypto", 9, 8, 3, 3);
    p.at("world_fx", 0, 9, 3, 3);
    p.at("world_commodities", 9, 11, 3, 3);
    p.at("world_us_futures", 0, 12, 3, 3);
    return p.layout();
}

GridLayout arranged_myview() {
    Packer p;
    p.at("quote_list", 0, 0, 3, 5, cfg(R"j({"columns":["last","chg","pct","high","low"],"rows":[{"label":"","symbol":"^GSPC"},{"label":"","symbol":"^IXIC"},{"label":"","symbol":"^DJI"},{"label":"","symbol":"^RUT"},{"label":"","symbol":"^VIX"},{"label":"","symbol":"^FTSE"},{"label":"","symbol":"^GDAXI"},{"label":"","symbol":"^N225"},{"label":"","symbol":"^FCHI"},{"label":"","symbol":"^HSI"},{"label":"","symbol":"^AXJO"},{"label":"","symbol":"000300.SS"},{"label":"","symbol":"000001.SS"},{"label":"","symbol":"^STOXX50E"},{"label":"","symbol":"^NYA"},{"label":"","symbol":"^SOX"},{"label":"","symbol":"^IBEX"},{"label":"","symbol":"^AEX"}],"title":"Stock Indices"})j"));
    p.at("market_chart", 3, 0, 6, 8, cfg(R"j({"label":"^GSPC","symbol":"^GSPC"})j"));
    p.at("cond_macro", 9, 0, 3, 5);
    p.at("cond_vol", 0, 5, 3, 4);
    p.at("cond_odds", 9, 5, 3, 4);
    p.at("cond_corr", 3, 8, 3, 5);
    p.at("cond_curve", 6, 8, 3, 5);
    p.at("cond_futures", 0, 9, 3, 10);
    p.at("quote_list", 9, 9, 3, 4, cfg(R"j({"columns":["last","chg","pct","high","low"],"rows":[{"label":"","symbol":"^TNX"},{"label":"","symbol":"^TYX"},{"label":"","symbol":"^IRX"},{"label":"","symbol":"^FVX"},{"label":"","symbol":"TLT"},{"label":"","symbol":"IEF"},{"label":"","symbol":"SHY"},{"label":"","symbol":"BND"},{"label":"","symbol":"AGG"},{"label":"","symbol":"LQD"},{"label":"","symbol":"HYG"},{"label":"","symbol":"JNK"}],"title":"Bonds"})j"));
    p.at("cond_commod", 3, 13, 3, 8);
    p.at("quote_list", 6, 13, 3, 8, cfg(R"j({"columns":["last","chg","pct","high","low"],"rows":[{"label":"","symbol":"EURUSD=X"},{"label":"","symbol":"GBPUSD=X"},{"label":"","symbol":"USDJPY=X"},{"label":"","symbol":"USDCHF=X"},{"label":"","symbol":"USDCAD=X"},{"label":"","symbol":"AUDUSD=X"},{"label":"","symbol":"NZDUSD=X"},{"label":"","symbol":"EURGBP=X"},{"label":"","symbol":"EURJPY=X"},{"label":"","symbol":"GBPJPY=X"},{"label":"","symbol":"USDCNY=X"},{"label":"","symbol":"USDKRW=X"},{"label":"","symbol":"USDHKD=X"},{"label":"","symbol":"USDTWD=X"},{"label":"","symbol":"USDSGD=X"},{"label":"","symbol":"JPY=X"},{"label":"","symbol":"EUR=X"}],"title":"Forex"})j"));
    p.at("cond_credit", 9, 13, 3, 8);
    p.at("quote_list", 0, 19, 3, 2, cfg(R"j({"columns":["last","chg","pct","high","low"],"rows":[{"label":"","symbol":"GC=F"},{"label":"","symbol":"SI=F"},{"label":"","symbol":"PL=F"},{"label":"","symbol":"PA=F"},{"label":"","symbol":"HG=F"},{"label":"","symbol":"CL=F"},{"label":"","symbol":"BZ=F"},{"label":"","symbol":"NG=F"},{"label":"","symbol":"RB=F"},{"label":"","symbol":"HO=F"},{"label":"","symbol":"ZC=F"},{"label":"","symbol":"ZW=F"},{"label":"","symbol":"ZS=F"},{"label":"","symbol":"KC=F"},{"label":"","symbol":"CT=F"},{"label":"","symbol":"SB=F"},{"label":"","symbol":"CC=F"},{"label":"","symbol":"LBR=F"}],"title":"Commodities"})j"));
    return p.layout();
}

/// A FUTURES group the desk arranged (the others: futures_page's default).
std::optional<GridLayout> arranged_futures(const QString& group) {
    if (group == QLatin1String("CRYPTO")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 5, cfg(R"j({"fixed":true,"group":"CRYPTO"})j"));
        p.at("market_chart", 3, 0, 5, 9, cfg(R"j({"label":"BTC · Bitcoin","symbol":"BTC=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"BTC"})j"));
        p.at("fut_months", 0, 5, 3, 5, cfg(R"j({"root":"BTC"})j"));
        p.at("fut_detail", 3, 9, 5, 7, cfg(R"j({"root":"BTC"})j"));
        return p.layout();
    }
    if (group == QLatin1String("ENERGY")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 3, cfg(R"j({"fixed":true,"group":"ENERGY"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"CL · WTI Crude Oil","symbol":"CL=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"CL"})j"));
        p.at("fut_months", 0, 3, 3, 6, cfg(R"j({"root":"CL"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"CL"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("FX")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 4, cfg(R"j({"fixed":true,"group":"FX"})j"));
        p.at("market_chart", 3, 0, 5, 7, cfg(R"j({"label":"6J · Japanese Yen","symbol":"6J=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"6J"})j"));
        p.at("fut_months", 0, 4, 3, 4, cfg(R"j({"root":"6J"})j"));
        p.at("fut_detail", 3, 7, 5, 5, cfg(R"j({"root":"6J"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("GRAINS")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 3, cfg(R"j({"fixed":true,"group":"GRAINS"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"ZC · Corn","symbol":"ZC=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"ZC"})j"));
        p.at("fut_months", 0, 3, 3, 4, cfg(R"j({"root":"ZC"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"ZC"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("INDEX")) {
        Packer p;
        p.at("fut_months", 0, 0, 3, 5, cfg(R"j({"root":"ES"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"ES · E-mini S&P 500","symbol":"ES=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"ES"})j"));
        p.at("fut_board", 0, 5, 3, 7, cfg(R"j({"fixed":true,"group":"INDEX"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"ES"})j"));
        return p.layout();
    }
    if (group == QLatin1String("LIVESTOCK")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 2, cfg(R"j({"fixed":true,"group":"LIVESTOCK"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"LE · Live Cattle","symbol":"LE=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"LE"})j"));
        p.at("fut_months", 0, 2, 3, 4, cfg(R"j({"root":"LE"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"LE"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("MAJORS")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 6, cfg(R"j({"fixed":true,"group":"MAJORS"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"SI · Silver","symbol":"SI=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"SI"})j"));
        p.at("fut_months", 0, 6, 3, 4, cfg(R"j({"root":"SI"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"SI"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("METALS")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 3, cfg(R"j({"fixed":true,"group":"METALS"})j"));
        p.at("market_chart", 3, 0, 5, 4, cfg(R"j({"label":"GC · Gold","symbol":"GC=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"GC"})j"));
        p.at("fut_months", 0, 3, 3, 7, cfg(R"j({"root":"GC"})j"));
        p.at("fut_detail", 3, 4, 5, 7, cfg(R"j({"root":"GC"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("RATES")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 4, cfg(R"j({"fixed":true,"group":"RATES"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"ZB · U.S. Treasury Bond","symbol":"ZB=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"ZB"})j"));
        p.at("fut_months", 0, 4, 3, 3, cfg(R"j({"root":"ZB"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"ZB"})j"));
        return p.layout(12);
    }
    if (group == QLatin1String("SOFTS")) {
        Packer p;
        p.at("fut_board", 0, 0, 3, 3, cfg(R"j({"fixed":true,"group":"SOFTS"})j"));
        p.at("market_chart", 3, 0, 5, 6, cfg(R"j({"label":"KC · Coffee C","symbol":"KC=F"})j"));
        p.at("fut_curve", 8, 0, 4, 6, cfg(R"j({"root":"KC"})j"));
        p.at("fut_months", 0, 3, 3, 4, cfg(R"j({"root":"KC"})j"));
        p.at("fut_detail", 3, 6, 5, 6, cfg(R"j({"root":"KC"})j"));
        return p.layout(12);
    }
    return std::nullopt;
}

// END GENERATED

/// A FUTURES page no one has arranged: one group's board (the header picks
/// the group), the chart, the forward curve, the contract months and the
/// detail tabs — following the page's clicks.
GridLayout futures_page(const QString& group) {
    if (auto l = arranged_futures(group))
        return *l;
    Packer p;
    const auto list = services::futures::contracts_in(group);
    const QString lead = list.isEmpty() ? QStringLiteral("CL") : list.first()->root;
    const auto* c = services::futures::find_contract(lead);
    p.at("fut_board", 0, 0, 3, 12, QJsonObject{{"group", group}, {"fixed", true}});
    p.at("market_chart", 3, 0, 5, 6,
         QJsonObject{{"symbol", services::futures::continuous_symbol(lead)},
                     {"label", c ? lead + QStringLiteral(" · ") + c->name : lead}});
    p.at("fut_curve", 8, 0, 4, 6, root(lead));
    p.at("fut_months", 3, 6, 5, 6, root(lead));
    p.at("fut_detail", 8, 6, 4, 6, root(lead));
    return p.layout();
}

} // namespace

const QVector<ScreenTemplate>& market_pages() {
    static const QVector<ScreenTemplate> k = {
        {"markets", "MARKETS", "Cross-asset conditions at a glance: a read per asset class, volatility, index "
                               "conditions, FX, crypto, Strategy's premium — and the chart they drive"},
        {"equity", "EQUITY", "Inside the US market: ETFs and SPDR sectors, the heatmap, sector correlation, top movers"},
        {"rates", "RATES & MACRO", "The Treasury curve, bonds and credit stress, fed funds / CPI / debt / CAPE, "
                                   "cross-asset correlation, sovereign spreads"},
        {"world", "WORLD", "Global markets: sessions, every region, US futures, rates, FX, commodities, crypto"},
    };
    return k;
}

const ScreenTemplate& my_view_template() {
    static const ScreenTemplate k{"myview", "MY VIEW",
                                  "Your screen — arrange it as you like; + adds more (blank or a copy of any page)"};
    return k;
}

GridLayout template_layout(const QString& id) {
    if (id == QLatin1String("markets"))
        return arranged_markets();
    if (id == QLatin1String("equity"))
        return arranged_equity();
    if (id == QLatin1String("rates"))
        return arranged_rates();
    if (id == QLatin1String("world"))
        return arranged_world();
    if (id == QLatin1String("myview"))
        return arranged_myview();
    if (id.startsWith(QLatin1String("futures:")))
        return futures_page(id.mid(8));
    GridLayout blank;
    blank.cols = 12;
    blank.rows = 12;
    return blank;
}

QString futures_group_for(const QString& r) {
    // MAJORS when the market is one (the page most read), else its own group.
    if (services::futures::major_roots().contains(r))
        return services::futures::majors_group();
    const auto* c = services::futures::find_contract(r);
    return c ? c->group : services::futures::majors_group();
}

const QVector<std::pair<QString, QString>>& index_condition_rows() {
    static const QVector<std::pair<QString, QString>> k = {
        {"^GSPC", "S&P 500"},           {"^NDX", "Nasdaq 100"},       {"^IXIC", "Nasdaq Composite"},
        {"^DJI", "Dow"},                {"^RUT", "Russell 2000"},     {"^NYA", "NYSE Composite"},
        {"^SOX", "Philadelphia Semis"}, {"^STOXX50E", "Euro Stoxx 50"}, {"^GDAXI", "DAX"},
        {"^FTSE", "FTSE 100"},          {"^FCHI", "CAC 40"},          {"^IBEX", "IBEX 35"},
        {"^AEX", "AEX"},                {"^N225", "Nikkei 225"},      {"^HSI", "Hang Seng"},
        {"000300.SS", "CSI 300"},       {"000001.SS", "Shanghai Composite"}, {"^AXJO", "ASX 200"},
    };
    return k;
}

} // namespace finterm::screens::markets
