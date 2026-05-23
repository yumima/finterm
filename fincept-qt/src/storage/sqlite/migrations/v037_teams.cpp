// v037_teams — Multi-agent teams (Track 98).
//
// A team is a named group: one coordinator agent + N member agents,
// run together on a single user query.  The coordinator's job is to
// synthesise the member responses into a final answer; member agents
// each contribute their lens (sector, quant, macro, …) and don't
// see each other's outputs unless the strategy says so.
//
// Schema:
//   teams (id, name, coordinator_agent_id, strategy, description,
//          created_at, updated_at)
//     - id: kebab-case slug, primary key
//     - name: user-facing display name
//     - coordinator_agent_id: FK to agent_configs.id; runs LAST
//                             after gathering member responses
//     - strategy: how members are dispatched — for v037:
//                  'sequential'  members run one after another
//                  'parallel'    members run concurrently (the
//                                runtime collapses to sequential if
//                                concurrency primitives aren't wired)
//     - description: optional one-line blurb
//
//   team_members (team_id, agent_id, position, created_at)
//     - team_id + agent_id is the primary key
//     - position drives ordering within the team
//
// No FK enforcement on agent_configs / coordinator_agent_id because
// agents live in a separate registry (Python finagent_core); SQLite
// can't validate.  Membership rows that point at non-existent agents
// surface as "unknown agent" in the UI; run_team skips them.

#include "core/logging/Logger.h"
#include "storage/sqlite/migrations/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>

namespace fincept {
namespace {

constexpr const char* kTag = "Migration037";

static Result<void> sql(QSqlDatabase& db, const char* stmt) {
    QSqlQuery q(db);
    if (!q.exec(stmt))
        return Result<void>::err(q.lastError().text().toStdString());
    return Result<void>::ok();
}

static Result<void> apply_v037(QSqlDatabase& db) {
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS teams ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  coordinator_agent_id TEXT NOT NULL,"
            "  strategy TEXT NOT NULL DEFAULT 'sequential',"
            "  description TEXT,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  updated_at TEXT NOT NULL DEFAULT (datetime('now'))"
            ")"); r.is_err())
        return r;
    if (auto r = sql(db,
            "CREATE TABLE IF NOT EXISTS team_members ("
            "  team_id TEXT NOT NULL,"
            "  agent_id TEXT NOT NULL,"
            "  position INTEGER NOT NULL DEFAULT 0,"
            "  created_at TEXT NOT NULL DEFAULT (datetime('now')),"
            "  PRIMARY KEY (team_id, agent_id),"
            "  FOREIGN KEY (team_id) REFERENCES teams(id) ON DELETE CASCADE"
            ")"); r.is_err())
        return r;
    auto warn_on_fail = [&db](const char* stmt) {
        if (auto r = sql(db, stmt); r.is_err())
            LOG_WARN(kTag, QString("index create failed: %1").arg(QString::fromStdString(r.error())));
    };
    warn_on_fail("CREATE INDEX IF NOT EXISTS idx_team_members_team "
                 "ON team_members(team_id, position)");
    LOG_INFO(kTag, "teams + team_members ready");
    return Result<void>::ok();
}

} // anonymous namespace

void register_migration_v037() {
    static bool done = false;
    if (done)
        return;
    done = true;
    MigrationRunner::register_migration({37, "teams", apply_v037});
}

} // namespace fincept
