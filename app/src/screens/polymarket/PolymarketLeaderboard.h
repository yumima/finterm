#pragma once

#include "services/polymarket/PolymarketTypes.h"

#include <QTableWidget>
#include <QWidget>

namespace finterm::screens::polymarket {

/// Leaderboard panel showing top traders.
class PolymarketLeaderboard : public QWidget {
    Q_OBJECT
  public:
    explicit PolymarketLeaderboard(QWidget* parent = nullptr);

    void set_loading(bool loading);

  private:
    QTableWidget* table_ = nullptr;
};

} // namespace finterm::screens::polymarket
