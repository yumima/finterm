#include "screens/markets/overview/MarketTemplates.h"

#include "screens/dashboard/canvas/WidgetRegistry.h"
#include "screens/markets/MarketPanelStore.h"
#include "services/futures/FuturesCatalog.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

#include <algorithm>

namespace finterm::screens::markets {

namespace {

/// Left-to-right, top-to-bottom placement on the 12-column grid.
struct Packer {
    int x = 0, y = 0, row_h = 0;
    QVector<GridItem> items;
    void add(const QString& id, int w, int h, const QJsonObject& cfg = {}) {
        const auto* meta = WidgetRegistry::instance().find(id);
        if (!meta)
            return; // a widget this build does not have
        if (x + w > 12) {
            x = 0;
            y += row_h;
            row_h = 0;
        }
        GridItem it;
        it.id = id;
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        it.cell = {x, y, w, h, meta->min_w, meta->min_h};
        it.config = cfg;
        items.append(it);
        x += w;
        row_h = std::max(row_h, h);
    }
    /// At an explicit cell (x, y in grid units).
    void at(const QString& id, int x, int y, int w, int h, const QJsonObject& cfg = {}) {
        const auto* meta = WidgetRegistry::instance().find(id);
        if (!meta)
            return;
        GridItem it;
        it.id = id;
        it.instance_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        it.cell = {x, y, w, h, meta->min_w, meta->min_h};
        it.config = cfg;
        items.append(it);
    }
    GridLayout layout() const {
        GridLayout l;
        l.cols = 12;
        l.items = compact_vertical(items);
        return l;
    }
};

using Row = std::pair<const char*, const char*>; // symbol, label

QJsonObject list(const QString& title, std::initializer_list<Row> rows, const QStringList& columns) {
    QJsonArray arr;
    for (const auto& [s, l] : rows)
        arr.append(QJsonObject{{"symbol", QString::fromUtf8(s)}, {"label", QString::fromUtf8(l)}});
    return QJsonObject{{"title", title}, {"rows", arr}, {"columns", QJsonArray::fromStringList(columns)}};
}

QJsonObject chart(const QString& symbol, const QString& label) {
    return QJsonObject{{"symbol", symbol}, {"label", label}};
}

QJsonObject root(const QString& r) {
    return QJsonObject{{"root", r}};
}

const QStringList kPrice = {"last", "pct", "vs200", "p1y", "trend"};

// Four screens, nothing on two of them: what a desk reads, by job. Each is a
// 12 × 12 grid (it fits the view — rows shrink and stretch to it).

/// MARKETS — the cross-asset glance: what every class is doing, now.
GridLayout markets_screen() {
    Packer p;
    p.at("market_regime", 0, 0, 4, 6);
    p.at("market_chart", 4, 0, 5, 6, chart("^GSPC", "S&P 500"));
    p.at("cond_vol", 9, 0, 3, 6);
    // Readings, not the price board (that is WORLD): where each index stands.
    p.at("quote_list", 0, 6, 4, 6,
         list("INDEX CONDITIONS",
              {{"^GSPC", "S&P 500"}, {"^NDX", "Nasdaq 100"}, {"^DJI", "Dow"}, {"^RUT", "Russell 2000"},
               {"^STOXX50E", "Euro Stoxx 50"}, {"^GDAXI", "DAX"}, {"^FTSE", "FTSE 100"}, {"^N225", "Nikkei 225"},
               {"^HSI", "Hang Seng"}, {"000300.SS", "CSI 300"}},
              kPrice));
    p.at("cond_fx", 4, 6, 3, 6);
    p.at("cond_crypto", 7, 6, 2, 6, QJsonObject{{"columns", QJsonArray{"pct", "p1y", "trend"}}});
    p.at("mstr_mnav", 9, 6, 3, 6);
    return p.layout();
}

/// EQUITY — inside the US market: sectors, leadership, how they move together.
GridLayout equity_screen() {
    Packer p;
    p.at("market_chart", 0, 0, 6, 6, chart("SPY", "S&P 500 (SPY)"));
    p.at("cond_equity", 6, 0, 3, 12);
    p.at("sector_heatmap", 9, 0, 3, 6);
    p.at("cond_sector_corr", 0, 6, 6, 6);
    p.at("top_movers", 9, 6, 3, 6);
    return p.layout();
}

/// RATES & MACRO — the curve, credit, the economy, valuation, correlation.
GridLayout rates_screen() {
    Packer p;
    p.at("cond_curve", 0, 0, 4, 6);
    p.at("market_chart", 4, 0, 5, 6, chart("FRED:T10Y2Y", "10Y–2Y Treasury spread"));
    p.at("cond_credit", 9, 0, 3, 6);
    p.at("cond_macro", 0, 6, 4, 6);
    p.at("cond_corr", 4, 6, 5, 6);
    p.at("sov_spreads", 9, 6, 3, 6);
    return p.layout();
}

/// A FUTURES page: one group's board (the header picks the group), the
/// chart, the forward curve, the contract months and the detail tabs —
/// the old FUTURES screen, as widgets following the page's clicks.
GridLayout futures_page(const QString& group) {
    Packer p;
    const auto list = services::futures::contracts_in(group);
    const QString lead = list.isEmpty() ? QStringLiteral("CL") : list.first()->root;
    const auto* c = services::futures::find_contract(lead);
    p.at("fut_board", 0, 0, 3, 12, QJsonObject{{"group", group}, {"fixed", true}});
    p.at("market_chart", 3, 0, 5, 6,
         chart(services::futures::continuous_symbol(lead), c ? lead + QStringLiteral(" · ") + c->name : lead));
    p.at("fut_curve", 8, 0, 4, 6, root(lead));
    p.at("fut_months", 3, 6, 5, 6, root(lead));
    p.at("fut_detail", 8, 6, 4, 6, root(lead));
    return p.layout();
}

/// WORLD — the global price board as tiles: sessions, each region and
/// section, and the chart they drive (embedded, never a pop-up).
GridLayout world() {
    Packer p;
    p.at("world_sessions", 0, 0, 12, 2);
    p.at("world_americas", 0, 2, 4, 4);
    p.at("world_europe", 4, 2, 4, 4);
    p.at("world_asia_pacific", 8, 2, 4, 4);
    p.at("market_chart", 0, 6, 4, 6, chart("^GSPC", "S&P 500"));
    p.at("world_us_futures", 4, 6, 4, 3);
    p.at("world_us_rates", 8, 6, 4, 3);
    p.at("world_fx", 4, 9, 3, 3);
    p.at("world_commodities", 7, 9, 3, 3);
    p.at("world_crypto", 10, 9, 2, 3); // volatility is on MARKETS, with its percentiles
    return p.layout();
}

/// The user's PANELS (only when they made their own): a Quote List each.
GridLayout panels() {
    static const QHash<QString, QString> col = {{"LAST", "last"}, {"CHG", "chg"}, {"CHG%", "pct"}, {"HIGH", "high"},
                                                {"LOW", "low"},   {"VOL", "vol"}, {"BID", "bid"},  {"ASK", "ask"}};
    Packer p;
    for (const auto& cfg : MarketPanelStore::instance().load()) {
        QJsonArray rows;
        for (const auto& s : cfg.symbols)
            rows.append(QJsonObject{{"symbol", s}, {"label", QString()}});
        QStringList cols;
        for (const auto& c : cfg.column_order)
            if (const QString k = col.value(c); !k.isEmpty() && !cols.contains(k))
                cols << k;
        p.add("quote_list", 4, 6,
              QJsonObject{{"title", cfg.title}, {"rows", rows}, {"columns", QJsonArray::fromStringList(cols)}});
    }
    return p.layout();
}

} // namespace

const QVector<ScreenTemplate>& default_screens() {
    static const QVector<ScreenTemplate> k = {
        {"markets", "MARKETS", "Cross-asset conditions at a glance: a read per asset class, volatility, index "
                               "conditions, FX, crypto, Strategy's premium — and the chart they drive"},
        {"equity", "EQUITY", "Inside the US market: SPDR sectors, the heatmap, sector correlation, top movers"},
        {"rates", "RATES & MACRO", "The Treasury curve and 10Y–2Y, credit stress, fed funds / CPI / debt / CAPE, "
                                   "cross-asset correlation, sovereign spreads"},
    };
    return k;
}

const QVector<ScreenTemplate>& extra_templates() {
    static const QVector<ScreenTemplate> k = {
        {"world", "WORLD", "Global markets: sessions, every region, US futures, rates, FX, commodities, crypto, vol"},
    };
    return k;
}

GridLayout template_layout(const QString& id) {
    if (id == QLatin1String("markets"))
        return markets_screen();
    if (id == QLatin1String("equity"))
        return equity_screen();
    if (id == QLatin1String("rates"))
        return rates_screen();
    if (id.startsWith(QLatin1String("futures:")))
        return futures_page(id.mid(8));
    if (id == QLatin1String("world"))
        return world();
    if (id == QLatin1String("panels"))
        return panels();
    GridLayout blank;
    blank.cols = 12;
    return blank;
}

QString futures_group_for(const QString& r) {
    // MAJORS when the market is one (the page most read), else its own group.
    if (services::futures::major_roots().contains(r))
        return services::futures::majors_group();
    const auto* c = services::futures::find_contract(r);
    return c ? c->group : services::futures::majors_group();
}

} // namespace finterm::screens::markets
