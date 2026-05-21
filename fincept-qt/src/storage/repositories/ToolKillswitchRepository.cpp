#include "storage/repositories/ToolKillswitchRepository.h"

#include "storage/sqlite/Database.h"

#include <QSqlQuery>

namespace fincept {

ToolKillswitchRepository& ToolKillswitchRepository::instance() {
    static ToolKillswitchRepository r;
    return r;
}

Result<QSet<QString>> ToolKillswitchRepository::disabled_names() {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT tool_name FROM tool_killswitch"));
    if (r.is_err())
        return Result<QSet<QString>>::err(r.error());
    QSet<QString> names;
    auto& q = r.value();
    while (q.next())
        names.insert(q.value(0).toString());
    return Result<QSet<QString>>::ok(std::move(names));
}

Result<QString> ToolKillswitchRepository::reason_for(const QString& tool_name) {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT reason FROM tool_killswitch WHERE tool_name = ?"),
        {tool_name});
    if (r.is_err())
        return Result<QString>::err(r.error());
    auto& q = r.value();
    if (q.next())
        return Result<QString>::ok(q.value(0).toString());
    return Result<QString>::ok(QString{});
}

Result<QVector<ToolKillswitchEntry>> ToolKillswitchRepository::list_all() {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT tool_name, reason, disabled_at "
                       "FROM tool_killswitch ORDER BY tool_name"));
    if (r.is_err())
        return Result<QVector<ToolKillswitchEntry>>::err(r.error());
    QVector<ToolKillswitchEntry> rows;
    auto& q = r.value();
    while (q.next()) {
        ToolKillswitchEntry e;
        e.tool_name = q.value(0).toString();
        e.reason = q.value(1).toString();
        e.disabled_at = q.value(2).toString();
        rows.append(e);
    }
    return Result<QVector<ToolKillswitchEntry>>::ok(std::move(rows));
}

Result<void> ToolKillswitchRepository::disable(const QString& tool_name, const QString& reason) {
    if (tool_name.isEmpty())
        return Result<void>::err("ToolKillswitchRepository::disable: empty tool_name");
    auto r = Database::instance().execute(
        QStringLiteral(
            "INSERT INTO tool_killswitch (tool_name, reason, disabled_at) "
            "VALUES (?, ?, datetime('now')) "
            "ON CONFLICT(tool_name) DO UPDATE SET "
            "  reason = excluded.reason, "
            "  disabled_at = excluded.disabled_at"),
        {tool_name, reason});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<void> ToolKillswitchRepository::enable(const QString& tool_name) {
    if (tool_name.isEmpty())
        return Result<void>::err("ToolKillswitchRepository::enable: empty tool_name");
    auto r = Database::instance().execute(
        QStringLiteral("DELETE FROM tool_killswitch WHERE tool_name = ?"),
        {tool_name});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

} // namespace fincept
