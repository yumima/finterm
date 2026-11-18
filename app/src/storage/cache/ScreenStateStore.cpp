#include "storage/cache/ScreenStateStore.h"

#include "storage/sqlite/CacheDatabase.h"

#include <QJsonDocument>

namespace finterm {

ScreenStateStore& ScreenStateStore::instance() {
    static ScreenStateStore s;
    return s;
}

Result<void> ScreenStateStore::save_screen_state(const QString& key, const QJsonObject& state, int state_version,
                                                const QString& session_id) {
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return Result<void>::err("Cache database not open");

    const QString json = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));

    auto r = cdb.execute("INSERT OR REPLACE INTO screen_state "
                         "(screen_key, state_version, state_json, updated_at, session_id) "
                         "VALUES (?, ?, ?, strftime('%Y-%m-%dT%H:%M:%fZ','now'), ?)",
                         {key, state_version, json, session_id});

    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<QJsonObject> ScreenStateStore::load_screen_state(const QString& key, int expected_version) {
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return Result<QJsonObject>::err("Cache database not open");

    auto r = cdb.execute("SELECT state_version, state_json FROM screen_state WHERE screen_key = ?", {key});

    if (r.is_err())
        return Result<QJsonObject>::err(r.error());

    auto& q = r.value();
    if (!q.next())
        return Result<QJsonObject>::ok(QJsonObject{}); // no saved state yet

    const int saved_version = q.value(0).toInt();
    if (saved_version != expected_version)
        return Result<QJsonObject>::ok(QJsonObject{}); // version mismatch — start fresh

    const QJsonObject obj = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object();
    return Result<QJsonObject>::ok(obj);
}

} // namespace finterm
