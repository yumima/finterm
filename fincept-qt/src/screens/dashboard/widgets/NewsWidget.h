#pragma once
#include "screens/dashboard/widgets/BaseWidget.h"

#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>

namespace fincept::screens::widgets {

/// Market news widget — fetches real news via yfinance for major tickers.
class NewsWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit NewsWidget(QWidget* parent = nullptr);

  protected:
    void on_theme_changed() override;

  private:
    void apply_styles();
    void refresh_data();
    void populate(const QJsonArray& articles);
    /// Always called on the callback's return path (success / empty / error /
    /// watchdog timeout). Updates the "Last updated" label so the user sees
    /// visible feedback even when yfinance returns the same articles after
    /// a long idle — otherwise the refresh button looks broken.
    void finalize_refresh(const QString& outcome);

    QScrollArea* scroll_area_  = nullptr;
    QVBoxLayout* news_layout_  = nullptr;
    QLabel*      status_label_ = nullptr; // "Last updated HH:MM · N items"
    /// Watchdog: clears the loading spinner if the daemon callback never
    /// returns within kRefreshTimeoutMs. The PythonWorker has its own
    /// timeout but a missed dispatch (worker not yet ready, queue drop)
    /// could otherwise leave the spinner on forever.
    QTimer*      watchdog_     = nullptr;
    QJsonArray   last_articles_; // cached for theme-change re-populate
    qint64       refresh_token_ = 0; // increments per click; callbacks check it
};

} // namespace fincept::screens::widgets
