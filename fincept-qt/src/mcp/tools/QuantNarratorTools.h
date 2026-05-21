#pragma once
#include "mcp/McpTypes.h"

#include <vector>

// QuantNarratorTools — LLM-driven commentary on quant outputs.
//
// Three stateless tools per Track 11 / plans/ai-stack-free-local.md
// item 32:
//   narrate_backtest_result   English commentary on a backtest run
//   propose_param_sweep       Suggested parameter ranges for a re-run
//   narrate_live_trade        English commentary on one live trade
//
// All three call ctx.sample (MCP-spec sampling/createMessage, see
// Track 5 commit I) to invoke the active LLM profile.  If sampling
// isn't wired into the runtime yet, the tools return a clear error
// directing the user to enable an Anthropic or local profile.

namespace fincept::mcp::tools {
std::vector<ToolDef> get_quant_narrator_tools();
} // namespace fincept::mcp::tools
