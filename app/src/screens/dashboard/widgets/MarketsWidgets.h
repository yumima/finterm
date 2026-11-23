#pragma once
// MarketsWidgets.h — widgets made for the MARKETS screens (also on the
// dashboard's widget list):
//
//   MarketRegimeWidget   one read per asset class from its benchmark's own
//                        readings — the doc's "one label for each asset class"
//   WorldBoardWidget     the WORLD board (global sessions, regions, rates, FX,
//                        commodities, crypto, vol) as a tile — or one part of
//                        it (the sessions strip, one region / section)
//   MstrPremiumWidget    Strategy (MSTR) against the bitcoin it holds: market
//                        cap ÷ (BTC held × BTC price)
//
// Same data as everywhere: quotes from market:quote:*, readings from
// ConditionService (cond:stats / cond:fred / cond:mstr). A click selects the
// symbol on the screen (its chart follows).

#include "screens/dashboard/widgets/BaseWidget.h"
#include "services/markets/MarketDataService.h"
#include "ui/RenderCoalescer.h"

#include <QHash>
#include <QJsonObject>
#include <QSet>

class QLabel;

namespace finterm::ui {
class DataTable;
}
namespace finterm::screens::world {
class WorldMarketsView;
}

namespace finterm::screens::widgets {

class MarketRegimeWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit MarketRegimeWidget(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void subscribe_all();
    void render();
    ui::DataTable* table_ = nullptr;
    QHash<QString, services::QuoteData> quotes_;
    QHash<QString, QJsonObject> cond_; ///< by topic
    QSet<QString> fresh_;
    bool subscribed_ = false;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

class WorldBoardWidget : public BaseWidget {
    Q_OBJECT
  public:
    /// `part`: "" = the whole board; WorldMarketsView::kSessionsPart; or one
    /// section's title ("AMERICAS", "FX" …).
    explicit WorldBoardWidget(const QString& part = {}, QWidget* parent = nullptr);

  private:
    world::WorldMarketsView* view_ = nullptr;
};

class MstrPremiumWidget : public BaseWidget {
    Q_OBJECT
  public:
    explicit MstrPremiumWidget(QWidget* parent = nullptr);

  protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

  private:
    void fetch_cap();
    void render();
    QLabel* body_ = nullptr;
    QJsonObject holding_;
    QString holding_error_;
    services::QuoteData btc_, mstr_;
    double market_cap_ = std::numeric_limits<double>::quiet_NaN();
    QString cap_error_;
    bool subscribed_ = false;
    ui::RenderCoalescer render_{this, [this]() { render(); }};
};

} // namespace finterm::screens::widgets
