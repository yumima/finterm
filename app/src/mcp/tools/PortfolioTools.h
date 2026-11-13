#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace finterm::mcp::tools {
std::vector<ToolDef> get_portfolio_tools();

/// Resources exposed by the portfolio surface (MCP spec resources).
/// Agents read these by uri without spending tool-call budget per fetch.
std::vector<Resource> get_portfolio_resources();
} // namespace finterm::mcp::tools
