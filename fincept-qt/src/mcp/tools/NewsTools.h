#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace finterm::mcp::tools {
std::vector<ToolDef> get_news_tools();
std::vector<Resource> get_news_resources();
} // namespace finterm::mcp::tools
