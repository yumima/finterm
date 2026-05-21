#pragma once
// ToolKillswitchRepository.h — per-tool kill-switch CRUD (Track 14 #39).
//
// Wire-form keys (`<server_id>__<tool_name>`) match what
// McpService::execute_tool dispatches on.  McpService loads the
// disabled set once per cache cycle and refuses dispatch for any
// tool whose key is in the set.

#include "core/result/Result.h"

#include <QSet>
#include <QString>
#include <QVector>

namespace fincept {

struct ToolKillswitchEntry {
    QString tool_name;     ///< wire-form: `<server_id>__<tool_name>`
    QString reason;        ///< user-supplied; surfaced in the error message
    QString disabled_at;   ///< ISO timestamp
};

class ToolKillswitchRepository {
  public:
    static ToolKillswitchRepository& instance();

    /// Fast path for the dispatch chokepoint: just the names.
    Result<QSet<QString>> disabled_names();

    /// Reason for a disabled tool, or empty string if not disabled or
    /// disabled without a reason.  Only called on the slow path, after
    /// disabled_names() has confirmed the tool is in the set.
    Result<QString> reason_for(const QString& tool_name);

    /// All rows, for the Workbench System tab.
    Result<QVector<ToolKillswitchEntry>> list_all();

    /// Disable a tool.  `reason` is optional; surfaced when McpService
    /// refuses dispatch so the user knows why.  Idempotent — re-disabling
    /// an already-disabled tool updates the reason + timestamp.
    Result<void> disable(const QString& tool_name, const QString& reason = {});

    /// Re-enable a tool.  No-op if not currently disabled.
    Result<void> enable(const QString& tool_name);

    ToolKillswitchRepository(const ToolKillswitchRepository&) = delete;
    ToolKillswitchRepository& operator=(const ToolKillswitchRepository&) = delete;

  private:
    ToolKillswitchRepository() = default;
};

} // namespace fincept
