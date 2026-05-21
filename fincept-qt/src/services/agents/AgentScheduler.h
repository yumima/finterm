#pragma once
// AgentScheduler.h — cron-shaped scheduler for agent runs (Track 10).
//
// Owns a SQLite-backed queue (`agent_schedule` table, migration v027)
// of (agent_id, skill, args, cron) entries.  A 60-second tick scans
// for due entries and dispatches each through AgentService::run_agent.
//
// Minimal cron grammar in this revision (extend later if needed):
//   @daily HH:MM     fires once a day, anacron-style: at or after HH:MM
//                    local time if it hasn't fired today.  Catches the
//                    "morning brief" case when the app was off at HH:MM.
//   @every Nm        fires every N minutes (1..1440)
//   @every Nh        fires every N hours   (1..168)
//
// "Morning note at 6:30am" → "@daily 06:30"
// "Catalyst watch every 30 min" → "@every 30m"
//
// Full 5-field POSIX cron (`30 6 * * *`) can be added when a schedule
// needs it; the minimal grammar covers the design-doc examples and is
// trivially testable.

#include "core/result/Result.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <optional>

namespace fincept::services {

/// One entry in the agent_schedule table.  `last_run_at` is empty
/// until the first successful fire.
struct ScheduleEntry {
    QString id;
    QString agent_id;
    QString skill;
    QString args_json;  // serialised QJsonObject
    QString cron;       // grammar above
    bool enabled = true;
    QString last_run_at;
    QString last_status; // "ok" | "error: <msg>"
    QString created_at;
    QString updated_at;
};

/// Parsed cron spec.  Result of parse_cron().
struct ScheduleSpec {
    enum Kind { Daily, EveryMinutes, EveryHours };
    Kind kind = Daily;
    int hour = 0;       // for Daily
    int minute = 0;     // for Daily
    int interval = 0;   // for EveryMinutes / EveryHours
};

/// Parse a cron string into a ScheduleSpec.  Returns nullopt on
/// any syntax error — caller surfaces a user-visible message.
std::optional<ScheduleSpec> parse_cron(const QString& cron);

/// Decide whether an entry with `last_run` (may be invalid) is due
/// to fire by `now`.  Pure function; trivially testable.
bool is_due(const ScheduleSpec& spec, const QDateTime& last_run, const QDateTime& now);

class AgentScheduler : public QObject {
    Q_OBJECT
  public:
    static AgentScheduler& instance();

    /// Start the periodic tick.  Idempotent; safe to call from
    /// app startup.  Reads agent_schedule on each tick — no in-memory
    /// cache, so admin edits land immediately.
    void start();

    /// Stop the periodic tick (e.g. during shutdown).
    void stop();

    /// CRUD passthroughs — surface the same operations the future
    /// Workbench scheduler tab will need.
    Result<QVector<ScheduleEntry>> list_entries();
    Result<void> save_entry(const ScheduleEntry& e);
    Result<void> remove_entry(const QString& id);
    Result<void> set_enabled(const QString& id, bool enabled);

    /// Force one tick now (mostly for tests / admin "run now" UI).
    /// Synchronous; returns the count of fired entries.
    int tick_now();

    AgentScheduler(const AgentScheduler&) = delete;
    AgentScheduler& operator=(const AgentScheduler&) = delete;

  signals:
    void entry_fired(const QString& entry_id, const QString& agent_id, const QString& skill);
    void entry_failed(const QString& entry_id, const QString& error);

  private:
    AgentScheduler();
    QTimer tick_;

    void on_tick();
    void run_entry(ScheduleEntry& e);
};

} // namespace fincept::services
