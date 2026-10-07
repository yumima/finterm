#include "storage/repositories/IvHistoryRepository.h"

#include <QDate>

namespace fincept {

IvHistoryRepository& IvHistoryRepository::instance() {
    static IvHistoryRepository s;
    return s;
}

IvHistoryRow IvHistoryRepository::map_row(QSqlQuery& q) {
    IvHistoryRow r;
    r.underlying = q.value(0).toString();
    r.expiry_kind = q.value(1).toString();
    r.date_iso = q.value(2).toString();
    r.expiry = q.value(3).toString();
    r.atm_iv = q.value(4).toDouble();
    return r;
}

Result<void> IvHistoryRepository::upsert(const QString& underlying, const QString& expiry_kind,
                                         const QString& date_iso, const QString& expiry, double atm_iv) {
    return exec_write(
        "INSERT OR REPLACE INTO iv_history_daily (underlying, expiry_kind, date_iso, expiry, atm_iv, updated_at) "
        "VALUES (?, ?, ?, ?, ?, datetime('now'))",
        {underlying, expiry_kind, date_iso, expiry, atm_iv});
}

Result<QVector<IvHistoryRow>> IvHistoryRepository::get_window(const QString& underlying,
                                                              const QString& expiry_kind,
                                                              const QString& since_iso) {
    return query_list(
        "SELECT underlying, expiry_kind, date_iso, expiry, atm_iv FROM iv_history_daily "
        "WHERE underlying = ? AND expiry_kind = ? AND date_iso >= ? ORDER BY date_iso ASC",
        {underlying, expiry_kind, since_iso}, &IvHistoryRepository::map_row);
}

std::optional<IvHistoryRow> IvHistoryRepository::get_today(const QString& underlying, const QString& expiry_kind) {
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    return query_optional(
        "SELECT underlying, expiry_kind, date_iso, expiry, atm_iv FROM iv_history_daily "
        "WHERE underlying = ? AND expiry_kind = ? AND date_iso = ?",
        {underlying, expiry_kind, today}, &IvHistoryRepository::map_row);
}

} // namespace fincept
