#pragma once
// MemoryTools.h — `memory_*` MCP tool family (Track 9 stub).
//
// Three operations exposed to local-runtime agents (the Anthropic
// runtime uses the SDK's native memory tool):
//
//   - memory_upsert(scope, key, content, metadata?)
//   - memory_search(scope, query, limit?)        — keyword LIKE today
//   - memory_list(scope, limit?)                 — recent N entries
//
// Backed by `memory_entries` (v032).  When sqlite-vec is wired up
// `memory_search` switches to cosine-similarity over an `embedding`
// column without changing the tool API.

#include "mcp/McpTypes.h"

#include <vector>

namespace fincept::mcp::tools {

std::vector<ToolDef> get_memory_tools();

} // namespace fincept::mcp::tools
