#pragma once
#include "storage/repositories/BaseRepository.h"

// LEGACY — the flat single-table `portfolio_holdings` data path. No live reader
// or writer uses this anymore: the MCP portfolio tools, the ExposurePanel, the
// Portfolio screen and the dashboard widget all go through the canonical
// multi-portfolio schema (PortfolioRepository → portfolios + portfolio_assets,
// surfaced by PortfolioService). This class is retained only because
// StorageManager and the migrations still reference it; removing it (and the
// `portfolio_holdings` table/migration) is a separate cleanup.

namespace fincept {

struct PortfolioHolding {
    int id = 0;
    QString symbol;
    QString name;
    double shares = 0;
    double avg_cost = 0;
    bool active = true;
    QString added_at;
    QString updated_at;
};

class PortfolioHoldingsRepository : public BaseRepository<PortfolioHolding> {
  public:
    static PortfolioHoldingsRepository& instance();

    Result<QVector<PortfolioHolding>> get_active();
    Result<QVector<PortfolioHolding>> list_all();
    Result<qint64> add(const QString& symbol, double shares, double avg_cost, const QString& name = {});
    Result<void> update(int id, double shares, double avg_cost);
    Result<void> deactivate(int id);
    Result<void> remove(int id);

  private:
    PortfolioHoldingsRepository() = default;
    static PortfolioHolding map_row(QSqlQuery& q);
};

} // namespace fincept
