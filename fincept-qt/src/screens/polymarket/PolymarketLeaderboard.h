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

    void set_entries(const QVector<services::polymarket::LeaderboardEntry>& entries);
    void set_loading(bool loading);

  signals:
    void trader_clicked(const QString& address);

  private:
    QTableWidget* table_ = nullptr;
};

} // namespace finterm::screens::polymarket
