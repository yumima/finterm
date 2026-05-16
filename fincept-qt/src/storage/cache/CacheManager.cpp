#include "storage/cache/CacheManager.h"

#include "storage/sqlite/CacheDatabase.h"

#include <QSqlQuery>
#include <utility>

namespace fincept {

namespace {
// Exclusive upper bound for a prefix range query: increment the last char
// (e.g. "market:" → "market;"). Used so DELETE/SELECT on a key prefix can be
// expressed as `key >= prefix AND key < upper`, which is sargable on the
// PRIMARY KEY index. SQLite's default LIKE is case-insensitive and cannot
// use a TEXT-PK index, so the old `LIKE 'prefix%'` did a full table scan.
//
// Pre-condition: prefix must be non-empty — callers must guard. Returns an
// empty string for empty input so range queries with two empty bounds match
// nothing (rather than UB on .at(-1)).
QString prefix_upper_bound(const QString& prefix) {
    if (prefix.isEmpty())
        return QString();
    QString upper = prefix;
    const QChar last = upper.at(upper.size() - 1);
    // ushort + 1 wraps for U+FFFF; our prefixes are ASCII (e.g. "market:")
    // so this can't happen in practice. Guard anyway with an appended sentinel
    // — `prefix + U+FFFF` is still a valid exclusive upper bound for any
    // realistic ASCII-prefix range, just not a tight one.
    if (last.unicode() == 0xFFFF) {
        upper.append(QChar(0xFFFF));
    } else {
        upper[upper.size() - 1] = QChar(static_cast<ushort>(last.unicode() + 1));
    }
    return upper;
}
} // namespace

CacheManager& CacheManager::instance() {
    static CacheManager s;
    return s;
}

CacheManager::CacheManager(QObject* parent) : QObject(parent) {}

void CacheManager::put(const QString& key, const QVariant& value, int ttl_seconds, const QString& category) {
    if (key.isEmpty())
        return;
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return;

    const QString data = value.toString();
    const int size_bytes = data.toUtf8().size();
    // ON CONFLICT DO UPDATE preserves created_at and hit_count across re-puts of the same key.
    // (INSERT OR REPLACE would delete+insert, resetting those fields.)
    cdb.execute("INSERT INTO unified_cache "
                "(key, value, category, ttl_seconds, expires_at, size_bytes) "
                "VALUES (?, ?, ?, ?, datetime('now', '+' || ? || ' seconds'), ?) "
                "ON CONFLICT(key) DO UPDATE SET "
                "  value=excluded.value, "
                "  category=excluded.category, "
                "  ttl_seconds=excluded.ttl_seconds, "
                "  expires_at=excluded.expires_at, "
                "  size_bytes=excluded.size_bytes",
                {key, data, category, ttl_seconds, ttl_seconds, size_bytes});
}

QVariant CacheManager::get(const QString& key) const {
    if (key.isEmpty())
        return {};
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return {};

    auto r = cdb.execute("SELECT value FROM unified_cache WHERE key = ? AND expires_at > datetime('now')", {key});
    if (r.is_err())
        return {};

    QSqlQuery q = std::move(r.value());
    if (!q.next())
        return {};

    return q.value(0).toString();
}

QHash<QString, QString> CacheManager::multi_get(const QStringList& keys) const {
    QHash<QString, QString> out;
    if (keys.isEmpty())
        return out;
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return out;

    QStringList placeholders;
    placeholders.reserve(keys.size());
    QVariantList params;
    params.reserve(keys.size());
    for (const auto& k : keys) {
        placeholders.append(QStringLiteral("?"));
        params.append(k);
    }
    const QString sql = QStringLiteral(
        "SELECT key, value FROM unified_cache WHERE key IN (%1) AND expires_at > datetime('now')")
        .arg(placeholders.join(QStringLiteral(",")));

    auto r = cdb.execute(sql, params);
    if (r.is_err())
        return out;

    QSqlQuery q = std::move(r.value());
    out.reserve(keys.size());
    while (q.next())
        out.insert(q.value(0).toString(), q.value(1).toString());
    return out;
}

std::optional<QString> CacheManager::try_get(const QString& key) const {
    const QVariant v = get(key);
    if (v.isNull())
        return std::nullopt;
    return v.toString();
}

QHash<QString, QString> CacheManager::get_prefix(const QString& prefix) const {
    QHash<QString, QString> out;
    if (prefix.isEmpty())
        return out;
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return out;

    const QString upper = prefix_upper_bound(prefix);
    auto r = cdb.execute(
        "SELECT key, value FROM unified_cache "
        "WHERE key >= ? AND key < ? AND expires_at > datetime('now')",
        {prefix, upper});
    if (r.is_err())
        return out;

    QSqlQuery q = std::move(r.value());
    while (q.next())
        out.insert(q.value(0).toString(), q.value(1).toString());
    return out;
}

bool CacheManager::has(const QString& key) const {
    if (key.isEmpty())
        return false;
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return false;

    auto r = cdb.execute("SELECT 1 FROM unified_cache WHERE key = ? AND expires_at > datetime('now')", {key});
    if (r.is_err())
        return false;

    QSqlQuery q = std::move(r.value());
    return q.next();
}

void CacheManager::remove(const QString& key) {
    if (key.isEmpty())
        return;
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.execute("DELETE FROM unified_cache WHERE key = ?", {key});
}

void CacheManager::remove_prefix(const QString& prefix) {
    // Empty prefix would match everything — refuse to accidentally DELETE FROM unified_cache.
    if (prefix.isEmpty())
        return;
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return;
    // Range query is sargable on the PRIMARY KEY index. The previous
    // `LIKE 'prefix%'` form forced a full table scan on every call because
    // SQLite's default LIKE is case-insensitive and cannot use a TEXT PK
    // index — that was the GUI-thread freeze on every tab show.
    //
    // Behavioral note: this is case-SENSITIVE (BINARY collation on the
    // TEXT PK), unlike the old LIKE which was case-INSENSITIVE. All current
    // callers write cache keys with the exact same case they invalidate
    // with (`"market:" + SYM`), so this is not a regression. If a future
    // caller mixes cases in the prefix portion of keys, they'll need to
    // either normalise on write or call remove() per fully-qualified key.
    cdb.execute("DELETE FROM unified_cache WHERE key >= ? AND key < ?",
                {prefix, prefix_upper_bound(prefix)});
}

void CacheManager::clear() {
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.exec("DELETE FROM unified_cache");
}

void CacheManager::clear_category(const QString& category) {
    if (category.isEmpty())
        return;
    auto& cdb = CacheDatabase::instance();
    if (cdb.is_open())
        cdb.execute("DELETE FROM unified_cache WHERE category = ?", {category});
}

int CacheManager::entry_count() const {
    auto& cdb = CacheDatabase::instance();
    if (!cdb.is_open())
        return 0;

    auto r = cdb.execute("SELECT COUNT(*) FROM unified_cache WHERE expires_at > datetime('now')", {});
    if (r.is_err())
        return 0;

    QSqlQuery q = std::move(r.value());
    return q.next() ? q.value(0).toInt() : 0;
}

} // namespace fincept
