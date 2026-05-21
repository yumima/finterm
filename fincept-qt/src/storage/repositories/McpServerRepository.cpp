#include "storage/repositories/McpServerRepository.h"

namespace fincept {

McpServerRepository& McpServerRepository::instance() {
    static McpServerRepository s;
    return s;
}

McpServer McpServerRepository::map_row(QSqlQuery& q) {
    McpServer s{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(),  q.value(3).toString(),
                q.value(4).toString(), q.value(5).toString(), q.value(6).toString(),  q.value(7).toString(),
                q.value(8).toBool(),   q.value(9).toBool(),   q.value(10).toString(), q.value(11).toString(),
                q.value(12).toString()};
    // v025 columns — defensive default for pre-v025 rows that snuck
    // through (shouldn't happen because the migration runs before
    // any read, but cheap insurance).
    QString tt = q.value(13).toString();
    s.transport_type = tt.isEmpty() ? QStringLiteral("stdio") : tt;
    s.base_url       = q.value(14).toString();
    QString as = q.value(15).toString();
    s.auth_scheme    = as.isEmpty() ? QStringLiteral("none") : as;
    s.auth_header    = q.value(16).toString();
    return s;
}

static const char* kCols = "id, name, description, command, args, env, category, icon, enabled, "
                           "auto_start, status, created_at, updated_at, "
                           "transport_type, base_url, auth_scheme, auth_header";

Result<QVector<McpServer>> McpServerRepository::list_all() {
    return query_list(QString("SELECT %1 FROM mcp_servers ORDER BY name").arg(kCols), {}, map_row);
}

Result<McpServer> McpServerRepository::get(const QString& id) {
    return query_one(QString("SELECT %1 FROM mcp_servers WHERE id = ?").arg(kCols), {id}, map_row);
}

Result<void> McpServerRepository::save(const McpServer& s) {
    return exec_write(
        "INSERT OR REPLACE INTO mcp_servers "
        "(id, name, description, command, args, env, category, icon, enabled, auto_start, status, "
        " transport_type, base_url, auth_scheme, auth_header, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'))",
        {s.id, s.name, s.description, s.command, s.args, s.env, s.category, s.icon, s.enabled ? 1 : 0,
         s.auto_start ? 1 : 0, s.status,
         s.transport_type.isEmpty() ? QStringLiteral("stdio") : s.transport_type,
         s.base_url,
         s.auth_scheme.isEmpty() ? QStringLiteral("none") : s.auth_scheme,
         s.auth_header});
}

Result<void> McpServerRepository::remove(const QString& id) {
    return exec_write("DELETE FROM mcp_servers WHERE id = ?", {id});
}

Result<void> McpServerRepository::set_status(const QString& id, const QString& status) {
    return exec_write("UPDATE mcp_servers SET status = ?, updated_at = datetime('now') WHERE id = ?", {status, id});
}

Result<void> McpServerRepository::set_enabled(const QString& id, bool enabled) {
    return exec_write("UPDATE mcp_servers SET enabled = ?, updated_at = datetime('now') WHERE id = ?",
                      {enabled ? 1 : 0, id});
}

} // namespace fincept
