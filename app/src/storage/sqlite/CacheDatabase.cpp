#include "storage/sqlite/CacheDatabase.h"

#include "core/logging/Logger.h"

#include <QThread>

#include <cstring>


namespace finterm {

CacheDatabase& CacheDatabase::instance() {
    static CacheDatabase s;
    return s;
}

CacheDatabase::CacheDatabase() : conns_(QStringLiteral("finterm_cache"), &CacheDatabase::apply_pragmas) {}

Result<void> CacheDatabase::open(const QString& path) {
    const QString error = conns_.set_path(path);
    if (!error.isEmpty()) {
        conns_.set_path({});
        return Result<void>::err(error.toStdString());
    }
    LOG_INFO("CacheDB", "Opened cache database: " + path);

    auto tr = create_tables();
    if (tr.is_err())
        return tr;

    // One-shot startup sweep of expired rows — prevents unbounded growth of unified_cache
    // across sessions. Cheap: idx_cache_expires makes this a range scan.
    auto sweep = execute("DELETE FROM unified_cache WHERE expires_at < datetime('now')");
    if (sweep.is_ok()) {
        const int removed = sweep.value().numRowsAffected();
        if (removed > 0)
            LOG_INFO("CacheDB", QString("Startup sweep removed %1 expired cache rows").arg(removed));
    }

    return Result<void>::ok();
}

void CacheDatabase::close() {
    conns_.set_path({});
}

bool CacheDatabase::is_open() const {
    return !conns_.path().isEmpty();
}

Result<QSqlQuery> CacheDatabase::execute(const QString& sql, const QVariantList& params) {
    QString error;
    QSqlDatabase db = conns_.connection(&error);
    if (!db.isOpen())
        return Result<QSqlQuery>::err(error.toStdString());
    QSqlQuery query(db);
    query.prepare(sql);
    for (int i = 0; i < params.size(); ++i) {
        query.bindValue(i, params[i]);
    }
    if (!query.exec()) {
        return Result<QSqlQuery>::err(query.lastError().text().toStdString());
    }
    return Result<QSqlQuery>::ok(std::move(query));
}

Result<void> CacheDatabase::exec(const QString& sql) {
    QString error;
    QSqlDatabase db = conns_.connection(&error);
    if (!db.isOpen())
        return Result<void>::err(error.toStdString());
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        return Result<void>::err(query.lastError().text().toStdString());
    }
    return Result<void>::ok();
}

QString CacheDatabase::apply_pragmas(QSqlDatabase& db) {
    // synchronous=NORMAL under WAL is the standard safe/fast combo. OFF risked full DB
    // corruption on power loss — now that cache.db also holds
    // screen_state, durability matters more than the marginal write gain of OFF.
    const char* pragmas[] = {
        "PRAGMA journal_mode = WAL", "PRAGMA synchronous = NORMAL", "PRAGMA cache_size = -10000",
        "PRAGMA temp_store = MEMORY", "PRAGMA mmap_size = 134217728", "PRAGMA busy_timeout = 3000",
        // cache.db sees the most write churn (quote TTL refresh, tab session
        // snapshots, screen state) — capping WAL growth matters more here
        // than for the main DB. See Database::apply_pragmas() for rationale.
        "PRAGMA wal_autocheckpoint = 400",
        "PRAGMA journal_size_limit = 33554432", // 32 MB
    };
    // Per connection, so every thread's connection gets them.
    for (auto* p : pragmas) {
        QSqlQuery q(db);
        // The large page cache is for the GUI thread's connection; every worker
        // thread has one too, and 2 MB each is plenty for what they run.
        if (std::strstr(p, "cache_size") && !QThread::isMainThread())
            p = "PRAGMA cache_size = -2000";
        if (!q.exec(QString::fromLatin1(p))) {
            LOG_WARN("CacheDB", QString("PRAGMA failed: %1 — %2").arg(p, q.lastError().text()));
        }
    }
    return {};
}

Result<void> CacheDatabase::create_tables() {
    // Unified cache with TTL
    auto r = exec("CREATE TABLE IF NOT EXISTS unified_cache ("
                  "  key TEXT PRIMARY KEY,"
                  "  value TEXT NOT NULL,"
                  "  category TEXT DEFAULT 'general',"
                  "  content_type TEXT DEFAULT 'json',"
                  "  ttl_seconds INTEGER DEFAULT 300,"
                  "  expires_at TEXT NOT NULL,"
                  "  created_at TEXT DEFAULT (datetime('now')),"
                  "  hit_count INTEGER DEFAULT 0,"
                  "  size_bytes INTEGER DEFAULT 0"
                  ")");
    if (r.is_err())
        return r;

    r = exec("CREATE INDEX IF NOT EXISTS idx_cache_expires ON unified_cache(expires_at)");
    if (r.is_err())
        return r;

    r = exec("CREATE INDEX IF NOT EXISTS idx_cache_category ON unified_cache(category)");
    if (r.is_err())
        return r;

    // tab_sessions (per-tab scroll/filters/selections) never had a writer;
    // screen_state below is the one store for a screen's own UI state.
    r = exec("DROP TABLE IF EXISTS tab_sessions");
    if (r.is_err())
        return r;

    r = exec("CREATE TABLE IF NOT EXISTS screen_state ("
             "  screen_key    TEXT PRIMARY KEY,"
             "  state_version INTEGER NOT NULL DEFAULT 1,"
             "  state_json    TEXT    NOT NULL DEFAULT '{}',"
             "  updated_at    TEXT    NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),"
             "  session_id    TEXT    NOT NULL DEFAULT ''"
             ")");
    if (r.is_err())
        return r;

    r = exec("CREATE INDEX IF NOT EXISTS idx_screen_state_updated ON screen_state(updated_at)");
    if (r.is_err())
        return r;

    LOG_INFO("CacheDB", "Cache tables initialized");
    return Result<void>::ok();
}

} // namespace finterm
