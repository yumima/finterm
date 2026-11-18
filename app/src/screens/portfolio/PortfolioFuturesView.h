#pragma once
#include "screens/portfolio/PortfolioTypes.h"
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QDateTime>
#include <QHash>
#include <QHideEvent>
#include <QLabel>
#include <QSet>
#include <QShowEvent>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace finterm::screens {

/// "Futures & Extended Hours" detail view.
///
/// Two stacked tables fed from the current PortfolioSummary:
///   1. **Futures positions** — holdings whose symbol is unambiguously a
///      future (yfinance =F suffix, /XX TradingView, or a dated contract
///      code like ESZ6; a bare root is a stock ticker). Live quotes come from the DataHub's
///      `market:quote:<ROOT>=F` topics while the view is visible.
///   2. **Extended hours** — every holding, with regular close + last
///      pre/post-market price and the ext-hours move. Sourced from the
///      yfinance daemon's `extended_hours` action (preMarketPrice /
///      postMarketPrice fields on `Ticker.info`).
class PortfolioFuturesView : public QWidget {
    Q_OBJECT
  public:
    explicit PortfolioFuturesView(QWidget* parent = nullptr);

    void set_summary(const portfolio::PortfolioSummary& summary, const QString& currency);

    // Public for the in-translation-unit smoke test in
    // PortfolioFuturesView.cpp; otherwise these would be private.
    static bool looks_like_future(const QString& symbol);
    static QString futures_root(const QString& symbol);

  protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

  private:
    void build_ui();
    void populate_futures();
    void populate_extended();
    /// Tell the shared extended-hours source which holdings are on screen.
    void watch_ext();
    void update_ext_status();
    void resubscribe_futures();
    /// The feed symbol a futures holding is priced from (ROOT=F).
    static QString quote_symbol(const QString& symbol);

    portfolio::PortfolioSummary summary_;
    QString currency_ = "USD";

    QTableWidget* futures_table_ = nullptr;
    QTableWidget* extended_table_ = nullptr;
    QLabel* futures_status_ = nullptr;
    QLabel* extended_status_ = nullptr;

    QHash<QString, services::QuoteData> fut_quotes_; // keyed by quote_symbol()
    QDateTime fut_last_update_;
    QSet<QString> fut_subscribed_; // topics currently held on the hub
    ui::RenderCoalescer ext_render_{this, [this]() { populate_extended(); }};
};

} // namespace finterm::screens
