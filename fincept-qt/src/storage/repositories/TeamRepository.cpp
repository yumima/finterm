#include "storage/repositories/TeamRepository.h"

#include "storage/sqlite/Database.h"

#include <QSqlQuery>

namespace fincept {

namespace {

constexpr const char* kTeamCols =
    "id, name, coordinator_agent_id, mode, description, created_at, updated_at";

TeamRow map_row(QSqlQuery& q) {
    TeamRow t;
    t.id = q.value(0).toString();
    t.name = q.value(1).toString();
    t.coordinator_agent_id = q.value(2).toString();
    t.mode = q.value(3).toString();
    t.description = q.value(4).toString();
    t.created_at = q.value(5).toString();
    t.updated_at = q.value(6).toString();
    return t;
}

} // anonymous namespace

TeamRepository& TeamRepository::instance() {
    static TeamRepository r;
    return r;
}

Result<QStringList> TeamRepository::load_members(const QString& team_id) {
    auto r = Database::instance().execute(
        QStringLiteral("SELECT agent_id FROM team_members "
                       "WHERE team_id = ? ORDER BY position ASC"),
        {team_id});
    if (r.is_err())
        return Result<QStringList>::err(r.error());
    QStringList out;
    auto& q = r.value();
    while (q.next())
        out.append(q.value(0).toString());
    return Result<QStringList>::ok(std::move(out));
}

Result<void> TeamRepository::write_members(const QString& team_id, const QStringList& member_agent_ids) {
    // Transactional replace: DELETE + INSERTs in one commit so a
    // mid-loop failure (FK violation, disk-full, busy-timeout) can't
    // leave the team with a partial membership list — which would
    // contradict the documented contract on TeamRepository.h.
    auto& db = Database::instance();
    if (auto bt = db.begin_transaction(); bt.is_err())
        return Result<void>::err(bt.error());
    if (auto r = db.execute(
            QStringLiteral("DELETE FROM team_members WHERE team_id = ?"), {team_id});
        r.is_err()) {
        db.rollback();
        return Result<void>::err(r.error());
    }
    int pos = 0;
    for (const QString& agent_id : member_agent_ids) {
        if (agent_id.isEmpty())
            continue;
        if (auto r = db.execute(
                QStringLiteral("INSERT INTO team_members (team_id, agent_id, position) "
                               "VALUES (?, ?, ?)"),
                {team_id, agent_id, pos});
            r.is_err()) {
            db.rollback();
            return Result<void>::err(r.error());
        }
        ++pos;
    }
    if (auto c = db.commit(); c.is_err()) {
        db.rollback();
        return Result<void>::err(c.error());
    }
    return Result<void>::ok();
}

Result<void> TeamRepository::create(const TeamCreate& c) {
    if (c.id.isEmpty() || c.name.isEmpty() || c.coordinator_agent_id.isEmpty())
        return Result<void>::err("TeamRepository::create: id / name / coordinator are required");
    auto r = Database::instance().execute(
        QStringLiteral("INSERT INTO teams (id, name, coordinator_agent_id, mode, description) "
                       "VALUES (?, ?, ?, ?, ?)"),
        {c.id, c.name, c.coordinator_agent_id,
         c.mode.isEmpty() ? QStringLiteral("coordinate") : c.mode,
         c.description});
    if (r.is_err())
        return Result<void>::err(r.error());
    auto mr = write_members(c.id, c.member_agent_ids);
    if (mr.is_err()) {
        // write_members failed AFTER the teams row landed — undo so
        // we don't leave an orphan team-with-no-members.  delete_
        // cascades on team_members but write_members's transaction
        // already rolled back, so this is just the parent row.
        Database::instance().execute(
            QStringLiteral("DELETE FROM teams WHERE id = ?"), {c.id});
    }
    return mr;
}

Result<void> TeamRepository::update(const TeamCreate& c) {
    if (c.id.isEmpty())
        return Result<void>::err("TeamRepository::update: empty id");
    auto r = Database::instance().execute(
        QStringLiteral("UPDATE teams SET "
                       "  name = ?, coordinator_agent_id = ?, mode = ?, "
                       "  description = ?, updated_at = datetime('now') "
                       "WHERE id = ?"),
        {c.name, c.coordinator_agent_id,
         c.mode.isEmpty() ? QStringLiteral("coordinate") : c.mode,
         c.description, c.id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return write_members(c.id, c.member_agent_ids);
}

Result<void> TeamRepository::delete_(const QString& id) {
    if (id.isEmpty())
        return Result<void>::err("TeamRepository::delete_: empty id");
    // ON DELETE CASCADE on team_members handles the membership rows.
    auto r = Database::instance().execute(
        QStringLiteral("DELETE FROM teams WHERE id = ?"), {id});
    if (r.is_err())
        return Result<void>::err(r.error());
    return Result<void>::ok();
}

Result<QVector<TeamRow>> TeamRepository::list_all() {
    using Out = Result<QVector<TeamRow>>;
    const QString sql = QStringLiteral("SELECT %1 FROM teams ORDER BY name ASC").arg(kTeamCols);
    auto r = Database::instance().execute(sql, {});
    if (r.is_err())
        return Out::err(r.error());
    QVector<TeamRow> rows;
    auto& q = r.value();
    while (q.next())
        rows.append(map_row(q));
    // Membership fill — separate query per team; teams count is
    // expected to be small (tens at most), so the N+1 is fine.
    for (auto& t : rows) {
        auto m = load_members(t.id);
        if (m.is_ok())
            t.member_agent_ids = m.value();
    }
    return Out::ok(std::move(rows));
}

Result<std::optional<TeamRow>> TeamRepository::get(const QString& id) {
    using Out = Result<std::optional<TeamRow>>;
    const QString sql = QStringLiteral("SELECT %1 FROM teams WHERE id = ?").arg(kTeamCols);
    auto r = Database::instance().execute(sql, {id});
    if (r.is_err())
        return Out::err(r.error());
    auto& q = r.value();
    if (!q.next())
        return Out::ok(std::nullopt);
    TeamRow t = map_row(q);
    auto m = load_members(t.id);
    if (m.is_ok())
        t.member_agent_ids = m.value();
    return Out::ok(t);
}

Result<std::optional<TeamRow>> TeamRepository::get_by_name(const QString& name) {
    using Out = Result<std::optional<TeamRow>>;
    const QString sql = QStringLiteral("SELECT %1 FROM teams WHERE name = ? LIMIT 1").arg(kTeamCols);
    auto r = Database::instance().execute(sql, {name});
    if (r.is_err())
        return Out::err(r.error());
    auto& q = r.value();
    if (!q.next())
        return Out::ok(std::nullopt);
    TeamRow t = map_row(q);
    auto m = load_members(t.id);
    if (m.is_ok())
        t.member_agent_ids = m.value();
    return Out::ok(t);
}

} // namespace fincept
