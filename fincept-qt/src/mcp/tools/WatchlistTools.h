#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace fincept::mcp::tools {
std::vector<ToolDef> get_watchlist_tools();
std::vector<Resource> get_watchlist_resources();
} // namespace fincept::mcp::tools
