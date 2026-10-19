#pragma once
#include "screens/portfolio/PortfolioTypes.h"
#include "services/markets/MarketDataService.h"

#include <QDateTime>
#include <QHash>
#include <QHideEvent>
#include <QSet>
#include <QLabel>
#include <QShowEvent>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

namespace fincept::screens {

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
    struct ExtQuote {
        double  regular = 0;
        double  ext_price = 0;
        double  ext_change = 0;
        double  ext_change_pct = 0;
        QString session;        // "REGULAR" / "PRE" / "POST" / "CLOSED"
        bool    has_ext = false;
    };

    void build_ui();
    void populate_futures();
    void populate_extended();
    void refresh_extended_hours();
    void resubscribe_futures();
    /// The feed symbol a futures holding is priced from (ROOT=F).
    static QString quote_symbol(const QString& symbol);

    /// Disk-cache last successful extended-hours batch so the table
    /// paints with real (if stale) values on app startup instead of
    /// "—" everywhere until the first 20 s refresh returns. Flat
    /// symbol→ExtQuote map; symbols not in the current holdings are
    /// silently ignored by populate_extended.
    QString ext_cache_path() const;
    void    save_ext_cache() const;
    void    load_ext_cache();

    portfolio::PortfolioSummary summary_;
    QString                     currency_ = "USD";

    QTableWidget* futures_table_  = nullptr;
    QTableWidget* extended_table_ = nullptr;
    QLabel*       futures_status_ = nullptr;
    QLabel*       extended_status_ = nullptr;

    QHash<QString, services::QuoteData> fut_quotes_;   // keyed by quote_symbol()
    QDateTime                fut_last_update_;
    QSet<QString>            fut_subscribed_;    // topics currently held on the hub
    QHash<QString, ExtQuote> ext_quotes_;
    quint64                  ext_gen_ = 0;
    // Suppresses timer-driven refresh while one is already in flight.
    // Without this guard a slow yfinance response (>20 s) lets the
    // 20 s tick fire repeatedly, stacking requests on the daemon. The
    // ext_gen_ epoch only drops *responses*; this drops *requests*.
    // set_summary still proceeds even when true — its callers explicitly
    // want fresh data for newly-arrived holdings.
    bool                     ext_in_flight_ = false;

    // Periodic refresh of the extended-hours table while the view is
    // visible. The futures table above ticks with the DataHub quote
    // stream instead; the timer is
    // started in showEvent and stopped in hideEvent so no work happens
    // when the view isn't on screen.
    QTimer*                  ext_refresh_timer_ = nullptr;
    static constexpr int     kExtRefreshIntervalMs = 20'000;
};

} // namespace fincept::screens
