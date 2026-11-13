#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace finterm::mcp::tools {
std::vector<ToolDef> get_watchlist_tools();
std::vector<Resource> get_watchlist_resources();
} // namespace finterm::mcp::tools
