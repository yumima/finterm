#pragma once
#include "screens/portfolio/PortfolioTypes.h"

#include <QHash>
#include <QHideEvent>
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
///   1. **Futures positions** — holdings whose symbol matches a futures
///      pattern (yfinance =F suffix, /XX TradingView, or root in the
///      Futures catalog). Live quotes come from FuturesQuoteCache so we
///      reuse the work the FuturesScreen already does.
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
    void on_quote_cache_updated();

    portfolio::PortfolioSummary summary_;
    QString                     currency_ = "USD";

    QTableWidget* futures_table_  = nullptr;
    QTableWidget* extended_table_ = nullptr;
    QLabel*       futures_status_ = nullptr;
    QLabel*       extended_status_ = nullptr;

    QHash<QString, ExtQuote> ext_quotes_;
    quint64                  ext_gen_ = 0;

    // Periodic refresh of the extended-hours table while the view is
    // visible. Runs in parallel with FuturesQuoteCache's own 20 s
    // polling (which only feeds the futures table above). 20 s matches
    // the futures cadence so both tables tick together; the timer is
    // started in showEvent and stopped in hideEvent so no work happens
    // when the view isn't on screen.
    QTimer*                  ext_refresh_timer_ = nullptr;
    static constexpr int     kExtRefreshIntervalMs = 20'000;
};

} // namespace fincept::screens
