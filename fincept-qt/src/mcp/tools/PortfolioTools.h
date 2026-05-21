#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace fincept::mcp::tools {
std::vector<ToolDef> get_portfolio_tools();

/// Resources exposed by the portfolio surface (MCP spec resources).
/// Agents read these by uri without spending tool-call budget per fetch.
std::vector<Resource> get_portfolio_resources();
} // namespace fincept::mcp::tools
