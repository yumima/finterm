#pragma once
// IvHistoryRepository — daily ATM IV per (underlying, expiry kind).
//
// expiry_kind is "front" (nearest listed expiry) or "monthly" (nearest
// month-end expiry) so the percentile compares like with like (v054).
//
// Auto-populated by OptionChainService whenever a chain refresh produces
// an ATM IV value. Read by FnoHeaderBar's IV percentile pill (trailing
// 90-day percentile rank).

#include "storage/repositories/BaseRepository.h"

#include <QString>

namespace fincept {

struct IvHistoryRow {
    QString underlying;
    QString expiry_kind;
    QString date_iso;
    QString expiry;
    double atm_iv = 0;
};

class IvHistoryRepository : public BaseRepository<IvHistoryRow> {
  public:
    static IvHistoryRepository& instance();

    /// INSERT OR REPLACE — last write wins per (underlying, expiry_kind, date).
    Result<void> upsert(const QString& underlying, const QString& expiry_kind, const QString& date_iso,
                        const QString& expiry, double atm_iv);

    /// Trailing window for (`underlying`, `expiry_kind`) from `since_iso`
    /// (inclusive) to today. Ascending by date.
    Result<QVector<IvHistoryRow>> get_window(const QString& underlying, const QString& expiry_kind,
                                             const QString& since_iso);

    /// Today's row for (`underlying`, `expiry_kind`), or std::nullopt when not yet populated.
    std::optional<IvHistoryRow> get_today(const QString& underlying, const QString& expiry_kind);

  private:
    IvHistoryRepository() = default;
    static IvHistoryRow map_row(QSqlQuery& q);
};

} // namespace fincept
