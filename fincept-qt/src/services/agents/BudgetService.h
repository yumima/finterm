#pragma once
// BudgetService.h — per-agent and global cost budgeting (Track 14 #41, R24).
//
// Reads `cost_usd` from agent_traces (v029) and compares against
// per-agent caps stored in agent_configs.config_json.budget:
//
//   {
//     "system_prompt": "…",
//     "skills": [...],
//     "allow_tools": [...],
//     "budget": {
//       "max_usd_per_day": 5.0
//     }
//   }
//
// Missing / null / non-positive `max_usd_per_day` disables the cap.
// "Today" is the local-time calendar day — the same window used by
// the eventual Workbench System tab cost display.
//
// What this *isn't*: in-flight token budgets that abort mid-stream.
// That requires runtime-side enforcement (Python claude-agent-sdk
// + the local runtime).  This C++ gate covers the simpler case —
// "you've already spent past your daily limit, the next dispatch
// is refused before it starts" — which is most of the user-visible
// value.

#include "core/result/Result.h"

#include <QString>

namespace fincept::services {

class BudgetService {
  public:
    static BudgetService& instance();

    /// Sum of `cost_usd` for all traces this agent has dispatched
    /// today (local time).  NULL costs (most runs today, since cost
    /// reporting isn't fully wired yet) sum to 0.
    Result<double> spend_today(const QString& agent_id);

    /// Global daily spend across all agents.  Used by the UI cost
    /// meter and by any future global cap.
    Result<double> spend_today_total();

    /// Check whether the next dispatch for `agent_id` is within
    /// budget.  Returns Result::ok() when within budget OR when
    /// no cap is configured.  Returns Result::err(reason) when the
    /// cap is exceeded — the caller surfaces `reason` to the user.
    Result<void> check_dispatch(const QString& agent_id);

    BudgetService(const BudgetService&) = delete;
    BudgetService& operator=(const BudgetService&) = delete;

  private:
    BudgetService() = default;
};

} // namespace fincept::services
