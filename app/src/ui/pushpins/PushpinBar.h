#pragma once
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QWidget>

namespace finterm::ui {

class TickerBar;

/// The banner row at the top of every main window, on every screen: the
/// scrolling market tape (TickerBar) of the user's pinned symbols (accent
/// colour, first) followed by the default list (right-click › Edit Default
/// Symbols…). Dropping a symbol anywhere on it pins it; right-click an entry
/// to unpin it.
///
/// Quotes come from the DataHub (`market:quote:<sym>`), subscribed while the
/// banner is shown and dropped while it is hidden (sign-in, lock, focus mode,
/// Settings › Appearance › Ticker Bar off). The cadence is the hub's.
class PushpinBar : public QWidget {
    Q_OBJECT
  public:
    explicit PushpinBar(QWidget* parent = nullptr);
    ~PushpinBar() override;

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    /// Pinned symbols first, then the defaults not already pinned.
    QStringList banner_symbols() const;
    /// `force`: the list changed — fetch now; a re-show leans on the hub's
    /// last-known values and its own cadence (no burst of forced quotes on
    /// every focus-mode / lock / workbench toggle).
    void resubscribe(bool force);
    void unsubscribe();
    void render();

    TickerBar* ticker_ = nullptr;
    QHash<QString, services::QuoteData> cache_;
    QStringList subscribed_;
    QSet<QString> no_quote_; ///< pins with no Yahoo quote (crypto pairs): shown, never fetched
    bool active_ = false;
    RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::ui
