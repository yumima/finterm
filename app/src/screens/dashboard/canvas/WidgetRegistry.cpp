#include "screens/dashboard/canvas/WidgetRegistry.h"

#include "screens/dashboard/widgets/AgentErrorsWidget.h"
#include "screens/dashboard/widgets/BrokerHoldingsWidget.h"
#include "screens/dashboard/widgets/CommoditiesWidget.h"
#include "screens/dashboard/widgets/ConditionWidgets.h"
#include "screens/dashboard/widgets/CryptoTickerWidget.h"
#include "screens/dashboard/widgets/CryptoWidget.h"
#include "screens/dashboard/widgets/EarningsCalendarWidget.h"
#include "screens/dashboard/widgets/EconomicCalendarWidget.h"
#include "screens/dashboard/widgets/ForexWidget.h"
#include "screens/dashboard/widgets/IndicesWidget.h"
#include "screens/dashboard/widgets/IpoCalendarWidget.h"
#include "screens/dashboard/widgets/MarginUsageWidget.h"
#include "screens/dashboard/widgets/MarketChartWidget.h"
#include "screens/dashboard/widgets/MarketsWidgets.h"
#include "screens/futures/FuturesWidgets.h"
#include "screens/dashboard/widgets/MarketQuoteStripWidget.h"
#include "screens/dashboard/widgets/MarketSentimentWidget.h"
#include "screens/dashboard/widgets/NewsCategoryWidget.h"
#include "screens/dashboard/widgets/NewsWidget.h"
#include "screens/dashboard/widgets/OpenPositionsWidget.h"
#include "screens/dashboard/widgets/OrderBookMiniWidget.h"
#include "screens/dashboard/widgets/OwnershipCalendarWidget.h"
#include "screens/dashboard/widgets/PerformanceWidget.h"
#include "screens/dashboard/widgets/PolymarketPriceWidget.h"
#include "screens/dashboard/widgets/PortfolioSummaryWidget.h"
#include "screens/dashboard/widgets/QuickTradeWidget.h"
#include "screens/dashboard/widgets/RecentFilesWidget.h"
#include "screens/dashboard/widgets/RiskMetricsWidget.h"
#include "screens/dashboard/widgets/ScreenerWidget.h"
#include "screens/dashboard/widgets/SectorHeatmapWidget.h"
#include "screens/dashboard/widgets/SparklineStripWidget.h"
#include "screens/dashboard/widgets/StockQuoteWidget.h"
#include "screens/dashboard/widgets/TodayPnLWidget.h"
#include "screens/dashboard/widgets/TopMoversWidget.h"
#include "screens/dashboard/widgets/TradeTapeWidget.h"
#include "screens/dashboard/widgets/VideoPlayerWidget.h"
#include "screens/dashboard/widgets/WatchlistWidget.h"
#include "screens/dashboard/widgets/WebScraperWidget.h"
#include "screens/markets/world/WorldMarketsModel.h"
#include "screens/markets/world/WorldMarketsView.h"
#include "screens/pre_ipo/PrivateMarketWidgets.h"

namespace finterm::screens {

WidgetRegistry& WidgetRegistry::instance() {
    static WidgetRegistry inst;
    return inst;
}

WidgetRegistry::WidgetRegistry() {
    // 12-column grid: default_w, default_h, min_w, min_h
    // min_h is 1 row across the board — default_h still opens the widget at a
    // comfortable height, but nothing stops the user dragging it down to a
    // strip. A larger min_h only ever produced blank bands the user couldn't
    // get rid of; content that no longer fits is clipped, which is visible and
    // recoverable, unlike a resize that silently refuses to move.
    // Factory receives per-instance config (empty QJsonObject for fresh widgets).
    // Existing widgets ignore it until they opt into configurable behaviour.

    // ── Markets ───────────────────────────────────────────────────────────────
    register_widget({"indices", "Market Indices", "Markets", "Major global indices, with their index futures live", 4,
                     5, 3, 1, [](const QJsonObject&) { return widgets::create_indices_widget(); }});

    register_widget({"forex", "Forex Pairs", "Markets", "Major FX pairs — EURUSD, GBPUSD, USDJPY", 4, 4, 2, 1,
                     [](const QJsonObject&) { return widgets::create_forex_widget(); }});

    register_widget({"crypto", "Crypto Markets", "Markets", "Top cryptocurrencies — BTC, ETH, BNB, SOL", 4, 4, 2, 1,
                     [](const QJsonObject&) { return widgets::create_crypto_widget(); }});

    register_widget({"commodities", "Commodities", "Markets", "Gold, Oil, Natural Gas, Copper", 4, 4, 2, 1,
                     [](const QJsonObject&) { return widgets::create_commodities_widget(); }});

    register_widget({"sector_heatmap", "Sector Heatmap", "Markets", "S&P 500 sector performance heatmap", 6, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::SectorHeatmapWidget; }});

    register_widget({"top_movers", "Top Movers", "Markets", "Biggest gainers and losers today", 6, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::TopMoversWidget; }});

    register_widget({"sentiment", "Market Sentiment", "Markets", "Fear & greed, bull/bear indicators", 4, 4, 2, 1,
                     [](const QJsonObject&) { return new widgets::MarketSentimentWidget; }});

    register_widget({"quote_list", "Quote List", "Markets",
                     "Your own list of symbols — live quotes, columns you choose (incl. 200-day, percentile, trend); "
                     "+ ADD / − in place",
                     3, 4, 2, 1, [](const QJsonObject& cfg) { return widgets::create_condition_widget("quote_list", cfg); }});
    register_widget({"market_chart", "Chart (linked)", "Markets",
                     "A price chart that follows the rows you click on the same canvas — periods, MAs, condition strip",
                     6, 8, 3, 2, [](const QJsonObject& cfg) { return new widgets::MarketChartWidget(cfg); }});

    register_widget({"market_regime", "Asset-Class Conditions", "Market Conditions",
                     "One read per asset class — risk-on/off, stress/calm, trend — from each benchmark's readings", 4,
                     6, 3, 1, [](const QJsonObject&) { return new widgets::MarketRegimeWidget; }});
    register_widget({"sov_spreads", "Sovereign Spreads", "Market Conditions",
                     "10-year government bond spreads vs Germany / the US, any OECD country (FRED, monthly)", 3, 6, 2,
                     1, [](const QJsonObject& cfg) { return widgets::create_condition_widget("sov_spreads", cfg); }});
    register_widget({"mstr_mnav", "MSTR Premium (mNAV)", "Market Conditions",
                     "Strategy's market cap against the bitcoin it holds (8-K holdings, live prices)", 3, 6, 2, 1,
                     [](const QJsonObject&) { return new widgets::MstrPremiumWidget; }});
    register_widget({"world_board", "World Markets", "Markets",
                     "Global markets on one board — sessions, regions, US futures, rates, FX, commodities, crypto, vol",
                     12, 12, 6, 4, [](const QJsonObject&) { return new widgets::WorldBoardWidget; }});
    // The board's parts, each a tile (the WORLD screen is built of them).
    register_widget({"world_sessions", "World Sessions", "Markets",
                     "Exchange clocks (open / closed, time to the next bell) and the overnight line", 12, 2, 4, 1,
                     [](const QJsonObject&) { return new widgets::WorldBoardWidget(world::WorldMarketsView::kSessionsPart); }});
    for (const auto& sec : world::world_sections()) {
        const QString title = sec.title;
        QString id = QStringLiteral("world_") + title.toLower();
        id.replace(QLatin1Char(' '), QLatin1Char('_')).replace(QLatin1Char('-'), QLatin1Char('_'));
        register_widget({id, QStringLiteral("World · ") + title, "Markets",
                         sec.note.isEmpty() ? QStringLiteral("The WORLD board's %1 section").arg(title) : sec.note, 4, 4,
                         2, 1, [title](const QJsonObject&) { return new widgets::WorldBoardWidget(title); }});
    }

    // ── Futures (the MARKETS FUTURES pages) ─────────────────────────────────
    register_widget({"fut_board", "Futures Board", "Futures",
                     "Every root of a futures group, live front month — click one: the page follows", 3, 12, 2, 2,
                     [](const QJsonObject& cfg) { return new widgets::FuturesBoardWidget(cfg); }});
    register_widget({"fut_curve", "Forward Curve", "Futures",
                     "A futures market's strip and its curve now vs 1W / 1M ago (follows the page's clicks)", 4, 6, 3, 2,
                     [](const QJsonObject& cfg) { return new widgets::FuturesCurveWidget(cfg); }});
    register_widget({"fut_months", "Contract Months", "Futures",
                     "Every listed month: last, change, spread to the front, carry, volume, OI, last day", 5, 6, 3, 2,
                     [](const QJsonObject& cfg) { return new widgets::FuturesMonthsWidget(cfg); }});
    register_widget({"fut_detail", "Futures Detail", "Futures",
                     "Calendar spread · CFTC positioning · paper ticket · contract terms", 4, 6, 3, 2,
                     [](const QJsonObject& cfg) { return new widgets::FuturesDetailWidget(cfg); }});

    // ── Market Conditions (also on the MARKETS screens) ─────────────────────
    // Indicators beyond the price: trend, 200-day, percentile, curve,
    // correlation, macro and prediction-market odds. Every list is editable
    // in place (+ ADD / −).
    for (const auto& d : widgets::condition_widget_defs()) {
        const QString id = d.type_id;
        register_widget({id, d.name, "Market Conditions", d.description, d.w, d.h, 3, 1,
                         [id](const QJsonObject& cfg) { return widgets::create_condition_widget(id, cfg); }});
    }

    // ── Private Market (also the PRIVATE MARKET screen's MARKET canvas) ────────
    register_widget({"pm_c50", "C50 Private Market Index", "Private Market",
                     "Clarity Liquidity Index — the 50 most liquid late-stage private companies", 6, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::PmC50Widget; }});
    register_widget({"pm_upcoming", "Upcoming IPOs", "Private Market",
                     "Operating-company IPOs in the next 60 days, with the projected date and its basis", 6, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::PmUpcomingIposWidget; }});
    register_widget({"pm_gainers", "Private Gainers · 6M", "Private Market",
                     "Private companies with the largest 6-month rise in marketplace model price", 4, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::PmMoversWidget(true); }});
    register_widget({"pm_losers", "Private Losers · 6M", "Private Market",
                     "Private companies with the largest 6-month fall in marketplace model price", 4, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::PmMoversWidget(false); }});
    register_widget({"pm_valuable", "Most Valuable Private Cos", "Private Market",
                     "Implied (price × round shares) or last-round valuation, with IPO status", 4, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::PmMostValuableWidget; }});
    register_widget({"pm_rounds", "Recent Rounds & Tenders", "Private Market",
                     "Primary rounds and tender offers closed in the last six months", 6, 4, 3, 1,
                     [](const QJsonObject&) { return new widgets::PmRecentRoundsWidget; }});

    // ── Research ──────────────────────────────────────────────────────────────
    register_widget({"news", "News Feed", "Research", "Latest financial news headlines", 8, 4, 3, 1,
                     [](const QJsonObject&) { return new widgets::NewsWidget; }});

    register_widget({"stock_quote", "Stock Quote", "Research", "Single stock detail — price, volume, chart", 4, 5, 2, 1,
                     [](const QJsonObject& cfg) {
                         const QString sym = cfg.value("symbol").toString("AAPL");
                         return new widgets::StockQuoteWidget(sym);
                     }});

    register_widget({"screener", "Stock Screener", "Research", "Filter stocks by fundamentals and technicals", 6, 5, 3,
                     1, [](const QJsonObject&) { return new widgets::ScreenerWidget; }});

    register_widget({"econ_calendar", "Economic Calendar", "Research", "Upcoming macro events and releases", 4, 4, 2, 1,
                     [](const QJsonObject&) { return new widgets::EconomicCalendarWidget; }});

    register_widget({"ipo_calendar", "IPO Calendar", "Research",
                     "IPOs priced in the last 30 days, and expected in the next 7 or 30", 4, 5, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::IpoCalendarWidget(cfg); }});

    register_widget({"ownership_calendar", "Ownership Calendar", "Research",
                     "13F deadlines, short-interest publication dates, IPO lock-up expiries", 4, 5, 2, 1,
                     [](const QJsonObject&) { return new widgets::OwnershipCalendarWidget; }});

    register_widget({"earnings_calendar", "Earnings Calendar", "Research",
                     "Upcoming earnings — this week's tape, or just your holdings", 4, 5, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::EarningsCalendarWidget(cfg); }});

    // ── Portfolio ─────────────────────────────────────────────────────────────
    register_widget({"watchlist", "Watchlist", "Portfolio", "Your saved symbols with live prices", 6, 4, 2, 1,
                     [](const QJsonObject&) { return new widgets::WatchlistWidget; }});

    register_widget({"performance", "Performance", "Portfolio", "Portfolio P&L — today, week, month, YTD", 4, 5, 3, 1,
                     [](const QJsonObject&) { return new widgets::PerformanceWidget; }});

    register_widget({"portfolio_summary", "Portfolio Summary", "Portfolio",
                     "Holdings overview with allocation breakdown", 6, 4, 2, 1,
                     [](const QJsonObject&) { return new widgets::PortfolioSummaryWidget; }});

    register_widget({"risk_metrics", "Risk Metrics", "Portfolio", "Volatility, beta, drawdown, Sharpe ratio", 4, 5, 3,
                     1, [](const QJsonObject&) { return new widgets::RiskMetricsWidget; }});

    // ── Trading ───────────────────────────────────────────────────────────────
    register_widget({"quick_trade", "Quick Trade", "Trading", "Fast order entry for crypto and equities", 4, 5, 2, 1,
                     [](const QJsonObject&) { return new widgets::QuickTradeWidget; }});

    register_widget({"open_positions", "Open Positions", "Trading",
                     "Live open positions for a broker account — pick via gear icon", 6, 5, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::OpenPositionsWidget(cfg); }});

    register_widget({"working_orders", "Working Orders", "Trading",
                     "Pending/working orders for a broker account — click × to cancel", 6, 5, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::OrderBookMiniWidget(cfg); }});

    register_widget({"margin_usage", "Margin Usage", "Trading",
                     "Broker account funds — available, used margin, total, usage %", 3, 4, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::MarginUsageWidget(cfg); }});

    register_widget({"today_pnl", "Today P&L", "Trading",
                     "Aggregate broker account P&L — total, day, realized, open positions", 3, 4, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::TodayPnLWidget(cfg); }});

    register_widget({"holdings", "Holdings", "Portfolio", "Long-term broker holdings — avg cost, LTP, P&L %", 6, 5, 3,
                     1, [](const QJsonObject& cfg) { return new widgets::BrokerHoldingsWidget(cfg); }});

    // ── Tools ────────────────────────────────────────────────────────────────
    register_widget({"video_player", "Live TV / Streams", "Tools",
                     "Financial TV — Bloomberg, CNBC, Reuters and custom streams", 4, 5, 3, 1,
                     [](const QJsonObject&) { return widgets::create_video_player_widget(); }});

    register_widget({"recent_files", "Recent Files", "Tools",
                     "Recently saved files — exports, reports, notebooks and more", 4, 4, 2, 1,
                     [](const QJsonObject&) { return new widgets::RecentFilesWidget; }});

    register_widget({"quote_strip", "Quote Strip", "Markets",
                     "Configurable live quote list — pick symbols via gear icon", 3, 5, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::MarketQuoteStripWidget(cfg); }});

    register_widget({"crypto_ticker", "Crypto Ticker", "Markets",
                     "Live Kraken / HyperLiquid ticker strip — configurable pair list", 3, 5, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::CryptoTickerWidget(cfg); }});

    register_widget({"polymarket_prices", "Polymarket", "Markets",
                     "Live prediction-market prices — configurable asset list", 3, 5, 2, 1,
                     [](const QJsonObject& cfg) { return new widgets::PolymarketPriceWidget(cfg); }});

    register_widget({"agent_errors", "Agent Errors", "Tools",
                     "Recent agent execution failures — subscribes to agent:error:*", 5, 4, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::AgentErrorsWidget(cfg); }});

    register_widget({"sparklines", "Sparklines", "Markets",
                     "Configurable sparkline strip — subscribes to market:sparkline:*", 4, 5, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::SparklineStripWidget(cfg); }});

    register_widget({"trade_tape", "Trade Tape", "Markets",
                     "Live trade prints for a crypto pair — ws:<exchange>:trades:<pair>", 4, 5, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::TradeTapeWidget(cfg); }});

    register_widget({"news_category", "News — Category", "Research",
                     "Category-filtered news headlines — news:category:<category>", 5, 5, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::NewsCategoryWidget(cfg); }});

    register_widget({"web_scraper", "Web Scraper", "Tools",
                     "Scrape tables from any URL — auto-detects HTML, JSON, CSV, XML/RSS", 6, 5, 3, 1,
                     [](const QJsonObject& cfg) { return new widgets::WebScraperWidget(cfg); }});
}

void WidgetRegistry::register_widget(WidgetMeta meta) {
    registry_.insert(meta.type_id, std::move(meta));
}

const WidgetMeta* WidgetRegistry::find(const QString& type_id) const {
    auto it = registry_.find(type_id);
    return (it != registry_.end()) ? &it.value() : nullptr;
}

QVector<WidgetMeta> WidgetRegistry::all() const {
    QVector<WidgetMeta> result;
    result.reserve(registry_.size());
    for (auto it = registry_.cbegin(); it != registry_.cend(); ++it)
        result.append(it.value());
    return result;
}

QVector<WidgetMeta> WidgetRegistry::by_category(const QString& category) const {
    QVector<WidgetMeta> result;
    for (const auto& m : registry_) {
        if (category.isEmpty() || m.category == category)
            result.append(m);
    }
    return result;
}

} // namespace finterm::screens
