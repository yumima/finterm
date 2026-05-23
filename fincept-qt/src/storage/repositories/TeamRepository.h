#pragma once
// TeamRepository.h — CRUD over teams + team_members (v037).
//
// One row per named multi-agent team: coordinator + members + strategy.
// Used by the Workbench Teams panel and AgentService::run_team.

#include "core/result/Result.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace fincept {

struct TeamRow {
    QString id;
    QString name;
    QString coordinator_agent_id;
    QString mode;              ///< agno coordination mode: "coordinate" | "route" | "collaborate"
    QString description;
    QString created_at;
    QString updated_at;
    QStringList member_agent_ids;   ///< In position order; filled by get / list_all
};

struct TeamCreate {
    QString id;            ///< kebab-case slug; required
    QString name;
    QString coordinator_agent_id;
    QString mode = "coordinate";
    QString description;
    QStringList member_agent_ids;
};

class TeamRepository {
  public:
    static TeamRepository& instance();

    Result<void> create(const TeamCreate& c);
    /// Full replace: name, coordinator, strategy, description, and
    /// the entire member list (transactional — old members are
    /// deleted, new ones inserted, all in one execute batch).
    Result<void> update(const TeamCreate& c);
    Result<void> delete_(const QString& id);

    Result<QVector<TeamRow>> list_all();
    Result<std::optional<TeamRow>> get(const QString& id);
    Result<std::optional<TeamRow>> get_by_name(const QString& name);

    TeamRepository(const TeamRepository&) = delete;
    TeamRepository& operator=(const TeamRepository&) = delete;

  private:
    TeamRepository() = default;

    Result<QStringList> load_members(const QString& team_id);
    Result<void> write_members(const QString& team_id, const QStringList& member_agent_ids);
};

} // namespace fincept
