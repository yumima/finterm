#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace finterm::mcp::tools {

/// Generic DataHub introspection surface for LLMs. See Phase 9 plan.
std::vector<ToolDef> get_datahub_tools();

} // namespace finterm::mcp::tools
