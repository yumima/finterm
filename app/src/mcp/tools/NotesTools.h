#pragma once
#include "mcp/McpTypes.h"

#include <vector>

namespace finterm::mcp::tools {
std::vector<ToolDef> get_notes_tools();
std::vector<Resource> get_notes_resources();
std::vector<Prompt> get_notes_prompts();
} // namespace finterm::mcp::tools
